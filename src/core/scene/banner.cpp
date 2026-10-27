#include "core/scene/banner.hpp"

#include <algorithm>
#include <bit>
#include <map>

#include "core/pica/fragment/format.hpp"
#include "core/pica/gpu/gpu.hpp"
#include "core/pica/shader/shader.hpp"

namespace kabegami::scene {
namespace {

namespace reg = pica::reg;
namespace sh = pica::reg::shader;
constexpr std::size_t vsh = reg::vsh_block;

// the top screen's buffer, as the console has it: 240 across, 400 down, on
// its side
constexpr std::uint32_t buffer_width = 240, buffer_height = 400;

using Uniform = std::array<float, 4>;

// float1.7.23, the viewport's inverse sizes
std::uint32_t float31(float value) {
    auto bits = std::bit_cast<std::uint32_t>(value);
    auto exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 63;
    if (value == 0 || exponent <= 0) return 0;
    return ((bits >> 31) << 30 | static_cast<std::uint32_t>(exponent) << 23 | (bits & 0x7FFFFF)) << 1;
}

// four float24s in three words, w first, as the fixed attributes take them
std::array<std::uint32_t, 3> pack24(const Uniform& v) {
    auto x = pica::encode_float24(v[0]), y = pica::encode_float24(v[1]), z = pica::encode_float24(v[2]), w = pica::encode_float24(v[3]);
    return {w << 8 | z >> 16, (z & 0xFFFF) << 16 | y >> 8, (y & 0xFF) << 24 | x};
}

// what the material's animations make of it at a frame
struct MaterialState {
    std::array<cgfx::Vec4, cgfx::color_count> colors;
    std::array<bool, cgfx::color_count> animated{};
    std::array<cgfx::Vec2, 3> scale, translate;
    std::array<float, 3> rotate{};
    std::array<std::string, 3> textures;
};

std::optional<int> color_named(std::string_view name) {
    static constexpr std::string_view names[] = {"Emission",  "Ambient",   "Diffuse",   "Specular0", "Specular1", "Constant0",
                                                 "Constant1", "Constant2", "Constant3", "Constant4", "Constant5"};
    for (int i = 0; i < cgfx::color_count; ++i)
        if (names[i] == name) return i;
    return std::nullopt;
}

// "Materials[\"mt_BG\"].TextureCoordinators[1].Translate" for mt_BG, as
// "TextureCoordinators[1].Translate"
std::optional<std::string_view> member_of(std::string_view path, std::string_view material) {
    std::string prefix = "Materials[\"" + std::string(material) + "\"].";
    if (!path.starts_with(prefix)) return std::nullopt;
    return path.substr(prefix.size());
}

// "TextureCoordinators[1].Translate" as 1 and "Translate"
std::optional<std::pair<std::size_t, std::string_view>> indexed(std::string_view member, std::string_view array) {
    std::string prefix = std::string(array) + "[";
    if (!member.starts_with(prefix) || member.size() < prefix.size() + 4) return std::nullopt;
    auto digit = member[prefix.size()];
    if (digit < '0' || digit > '2' || member.substr(prefix.size() + 1, 2) != "].") return std::nullopt;
    return std::pair{static_cast<std::size_t>(digit - '0'), member.substr(prefix.size() + 3)};
}

MaterialState material_state(const cgfx::File& file, const cgfx::Material& material, const cgfx::Animation* animation, float frame) {
    MaterialState s;
    s.colors = material.colors;
    for (std::size_t i = 0; i < 3; ++i) {
        s.scale[i] = material.coordinators[i].scale;
        s.rotate[i] = material.coordinators[i].rotate;
        s.translate[i] = material.coordinators[i].translate;
        if (material.mappers[i]) s.textures[i] = material.mappers[i]->texture;
    }
    if (!animation) return s;
    for (auto& m : animation->members) {
        auto member = member_of(m.path, material.name);
        if (!member) continue;
        auto value = cgfx::evaluate(file, m, frame);
        if (!value.written) continue;
        auto put = [&](float& target, std::size_t component) {
            if (value.written & (1u << component)) target = value.values[component];
        };
        if (member->starts_with("MaterialColor.")) {
            if (auto c = color_named(member->substr(14))) {
                auto& color = s.colors[static_cast<std::size_t>(*c)];
                put(color.x, 0);
                put(color.y, 1);
                put(color.z, 2);
                put(color.w, 3);
                s.animated[static_cast<std::size_t>(*c)] = true;
            }
        } else if (auto coordinator = indexed(*member, "TextureCoordinators")) {
            auto [i, field] = *coordinator;
            if (field == "Translate") {
                put(s.translate[i].x, 0);
                put(s.translate[i].y, 1);
            } else if (field == "Scale") {
                put(s.scale[i].x, 0);
                put(s.scale[i].y, 1);
            } else if (field == "Rotate") {
                put(s.rotate[i], 0);
            }
        } else if (auto mapper = indexed(*member, "TextureMappers")) {
            if (mapper->second == "Texture" && value.texture) s.textures[mapper->first] = *value.texture;
        }
    }
    return s;
}

// the bones' world matrices at a frame of a skeletal animation, the ones it
// doesn't move at their bind pose
std::vector<cgfx::Matrix34> bone_worlds(const cgfx::File& file, const cgfx::Model& model, const cgfx::Animation* animation, float frame) {
    if (!model.skeleton) return {};
    std::vector<cgfx::Matrix34> locals;
    for (auto& bone : model.skeleton->bones) {
        auto scale = bone.scale, rotation = bone.rotation, translation = bone.translation;
        if (auto member = animation ? animation->find(bone.name) : nullptr; member && member->type == cgfx::MemberType::transform) {
            auto v = cgfx::evaluate(file, *member, frame);
            float* targets[10] = {&scale.x, &scale.y, &scale.z, &rotation.x, &rotation.y, &rotation.z, nullptr,
                                  &translation.x, &translation.y, &translation.z};
            for (std::size_t c = 0; c < 10; ++c)
                if (targets[c] && (v.written & (1u << c))) *targets[c] = v.values[c];
        }
        locals.push_back(cgfx::srt_matrix(scale, rotation, translation));
    }
    auto worlds = cgfx::world_matrices(*model.skeleton, locals);
    for (auto& w : worlds) w = cgfx::multiply(model.world, w);
    return worlds;
}

void rows(std::array<Uniform, pica::float_uniform_count>& u, std::size_t first, const float* m, std::size_t count) {
    for (std::size_t r = 0; r < count; ++r) u[first + r] = {m[r * 4], m[r * 4 + 1], m[r * 4 + 2], m[r * 4 + 3]};
}

// the IrScale component an attribute usage's scale goes to, c7.xyzw then c8.xyzw
std::optional<std::size_t> scale_slot(cgfx::Usage usage) {
    switch (usage) {
        case cgfx::Usage::position: return 0;
        case cgfx::Usage::normal: return 1;
        case cgfx::Usage::tangent: return 2;
        case cgfx::Usage::color: return 3;
        case cgfx::Usage::texcoord0: return 4;
        case cgfx::Usage::texcoord1: return 5;
        case cgfx::Usage::texcoord2: return 6;
        case cgfx::Usage::bone_weight: return 7;
        default: return std::nullopt;
    }
}

// a uniform of the program by its name, which the binary may keep with the
// components it declares, "PosOffs.xyz"
const cgfx::ShaderUniform& uniform(const cgfx::ShaderEntry& entry, std::string_view name) {
    for (auto& u : entry.uniforms) {
        std::string_view n = u.name;
        if (n == name || (n.size() > name.size() && n.starts_with(name) && n[name.size()] == '.')) return u;
    }
    throw FormatError("the vertex program has no uniform " + std::string(name));
}

}  // namespace

Banner::Banner(cgfx::File banner, cgfx::File camera, cgfx::File shader)
    : banner_(std::move(banner)), camera_file_(std::move(camera)), shader_file_(std::move(shader)) {
    camera_ = cgfx::read_camera(camera_file_);
    shader_ = cgfx::read_shader(shader_file_);
    reload();
}

Banner Banner::load(const HomeFiles& home) {
    auto read = [&](const char* path) {
        auto bytes = home.read(path);
        if (!bytes) throw FormatError(std::string("this HOME Menu has no ") + path);
        return cgfx::File::load(*bytes, path);
    };
    return Banner(read("3D/BannerBG_LZ.bin"), read("3D/BannerCamera_LZ.bin"), read("3D/nwgfx_DefaultShader_LZ.bin"));
}

void Banner::reload() {
    model_ = cgfx::read_model(banner_);
    materials_.clear();
    for (std::size_t i = 0; i < model_.materials.size(); ++i) materials_.push_back(cgfx::read_material(banner_, model_.materials.value(i)));
    loop_ = cgfx::find_animation(banner_, cgfx::Slot::material_animations, "BannerBG_Loop");
    scene_in_ = cgfx::find_animation(banner_, cgfx::Slot::skeletal_animations, "BannerBG_SceneIn");
}

Banner::Frame Banner::build(const Pose& pose) const {
    Frame frame{pica::Memory(4 << 20), {}, 0, 0, 0};
    auto& memory = frame.memory;
    frame.color_at = memory.allocate(buffer_width * buffer_height * 4, 0x100);
    frame.depth_at = memory.allocate(buffer_width * buffer_height * 4, 0x100);
    auto arrays = memory.base();
    pica::CommandList list;

    // the frame buffer: RGBA8 colour and D24S8 depth, 240x400
    list.write(reg::framebuffer_invalidate, 1);
    list.write(reg::colorbuffer_location, frame.color_at >> 3);
    list.write(reg::depthbuffer_location, frame.depth_at >> 3);
    list.write(reg::colorbuffer_format, 0x00000002);
    list.write(reg::depthbuffer_format, 3);
    auto dimensions = buffer_width | (buffer_height - 1) << 12 | 1u << 24;
    list.write(reg::framebuffer_dimensions, dimensions);
    list.write(0x006E, dimensions);  // RENDERBUF_DIM
    list.write(reg::colorbuffer_read, 0xF);
    list.write(reg::colorbuffer_write, 0xF);

    // the viewport over the whole buffer, and the camera's depth map
    list.write(reg::viewport_width, pica::encode_float24(buffer_width / 2.0f));
    list.write(reg::viewport_width_inverse, float31(2.0f / buffer_width));
    list.write(reg::viewport_height, pica::encode_float24(buffer_height / 2.0f));
    list.write(reg::viewport_height_inverse, float31(2.0f / buffer_height));
    list.write(reg::viewport_xy, 0);
    auto depth = cgfx::depth_map(camera_);
    list.write(reg::depthmap_enable, depth.enable ? 1 : 0);
    list.write(reg::depthmap_scale, pica::encode_float24(depth.scale));
    list.write(reg::depthmap_offset, pica::encode_float24(depth.offset));

    // the camera's matrices, with the pivot that turns the picture onto the
    // sideways screen
    auto view = cgfx::view_matrix(camera_);
    auto projection = cgfx::projection_matrix(camera_, 1);
    // the frames as HOME's frame controllers place them: the loop wrapped,
    // SceneIn held at its ends
    auto place = [](const std::optional<cgfx::Animation>& a, float f) {
        return a ? cgfx::FrameController(a->frames, a->loop).place(f) : 0.0f;
    };
    auto loop_frame = place(loop_, pose.loop), scene_in_frame = place(scene_in_, pose.scene_in);
    auto bones = bone_worlds(banner_, model_, scene_in_ ? &*scene_in_ : nullptr, scene_in_frame);

    // the textures, each put in memory once
    std::map<std::string, std::uint32_t> placed;
    auto place_texture = [&](const cgfx::Texture& t) {
        auto [it, fresh] = placed.try_emplace(t.name, 0);
        if (fresh) {
            auto bytes = std::span(banner_.bytes()).subspan(t.data_at, t.data_size);
            it->second = memory.put(bytes, 0x80);
        }
        return it->second;
    };

    int shader_loaded = -1;
    for (auto& mesh : model_.meshes) {
        if (!mesh.visible || mesh.shape < 0 || static_cast<std::size_t>(mesh.shape) >= model_.shapes.size()) continue;
        auto& shape = model_.shapes[static_cast<std::size_t>(mesh.shape)];
        auto& material = materials_.at(static_cast<std::size_t>(mesh.material));
        auto state = material_state(banner_, material, loop_ ? &*loop_ : nullptr, loop_frame);
        auto& description = shader_.descriptions.at(static_cast<std::size_t>(material.program_index));
        auto& entry = shader_.binary.entries.at(static_cast<std::size_t>(description.vertex));

        // the program, its descriptors and its output map
        if (shader_loaded != description.vertex) {
            shader_loaded = description.vertex;
            list.write(reg::vsh_com_mode, 0);
            list.write(reg::geostage_config, 0);
            list.write(vsh + sh::code_transfer_index, 0);
            list.port(vsh + sh::code_transfer_data0, shader_.binary.code);
            list.write(vsh + sh::code_transfer_end, 1);
            list.write(vsh + sh::opdescs_index, 0);
            list.port(vsh + sh::opdescs_data0, shader_.binary.descriptors);
            list.write(vsh + sh::entry_point, 0x7FFF0000 | entry.main);
            list.write(vsh + sh::outmap_mask, entry.outmap.mask);
            list.write(reg::outmap_total, entry.outmap.total);
            list.run(reg::outmap_o0, entry.outmap.regs);
            list.write(reg::outattr_mode, entry.outmap.mode);
            list.write(reg::outattr_clock, entry.outmap.clock);
        }

        // the vertex arrays: a loader each, the attributes in the order their
        // offsets have, padding where the file leaves gaps
        std::uint64_t formats = 0, permutation = 0;
        std::uint32_t fixed_mask = 0, id = 0;
        std::array<float, 8> scales{};
        bool color4 = false, weight4 = false;
        struct Loader {
            std::uint32_t offset;
            std::uint64_t components = 0;
            std::uint32_t count = 0, stride;
        };
        std::vector<Loader> loaders;
        for (auto& buffer : shape.buffers) {
            auto attributes = buffer.attributes;
            std::sort(attributes.begin(), attributes.end(), [](auto& a, auto& b) { return a.offset < b.offset; });
            auto data = std::span(banner_.bytes()).subspan(buffer.data_at, buffer.size);
            Loader loader{memory.put(data, 0x10) - arrays, 0, 0, buffer.stride};
            std::uint32_t cursor = 0;
            auto component = [&](std::uint32_t c) {
                if (loader.count == 12) throw FormatError(banner_.name() + ": a vertex needs more than 12 components");
                loader.components |= static_cast<std::uint64_t>(c) << (4 * loader.count++);
            };
            for (auto& a : attributes) {
                auto input = description.inputs[static_cast<std::size_t>(a.usage)];
                if (input < 0) continue;
                auto size = cgfx::component_size(a.format);
                auto natural = (cursor + size - 1) / size * size;
                if (natural != a.offset) {
                    // a padding component aligns to a word, then skips 4 to 16 bytes
                    auto from = (cursor + 3) / 4 * 4;
                    if (a.offset < from || (a.offset - from) % 4) throw FormatError(banner_.name() + ": a vertex layout the GPU can't fetch");
                    for (auto gap = a.offset - from; gap > 0;) {
                        auto skip = std::min(gap, 16u);
                        component(11 + skip / 4);
                        gap -= skip;
                    }
                    cursor = a.offset;
                }
                formats |= static_cast<std::uint64_t>(cgfx::pica_attribute_type(a.format) | (a.components - 1) << 2) << (4 * id);
                permutation |= static_cast<std::uint64_t>(input) << (4 * id);
                component(id++);
                cursor = a.offset + size * a.components;
                if (auto slot = scale_slot(a.usage)) scales[*slot] = a.scale;
                if (a.usage == cgfx::Usage::color && a.components == 4) color4 = true;
                if (a.usage == cgfx::Usage::bone_weight && a.components == 4) weight4 = true;
            }
            if (loader.count) loaders.push_back(loader);
        }
        std::vector<std::pair<std::uint32_t, Uniform>> fixed;
        for (auto& f : shape.fixed) {
            auto input = description.inputs[static_cast<std::size_t>(f.usage)];
            if (input < 0) continue;
            Uniform value{0, 0, 0, 1};
            for (std::size_t c = 0; c < std::min<std::size_t>(4, f.values.size()); ++c) value[c] = f.values[c] * f.scale;
            formats |= static_cast<std::uint64_t>(3 | 3u << 2) << (4 * id);
            permutation |= static_cast<std::uint64_t>(input) << (4 * id);
            fixed_mask |= 1u << id;
            fixed.push_back({id++, value});
            // a fixed attribute goes to the GPU as it is, at a scale of one
            if (auto slot = scale_slot(f.usage)) scales[*slot] = 1;
            if (f.usage == cgfx::Usage::color && f.components == 4) color4 = true;
        }
        if (id == 0) continue;
        list.write(reg::attribbuffers_location, arrays >> 3);
        list.write(reg::attribbuffers_format_low, static_cast<std::uint32_t>(formats));
        list.write(reg::attribbuffers_format_high, static_cast<std::uint32_t>(formats >> 32) | fixed_mask << 16 | (id - 1) << 28);
        for (std::size_t l = 0; l < reg::loader_count; ++l) {
            std::array<std::uint32_t, 3> words{};
            if (l < loaders.size()) {
                auto& loader = loaders[l];
                words = {loader.offset, static_cast<std::uint32_t>(loader.components),
                         static_cast<std::uint32_t>(loader.components >> 32) | loader.stride << 16 | loader.count << 28};
            }
            list.run(reg::attribbuffer0 + l * 3, words);
        }
        for (auto& [index, value] : fixed) {
            list.write(reg::fixedattrib_index, index);
            list.run(reg::fixedattrib_data0, pack24(value));
        }
        list.write(vsh + sh::input_config, 0xA0000000 | (id - 1));
        list.write(reg::vsh_num_attr, id - 1);
        list.write(vsh + sh::attributes_permutation_low, static_cast<std::uint32_t>(permutation));
        list.write(vsh + sh::attributes_permutation_high, static_cast<std::uint32_t>(permutation >> 32));

        // the material: its own command blocks, with the colour writes, the
        // texture units and the combiners' colours it leaves to the runtime
        bool depth_write = material.depth_flags & 2;
        list.write(reg::depth_color_mask, 0xFu << 8 | (depth_write ? 1u << 12 : 0));
        list.write(reg::depthbuffer_read, material.depth_flags & 1 ? 3 : 0);
        list.write(reg::depthbuffer_write, depth_write ? 3 : 0);
        list.raw(material.culling_commands);
        list.raw(material.depth_commands);
        list.raw(material.blend_commands);
        list.raw(material.stencil_commands);
        list.raw(material.fragment.alpha_test);
        list.raw(material.fragment.commands);
        std::uint32_t units = 0;
        std::vector<std::uint32_t> mapper_commands;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!material.mappers[i]) continue;
            auto texture = cgfx::find_texture(banner_, state.textures[i]);
            if (!texture) throw FormatError(banner_.name() + ": " + material.name + " binds " + state.textures[i] + ", which isn't there");
            auto words = cgfx::bind_texture(*material.mappers[i], texture->format, texture->width, texture->height, texture->levels,
                                            place_texture(*texture));
            mapper_commands.insert(mapper_commands.end(), words.begin(), words.end());
            units |= 1u << i;
        }
        // Config0120: each unit its own coordinates
        list.write(reg::texunit_config, units | 1u << 12);
        list.raw(mapper_commands);
        for (int i = 0; i < 6; ++i) {
            auto stage = material.fragment.stages[static_cast<std::size_t>(i)];
            // the colour the stage's Constant names, packed again by HOME's
            // packer where HOME or an animation set it
            auto constant = stage.constant;
            std::size_t color = constant <= 5 ? constant + 5 : constant - 6;
            bool repacked = state.animated[color] || color == 6 || color == 7 || color == 8 || color == 10;
            stage.commands[4] = repacked ? cgfx::pack_color_home(state.colors[color]) : material.packed_colors[color];
            list.raw(stage.commands);
        }

