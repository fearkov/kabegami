// the fragment pipeline's parts: Zakuro's own tests for them, ported, and
// HOME's textures decoded (KABEGAMI_ROMFS=<an extracted RomFS>).
#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <vector>

#include "core/pica/fragment/blend.hpp"
#include "core/pica/fragment/format.hpp"
#include "core/pica/fragment/tev.hpp"
#include "core/pica/fragment/texture.hpp"
#include "core/pica/registers.hpp"

using namespace kabegami;
using namespace kabegami::pica;

TEST_CASE("colour buffers number 5-6-5 and 5-5-5-1 the other way round from screens") {
    CHECK(color_format_from_buffer(3) == ColorFormat::rgb565);
    CHECK(color_format_from_raw(3) == ColorFormat::rgb5a1);
    for (auto f : {ColorFormat::rgba8, ColorFormat::rgb8, ColorFormat::rgb565, ColorFormat::rgb5a1, ColorFormat::rgba4})
        CHECK(color_format_from_buffer(color_buffer_raw(f)) == f);
}

TEST_CASE("morton covers a tile exactly once") {
    std::set<std::uint32_t> seen;
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) seen.insert(morton_interleave(x, y));
    CHECK(seen.size() == 64);
    CHECK(*seen.rbegin() == 63);
}

TEST_CASE("morton matches known values") {
    std::vector<std::uint32_t> row, column;
    for (std::uint32_t i = 0; i < 8; ++i) {
        row.push_back(morton_interleave(i, 0));
        column.push_back(morton_interleave(0, i));
    }
    CHECK(row == std::vector<std::uint32_t>{0, 1, 4, 5, 16, 17, 20, 21});
    CHECK(column == std::vector<std::uint32_t>{0, 2, 8, 10, 32, 34, 40, 42});
}

TEST_CASE("colour formats round trip") {
    for (auto f : {ColorFormat::rgba8, ColorFormat::rgb8, ColorFormat::rgb565, ColorFormat::rgb5a1, ColorFormat::rgba4}) {
        std::uint8_t buf[4]{};
        Rgba original{0xFF, 0x00, 0xFF, 0xFF};
        encode_pixel(f, original, buf);
        CHECK(decode_pixel(f, buf) == original);
    }
}

TEST_CASE("rgba8 texels are kept reversed") {
    std::uint8_t bytes[] = {0x11, 0x22, 0x33, 0x44};
    CHECK(sample_texel(bytes, TextureFormat::rgba8, 8, 0, 0) == Rgba{0x44, 0x33, 0x22, 0x11});
}

TEST_CASE("alpha alone comes with black, luminance alone with full alpha") {
    std::uint8_t a4[] = {0x0F};
    CHECK(sample_texel(a4, TextureFormat::a4, 8, 0, 0) == Rgba{0, 0, 0, 0xFF});
    CHECK(sample_texel(a4, TextureFormat::a4, 8, 1, 0) == Rgba{0, 0, 0, 0});
    std::uint8_t l4[] = {0xF0};
    CHECK(sample_texel(l4, TextureFormat::l4, 8, 1, 0) == Rgba{0xFF, 0xFF, 0xFF, 0xFF});
    std::uint8_t a8[] = {0x80};
    CHECK(sample_texel(a8, TextureFormat::a8, 8, 0, 0) == Rgba{0, 0, 0, 0x80});
}

namespace {

// a differential ETC1 block, from the fields at the bits the decoder reads
std::array<std::uint8_t, 8> diff_block(int r0, int g0, int b0, int dr, int dg, int db, int table0, int table1, bool flip, std::uint32_t pixels) {
    auto three = [](int d) { return static_cast<std::uint32_t>(d) & 7; };
    std::uint32_t high = (static_cast<std::uint32_t>(r0) & 0x1F) << 27 | three(dr) << 24 | (static_cast<std::uint32_t>(g0) & 0x1F) << 19 |
                         three(dg) << 16 | (static_cast<std::uint32_t>(b0) & 0x1F) << 11 | three(db) << 8 |
                         (static_cast<std::uint32_t>(table0) & 7) << 5 | (static_cast<std::uint32_t>(table1) & 7) << 2 | 2u |
                         (flip ? 1u : 0u);
    std::array<std::uint8_t, 8> block{};
    for (int i = 0; i < 4; ++i) {
        block[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(pixels >> (8 * i));
        block[static_cast<std::size_t>(4 + i)] = static_cast<std::uint8_t>(high >> (8 * i));
    }
    return block;
}

Rgba etc1_at(const std::array<std::uint8_t, 8>& block, std::uint32_t x, std::uint32_t y) {
    return sample_texel(block, TextureFormat::etc1, 4, x, y);
}

}  // namespace

TEST_CASE("a flat ETC1 block is one colour") {
    auto block = diff_block(16, 16, 16, 0, 0, 0, 0, 0, false, 0);
    std::set<Rgba> colors;
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x) colors.insert(etc1_at(block, x, y));
    CHECK(colors.size() == 1);
}

