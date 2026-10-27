#include "core/pica/gpu/raster.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

#include "core/pica/fragment/blend.hpp"
#include "core/pica/fragment/format.hpp"
#include "core/pica/fragment/texture.hpp"

namespace kabegami::pica {
namespace {

std::uint32_t location(const Registers& r, std::size_t index) { return (r[index] & 0x0FFFFFFF) << 3; }

float float24_register(const Registers& r, std::size_t index) { return decode_float24(r[index]); }

// Rust's float-to-int casts: truncation, NaN to 0, held at the ends
template <typename I>
I saturate(float f) {
    if (std::isnan(f)) return 0;
    if (f <= static_cast<float>(std::numeric_limits<I>::min())) return std::numeric_limits<I>::min();
    if (f >= static_cast<float>(std::numeric_limits<I>::max())) return std::numeric_limits<I>::max();
    return static_cast<I>(f);
}

std::uint8_t to_byte(float c) { return saturate<std::uint8_t>(c * 255.0f); }

// -- vertex fetch ----------------------------------------------------------

struct Field {
    std::uint32_t offset, type, count;
    std::size_t reg;
};

struct LoaderPlan {
    std::uint32_t offset, stride, size;
    std::vector<Field> fields;
};

// what a draw's vertices are made of: what each input register holds when no
// array feeds it, and the fields the arrays give
struct InputPlan {
    std::array<Vec4, input_registers> base{};
    std::vector<LoaderPlan> loaders;

    InputPlan(const Registers& r, const std::array<Vec4, 16>& fixed) {
        auto formats = static_cast<std::uint64_t>(r[reg::attribbuffers_format_low]) |
                       static_cast<std::uint64_t>(r[reg::attribbuffers_format_high]) << 32;
        struct Source {
            std::size_t loader, field;
        };
        std::array<std::optional<Source>, 12> source;
        struct Laid {
            std::uint32_t id, offset, type, count;
        };
        std::vector<std::vector<Laid>> laid;
        for (std::size_t i = 0; i < reg::loader_count; ++i) {
            auto at = reg::attribbuffer0 + i * 3;
            auto word1 = r[at + 1], word2 = r[at + 2];
            auto components = static_cast<std::uint64_t>(word1) | static_cast<std::uint64_t>(word2 & 0xFFFF) << 32;
            // the bytes a vertex takes are bits 16-23, the component count bits 28-31
            std::uint32_t stride = (word2 >> 16) & 0xFF, count = (word2 >> 28) & 0xF;
            if (count == 0) continue;
            std::uint32_t offset = 0;
            std::vector<Laid> fields;
            for (std::uint32_t slot = 0; slot < std::min(count, 12u); ++slot) {
                auto id = static_cast<std::uint32_t>((components >> (slot * 4)) & 0xF);
                if (id >= 12) {
                    // padding, aligned to a word, of 4 to 16 bytes
                    offset = (offset + 3) / 4 * 4 + (id - 11) * 4;
                    continue;
                }
                auto nibble = static_cast<std::uint32_t>((formats >> (id * 4)) & 0xF);
                std::uint32_t type = nibble & 3, n = (nibble >> 2) + 1;
                std::uint32_t size = type <= 1 ? 1 : type == 2 ? 2 : 4;
                // each attribute starts aligned to its component size
                offset = (offset + size - 1) / size * size;
                fields.push_back({id, offset, type, n});
                offset += size * n;
            }
            for (std::size_t f = 0; f < fields.size(); ++f) source[fields[f].id] = Source{loaders.size(), f};
            loaders.push_back({r[at] & 0x0FFFFFFF, stride, offset, {}});
            laid.push_back(std::move(fields));
        }
        // the unit's permutation places the attributes, a later one over an
        // earlier one in the same register
        auto count = std::min<std::size_t>((r[reg::vsh_block + reg::shader::input_config] & 0xF) + 1, 12);
        auto map = static_cast<std::uint64_t>(r[reg::vsh_block + reg::shader::attributes_permutation_low]) |
                   static_cast<std::uint64_t>(r[reg::vsh_block + reg::shader::attributes_permutation_high]) << 32;
        std::array<std::optional<std::size_t>, input_registers> owner;
        for (std::size_t id = 0; id < count; ++id) owner[(map >> (id * 4)) & 0xF] = id;
        auto fixed_mask = (r[reg::attribbuffers_format_high] >> 16) & 0xFFF;
        for (std::size_t reg_index = 0; reg_index < input_registers; ++reg_index) {
            if (!owner[reg_index]) continue;
            auto id = *owner[reg_index];
            if (source[id]) {
                auto& f = laid[source[id]->loader][source[id]->field];
                loaders[source[id]->loader].fields.push_back({f.offset, f.type, f.count, reg_index});
            } else if (fixed_mask & (1u << id)) {
                base[reg_index] = fixed[id];
            } else {
                base[reg_index] = {0, 0, 0, 1};
            }
        }
        std::erase_if(loaders, [](const LoaderPlan& l) { return l.fields.empty(); });
    }

