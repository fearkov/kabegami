#include "core/cgfx/material/material.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "core/md5.hpp"

namespace kabegami::cgfx {
namespace {

Vec4 vec4(const Reader& r, std::size_t at) { return {r.f32(at), r.f32(at + 4), r.f32(at + 8), r.f32(at + 12)}; }

template <std::size_t N>
std::array<std::uint32_t, N> words(const Reader& r, std::size_t at) {
    std::array<std::uint32_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = r.u32(at + 4 * i);
    return out;
}

constexpr std::size_t colors_at = 0x24;
constexpr std::size_t packed_colors_at = 0xD4;
constexpr std::size_t coordinators_at = 0x16C;
constexpr std::size_t coordinator_size = 0x58;

}  // namespace

std::vector<RegisterWrite> decode_commands(std::span<const std::uint32_t> words_in) {
    std::vector<RegisterWrite> out;
    std::size_t i = 0;
    while (i + 1 < words_in.size()) {
        auto param = words_in[i];
        auto header = words_in[i + 1];
        auto reg = static_cast<std::uint16_t>(header & 0xFFFF);
        auto mask = static_cast<std::uint8_t>((header >> 16) & 0xF);
        auto extra = (header >> 20) & 0xFF;
        bool consecutive = header >> 31;
        if (i + 2 + extra > words_in.size()) throw FormatError("a command block runs past its end");
        out.push_back({reg, mask, param});
        for (std::uint32_t k = 0; k < extra; ++k) {
            auto next = consecutive ? static_cast<std::uint16_t>(reg + k + 1) : reg;
            out.push_back({next, mask, words_in[i + 2 + k]});
        }
        i += 2 + extra;
        if (i % 2) ++i;  // commands are 8-byte aligned
    }
    return out;
}

std::uint32_t pack_color(Vec4 color) {
    auto byte = [](float f) {
        double d = std::clamp(static_cast<double>(f), 0.0, 1.0);
        return static_cast<std::uint32_t>(d * 255.0 + 0.5);
    };
    return byte(color.x) | byte(color.y) << 8 | byte(color.z) << 16 | byte(color.w) << 24;
}

std::uint32_t pack_color_home(Vec4 color) {
    auto byte = [](float f) {
        f = std::clamp(f, 0.0f, 1.0f);
        float scaled = f * 255.0f;
        return static_cast<std::uint32_t>(scaled + 0.5f);
    };
    return byte(color.x) | byte(color.y) << 8 | byte(color.z) << 16 | byte(color.w) << 24;
}

Matrix34 coordinator_matrix(std::uint32_t mode, Vec2 scale, float rotate, Vec2 translate) {
    auto [s, c] = sincos_fidx(rotate);
    float sx = scale.x, sy = scale.y, tx = translate.x, ty = translate.y;
    Matrix34 m;
    m.at(0, 0) = sx * c;
    m.at(0, 1) = -sx * s;
    m.at(1, 0) = sy * s;
    m.at(1, 1) = sy * c;
    m.at(2, 2) = 1.0f;
    switch (mode) {
        case 0:  // Maya
            m.at(0, 3) = sx * (0.5f * s - 0.5f * c + 0.5f - tx);
            m.at(1, 3) = sy * (-0.5f * s - 0.5f * c + 0.5f - ty);
            break;
        case 1:  // SoftImage, where HOME turns the other way
            m.at(0, 1) = sx * s;
            m.at(1, 0) = -sy * s;
            m.at(0, 3) = sx * (-c * tx - s * ty);
            m.at(1, 3) = sy * (s * tx - c * ty);
            break;
        default:  // 3ds Max
            m.at(0, 3) = sx * c * (-tx - 0.5f) - sx * s * (ty - 0.5f) + 0.5f;
            m.at(1, 3) = sy * s * (-tx - 0.5f) + sy * c * (ty - 0.5f) + 0.5f;
    }
    return m;
}

std::vector<std::uint32_t> bind_texture(const TextureMapper& mapper, std::uint32_t format, int width, int height, int levels,
                                        std::uint32_t physical_address) {
    std::vector<std::uint32_t> w(mapper.commands.begin(), mapper.commands.begin() + 8);
    w[0] = (w[0] & ~0xFu) | (format & 0xF);
    w[1] = (w[1] & ~(0xFu << 16)) | (0xFu << 16);
    w[3] = (w[3] & ~(0xFFu << 20)) | (4u << 20);
    w[4] = (static_cast<std::uint32_t>(height) & 0x7FF) | (static_cast<std::uint32_t>(width) & 0x7FF) << 16;
    w[5] &= ~(3u << 4);
    if (format == 12) w[5] |= 2u << 4;
    w[6] = (w[6] & ~(0xFu << 16)) | (static_cast<std::uint32_t>(levels - 1) & 0xF) << 16;
    w[7] = physical_address >> 3;
    return w;
}

std::uint32_t Material::stage_color(int stage) const {
    auto constant = fragment.stages.at(static_cast<std::size_t>(stage)).constant;
    // 0-5 name Constant0-5, which come after the first five colours
    if (constant <= 5) return packed_colors[static_cast<std::size_t>(constant) + 5];
    if (constant <= 10) return packed_colors[static_cast<std::size_t>(constant) - 6];
    throw FormatError("a TEV stage of " + name + " names a colour that does not exist");
}

Material read_material(const File& file, std::size_t at) {
    auto r = file.reader();
    auto header = object_header(r, at);
    if (header.magic != "MTOB") throw FormatError(file.name() + ": no material at " + Reader::hex(at));
    Material m;
    m.at = at;
    m.name = header.name;
    m.flags = r.u32(at + 0x18);
    m.texcoord_config = r.u32(at + 0x1C);
    m.translucency = r.u32(at + 0x20);
    for (std::size_t i = 0; i < color_count; ++i) {
        m.colors[i] = vec4(r, at + colors_at + 0x10 * i);
        m.packed_colors[i] = r.u32(at + packed_colors_at + 4 * i);
    }
    m.polygon_offset = r.u32(at + 0x104) != 0;
    m.face_culling = r.u32(at + 0x108);
    m.polygon_offset_unit = r.f32(at + 0x10C);
    m.culling_commands = words<2>(r, at + 0x110);
    m.depth_flags = r.u32(at + 0x118);
    m.depth_commands = words<4>(r, at + 0x11C);
    m.blend_mode = r.u32(at + 0x12C);
    m.blend_color = vec4(r, at + 0x130);
    m.blend_commands = words<6>(r, at + 0x140);
    m.stencil_commands = words<4>(r, at + 0x158);
    m.used_texcoords = r.s32(at + 0x168);
    for (std::size_t i = 0; i < 3; ++i) {
        auto c = at + coordinators_at + coordinator_size * i;
        auto& t = m.coordinators[i];
        t.at = c;
        t.source = r.s32(c);
        t.mapping = r.u32(c + 0x04);
        t.reference_camera = r.s32(c + 0x08);
        t.mode = r.u32(c + 0x0C);
        t.scale = {r.f32(c + 0x10), r.f32(c + 0x14)};
        t.rotate = r.f32(c + 0x18);
        t.translate = {r.f32(c + 0x1C), r.f32(c + 0x20)};
        for (std::size_t k = 0; k < 12; ++k) t.matrix.m[k] = r.f32(c + 0x28 + 4 * k);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        auto mapper = r.relative(at + 0x274 + 4 * i);
        if (!mapper) continue;
        TextureMapper t;
        t.at = mapper;
        if (auto reference = r.relative(mapper + 0x08)) t.texture = string_at(r, reference + 0x18);
        if (auto sampler = r.relative(mapper + 0x0C)) {
            t.min_filter = r.u32(sampler + 0x08);
            t.border_color = vec4(r, sampler + 0x0C);
            t.lod_bias = r.f32(sampler + 0x1C);
        }
        t.commands = words<14>(r, mapper + 0x10);
        t.commands_length = r.s32(mapper + 0x48);
        m.mappers[i] = t;
    }
    if (auto shader = r.relative(at + 0x284)) m.shader = string_at(r, shader + 0x18);
    m.program_index = r.s32(at + 0x28C);
    if (auto fs = r.relative(at + 0x288)) {
        auto& f = m.fragment;
        f.at = fs;
        f.buffer_color = vec4(r, fs);
        f.lighting_flags = r.u32(fs + 0x10);
        f.lighting_config = r.u32(fs + 0x14);
        f.fresnel = r.u32(fs + 0x18);
        f.bump_texture = r.s32(fs + 0x1C);
        f.bump_mode = r.u32(fs + 0x20);
        f.bump_renormalize = r.u32(fs + 0x24) != 0;
        if (auto luts = r.relative(fs + 0x28))
            for (std::size_t i = 0; i < 6; ++i) f.luts[i] = r.relative(luts + 4 * i);
        for (std::size_t i = 0; i < 6; ++i) {
            auto s = fs + 0x2C + 0x1C * i;
            f.stages[i].constant = r.u32(s);
            f.stages[i].commands = words<6>(r, s + 4);
        }
        f.alpha_test = words<2>(r, fs + 0xD4);
        f.commands = words<6>(r, fs + 0xDC);
    }
    m.hashes = words<14>(r, at + 0x2A0);
    return m;
}

std::uint32_t hash_buffer(std::span<const std::uint8_t> bytes) {
    auto digest = md5(bytes);
    std::uint32_t h = 0;
    for (std::size_t i = 0; i < digest.size(); ++i) h ^= static_cast<std::uint32_t>(digest[i]) << ((i % 4) * 8);
    return h ? h : 1;
}

std::uint32_t colors_hash(const std::array<Vec4, color_count>& colors) {
    std::vector<std::uint8_t> bytes;
    auto put = [&](float f) {
        auto bits = std::bit_cast<std::uint32_t>(f);
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    };
    for (std::size_t i = 0; i < static_cast<std::size_t>(color_count); ++i) {
        auto& c = colors[i];
        put(c.x);
        put(c.y);
        put(c.z);
        // Emission and the speculars go without their alpha; Ambient's is its scale
        auto which = static_cast<Color>(i);
        if (which != Color::emission && which != Color::specular0 && which != Color::specular1) put(c.w);
    }
    return hash_buffer(bytes);
}

void set_color(File& file, const Material& material, Color which, Vec4 value) {
    auto i = static_cast<std::size_t>(which);
    Patcher p(file.bytes());
    auto at = material.at + colors_at + 0x10 * i;
    p.f32(at, value.x);
    p.f32(at + 4, value.y);
    p.f32(at + 8, value.z);
    p.f32(at + 12, value.w);
    auto packed = pack_color(value);
    // Ambient's fourth float is a scale, and its byte stays what it was
    if (which == Color::ambient) packed = (packed & 0x00FFFFFF) | (material.packed_colors[i] & 0xFF000000);
    p.u32(material.at + packed_colors_at + 4 * i, packed);
    auto colors = material.colors;
    colors[i] = value;
    p.u32(material.at + 0x2B4, colors_hash(colors));
}

void set_coordinator(File& file, const Material& material, int index, Vec2 scale, float rotate, Vec2 translate) {
    auto& t = material.coordinators.at(static_cast<std::size_t>(index));
    Patcher p(file.bytes());
    p.f32(t.at + 0x10, scale.x);
    p.f32(t.at + 0x14, scale.y);
    p.f32(t.at + 0x18, rotate);
    p.f32(t.at + 0x1C, translate.x);
    p.f32(t.at + 0x20, translate.y);
    auto m = coordinator_matrix(t.mode, scale, rotate, translate);
    for (std::size_t k = 0; k < 12; ++k) p.f32(t.at + 0x28 + 4 * k, m.m[k]);
}

}  // namespace kabegami::cgfx
