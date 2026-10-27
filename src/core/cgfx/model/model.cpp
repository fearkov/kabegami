#include "core/cgfx/model/model.hpp"

namespace kabegami::cgfx {
namespace {

Vec3 vec3(const Reader& r, std::size_t at) { return {r.f32(at), r.f32(at + 4), r.f32(at + 8)}; }

Matrix34 matrix34(const Reader& r, std::size_t at) {
    Matrix34 m;
    for (std::size_t i = 0; i < 12; ++i) m.m[i] = r.f32(at + 4 * i);
    return m;
}

// a count at `at` and a link to that many plain values of T.
template <typename T>
std::vector<T> value_list(const Reader& r, std::size_t at) {
    std::vector<T> out;
    auto count = r.u32(at);
    if (count == 0) return out;
    auto values = r.relative(at + 4);
    if (values == 0) throw FormatError(r.what() + ": the list at " + Reader::hex(at) + " has values but no link");
    for (std::uint32_t i = 0; i < count; ++i) out.push_back(r.get<T>(values + sizeof(T) * i));
    return out;
}

void expect(const Reader& r, std::size_t at, std::uint32_t type, std::string_view what) {
    if (r.u32(at) != type)
        throw FormatError(r.what() + ": expected " + std::string(what) + " at " + Reader::hex(at) + ", found type " +
                          Reader::hex(std::uint64_t{r.u32(at)}));
}

AnimationGroup read_group(const Reader& r, std::size_t at) {
    AnimationGroup g;
    g.at = at;
    g.name = string_at(r, at + 0x08);
    g.member_type = r.u32(at + 0x0C);
    auto elements = dict_at(r, at + 0x10);
    for (std::size_t i = 0; i < elements.size(); ++i) {
        auto e = elements.value(i);
        g.elements.push_back({e, r.u32(e), string_at(r, e + 0x04), string_at(r, e + 0x08), r.u32(e + 0x10), r.u32(e + 0x14),
                              r.u32(e + 0x1C)});
    }
    return g;
}

IndexStream read_index_stream(const Reader& r, std::size_t at) {
    IndexStream s;
    s.at = at;
    s.format = r.u32(at);
    if (s.format != gl::ubyte && s.format != gl::ushort)
        throw FormatError(r.what() + ": an index stream at " + Reader::hex(at) + " has an unknown index format");
    s.mode = r.u8(at + 0x04);
    s.visible = r.u8(at + 0x05) != 0;
    s.size = r.u32(at + 0x08);
    s.data_at = r.relative(at + 0x0C);
    r.bytes(s.data_at, s.size);  // inside the file
    return s;
}

SubMesh read_sub_mesh(const Reader& r, std::size_t at) {
    SubMesh m;
    m.bone_indices = value_list<std::int32_t>(r, at);
    m.skinning = r.u32(at + 0x08);
    for (auto p : object_list(r, at + 0x0C)) {
        Primitive primitive;
        for (auto s : object_list(r, p)) primitive.streams.push_back(read_index_stream(r, s));
        m.primitives.push_back(std::move(primitive));
    }
    return m;
}

Shape read_shape(const Reader& r, std::size_t at) {
    expect(r, at, 0x10000001, "a shape");
    Shape s;
    s.at = at;
    s.flags = r.u32(at + 0x18);
    if (auto box = r.relative(at + 0x1C)) {
        s.box.centre = vec3(r, box + 0x04);
        for (std::size_t i = 0; i < 9; ++i) s.box.orientation[i] = r.f32(box + 0x10 + 4 * i);
        s.box.size = vec3(r, box + 0x34);
    }
    s.position_offset = vec3(r, at + 0x20);
    for (auto m : object_list(r, at + 0x2C)) s.sub_meshes.push_back(read_sub_mesh(r, m));
    for (auto b : object_list(r, at + 0x38)) {
        auto type = r.u32(b);
        if (type == 0x40000002) {
            VertexBuffer buffer;
            buffer.at = b;
            buffer.size = r.u32(b + 0x14);
            buffer.data_at = r.relative(b + 0x18);
            buffer.stride = static_cast<std::uint32_t>(r.s32(b + 0x24));
            r.bytes(buffer.data_at, buffer.size);
            for (auto a : object_list(r, b + 0x28)) {
                expect(r, a, 0x40000001, "a vertex attribute");
                Attribute attribute{static_cast<Usage>(r.u32(a + 0x04)), r.u32(a + 0x24), r.u32(a + 0x28), r.f32(a + 0x2C), r.u32(a + 0x30)};
                if (attribute.components < 1 || attribute.components > 4 ||
                    attribute.offset + attribute.components * component_size(attribute.format) > buffer.stride)
                    throw FormatError(r.what() + ": the attribute at " + Reader::hex(a) + " does not fit its vertex");
                buffer.attributes.push_back(attribute);
            }
            s.buffers.push_back(std::move(buffer));
        } else if (type == 0x80000000) {
            FixedAttribute fixed;
            fixed.at = b;
            fixed.usage = static_cast<Usage>(r.u32(b + 0x04));
            fixed.format = r.u32(b + 0x0C);
            fixed.components = r.u32(b + 0x10);
            fixed.scale = r.f32(b + 0x14);
            fixed.values = value_list<float>(r, b + 0x18);
            s.fixed.push_back(std::move(fixed));
        } else {
            // a top-level attribute with its own buffer, which no HOME file has
            throw FormatError(r.what() + ": the vertex buffer at " + Reader::hex(b) + " is of a kind Kabegami does not read yet");
        }
    }
    return s;
}

Mesh read_mesh(const Reader& r, std::size_t at) {
    expect(r, at, 0x01000000, "a mesh");
    Mesh m;
    m.at = at;
    m.name = string_at(r, at + 0x0C);
    m.shape = r.s32(at + 0x18);
    m.material = r.s32(at + 0x1C);
    m.visible = r.u8(at + 0x24) != 0;
    m.render_priority = r.u8(at + 0x25);
    m.mesh_node = static_cast<std::int16_t>(r.u16(at + 0x26));
    m.node_name = string_at(r, at + 0x70);
    return m;
}

Skeleton read_skeleton(const Reader& r, std::size_t at) {
    expect(r, at, 0x02000000, "a skeleton");
    Skeleton s;
    s.at = at;
    s.scaling_rule = r.u32(at + 0x24);
    s.flags = r.u32(at + 0x28);
    auto bones = dict_at(r, at + 0x18);
    for (std::size_t i = 0; i < bones.size(); ++i) {
        auto b = bones.value(i);
        Bone bone;
        bone.at = b;
        bone.name = string_at(r, b);
        bone.flags = r.u32(b + 0x04);
        bone.index = r.s32(b + 0x08);
        bone.parent = r.s32(b + 0x0C);
        bone.scale = vec3(r, b + 0x20);
        bone.rotation = vec3(r, b + 0x2C);
        bone.translation = vec3(r, b + 0x38);
        bone.local = matrix34(r, b + 0x44);
        bone.world = matrix34(r, b + 0x74);
        bone.inverse_bind = matrix34(r, b + 0xA4);
        bone.billboard = r.u32(b + 0xD4);
        if (bone.index != static_cast<std::int32_t>(i))
            throw FormatError(r.what() + ": bone " + bone.name + " is not where its index says");
        if (bone.parent >= static_cast<std::int32_t>(i))
            throw FormatError(r.what() + ": bone " + bone.name + " comes before its parent");
        s.bones.push_back(std::move(bone));
    }
    return s;
}

}  // namespace

std::uint32_t pica_attribute_type(std::uint32_t gl_format) {
    switch (gl_format) {
        case gl::byte: return 0;
        case gl::ubyte: return 1;
        case gl::short_: return 2;
        case gl::float_: return 3;
    }
    throw FormatError("a vertex attribute of an unknown type " + Reader::hex(std::uint64_t{gl_format}));
}

std::uint32_t component_size(std::uint32_t gl_format) {
    switch (gl_format) {
        case gl::byte:
        case gl::ubyte: return 1;
        case gl::short_:
        case gl::ushort: return 2;
        case gl::float_: return 4;
    }
    throw FormatError("a vertex attribute of an unknown type " + Reader::hex(std::uint64_t{gl_format}));
}

const AnimationGroup* Model::group(std::string_view group_name) const {
    for (auto& g : groups)
        if (g.name == group_name) return &g;
    return nullptr;
}

Model read_model(const File& file, std::size_t at) {
    auto r = file.reader();
    auto header = object_header(r, at);
    if (header.magic != "CMDL") throw FormatError(file.name() + ": no model at " + Reader::hex(at));
    Model m;
    m.at = at;
    m.type = header.type;
    m.name = header.name;
    auto groups = dict_at(r, at + 0x28);
    for (std::size_t i = 0; i < groups.size(); ++i) m.groups.push_back(read_group(r, groups.value(i)));
    m.scale = vec3(r, at + 0x30);
    m.rotation = vec3(r, at + 0x3C);
    m.translation = vec3(r, at + 0x48);
    m.local = matrix34(r, at + 0x54);
    m.world = matrix34(r, at + 0x84);
    for (auto mesh : object_list(r, at + 0xB4)) m.meshes.push_back(read_mesh(r, mesh));
    m.materials = dict_at(r, at + 0xBC);
    for (auto shape : object_list(r, at + 0xC4)) m.shapes.push_back(read_shape(r, shape));
    m.flags = r.u32(at + 0xD4);
    // bit 7 of the type says the model has a skeleton, and the link to it
    if (m.type & 0x80) {
        if (auto skeleton = r.relative(at + 0xE0)) m.skeleton = read_skeleton(r, skeleton);
    }
    for (auto& mesh : m.meshes) {
        if (mesh.shape < 0 || static_cast<std::size_t>(mesh.shape) >= m.shapes.size())
            throw FormatError(file.name() + ": a mesh of " + m.name + " names a shape it does not have");
        if (mesh.material < 0 || static_cast<std::size_t>(mesh.material) >= m.materials.size())
            throw FormatError(file.name() + ": a mesh of " + m.name + " names a material it does not have");
    }
    return m;
}

Model read_model(const File& file, std::string_view name) {
    auto& models = file.dict(Slot::models);
    if (models.empty()) throw FormatError(file.name() + " has no model");
    if (name.empty()) return read_model(file, models.value(0));
    auto at = models.value_of(name);
    if (!at) throw FormatError(file.name() + " has no model named " + std::string(name));
    return read_model(file, *at);
}

std::array<float, 4> attribute_value(const File& file, const VertexBuffer& buffer, const Attribute& attribute, std::size_t vertex) {
    auto r = file.reader();
    std::array<float, 4> out{0, 0, 0, 1};
    auto at = buffer.data_at + vertex * buffer.stride + attribute.offset;
    if (vertex >= buffer.size / buffer.stride) throw FormatError(file.name() + ": a vertex past the end of its buffer");
    auto size = component_size(attribute.format);
    for (std::uint32_t c = 0; c < attribute.components; ++c) {
        auto p = at + c * size;
        switch (attribute.format) {
            case gl::byte: out[c] = static_cast<float>(static_cast<std::int8_t>(r.u8(p))); break;
            case gl::ubyte: out[c] = static_cast<float>(r.u8(p)); break;
            case gl::short_: out[c] = static_cast<float>(static_cast<std::int16_t>(r.u16(p))); break;
            case gl::float_: out[c] = r.f32(p); break;
        }
    }
    return out;
}

std::uint32_t index_at(const File& file, const IndexStream& stream, std::size_t i) {
    auto r = file.reader();
    if (i >= stream.count()) throw FormatError(file.name() + ": an index past the end of its stream");
    return stream.format == gl::ushort ? r.u16(stream.data_at + 2 * i) : r.u8(stream.data_at + i);
}

std::vector<Matrix34> world_matrices(const Skeleton& skeleton, const std::vector<Matrix34>& locals) {
    std::vector<Matrix34> world(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        auto parent = skeleton.bones[i].parent;
        world[i] = parent < 0 ? locals[i] : multiply(world[static_cast<std::size_t>(parent)], locals[i]);
    }
    return world;
}

int texture_format_bits(std::uint32_t format) {
    static constexpr int bits[] = {32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8};
    return format < 14 ? bits[format] : 0;
}

Texture read_texture(const File& file, std::size_t at) {
    auto r = file.reader();
    auto header = object_header(r, at);
    if (header.magic != "TXOB") throw FormatError(file.name() + ": no texture at " + Reader::hex(at));
    Texture t;
    t.at = at;
    t.type = header.type;
    t.name = header.name;
    t.height = r.s32(at + 0x18);
    t.width = r.s32(at + 0x1C);
    t.gl_format = r.u32(at + 0x20);
    t.gl_type = r.u32(at + 0x24);
    t.levels = r.s32(at + 0x28);
    t.format = r.u32(at + 0x34);
    if (t.type != 0x20000011)
        throw FormatError(file.name() + ": texture " + t.name + " is not a plain image, which Kabegami does not read yet");
    if (texture_format_bits(t.format) == 0) throw FormatError(file.name() + ": texture " + t.name + " has an unknown format");
    if (t.width <= 0 || t.height <= 0 || t.width % 8 || t.height % 8 || t.levels < 1)
        throw FormatError(file.name() + ": texture " + t.name + " has a size the GPU can't use");
    t.image_at = r.relative(at + 0x38);
    t.data_size = r.u32(t.image_at + 0x08);
    t.data_at = r.relative(t.image_at + 0x0C);
    t.bits_per_pixel = r.s32(t.image_at + 0x14);
    std::size_t expected = 0;
    for (int level = 0, w = t.width, h = t.height; level < t.levels; ++level, w /= 2, h /= 2)
        expected += static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(texture_format_bits(t.format)) / 8;
    if (t.data_size < expected) throw FormatError(file.name() + ": texture " + t.name + " holds fewer texels than its size needs");
    r.bytes(t.data_at, t.data_size);
    return t;
}

std::optional<Texture> find_texture(const File& file, std::string_view name) {
    auto at = file.dict(Slot::textures).value_of(name);
    if (!at) return std::nullopt;
    return read_texture(file, *at);
}

}  // namespace kabegami::cgfx