    std::array<Vec4, input_registers> fetch(const Memory& memory, std::uint32_t arrays, std::uint32_t vertex) const {
        auto input = base;
        std::uint8_t bytes[256];
        for (auto& loader : loaders) {
            auto addr = arrays + loader.offset + vertex * loader.stride;
            auto size = std::min<std::size_t>(loader.size, sizeof bytes);
            memory.read(addr, {bytes, size});
            for (auto& f : loader.fields) {
                // components an array doesn't give read as (0, 0, 0, 1)
                Vec4 value{0, 0, 0, 1};
                for (std::uint32_t i = 0; i < f.count; ++i) {
                    auto at = f.offset + i * (f.type <= 1 ? 1 : f.type == 2 ? 2 : 4);
                    if (at + 4 > sizeof bytes) break;
                    auto& slot = value[i];
                    switch (f.type) {
                        case 0: slot = static_cast<float>(static_cast<std::int8_t>(bytes[at])); break;
                        case 1: slot = static_cast<float>(bytes[at]); break;
                        case 2: slot = static_cast<float>(static_cast<std::int16_t>(bytes[at] | bytes[at + 1] << 8)); break;
                        default:
                            slot = std::bit_cast<float>(static_cast<std::uint32_t>(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 |
                                                                                   static_cast<std::uint32_t>(bytes[at + 3]) << 24));
                    }
                }
                input[f.reg] = value;
            }
        }
        return input;
    }
};

// -- shader outputs to varyings ----------------------------------------------

// where each of a vertex's values is in the output registers: the register
// times four plus the component, or one of these
constexpr std::uint8_t plan_zero = 64;
// a colour no output gives is white: a shader that writes no colour means
// an unlit, unmodulated draw
constexpr std::uint8_t plan_one = 65;

struct VertexPlan {
    std::array<std::uint8_t, 14> from{};  // clip, colour, the three texcoords

    explicit VertexPlan(const Registers& r) {
        using Slot = std::optional<std::pair<std::size_t, std::size_t>>;
        std::array<std::pair<std::size_t, std::size_t>, 4> position{{{0, 0}, {0, 1}, {0, 2}, {0, 3}}};
        std::array<Slot, 4> color;
        std::array<std::array<Slot, 2>, 3> texcoords;
        auto total = std::min<std::uint32_t>(r[reg::outmap_total] & 7, 7);
        bool saw_position = false;
        for (std::size_t attribute = 0; attribute < total; ++attribute) {
            auto word = r[reg::outmap_o0 + attribute];
            for (std::size_t c = 0; c < 4; ++c) {
                // each byte names what that component carries
                auto semantic = (word >> (c * 8)) & 0x1F;
                std::pair slot{attribute, c};
                if (semantic <= 3) {
                    position[semantic] = slot;
                    saw_position = true;
                } else if (semantic >= 8 && semantic <= 11) {
                    color[semantic - 8] = slot;
                } else if (semantic == 12 || semantic == 13) {
                    texcoords[0][semantic - 12] = slot;
                } else if (semantic == 14 || semantic == 15) {
                    texcoords[1][semantic - 14] = slot;
                } else if (semantic == 22 || semantic == 23) {
                    texcoords[2][semantic - 22] = slot;
                }
            }
        }
        if (total == 0 || !saw_position) {
            // what picasso gives a shader that says nothing, for registers
            // that describe nothing at all
            position = {{{0, 0}, {0, 1}, {0, 2}, {0, 3}}};
            color = {std::pair{2u, 0u}, std::pair{2u, 1u}, std::pair{2u, 2u}, std::pair{2u, 3u}};
            texcoords = {};
            texcoords[0] = {std::pair{3u, 0u}, std::pair{3u, 1u}};
        }
        // an attribute is the nth output register the mask enables; a mask
        // of zero takes the registers as they are
        auto mask = r[reg::vsh_block + reg::shader::outmap_mask] & 0xFFFF;
        std::array<std::optional<std::size_t>, output_registers> registers;
        std::size_t n = 0;
        for (std::size_t o = 0; o < output_registers; ++o)
            if (mask == 0 || mask & (1u << o)) registers[n++] = o;
        auto read = [&](std::pair<std::size_t, std::size_t> s) {
            return registers[s.first] ? static_cast<std::uint8_t>(*registers[s.first] * 4 + s.second) : plan_zero;
        };
        auto or_ = [&](const Slot& s, std::uint8_t missing) { return s ? read(*s) : missing; };
        for (std::size_t i = 0; i < 4; ++i) {
            from[i] = read(position[i]);
            from[4 + i] = or_(color[i], plan_one);
        }
        for (std::size_t set = 0; set < 3; ++set)
            for (std::size_t i = 0; i < 2; ++i) from[8 + set * 2 + i] = or_(texcoords[set][i], plan_zero);
    }