        // the uniforms of section 5.3, then the DVLE's constants
        std::array<Uniform, pica::float_uniform_count> u{};
        auto at = [&](const char* name) { return static_cast<std::size_t>(uniform(entry, name).first - 0x10); };
        auto identity = cgfx::Matrix34::identity();
        bool skeletal = model_.skeleton && (model_.skeleton->flags & 1);
        rows(u, at("WrldMtx"), skeletal ? model_.world.m.data() : identity.m.data(), 3);
        rows(u, at("NormMtx"), identity.m.data(), 3);
        u[at("PosOffs")] = {shape.position_offset.x, shape.position_offset.y, shape.position_offset.z, 0};
        u[at("IrScale")] = {scales[0], scales[1], scales[2], scales[3]};
        u[at("IrScale") + 1] = {scales[4], scales[5], scales[6], scales[7]};
        Uniform texcoord_map{};
        for (std::size_t i = 0; i < 3; ++i) {
            auto& c = material.coordinators[i];
            if (c.mapping == 0) texcoord_map[i] = static_cast<float>(c.source);
            else if (c.mapping == 1) texcoord_map[i] = 3;
            else if (c.mapping == 2) texcoord_map[i] = 4;
            auto m = cgfx::coordinator_matrix(c.mode, state.scale[i], state.rotate[i], state.translate[i]);
            rows(u, at("TexMtx0") + 3 * i, m.m.data(), i < 2 ? 3 : 2);
        }
        u[at("TexcMap")] = texcoord_map;
        auto ambient = state.colors[static_cast<std::size_t>(cgfx::Color::ambient)];
        auto diffuse = state.colors[static_cast<std::size_t>(cgfx::Color::diffuse)];
        u[at("MatAmbi")] = {ambient.x, ambient.y, ambient.z, ambient.w};
        u[at("MatDiff")] = {diffuse.x, diffuse.y, diffuse.z, diffuse.w};
        rows(u, at("ProjMtx"), projection.m.data(), 4);
        rows(u, at("ViewMtx"), view.m.data(), 3);
        std::uint16_t bools = 0;
        std::array<std::uint32_t, 4> integers{};
        for (auto& c : entry.constants) {
            if (c.type == cgfx::ShaderConstant::Type::floating && c.reg < pica::float_uniform_count)
                u[c.reg] = {pica::decode_float24(c.raw[0]), pica::decode_float24(c.raw[1]), pica::decode_float24(c.raw[2]),
                            pica::decode_float24(c.raw[3])};
            else if (c.type == cgfx::ShaderConstant::Type::integer && c.reg < 4)
                integers[c.reg] = c.raw[0] | c.raw[1] << 8 | c.raw[2] << 16 | c.raw[3] << 24;
            else if (c.type == cgfx::ShaderConstant::Type::boolean && c.raw[0])
                bools = static_cast<std::uint16_t>(bools | 1u << c.reg);
        }
        auto bit = [&](const char* name) { return 1u << (uniform(entry, name).first - 0x78); };
        for (std::size_t i = 0; i < 3; ++i)
            if (static_cast<std::int32_t>(i) < material.used_texcoords && material.coordinators[i].mapping == 0)
                bools = static_cast<std::uint16_t>(bools | bit(i == 0 ? "UvMap0" : i == 1 ? "UvMap1" : "UvMap2"));
        if (material.used_texcoords > 1) bools = static_cast<std::uint16_t>(bools | bit("IsTex1"));
        if (material.used_texcoords > 2) bools = static_cast<std::uint16_t>(bools | bit("IsTex2"));
        if (color4) bools = static_cast<std::uint16_t>(bools | bit("IsVertA"));
        if (weight4) bools = static_cast<std::uint16_t>(bools | bit("IsBoneW"));
        list.run(vsh + sh::int_uniform_i0, integers);

