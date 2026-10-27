// the software GPU: Zakuro's own rasterizer and command list tests, ported.
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "core/pica/fragment/format.hpp"
#include "core/pica/gpu/gpu.hpp"
#include "core/pica/gpu/raster.hpp"

using namespace kabegami;
using namespace kabegami::pica;

namespace {

constexpr std::uint32_t color_at = 0x1000, depth_at = 0x3000;

// an 8x8 RGBA8 target with colour writes on and a D24S8 buffer beside it
Registers target_registers() {
    Registers r{};
    r[reg::viewport_width] = encode_float24(4);
    r[reg::viewport_height] = encode_float24(4);
    r[reg::colorbuffer_location] = color_at >> 3;
    r[reg::depthbuffer_location] = depth_at >> 3;
    r[reg::depthbuffer_format] = 3;
    r[reg::framebuffer_dimensions] = 8 | (7 << 12);
    r[reg::colorbuffer_write] = 0xF;
    r[reg::depthbuffer_write] = 3;
    r[reg::depth_color_mask] = 0xF << 8;
    r[reg::logic_op] = 3;
    return r;
}

constexpr Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1};

Vertex at(Vec4 clip, Color color = red) {
    Vertex v;
    v.clip = clip;
    v.color = color;
    return v;
}

// a triangle covering the whole target at clip-space depth z
std::vector<Vertex> cover(float z, Color color) {
    return {at({-1, -1, z, 1}, color), at({3, -1, z, 1}, color), at({-1, 3, z, 1}, color)};
}

std::vector<Rgba> pixels(const Memory& memory) {
    std::vector<Rgba> out;
    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint8_t raw[4];
        memory.read(color_at + i * 4, raw);
        out.push_back(decode_pixel(ColorFormat::rgba8, raw));
    }
    return out;
}

}  // namespace

TEST_CASE("reversed depth keeps the nearer surface") {
    // depth mapped with a scale of -1, so near is 1, and kept with GREATER
    // against a buffer cleared to 0
    auto r = target_registers();
    r[reg::depthmap_scale] = encode_float24(-1);
    r[reg::depthmap_enable] = 1;
    r[reg::depth_color_mask] |= 1 | (6 << 4) | (1 << 12);
    Memory memory(0x10000, 0);
    rasterize(r, cover(-0.2f, red), memory);
    rasterize(r, cover(-0.8f, green), memory);
    rasterize(r, cover(-0.3f, blue), memory);
    for (auto p : pixels(memory)) CHECK(p == Rgba{0, 255, 0, 255});
}

TEST_CASE("the stencil test masks pixels") {
    auto r = target_registers();
    // enabled, EQUAL, write mask 0xFF, reference 1, compare mask 0xFF
    r[reg::stencil_test] = 1 | (2 << 4) | (0xFF << 8) | (1 << 16) | (0xFFu << 24);
    Memory memory(0x10000, 0);
    std::uint8_t one = 1;
    for (std::uint32_t sample = 0; sample < 32; ++sample) memory.write(depth_at + sample * 4 + 3, {&one, 1});
    CHECK(rasterize(r, cover(-0.5f, red), memory).written == 32);
}

TEST_CASE("a triangle crossing the camera is clipped, not dropped") {
    auto r = target_registers();
    Memory memory(0x10000, 0);
    std::vector<Vertex> triangle{at({-1, -1, -0.5f, 1}), at({1, -1, -0.5f, 1}), at({0, 2, 0.5f, -1})};
    CHECK(rasterize(r, triangle, memory).written == 64);
}

TEST_CASE("the colour mask keeps the channels it doesn't write") {
    auto r = target_registers();
    r[reg::depth_color_mask] = 1 << 8;  // red only
    Memory memory(0x10000, 0);
    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint8_t raw[4];
        encode_pixel(ColorFormat::rgba8, {10, 20, 30, 40}, raw);
        memory.write(color_at + i * 4, raw);
    }
    rasterize(r, cover(-0.5f, {1, 1, 1, 1}), memory);
    for (auto p : pixels(memory)) CHECK(p == Rgba{255, 20, 30, 40});
}