    Vertex vertex(const std::array<Vec4, output_registers>& outputs) const {
        auto get = [&](std::size_t i) {
            auto f = from[i];
            if (f == plan_zero) return 0.0f;
            if (f == plan_one) return 1.0f;
            return outputs[f / 4][f % 4];
        };
        Vertex v;
        v.clip = {get(0), get(1), get(2), get(3)};
        v.color = {get(4), get(5), get(6), get(7)};
        for (std::size_t set = 0; set < 3; ++set) v.texcoords[set] = {get(8 + set * 2), get(9 + set * 2)};
        return v;
    }
};

// -- clipping and the screen ---------------------------------------------------

// a vertex divided by w, so the rasterizer interpolates linearly and divides
// once a pixel
struct Screen {
    float x, y;
    float z;  // z/w, which the depth map turns into what the buffer holds
    float inv_w;
    Color color_over_w;
    std::array<std::array<float, 2>, 3> texcoords_over_w;
};

std::optional<Screen> to_screen(const Vertex& v, float vx, float vy, float vw, float vh) {
    auto w = v.clip[3];
    if (std::abs(w) < 1e-8f) return std::nullopt;
    auto inv_w = 1.0f / w;
    auto ndc_x = v.clip[0] * inv_w, ndc_y = v.clip[1] * inv_w;
    // window coordinates, y up from the bottom of the buffer, on a sixteenth
    // of a pixel
    auto snap = [](float c) { return std::round(c * 16.0f) / 16.0f; };
    Screen s;
    s.x = snap(vx + (ndc_x * 0.5f + 0.5f) * vw);
    s.y = snap(vy + (ndc_y * 0.5f + 0.5f) * vh);
    s.z = v.clip[2] * inv_w;
    s.inv_w = inv_w;
    for (std::size_t i = 0; i < 4; ++i) s.color_over_w[i] = v.color[i] * inv_w;
    for (std::size_t set = 0; set < 3; ++set)
        s.texcoords_over_w[set] = {v.texcoords[set][0] * inv_w, v.texcoords[set][1] * inv_w};
    return s;
}

// the volume the PICA draws: w positive and -w <= z <= 0
float plane(int p, const Vertex& v) {
    switch (p) {
        case 0: return v.clip[3] - 1e-5f;
        case 1: return -v.clip[2];
        default: return v.clip[2] + v.clip[3];
    }
}

bool inside(const Vertex& v) { return plane(0, v) >= 0 && plane(1, v) >= 0 && plane(2, v) >= 0; }

// z on an end of the range when it misses one only by rounding, as singles
// do where the PICA's 24-bit floats land on 0 and -w
Vertex held_in_range(Vertex v) {
    auto z = v.clip[2], w = v.clip[3];
    auto zw = z / w;
    if (zw > 0 && zw < 1e-8f)
        v.clip[2] = 0;
    else if (zw < -1 && zw > -1.00001f)
        v.clip[2] = -w;
    return v;
}

std::vector<Vertex> clip_triangle(std::array<Vertex, 3> triangle) {
    for (auto& v : triangle) v = held_in_range(v);
    std::vector<Vertex> polygon(triangle.begin(), triangle.end());
    if (std::all_of(triangle.begin(), triangle.end(), inside)) return polygon;
    for (int p = 0; p < 3; ++p) {
        std::vector<Vertex> clipped;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            auto& current = polygon[i];
            auto& next = polygon[(i + 1) % polygon.size()];
            auto d0 = plane(p, current), d1 = plane(p, next);
            if (d0 >= 0) clipped.push_back(current);
            if ((d0 >= 0) != (d1 >= 0)) clipped.push_back(current.lerp(next, d0 / (d0 - d1)));
        }
        polygon = std::move(clipped);
        if (polygon.size() < 3) return {};
    }
    return polygon;
}

// -- textures ------------------------------------------------------------------

enum class Wrap { clamp_to_edge, clamp_to_border, repeat, mirrored_repeat };

Wrap wrap_from_raw(std::uint32_t value) {
    // three bits; the upper four encodings behave like the lower ones
    switch (value & 7) {
        case 0:
        case 4: return Wrap::clamp_to_edge;
        case 1:
        case 5: return Wrap::clamp_to_border;
        case 3: return Wrap::mirrored_repeat;
        default: return Wrap::repeat;
    }
}

std::uint32_t wrap(Wrap w, std::int32_t c, std::uint32_t size) {
    auto s = static_cast<std::int32_t>(size);
    switch (w) {
        case Wrap::clamp_to_edge:
        case Wrap::clamp_to_border: return static_cast<std::uint32_t>(std::clamp(c, 0, s - 1));
        case Wrap::repeat: return static_cast<std::uint32_t>(((c % s) + s) % s);
        case Wrap::mirrored_repeat: {
            auto period = ((c % (s * 2)) + s * 2) % (s * 2);
            return static_cast<std::uint32_t>(period < s ? period : s * 2 - 1 - period);
        }
    }
    return 0;
}

struct BoundTexture {
    std::vector<Rgba> texels;  // row by row from the top
    bool linear = false;
    Wrap wrap_s = Wrap::repeat, wrap_t = Wrap::repeat;
    std::uint32_t width = 1, height = 1;
    Color border{};