TEST_CASE("an unflipped ETC1 block splits left and right, a flipped one top and bottom") {
    auto block = diff_block(4, 4, 4, 3, 3, 3, 0, 0, false, 0);
    CHECK(etc1_at(block, 0, 0) != etc1_at(block, 3, 0));
    CHECK(etc1_at(block, 0, 0) == etc1_at(block, 1, 0));
    CHECK(etc1_at(block, 2, 0) == etc1_at(block, 3, 0));
    auto flipped = diff_block(4, 4, 4, 3, 3, 3, 0, 0, true, 0);
    CHECK(etc1_at(flipped, 0, 0) != etc1_at(flipped, 0, 3));
    CHECK(etc1_at(flipped, 0, 0) == etc1_at(flipped, 0, 1));
    CHECK(etc1_at(flipped, 0, 2) == etc1_at(flipped, 0, 3));
}

TEST_CASE("ETC1 blocks sit in tiles row by row, four to a tile in Z order") {
    std::uint32_t width = 32, height = 8;
    std::vector<std::uint8_t> data(width * height / 2);
    for (std::size_t b = 0; b < data.size() / 8; ++b) {
        auto gray = static_cast<int>(b) * 2;
        auto raw = diff_block(gray, gray, gray, 0, 0, 0, 0, 0, false, 0);
        std::copy(raw.begin(), raw.end(), data.begin() + static_cast<std::ptrdiff_t>(b * 8));
    }
    auto gray_at = [&](std::uint32_t x, std::uint32_t y) { return sample_texel(data, TextureFormat::etc1, width, x, y)[0]; };
    auto block_gray = [&](std::size_t n) {
        return sample_texel(std::span(data).subspan(n * 8), TextureFormat::etc1, 8, 0, 0)[0];
    };
    CHECK(gray_at(8, 0) == block_gray(4));
    CHECK(gray_at(12, 0) == block_gray(5));
    CHECK(gray_at(8, 4) == block_gray(6));
    CHECK(gray_at(12, 4) == block_gray(7));
}

TEST_CASE("encoding a picture gives back what decoding read") {
    std::vector<Rgba> rows(16 * 8);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto v = static_cast<std::uint8_t>(i * 37 % 256);
        rows[i] = {v, v, v, static_cast<std::uint8_t>(255 - v)};
    }
    for (auto f : {TextureFormat::rgba8, TextureFormat::la8, TextureFormat::l8}) {
        auto data = encode_texture(rows, f, 16, 8);
        auto back = decode_texture(data, f, 16, 8);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            CHECK(back[i][0] == rows[i][0]);
            if (f != TextureFormat::l8) CHECK(back[i][3] == rows[i][3]);
        }
    }
}

namespace {

std::vector<std::uint32_t> modulate_registers() {
    std::vector<std::uint32_t> r(reg::count);
    for (std::size_t i = 0; i < 6; ++i) {
        auto base = reg::texenv_stages[i];
        // primary colour and texture 0 for the first stage, previous for the rest
        r[base] = i == 0 ? 0x00030003 : 0x000F000F;
        r[base + 2] = i == 0 ? 0x00010001 : 0;
    }
    return r;
}

std::vector<std::uint32_t> passthrough_registers() {
    std::vector<std::uint32_t> r(reg::count);
    for (auto base : reg::texenv_stages) r[base] = 0x000F000F;
    return r;
}

const std::array<Color, 4> no_textures{};

}  // namespace

TEST_CASE("a modulate stage multiplies its inputs") {
    auto env = TexEnv::read(modulate_registers());
    std::array<Color, 4> half;
    half.fill({0.5f, 0.5f, 0.5f, 0.5f});
    auto out = env.apply({1.0f, 0.5f, 0.0f, 1.0f}, half);
    CHECK(out[0] == doctest::Approx(0.5f));
    CHECK(out[1] == doctest::Approx(0.25f));
    CHECK(out[2] == doctest::Approx(0.0f));
    CHECK(out[3] == doctest::Approx(0.5f));
}