TEST_CASE("a quad covers every pixel once") {
    auto r = target_registers();
    Memory memory(0x10000, 0);
    auto v = [](float x, float y) { return at({x, y, -0.5f, 1}); };
    std::vector<Vertex> quad{v(-1, -1), v(1, -1), v(1, 1), v(-1, -1), v(1, 1), v(-1, 1)};
    CHECK(rasterize(r, quad, memory).written == 64);
}

TEST_CASE("face culling keeps one winding") {
    auto v = [](float x, float y) { return at({x, y, -0.5f, 1}); };
    // counter-clockwise with y up
    std::vector<Vertex> triangle{v(-1, -1), v(3, -1), v(-1, 3)};
    for (auto [mode, expected] : {std::pair{0u, 64u}, std::pair{1u, 0u}, std::pair{2u, 64u}}) {
        auto r = target_registers();
        r[reg::face_culling] = mode;
        Memory memory(0x10000, 0);
        CHECK(rasterize(r, triangle, memory).written == expected);
    }
}

TEST_CASE("a viewport offset places the picture from the bottom") {
    // window y counts up from the bottom of the buffer, whose rows are kept
    // top first
    auto r = target_registers();
    r[reg::viewport_height] = encode_float24(2);
    r[reg::viewport_xy] = 4 << 16;
    Memory memory(0x10000, 0);
    CHECK(rasterize(r, cover(-0.5f, red), memory).written == 32);
    for (std::uint32_t row = 0; row < 8; ++row) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            std::uint8_t raw[4];
            memory.read(color_at + morton_offset(x, row, 8, 4), raw);
            CHECK((decode_pixel(ColorFormat::rgba8, raw) == Rgba{255, 0, 0, 255}) == (row < 4));
        }
    }
}

TEST_CASE("edges through pixel centres follow the PICA") {
    // a pixel centred on an edge goes to the triangle on its right, or above a
    // flat edge, and a corner a hair off a centre counts as on it
    auto r = target_registers();
    auto corner = [](float x, float y) { return at({x / 4 - 1, y / 4 - 1, -0.5f, 1}); };
    for (auto [left, right] : {std::pair{1.5f, 3.5f}, std::pair{1.4999967f, 3.499992f}}) {
        std::vector<Vertex> quad{corner(left, 1.5f), corner(right, 1.5f), corner(right, 3.5f),
                                 corner(left, 1.5f), corner(right, 3.5f), corner(left, 3.5f)};
        Memory memory(0x10000, 0);
        rasterize(r, quad, memory);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> covered;
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                std::uint8_t raw[4];
                memory.read(color_at + morton_offset(x, 7 - y, 8, 4), raw);
                if (raw[0] || raw[1] || raw[2] || raw[3]) covered.push_back({x, y});
            }
        }
        CHECK(covered == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{1, 1}, {2, 1}, {1, 2}, {2, 2}});
    }
}

namespace {

// a command: the parameter, then the header
std::vector<std::uint32_t> command(std::size_t reg_index, std::uint32_t value, std::uint32_t mask = 0xF) {
    return {value, static_cast<std::uint32_t>(reg_index) | mask << 16};
}

void put_words(Memory& memory, std::uint32_t at, const std::vector<std::uint32_t>& words) {
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::uint8_t b[4];
        for (int k = 0; k < 4; ++k) b[k] = static_cast<std::uint8_t>(words[i] >> (8 * k));
        memory.write(at + static_cast<std::uint32_t>(i * 4), b);
    }
}

std::vector<std::uint32_t> concat(std::initializer_list<std::vector<std::uint32_t>> parts) {
    std::vector<std::uint32_t> out;
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

}  // namespace

TEST_CASE("command buffers jump into each other") {
    constexpr std::uint32_t main_at = 0x100, back_at = 0x140, sub_at = 0x200;
    Memory memory(0x400, 0);
    auto main = concat({command(reg::cmdbuf_addr0, sub_at >> 3), command(reg::cmdbuf_size0, 32 >> 3),
                        command(reg::cmdbuf_addr0 + 1, back_at >> 3), command(reg::cmdbuf_size0 + 1, 8 >> 3),
                        command(reg::cmdbuf_jump0, 1),
                        // never reached, the jump leaves this list for good
                        command(0x0101, 0xDEAD)});
    put_words(memory, main_at, main);
    put_words(memory, back_at, command(0x0102, 0x2222));
    put_words(memory, sub_at, concat({command(0x0100, 0x1111), command(reg::cmdbuf_jump1, 1), {0, 0}, {0, 0}}));
    Gpu gpu;
    gpu.run_command_list(memory, main_at, static_cast<std::uint32_t>(main.size() * 4));
    CHECK(gpu.registers[0x0100] == 0x1111);
    CHECK(gpu.registers[0x0102] == 0x2222);
    CHECK(gpu.registers[0x0101] == 0);
}