    Color texel(std::int32_t x, std::int32_t y) const {
        auto outside = [](Wrap w, std::int32_t c, std::uint32_t size) {
            return w == Wrap::clamp_to_border && (c < 0 || c >= static_cast<std::int32_t>(size));
        };
        if (outside(wrap_s, x, width) || outside(wrap_t, y, height)) return border;
        auto t = texels[static_cast<std::size_t>(wrap(wrap_t, y, height)) * width + wrap(wrap_s, x, width)];
        return {static_cast<float>(t[0]) / 255.0f, static_cast<float>(t[1]) / 255.0f, static_cast<float>(t[2]) / 255.0f,
                static_cast<float>(t[3]) / 255.0f};
    }

    Color sample(float u, float v) const {
        // v = 0 is the bottom row
        auto fx = u * static_cast<float>(width), fy = (1.0f - v) * static_cast<float>(height);
        if (!linear) return texel(saturate<std::int32_t>(std::floor(fx)), saturate<std::int32_t>(std::floor(fy)));
        // texel centres sit at half-integers
        fx -= 0.5f;
        fy -= 0.5f;
        auto x0 = std::floor(fx), y0 = std::floor(fy);
        auto tx = fx - x0, ty = fy - y0;
        auto ix = saturate<std::int32_t>(x0), iy = saturate<std::int32_t>(y0);
        auto a = texel(ix, iy), b = texel(ix + 1, iy), c = texel(ix, iy + 1), d = texel(ix + 1, iy + 1);
        Color out;
        for (std::size_t i = 0; i < 4; ++i) {
            auto top = a[i] + (b[i] - a[i]) * tx;
            auto bottom = c[i] + (d[i] - c[i]) * tx;
            out[i] = top + (bottom - top) * ty;
        }
        return out;
    }
};

// what a unit that is on but points at no texture samples: transparent black
BoundTexture empty_texture() {
    BoundTexture t;
    t.texels = {Rgba{0, 0, 0, 0}};
    return t;
}

std::optional<BoundTexture> bind_texture(const Registers& r, const Memory& memory, std::size_t unit) {
    if (!(r[reg::texunit_config] & (1u << unit))) return std::nullopt;
    auto base = reg::texunit_bases[unit];
    auto dimensions = r[base + 1];
    std::uint32_t height = dimensions & 0x7FF, width = (dimensions >> 16) & 0x7FF;
    if (width == 0 || height == 0) return empty_texture();
    auto format = texture_format_from_raw(r[unit == 0 ? base + 13 : base + 5] & 0xF);
    auto address = location(r, base + 4);
    if (address == 0) return empty_texture();
    std::vector<std::uint8_t> data(texture_size(format, width, height));
    memory.read(address, data);
    // the filter for magnification in bit 1, the wrap modes for T and S in
    // bits 8-10 and 12-14
    auto config = r[base + 2];
    BoundTexture t;
    t.texels = decode_texture(data, format, width, height);
    t.linear = config & 2;
    t.wrap_t = wrap_from_raw(config >> 8);
    t.wrap_s = wrap_from_raw(config >> 12);
    t.width = width;
    t.height = height;
    for (std::size_t i = 0; i < 4; ++i) t.border[i] = static_cast<float>((r[base] >> (8 * i)) & 0xFF) / 255.0f;
    return t;
}

// -- the depth and stencil buffer --------------------------------------------

struct DepthStencil {
    std::uint32_t addr;
    std::uint32_t bytes;  // 2 for D16, 3 for D24, 4 for D24S8 with the stencil on top
    std::optional<Compare> test;
    bool write_depth;
    std::optional<Stencil> stencil;
    bool write_stencil;

    static std::optional<DepthStencil> read(const Registers& r) {
        // GPUREG_DEPTH_COLOR_MASK: bit 0 the test, bits 4-6 its function,
        // bit 12 writing depth back
        auto mask = r[reg::depth_color_mask];
        bool writable = r[reg::depthbuffer_write] != 0;
        auto format = r[reg::depthbuffer_format] & 3;
        std::optional<Compare> test;
        if (mask & 1) test = compare_from_raw(mask >> 4);
        bool write_depth = writable && (mask & (1u << 12));
        auto stencil = format == 3 ? Stencil::read(r) : std::nullopt;
        if (!test && !write_depth && !stencil) return std::nullopt;
        return DepthStencil{location(r, reg::depthbuffer_location), format == 0 ? 2u : format == 2 ? 3u : 4u, test, write_depth, stencil,
                            writable};
    }

    std::pair<float, std::uint8_t> read_sample(const std::uint8_t* p) const {
        std::uint32_t value = 0;
        for (std::uint32_t i = 0; i < bytes; ++i) value |= static_cast<std::uint32_t>(p[i]) << (8 * i);
        if (bytes == 2) return {static_cast<float>(value) / 65535.0f, 0};
        return {static_cast<float>(value & 0xFFFFFF) / 16777215.0f, static_cast<std::uint8_t>(value >> 24)};
    }