        for (auto& sub : shape.sub_meshes) {
            // the bone palette: each listed bone's world matrix, or the
            // model's when there is no list
            auto palette = u;
            auto universal = at("UnivReg");
            if (sub.bone_indices.empty() || bones.empty()) {
                rows(palette, universal, model_.world.m.data(), 3);
            } else {
                for (std::size_t i = 0; i < sub.bone_indices.size() && i < 20; ++i)
                    rows(palette, universal + 3 * i, bones.at(static_cast<std::size_t>(sub.bone_indices[i])).m.data(), 3);
            }
            palette[at("IrScale") + 2] = {1, 1, static_cast<float>(sub.bone_indices.size()), 1};
            auto sub_bools = bools;
            if (!sub.bone_indices.empty()) {
                if (sub.skinning == 2)
                    sub_bools = static_cast<std::uint16_t>(sub_bools | bit("IsSmoSk"));
                else
                    sub_bools = static_cast<std::uint16_t>(sub_bools | bit("IsRgdSk"));
            }
            list.write(vsh + sh::bool_uniform, 0x7FFF0000 | sub_bools);
            list.float_uniforms(vsh, 0, palette);
            for (auto& primitive : sub.primitives) {
                for (auto& stream : primitive.streams) {
                    if (!stream.visible || stream.count() == 0) continue;
                    auto indices = memory.put(std::span(banner_.bytes()).subspan(stream.data_at, stream.size), 0x10);
                    list.write(reg::primitive_config, static_cast<std::uint32_t>(stream.mode) << 8 | (entry.outmap.total - 1));
                    list.write(reg::indexbuffer_config, (indices - arrays) | (stream.format == cgfx::gl::ushort ? 0x80000000u : 0));
                    list.write(reg::num_vertices, static_cast<std::uint32_t>(stream.count()));
                    list.write(reg::vertex_offset, 0);
                    list.write(0x0245, 0);  // START_DRAW_FUNC0, drawing
                    list.write(reg::drawelements, 1);
                    list.write(0x0245, 1);
                    list.write(0x0231, 1);  // VTX_FUNC, the post-vertex cache cleared
                }
            }
        }
    }
    list.write(reg::framebuffer_flush, 1);
    frame.commands = list.finish();
    frame.commands_at = memory.put_words(frame.commands);
    return frame;
}

image::Image Banner::render(const Pose& pose) const {
    auto frame = build(pose);
    pica::Gpu gpu;
    pica::Gpu::memory_fill(frame.memory, frame.color_at, frame.color_at + buffer_width * buffer_height * 4, 0x000000FF, 4);
    gpu.run_command_list(frame.memory, frame.commands_at, static_cast<std::uint32_t>(frame.commands.size() * 4));
    auto picture = pica::read_color_buffer(frame.memory, frame.color_at, buffer_width, buffer_height, pica::ColorFormat::rgba8);
    return pica::rotate_counter_clockwise(picture);
}

}  // namespace kabegami::scene