TEST_CASE("byte masks write the bytes their bits name, and bursts run on or stay") {
    Memory memory(0x400, 0);
    // mask 0b0101: bytes 0 and 2
    auto list = concat({command(0x0101, 0xFFFFFFFF), command(0x0101, 0x11223344, 0x5),
                        // three words, consecutive, from 0x0104: 0x104-0x106
                        {0xA, 0x0104 | 0xFu << 16 | 2u << 20 | 0x80000000u, 0xB, 0xC},
                        // two more words to one register: the last stays
                        {0x1, 0x0108 | 0xFu << 16 | 2u << 20, 0x2, 0x3}});
    put_words(memory, 0, list);
    Gpu gpu;
    gpu.run_command_list(memory, 0, static_cast<std::uint32_t>(list.size() * 4));
    CHECK(gpu.registers[0x0101] == 0xFF22FF44);
    CHECK(gpu.registers[0x0104] == 0xA);
    CHECK(gpu.registers[0x0105] == 0xB);
    CHECK(gpu.registers[0x0106] == 0xC);
    CHECK(gpu.registers[0x0108] == 0x3);
}

namespace {

// packed as the fixed attribute registers take it
std::array<std::uint32_t, 3> pack(Vec4 v) {
    auto x = encode_float24(v[0]), y = encode_float24(v[1]), z = encode_float24(v[2]), w = encode_float24(v[3]);
    return {w << 8 | z >> 16, (z & 0xFFFF) << 16 | y >> 8, (y & 0xFF) << 24 | x};
}

// an 8x8 RGBA8 target, and a shader moving v0 to the position and v1 to the colour
Gpu gpu_for_immediate_draws(Memory& memory) {
    Gpu gpu;
    constexpr std::uint32_t mov = 0x13u << 26;
    gpu.vertex_shader.program[0] = mov;
    gpu.vertex_shader.program[1] = mov | 1u << 21 | 1u << 12;
    gpu.vertex_shader.program[2] = 0x22u << 26;
    gpu.vertex_shader.descriptors[0] = 0xF | (0b00'01'10'11u << 5) | (0b00'01'10'11u << 14);
    auto w = [&](std::size_t r, std::uint32_t v) { gpu.write(memory, r, v); };
    w(reg::outmap_total, 2);
    w(reg::outmap_o0, 0x03020100);
    w(reg::outmap_o0 + 1, 0x0B0A0908);
    w(reg::vsh_block + reg::shader::attributes_permutation_low, 0x10);
    w(reg::vsh_num_attr, 1);
    w(reg::viewport_width, encode_float24(4));
    w(reg::viewport_height, encode_float24(4));
    w(reg::colorbuffer_location, color_at >> 3);
    w(reg::framebuffer_dimensions, 8 | (7 << 12));
    w(reg::colorbuffer_write, 0xF);
    w(reg::depth_color_mask, 0xF << 8);
    w(reg::logic_op, 3);
    return gpu;
}

void send_vertex(Gpu& gpu, Memory& memory, std::initializer_list<Vec4> attributes) {
    for (auto& a : attributes) {
        auto words = pack(a);
        for (std::size_t i = 0; i < 3; ++i) gpu.write(memory, reg::fixedattrib_data0 + i, words[i]);
    }
}

}  // namespace