    void store_depth(std::uint8_t* p, float depth) const {
        if (bytes == 2) {
            auto v = saturate<std::uint16_t>(depth * 65535.0f);
            p[0] = static_cast<std::uint8_t>(v);
            p[1] = static_cast<std::uint8_t>(v >> 8);
        } else {
            // the low three bytes, the stencil shares the word
            auto v = saturate<std::uint32_t>(depth * 16777215.0f);
            for (int i = 0; i < 3; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    }

    void store_stencil(std::uint8_t* p, std::uint8_t value) const {
        if (write_stencil && bytes == 4) p[3] = value;
    }
};

// -- filling ---------------------------------------------------------------------

std::int64_t fixed(float c) { return saturate<std::int64_t>(std::round(c * 16.0f)); }

// one edge as a function that is zero on it and positive on the triangle's
// side, in whole sixteenths
struct Edge {
    std::int64_t x, y, dx, dy;
    bool owns_ties;

    Edge(const Screen& from, const Screen& to)
        : x(fixed(from.x)), y(fixed(from.y)), dx(fixed(to.x) - x), dy(fixed(to.y) - y),
          // a pixel centred on an edge goes to the triangle on its right, or
          // above a flat one
          owns_ties(dy < 0 || (dy == 0 && dx > 0)) {}

    std::int64_t at(std::int64_t px, std::int64_t py) const { return dx * (py - y) - dy * (px - x); }
    bool covers(std::int64_t value) const { return value > 0 || (value == 0 && owns_ties); }
};

struct Target {
    std::uint32_t addr;
    std::int32_t left, bottom, right, top;
    std::uint32_t width, height;
    ColorFormat format;
    std::array<bool, 4> write;
};

struct DrawState {
    Target target;
    float depth_scale, depth_offset;
    bool w_buffer;
    std::optional<DepthStencil> depth_stencil;
    TexEnv tex_env;
    std::optional<AlphaTest> alpha_test;
    std::optional<Blend> blend;
    std::optional<LogicOp> logic_op;
    std::array<std::optional<BoundTexture>, 3> textures;
    bool texture2_uses_coord1;
};

void fill_triangle(Memory& memory, std::int32_t low, std::int32_t high, Screen a, Screen b, Screen c, const DrawState& state,
                   DrawStats& stats) {
    auto& target = state.target;
    auto min_x = std::max(saturate<std::int32_t>(std::floor(std::min({a.x, b.x, c.x}))), target.left);
    auto max_x = std::min(saturate<std::int32_t>(std::ceil(std::max({a.x, b.x, c.x}))), target.right);
    auto min_y = std::max({saturate<std::int32_t>(std::floor(std::min({a.y, b.y, c.y}))), target.bottom, low});
    auto max_y = std::min({saturate<std::int32_t>(std::ceil(std::max({a.y, b.y, c.y}))), target.top, high});
    if (min_x >= max_x || min_y >= max_y) return;

    // wind every triangle the same way, so inside is the positive side of
    // all three edges
    auto area = Edge(a, b).at(fixed(c.x), fixed(c.y));
    if (area < 0) {
        area = -area;
        std::swap(b, c);
    }
    if (area == 0) return;
    auto inverse_area = 1.0f / static_cast<float>(area);
    const Edge edges[3] = {Edge(b, c), Edge(c, a), Edge(a, b)};

    auto bpp = bytes_per_pixel(target.format);
    bool writes_color = std::any_of(target.write.begin(), target.write.end(), [](bool w) { return w; });
    bool partial = writes_color && !std::all_of(target.write.begin(), target.write.end(), [](bool w) { return w; });
    std::size_t pixels = static_cast<std::size_t>(target.width) * target.height;
    auto color = writes_color ? memory.span(target.addr, pixels * bpp) : std::span<std::uint8_t>{};
    auto& ds = state.depth_stencil;
    auto depth = ds ? memory.span(ds->addr, pixels * ds->bytes) : std::span<std::uint8_t>{};

    for (auto y = min_y; y < max_y; ++y) {
        for (auto x = min_x; x < max_x; ++x) {
            // the pixel's centre, in sixteenths
            std::int64_t px = static_cast<std::int64_t>(x) * 16 + 8, py = static_cast<std::int64_t>(y) * 16 + 8;
            std::int64_t values[3];
            bool covered = true;
            for (int e = 0; e < 3; ++e) {
                values[e] = edges[e].at(px, py);
                covered = covered && edges[e].covers(values[e]);
            }
            if (!covered) continue;
            auto w0 = static_cast<float>(values[0]) * inverse_area, w1 = static_cast<float>(values[1]) * inverse_area,
                 w2 = static_cast<float>(values[2]) * inverse_area;
            // perspective-correct: what is over w and 1/w go linearly on the
            // screen, then a divide
            auto inv_w = w0 * a.inv_w + w1 * b.inv_w + w2 * c.inv_w;
            if (std::abs(inv_w) < 1e-8f) continue;
            auto w = 1.0f / inv_w;
            auto z = (w0 * a.z + w1 * b.z + w2 * c.z) * state.depth_scale + state.depth_offset;
            if (state.w_buffer) z *= w;
            z = std::clamp(z, 0.0f, 1.0f);

            auto row = target.height - 1 - static_cast<std::uint32_t>(y);
            auto index = morton_offset(static_cast<std::uint32_t>(x), row, target.width, 1);

            std::uint8_t* sample = nullptr;
            std::pair<float, std::uint8_t> stored{};
            if (ds && static_cast<std::size_t>(index) * ds->bytes + ds->bytes <= depth.size()) {
                sample = depth.data() + static_cast<std::size_t>(index) * ds->bytes;
                stored = ds->read_sample(sample);
                // with no stencil to update, a fragment failing the depth test
                // is gone whatever its colour
                if (!ds->stencil && ds->test && !passes(*ds->test, z, stored.first)) {
                    ++stats.depth_failed;
                    continue;
                }
            }

            auto interpolate = [&](float v0, float v1, float v2) { return (w0 * v0 + w1 * v1 + w2 * v2) * w; };
            Color primary;
            for (std::size_t i = 0; i < 4; ++i)
                primary[i] = std::clamp(interpolate(a.color_over_w[i], b.color_over_w[i], c.color_over_w[i]), 0.0f, 1.0f);
            std::array<Color, 4> samples;
            samples.fill({0, 0, 0, 1});
            for (std::size_t unit = 0; unit < 3; ++unit) {
                auto& texture = state.textures[unit];
                if (!texture) continue;
                auto set = unit == 2 && state.texture2_uses_coord1 ? 1 : unit;
                auto u = interpolate(a.texcoords_over_w[set][0], b.texcoords_over_w[set][0], c.texcoords_over_w[set][0]);
                auto v = interpolate(a.texcoords_over_w[set][1], b.texcoords_over_w[set][1], c.texcoords_over_w[set][1]);
                samples[unit] = texture->sample(u, v);
            }
            auto combined = state.tex_env.apply(primary, samples);
            Rgba rgba{to_byte(combined[0]), to_byte(combined[1]), to_byte(combined[2]), to_byte(combined[3])};

            if (state.alpha_test && !state.alpha_test->passes(rgba[3])) {
                ++stats.alpha_failed;
                continue;
            }

            if (sample) {
                if (ds->stencil && !ds->stencil->passes(stored.second)) {
                    ds->store_stencil(sample, ds->stencil->update(stored.second, ds->stencil->fail));
                    ++stats.stencil_failed;
                    continue;
                }
                bool depth_passes = !ds->test || passes(*ds->test, z, stored.first);
                if (ds->stencil)
                    ds->store_stencil(sample, ds->stencil->update(stored.second, depth_passes ? ds->stencil->pass : ds->stencil->depth_fail));
                if (!depth_passes) {
                    ++stats.depth_failed;
                    continue;
                }
                if (ds->write_depth) ds->store_depth(sample, z);
            }

            if (!writes_color || static_cast<std::size_t>(index) * bpp + bpp > color.size()) continue;
            auto pixel = color.data() + static_cast<std::size_t>(index) * bpp;
            // with what is in the buffer, as the output merger says, keeping the
            // channels the draw may not change
            if (state.blend || state.logic_op || partial) {
                auto existing = decode_pixel(target.format, pixel);
                if (state.blend) {
                    Color s, d;
                    for (std::size_t i = 0; i < 4; ++i) {
                        s[i] = static_cast<float>(rgba[i]) / 255.0f;
                        d[i] = static_cast<float>(existing[i]) / 255.0f;
                    }
                    auto blended = state.blend->apply(s, d);
                    for (std::size_t i = 0; i < 4; ++i) rgba[i] = to_byte(blended[i]);
                } else if (state.logic_op) {
                    for (std::size_t i = 0; i < 4; ++i) rgba[i] = apply(*state.logic_op, rgba[i], existing[i]);
                }
                for (std::size_t i = 0; i < 4; ++i)
                    if (!target.write[i]) rgba[i] = existing[i];
            }
            encode_pixel(target.format, rgba, pixel);
            ++stats.written;
        }
    }
}

// the vertices of each triangle out of count of them, for a list, a strip (1)
// or a fan (2)
std::vector<std::array<std::size_t, 3>> assemble(std::uint32_t topology, std::size_t count) {
    std::vector<std::array<std::size_t, 3>> triangles;
    if (topology == 1) {
        for (std::size_t i = 2; i < count; ++i)
            triangles.push_back(i % 2 == 0 ? std::array{i - 2, i - 1, i} : std::array{i - 1, i - 2, i});
    } else if (topology == 2) {
        for (std::size_t i = 2; i < count; ++i) triangles.push_back({0, i - 1, i});
    } else {
        for (std::size_t t = 0; t < count / 3; ++t) triangles.push_back({t * 3, t * 3 + 1, t * 3 + 2});
    }
    return triangles;
}

}  // namespace

Vertex Vertex::lerp(const Vertex& other, float t) const {
    auto mix = [t](float a, float b) { return a + (b - a) * t; };
    Vertex v;
    for (std::size_t i = 0; i < 4; ++i) {
        v.clip[i] = mix(clip[i], other.clip[i]);
        v.color[i] = mix(color[i], other.color[i]);
    }
    for (std::size_t set = 0; set < 3; ++set)
        for (std::size_t i = 0; i < 2; ++i) v.texcoords[set][i] = mix(texcoords[set][i], other.texcoords[set][i]);
    return v;
}

std::vector<Vertex> place_outputs(const Registers& registers, std::span<const std::array<Vec4, output_registers>> outputs) {
    VertexPlan plan(registers);
    std::vector<Vertex> vertices;
    vertices.reserve(outputs.size());
    for (auto& o : outputs) vertices.push_back(plan.vertex(o));
    return vertices;
}

DrawStats rasterize(const Registers& r, std::span<const Vertex> shaded, Memory& memory) {
    DrawStats stats;
    stats.vertices = static_cast<std::uint32_t>(shaded.size());
    // the offset is two signed 10-bit fields
    auto signed10 = [](std::uint32_t v) {
        return static_cast<float>(static_cast<std::int32_t>((v & 0x3FF) << 22) >> 22);
    };
    auto vx = signed10(r[reg::viewport_xy]), vy = signed10(r[reg::viewport_xy] >> 16);
    // the registers hold half the extent
    auto vw = float24_register(r, reg::viewport_width) * 2.0f, vh = float24_register(r, reg::viewport_height) * 2.0f;
    if (!(vw > 0) || !(vh > 0)) return stats;

    auto dimensions = r[reg::framebuffer_dimensions];
    auto viewport_right = saturate<std::int32_t>(std::round(vx + vw)), viewport_top = saturate<std::int32_t>(std::round(vy + vh));
    // the buffer's own size, often padded past the viewport
    auto width = std::max(dimensions & 0x7FF, static_cast<std::uint32_t>(std::max(viewport_right, 1)));
    auto height = std::max(((dimensions >> 12) & 0x3FF) + 1, static_cast<std::uint32_t>(std::max(viewport_top, 1)));
    bool color_writable = r[reg::colorbuffer_write] != 0;
    auto color_mask = r[reg::depth_color_mask] >> 8;

    DrawState state{
        Target{location(r, reg::colorbuffer_location), std::max(saturate<std::int32_t>(std::round(vx)), 0),
               std::max(saturate<std::int32_t>(std::round(vy)), 0), std::min(viewport_right, static_cast<std::int32_t>(width)),
               std::min(viewport_top, static_cast<std::int32_t>(height)), width, height,
               // the format sits in bits 16-18, the low half is the bytes a pixel takes
               color_format_from_buffer((r[reg::colorbuffer_format] >> 16) & 7),
               {color_writable && (color_mask & 1), color_writable && (color_mask & 2), color_writable && (color_mask & 4),
                color_writable && (color_mask & 8)}},
        float24_register(r, reg::depthmap_scale),
        float24_register(r, reg::depthmap_offset),
        !(r[reg::depthmap_enable] & 1),
        DepthStencil::read(r),
        TexEnv::read(r),
        AlphaTest::read(r),
        Blend::read(r),
        read_logic_op(r),
        {bind_texture(r, memory, 0), bind_texture(r, memory, 1), bind_texture(r, memory, 2)},
        (r[reg::texunit_config] & (1u << 13)) != 0,
    };

    // GPUREG_PRIMITIVE_CONFIG bits 8-9: 0 a list, 1 a strip, 2 a fan, 3 what
    // a geometry shader emitted, a list
    auto topology = (r[reg::primitive_config] >> 8) & 3;
    // 0 keeps everything, 1 keeps triangles wound clockwise and 2
    // counter-clockwise, with y up
    auto cull_mode = r[reg::face_culling] & 3;
    auto culls = [&](const Screen& p, const Screen& q, const Screen& s) {
        bool counter_clockwise = (q.x - p.x) * (s.y - p.y) - (q.y - p.y) * (s.x - p.x) > 0;
        return cull_mode != 0 && counter_clockwise == (cull_mode == 1);
    };

    std::vector<std::array<Screen, 3>> triangles;
    for (auto [ia, ib, ic] : assemble(topology, shaded.size())) {
        ++stats.triangles;
        std::array<Vertex, 3> t{shaded[ia], shaded[ib], shaded[ic]};
        if (inside(t[0]) && inside(t[1]) && inside(t[2])) {
            auto sa = to_screen(t[0], vx, vy, vw, vh), sb = to_screen(t[1], vx, vy, vw, vh), sc = to_screen(t[2], vx, vy, vw, vh);
            if (sa && sb && sc) {
                if (culls(*sa, *sb, *sc))
                    ++stats.culled;
                else
                    triangles.push_back({*sa, *sb, *sc});
                continue;
            }
        }
        auto polygon = clip_triangle(t);
        std::vector<Screen> screen;
        for (auto& v : polygon)
            if (auto s = to_screen(v, vx, vy, vw, vh)) screen.push_back(*s);
        if (screen.size() < 3 || screen.size() != polygon.size()) {
            ++stats.clipped_away;
            continue;
        }
        if (culls(screen[0], screen[1], screen[2])) {
            ++stats.culled;
            continue;
        }
        for (std::size_t i = 1; i + 1 < screen.size(); ++i) triangles.push_back({screen[0], screen[i], screen[i + 1]});
    }
    if (triangles.empty()) return stats;

    // the window rows the triangles can reach
    auto low = std::numeric_limits<std::int32_t>::max(), high = std::numeric_limits<std::int32_t>::min();
    for (auto& t : triangles) {
        low = std::min(low, saturate<std::int32_t>(std::floor(std::min({t[0].y, t[1].y, t[2].y}))));
        high = std::max(high, saturate<std::int32_t>(std::ceil(std::max({t[0].y, t[1].y, t[2].y}))));
    }
    low = std::max(low, state.target.bottom);
    high = std::min(high, state.target.top);
    if (low >= high) return stats;
    for (auto& t : triangles) fill_triangle(memory, low, high, t[0], t[1], t[2], state, stats);
    return stats;
}

namespace {

DrawStats shade_and_rasterize(const Registers& r, ShaderUnit& unit, const std::vector<std::array<Vec4, input_registers>>& inputs,
                              const std::vector<std::size_t>* order, Memory& memory) {
    std::vector<std::array<Vec4, output_registers>> outputs;
    outputs.reserve(inputs.size());
    ShaderState state;
    for (auto& in : inputs) {
        state.input = in;
        unit.run(state);
        outputs.push_back(state.output);
    }
    auto shaded = place_outputs(r, outputs);
    if (!order) return rasterize(r, shaded, memory);
    std::vector<Vertex> in_order;
    in_order.reserve(order->size());
    for (auto i : *order) in_order.push_back(shaded[i]);
    return rasterize(r, in_order, memory);
}

}  // namespace

DrawStats draw(const Registers& r, ShaderUnit& vertex_shader, const std::array<Vec4, 16>& fixed_attributes, Memory& memory, bool indexed) {
    auto count = r[reg::num_vertices];
    auto first = r[reg::vertex_offset];
    if (count == 0 || count > 0x10000) return {};
    // the geometry stage isn't modelled
    if ((r[reg::geostage_config] & 3) == 2) return {};
    auto arrays = location(r, reg::attribbuffers_location);
    InputPlan plan(r, fixed_attributes);

    // each vertex an index buffer names fetched and shaded once
    std::vector<std::uint32_t> unique;
    std::vector<std::size_t> order;
    if (indexed) {
        auto config = r[reg::indexbuffer_config];
        bool shorts = config & 0x80000000;
        auto at = arrays + (config & 0x0FFFFFFF);
        std::vector<std::uint8_t> bytes(count * (shorts ? 2 : 1));
        memory.read(at, bytes);
        std::vector<std::int32_t> slot_of(shorts ? 0x10000 : 0x100, -1);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t index = shorts ? static_cast<std::uint32_t>(bytes[i * 2] | bytes[i * 2 + 1] << 8) : bytes[i];
            if (slot_of[index] < 0) {
                slot_of[index] = static_cast<std::int32_t>(unique.size());
                unique.push_back(index);
            }
            order.push_back(static_cast<std::size_t>(slot_of[index]));
        }
    } else {
        for (std::uint32_t i = 0; i < count; ++i) unique.push_back(first + i);
    }
    std::vector<std::array<Vec4, input_registers>> inputs;
    inputs.reserve(unique.size());
    for (auto v : unique) inputs.push_back(plan.fetch(memory, arrays, v));
    return shade_and_rasterize(r, vertex_shader, inputs, indexed ? &order : nullptr, memory);
}

DrawStats draw_immediate(const Registers& r, ShaderUnit& vertex_shader, const std::vector<std::array<Vec4, 16>>& vertices, Memory& memory) {
    if ((r[reg::geostage_config] & 3) == 2) return {};
    // immediate mode sizes its vertices by GPUREG_VSH_NUM_ATTR
    auto count = static_cast<std::size_t>((r[reg::vsh_num_attr] & 0xF) + 1);
    auto map = static_cast<std::uint64_t>(r[reg::vsh_block + reg::shader::attributes_permutation_low]) |
               static_cast<std::uint64_t>(r[reg::vsh_block + reg::shader::attributes_permutation_high]) << 32;
    std::vector<std::array<Vec4, input_registers>> inputs;
    for (auto& attributes : vertices) {
        std::array<Vec4, input_registers> in{};
        for (std::size_t id = 0; id < count && id < 16; ++id) in[(map >> (id * 4)) & 0xF] = attributes[id];
        inputs.push_back(in);
    }
    return shade_and_rasterize(r, vertex_shader, inputs, nullptr, memory);
}

}  // namespace kabegami::pica
