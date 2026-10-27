#include "core/cgfx/program/program.hpp"

#include <cmath>

namespace kabegami::cgfx {
namespace {

Vec3 vec3(const Reader& r, std::size_t at) { return {r.f32(at), r.f32(at + 4), r.f32(at + 8)}; }

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalize(Vec3 v) {
    float inverse = 1.0f / std::sqrt(dot(v, v));
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}

Matrix34 rows(Vec3 x, Vec3 y, Vec3 z, Vec3 eye) {
    Matrix34 m;
    Vec3 axes[3] = {x, y, z};
    for (int r = 0; r < 3; ++r) {
        m.at(r, 0) = axes[r].x;
        m.at(r, 1) = axes[r].y;
        m.at(r, 2) = axes[r].z;
        m.at(r, 3) = -dot(axes[r], eye);
    }
    return m;
}

// the pivot rows: what HOME's updaters and MTX44Pivot do to rows 0 and 1.
void pivot_rows(Matrix44& p, int pivot) {
    auto row = [&](int r) { return std::array<float, 4>{p.at(r, 0), p.at(r, 1), p.at(r, 2), p.at(r, 3)}; };
    auto set = [&](int r, std::array<float, 4> v, float sign) {
        for (int c = 0; c < 4; ++c) p.at(r, c) = sign * v[static_cast<std::size_t>(c)];
    };
    auto r0 = row(0), r1 = row(1);
    switch (pivot) {
        case 1: set(0, r1, 1), set(1, r0, -1); break;
        case 2: set(0, r0, -1), set(1, r1, -1); break;
        case 3: set(0, r1, -1), set(1, r0, 1); break;
        default: break;  // 0 and 4 leave it
    }
}

}  // namespace

Camera read_camera(const File& file, std::string_view name) {
    auto r = file.reader();
    auto& cameras = file.dict(Slot::cameras);
    if (cameras.empty()) throw FormatError(file.name() + " has no camera");
    // HOME takes the first camera of the file
    auto at = name.empty() ? cameras.value(0) : cameras.value_of(name).value_or(0);
    if (at == 0) throw FormatError(file.name() + " has no camera named " + std::string(name));
    auto header = object_header(r, at);
    if (header.magic != "CCAM") throw FormatError(file.name() + ": no camera at " + Reader::hex(at));
    Camera c;
    c.at = at;
    c.name = header.name;
    c.scale = vec3(r, at + 0x30);
    c.rotation = vec3(r, at + 0x3C);
    c.translation = vec3(r, at + 0x48);
    c.view_type = r.u32(at + 0xB4);
    c.projection_type = r.u32(at + 0xB8);
    c.wscale = r.f32(at + 0xC4);
    if (auto view = r.relative(at + 0xBC)) {
        c.view_flags = r.u32(view + 0x04);
        switch (c.view_type) {
            case 0:
                c.target = vec3(r, view + 0x08);
                c.twist = r.f32(view + 0x14);
                break;
            case 1:
                c.target = vec3(r, view + 0x08);
                c.up = vec3(r, view + 0x14);
                break;
            case 2: c.view_rotation = vec3(r, view + 0x08); break;
            default: throw FormatError(file.name() + ": camera " + c.name + " has an unknown view type");
        }
    }
    if (auto projection = r.relative(at + 0xC0)) {
        c.near = r.f32(projection + 0x04);
        c.far = r.f32(projection + 0x08);
        c.aspect = r.f32(projection + 0x0C);
        if (c.projection_type == 0) {
            c.fovy = r.f32(projection + 0x10);
        } else if (c.projection_type <= 2) {
            c.height = r.f32(projection + 0x10);
            c.centre = {r.f32(projection + 0x14), r.f32(projection + 0x18)};
        } else {
            throw FormatError(file.name() + ": camera " + c.name + " has an unknown projection type");
        }
    }
    return c;
}

Matrix34 view_matrix(const Camera& c) {
    Vec3 eye = c.translation;
    switch (c.view_type) {
        case 0: {  // aim: MTX34LookAt(eye, twist, target)
            auto z = normalize(sub(eye, c.target));
            Vec3 v = normalize({z.z, 0.0f, -z.x});
            auto u = cross(z, v);
            auto [s, co] = sincos_fidx(c.twist);
            Vec3 x{co * v.x + s * u.x, co * v.y + s * u.y, co * v.z + s * u.z};
            Vec3 y{co * u.x - s * v.x, co * u.y - s * v.y, co * u.z - s * v.z};
            return rows(x, y, z, eye);
        }
        case 1: {  // look-at
            auto z = normalize(sub(eye, c.target));
            auto x = normalize(cross(c.up, z));
            auto y = cross(z, x);
            return rows(x, y, z, eye);
        }
        default: {  // rotate: the transpose of Ry · Rx · Rz
            auto [sx, cx] = sincos_fidx(c.view_rotation.x);
            auto [sy, cy] = sincos_fidx(c.view_rotation.y);
            auto [sz, cz] = sincos_fidx(c.view_rotation.z);
            // C = Ry · Rx · Rz, its rows
            Vec3 c0{cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx};
            Vec3 c1{cx * sz, cx * cz, -sx};
            Vec3 c2{-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx};
            // the view's rows are C's columns
            return rows({c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z}, eye);
        }
    }
}

Matrix44 projection_matrix(const Camera& c, int pivot) {
    Matrix44 p;
    float n = c.near, f = c.far;
    if (c.projection_type == 0) {
        auto [s, co] = sincos_fidx(c.fovy * 0.5f);
        float cot = co / s;
        float inv = 1.0f / (f - n);
        p.at(0, 0) = cot / c.aspect;
        p.at(1, 1) = cot;
        p.at(2, 2) = f * inv;
        p.at(2, 3) = (f * n) * inv;
        p.at(3, 2) = -1.0f;
    } else {
        float h = c.height;
        float l = c.centre.x - h * c.aspect / 2, r = c.centre.x + h * c.aspect / 2;
        float b = c.centre.y - h / 2, t = c.centre.y + h / 2;
        if (c.projection_type == 1) {  // frustum
            p.at(0, 0) = 2 * n / (r - l);
            p.at(0, 2) = (r + l) / (r - l);
            p.at(1, 1) = 2 * n / (t - b);
            p.at(1, 2) = (t + b) / (t - b);
            p.at(2, 2) = f / (f - n);
            p.at(2, 3) = f * n / (f - n);
            p.at(3, 2) = -1.0f;
        } else {  // orthographic
            p.at(0, 0) = 2 / (r - l);
            p.at(0, 3) = -(r + l) / (r - l);
            p.at(1, 1) = 2 / (t - b);
            p.at(1, 3) = -(t + b) / (t - b);
            p.at(2, 2) = 1 / (f - n);
            p.at(2, 3) = n / (f - n);
            p.at(3, 3) = 1.0f;
        }
    }
    pivot_rows(p, pivot);
    return p;
}

DepthMap depth_map(const Camera& c) {
    if (c.wscale == 0) return {true, -1.0f, 0.0f};
    return {false, -c.wscale, 0.0f};
}

const ShaderUniform* ShaderEntry::uniform(std::string_view name) const {
    for (auto& u : uniforms)
        if (u.name == name) return &u;
    return nullptr;
}

OutputMap generate_outmap(const std::vector<ShaderOutput>& outputs) {
    OutputMap map;
    std::array<std::uint32_t, 16> out;
    out.fill(0x1F1F1F1F);
    for (auto& o : outputs) {
        if (o.reg >= 16) throw FormatError("a shader output names a register past o15");
        auto& slot = out[o.reg];
        std::uint32_t mask = 0;
        // bit 0 of the mask is x, which goes in the lowest byte
        for (int c = 3; c >= 0; --c) mask = mask << 8 | ((o.mask >> c) & 1 ? 0xFFu : 0u);
        std::uint32_t value = 0x1F1F1F1F;
        switch (o.type) {
            case 0: value = 0x03020100; break;
            case 1: value = 0x07060504; break;
            case 2: value = 0x0B0A0908; break;
            case 3: value = 0x1F1F0D0C; break;
            case 4: value = 0x10101010; break;  // whichever component the mask picks
            case 5: value = 0x1F1F0F0E; break;
            case 6: value = 0x1F1F1716; break;
            case 8: value = 0x1F141312; break;
        }
        slot = (slot & ~mask) | (value & mask);
        switch (o.type) {
            case 0:
                if ((slot & 0xFF0000) == 0x020000) map.clock |= 1u << 0;
                break;
            case 1: map.clock |= 1u << 24; break;
            case 2: map.clock |= 1u << 1; break;
            case 3: map.mode = 1, map.clock |= 1u << 8; break;
            case 4: map.mode = 1, map.clock |= 1u << 16; break;
            case 5: map.mode = 1, map.clock |= 1u << 9; break;
            case 6: map.mode = 1, map.clock |= 1u << 10; break;
            case 8: map.clock |= 1u << 24; break;
        }
    }
    std::size_t next = 0;
    for (std::size_t reg = 0; reg < 16; ++reg) {
        if (out[reg] == 0x1F1F1F1F) continue;
        map.mask |= 1u << reg;
        if (next < map.regs.size()) map.regs[next] = out[reg];
        ++next;
    }
    map.total = static_cast<std::uint32_t>(next);
    return map;
}

ShaderBinary parse_shbin(ByteSpan dvlb, const std::string& what) {
    Reader r(dvlb, what);
    if (!r.magic(0, "DVLB")) throw FormatError(what + " has no DVLB");
    auto count = r.u32(4);
    auto dvlp = 8 + 4 * static_cast<std::size_t>(count);
    if (!r.magic(dvlp, "DVLP")) throw FormatError(what + " has no DVLP after its DVLE list");
    ShaderBinary b;
    auto code_at = dvlp + r.u32(dvlp + 0x08);
    auto code_words = r.u32(dvlp + 0x0C);
    for (std::uint32_t i = 0; i < code_words; ++i) b.code.push_back(r.u32(code_at + 4 * i));
    auto descriptors_at = dvlp + r.u32(dvlp + 0x10);
    auto descriptors = r.u32(dvlp + 0x14);
    for (std::uint32_t i = 0; i < descriptors; ++i) b.descriptors.push_back(r.u32(descriptors_at + 8 * i));
    for (std::uint32_t i = 0; i < count; ++i) {
        auto e = static_cast<std::size_t>(r.u32(8 + 4 * i));
        if (!r.magic(e, "DVLE")) throw FormatError(what + ": DVLE " + std::to_string(i) + " is not where the DVLB says");
        ShaderEntry entry;
        entry.type = r.u8(e + 0x06);
        entry.main = r.u32(e + 0x08);
        entry.endmain = r.u32(e + 0x0C);
        entry.input_mask = r.u16(e + 0x10);
        entry.output_mask = r.u16(e + 0x12);
        auto constants = e + r.u32(e + 0x18);
        for (std::uint32_t k = 0; k < r.u32(e + 0x1C); ++k) {
            auto at = constants + 0x14 * k;
            ShaderConstant c{static_cast<ShaderConstant::Type>(r.u8(at)), r.u8(at + 2), {}};
            if (c.type == ShaderConstant::Type::boolean) {
                c.raw[0] = r.u8(at + 4);
            } else if (c.type == ShaderConstant::Type::integer) {
                for (std::size_t j = 0; j < 4; ++j) c.raw[j] = r.u8(at + 4 + j);
            } else {
                for (std::size_t j = 0; j < 4; ++j) c.raw[j] = r.u32(at + 4 + 4 * j);
            }
            entry.constants.push_back(c);
        }
        auto outputs = e + r.u32(e + 0x28);
        for (std::uint32_t k = 0; k < r.u32(e + 0x2C); ++k) {
            auto at = outputs + 8 * k;
            entry.outputs.push_back({r.u16(at), r.u16(at + 2), r.u16(at + 4)});
        }
        auto symbols = e + r.u32(e + 0x38);
        auto uniforms = e + r.u32(e + 0x30);
        for (std::uint32_t k = 0; k < r.u32(e + 0x34); ++k) {
            auto at = uniforms + 8 * k;
            entry.uniforms.push_back({r.cstring(symbols + r.u32(at)), r.u16(at + 4), r.u16(at + 6)});
        }
        entry.outmap = generate_outmap(entry.outputs);
        b.entries.push_back(std::move(entry));
    }
    return b;
}

ShaderProgram read_shader(const File& file, std::string_view name) {
    auto r = file.reader();
    auto& shaders = file.dict(Slot::shaders);
    if (shaders.empty()) throw FormatError(file.name() + " has no shader");
    auto at = name.empty() ? shaders.value(0) : shaders.value_of(name).value_or(0);
    if (at == 0) throw FormatError(file.name() + " has no shader named " + std::string(name));
    auto header = object_header(r, at);
    if (header.magic != "SHDR") throw FormatError(file.name() + ": no shader at " + Reader::hex(at));
    ShaderProgram p;
    p.name = header.name;
    auto dvlb_size = r.u32(at + 0x18);
    auto dvlb = r.relative(at + 0x1C);
    p.binary = parse_shbin(r.bytes(dvlb, dvlb_size), file.name() + " " + p.name);
    for (auto d : object_list(r, at + 0x28)) {
        ProgramDescription desc;
        desc.flags = r.u32(d);
        desc.vertex = r.s32(d + 0x0C);
        desc.geometry = r.s32(d + 0x10);
        for (std::size_t u = 0; u < desc.inputs.size(); ++u) desc.inputs[u] = static_cast<std::int8_t>(r.u8(d + 0x4C + u));
        if (desc.vertex < 0 || static_cast<std::size_t>(desc.vertex) >= p.binary.entries.size())
            throw FormatError(file.name() + ": a program description names a DVLE that is not there");
        p.descriptions.push_back(desc);
    }
    return p;
}

}  // namespace kabegami::cgfx