TEST_CASE("immediate-mode vertices are drawn once the state moves on") {
    Memory memory(0x2000, 0);
    auto gpu = gpu_for_immediate_draws(memory);
    gpu.write(memory, reg::fixedattrib_index, 0xF);
    for (auto p : {Vec4{-1, -1, 0, 1}, Vec4{3, -1, 0, 1}, Vec4{-1, 3, 0, 1}}) send_vertex(gpu, memory, {p, Vec4{1, 0, 0, 1}});
    CHECK(gpu.draws.empty());
    gpu.write(memory, 0x245, 1);
    REQUIRE(gpu.draws.size() == 1);
    CHECK(gpu.draws[0].vertices == 3);
    for (auto p : pixels(memory)) CHECK(p == Rgba{255, 0, 0, 255});
}

TEST_CASE("fixed attributes below fifteen are defaults, not vertices") {
    Memory memory(0x2000, 0);
    auto gpu = gpu_for_immediate_draws(memory);
    gpu.write(memory, reg::fixedattrib_index, 2);
    send_vertex(gpu, memory, {Vec4{0.5f, 0.25f, 0, 1}});
    CHECK(gpu.fixed_attributes[2] == Vec4{0.5f, 0.25f, 0, 1});
    CHECK((gpu.registers[reg::fixedattrib_index] & 0xF) == 3);
    gpu.write(memory, 0x245, 1);
    CHECK(gpu.draws.empty());
}

TEST_CASE("a draw from vertex arrays and an index buffer") {
    // two triangles of a quad over the target, from a float position and a
    // ubyte colour in one interleaved array, picked by a ubyte index buffer
    Memory memory(0x4000, 0);
    auto gpu = gpu_for_immediate_draws(memory);
    // v1 is 0-255 from the bytes, the shader's colour wants 0-1: c0 scales it
    gpu.vertex_shader.program[1] = (0x08u << 26) | 1u << 21 | 0x20u << 12 | 1u << 7;  // mul o1, c0, v1
    gpu.vertex_shader.float_uniforms[0] = {1.0f / 255, 1.0f / 255, 1.0f / 255, 1.0f / 255};
    struct Vtx {
        float x, y, z;
        std::uint8_t rgba[4];
    };
    Vtx vertices[] = {{-1, -1, -0.5f, {0, 0, 255, 255}}, {1, -1, -0.5f, {0, 0, 255, 255}},
                      {1, 1, -0.5f, {0, 0, 255, 255}},   {-1, 1, -0.5f, {0, 0, 255, 255}}};
    constexpr std::uint32_t arrays = 0x2000;
    memory.write(arrays, {reinterpret_cast<const std::uint8_t*>(vertices), sizeof vertices});
    std::uint8_t indices[] = {0, 1, 2, 0, 2, 3};
    memory.write(arrays + 0x100, indices);
    auto w = [&](std::size_t r, std::uint32_t v) { gpu.write(memory, r, v); };
    w(reg::attribbuffers_location, arrays >> 3);
    // attribute 0: 3 floats (type 3, 3 components: 0b1011), attribute 1: 4 ubytes (type 1, 4: 0b1101)
    w(reg::attribbuffers_format_low, 0xB | 0xDu << 4);
    w(reg::attribbuffers_format_high, 1u << 28);  // two attributes, none fixed
    w(reg::attribbuffer0, 0);
    w(reg::attribbuffer0 + 1, 0x10);                  // attribute 0 then 1
    w(reg::attribbuffer0 + 2, 16u << 16 | 2u << 28);  // 16 bytes a vertex, two components
    w(reg::vsh_block + reg::shader::input_config, 1);
    w(reg::indexbuffer_config, 0x100);
    w(reg::num_vertices, 6);
    w(reg::drawelements, 1);
    REQUIRE(gpu.draws.size() == 1);
    CHECK(gpu.draws[0].written == 64);
    for (auto p : pixels(memory)) CHECK(p == Rgba{0, 0, 255, 255});
}

TEST_CASE("a quarter turn counter-clockwise") {
    image::Image picture(3, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) picture.at(x, y)[0] = static_cast<std::uint8_t>(y * 3 + x);
    auto turned = rotate_counter_clockwise(picture);
    REQUIRE(turned.width == 2);
    REQUIRE(turned.height == 3);
    // the right column on top, read downwards from the top row
    CHECK(turned.at(0, 0)[0] == 2);
    CHECK(turned.at(1, 0)[0] == 5);
    CHECK(turned.at(0, 2)[0] == 0);
    CHECK(turned.at(1, 2)[0] == 3);
}