TEST_CASE("a replace stage passes the primary colour through") {
    std::vector<std::uint32_t> r(reg::count);
    auto env = TexEnv::read(r);
    std::array<Color, 4> red;
    red.fill({1, 0, 0, 1});
    Color primary{0.25f, 0.5f, 0.75f, 1.0f};
    CHECK(env.apply(primary, red) == primary);
}

TEST_CASE("pass-through stages leave the colour alone") {
    auto env = TexEnv::read(passthrough_registers());
    for (auto& s : env.stages) CHECK(s.passthrough);
    Color color{0.25f, 0.5f, 0.75f, 1.0f};
    CHECK(env.apply(color, no_textures) == color);
}

TEST_CASE("lighting is read only through the inputs an operation uses") {
    auto r = passthrough_registers();
    r[reg::texenv_stages[0]] = 0x00030013;
    CHECK_FALSE(TexEnv::read(r).reads_lighting());
    r[reg::texenv_stages[0] + 2] = 1;
    CHECK(TexEnv::read(r).reads_lighting());
}

TEST_CASE("the buffer reaches the stage after next") {
    auto r = passthrough_registers();
    auto [s0, s1, s2, s3, s4, s5] = reg::texenv_stages;
    r[s0] = 0x000E000E;           // constant
    r[s0 + 3] = 0xFF0000FF;       // opaque red
    r[reg::texenv_update_buffer] = 0x1100;
    r[reg::texenv_buffer_color] = 0xFF00FF00;  // opaque green
    r[s1] = 0x000D000D;           // previous buffer
    CHECK(TexEnv::read(r).apply({}, no_textures) == Color{0, 1, 0, 1});
    r[s2] = 0x000D000D;
    CHECK(TexEnv::read(r).apply({}, no_textures) == Color{1, 0, 0, 1});
}

TEST_CASE("colour and alpha update the buffer separately") {
    auto r = passthrough_registers();
    r[reg::texenv_stages[0]] = 0x000E000E;
    r[reg::texenv_stages[0] + 3] = 0x800000FF;  // red, half alpha
    r[reg::texenv_update_buffer] = 0x0100;      // stage 0, colour only
    r[reg::texenv_buffer_color] = 0xFF00FF00;
    r[reg::texenv_stages[2]] = 0x000D000D;
    auto out = TexEnv::read(r).apply({}, no_textures);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 1.0f);
}

namespace {
std::vector<std::uint32_t> blend_registers(std::uint32_t func) {
    std::vector<std::uint32_t> r(reg::count);
    r[reg::color_operation] = 0x100;
    r[reg::blend_func] = func;
    return r;
}
}  // namespace

TEST_CASE("standard transparency mixes by source alpha") {
    auto blend = Blend::read(blend_registers(0x76760000));
    REQUIRE(blend);
    auto out = blend->apply({1, 0, 0, 0.25f}, {0, 0, 1, 1});
    CHECK(out[0] == doctest::Approx(0.25f));
    CHECK(out[2] == doctest::Approx(0.75f));
}

TEST_CASE("additive blending brightens") {
    auto out = Blend::read(blend_registers(0x11110000))->apply({0.25f, 0.25f, 0.25f, 1}, {0.5f, 0.5f, 0.5f, 1});
    CHECK(out[0] == doctest::Approx(0.75f));
}

TEST_CASE("a logic op that leaves the colours alone, and copy is no op at all") {
    auto r = blend_registers(0);
    r[reg::color_operation] = 0;
    CHECK_FALSE(Blend::read(r));
    r[reg::logic_op] = 6;
    auto op = read_logic_op(r);
    REQUIRE(op);
    CHECK(*op == LogicOp::noop);
    CHECK(apply(*op, 0x00, 0x7F) == 0x7F);
    r[reg::logic_op] = 3;
    CHECK_FALSE(read_logic_op(r));
    r[reg::logic_op] = 11;
    CHECK(apply(*read_logic_op(r), 0xF0, 0x3C) == 0xCC);
}

TEST_CASE("the stencil compares its reference against the stored value and writes through its mask") {
    std::vector<std::uint32_t> r(reg::count);
    // enabled, LESS, write mask 0x0F, reference 2, compare mask 0xFF
    r[reg::stencil_test] = 1 | (4u << 4) | (0x0Fu << 8) | (2u << 16) | (0xFFu << 24);
    r[reg::stencil_op] = 3u << 8;  // increment on pass
    auto s = Stencil::read(r);
    REQUIRE(s);
    CHECK(s->passes(3));  // 2 < 3
    CHECK_FALSE(s->passes(2));
    CHECK(s->update(0xF3, s->pass) == 0xF4);
    CHECK(s->update(0xFF, StencilOp::increment_wrap) == 0xF0);
}
