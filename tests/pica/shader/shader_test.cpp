// the shader unit: Zakuro's own interpreter tests, ported, then HOME's real
// DefaultShader run on BannerBG's vertices (KABEGAMI_ROMFS=<an extracted RomFS>).
#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>

#include "core/cgfx/model/model.hpp"
#include "core/cgfx/program/program.hpp"
#include "core/pica/shader/shader.hpp"

using namespace kabegami;
using pica::Vec4;

namespace {

pica::ShaderUnit unit_with(std::initializer_list<std::uint32_t> program, std::initializer_list<std::uint32_t> descriptors) {
    pica::ShaderUnit unit;
    std::size_t i = 0;
    for (auto w : program) unit.program[i++] = w;
    i = 0;
    for (auto w : descriptors) unit.descriptors[i++] = w;
    return unit;
}

// descriptor 0: every component written, identity swizzles on both sources
constexpr std::uint32_t identity = 0xF | (0b00'01'10'11u << 5) | (0b00'01'10'11u << 14);
constexpr std::uint32_t identity3 = identity | (0b00'01'10'11u << 23);
constexpr std::uint32_t end = 0x22u << 26;

}  // namespace

TEST_CASE("moves an input to an output") {
    auto unit = unit_with({0x13u << 26, end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    unit.run(s);
    CHECK(s.output[0] == Vec4{1, 2, 3, 4});
}

TEST_CASE("adds two inputs") {
    auto unit = unit_with({1u << 7, end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {10, 20, 30, 40};
    unit.run(s);
    CHECK(s.output[0] == Vec4{11, 22, 33, 44});
}

TEST_CASE("honours the destination mask") {
    auto unit = unit_with({0x13u << 26, end}, {0b1010u | (0b00'01'10'11u << 5) | (0b00'01'10'11u << 14)});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    unit.run(s);
    CHECK(s.output[0] == Vec4{1, 0, 3, 0});
}

TEST_CASE("an unwritten temporary has a w of one") {
    auto unit = unit_with({(0x13u << 26) | (0x1Fu << 12), end}, {identity});
    pica::ShaderState s;
    unit.run(s);
    CHECK(s.output[0] == Vec4{0, 0, 0, 1});
}

TEST_CASE("a runaway program stops") {
    auto unit = unit_with({0x2Cu << 26}, {identity});
    pica::ShaderState s;
    unit.run(s);
}

TEST_CASE("dot product broadcasts to every component") {
    auto unit = unit_with({(0x02u << 26) | (1u << 7), end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {1, 1, 1, 1};
    unit.run(s);
    CHECK(s.output[0] == Vec4{10, 10, 10, 10});
}

TEST_CASE("opcode eight is multiply") {
    auto unit = unit_with({(0x08u << 26) | (1u << 7), end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {2, 3, 4, 5};
    unit.run(s);
    CHECK(s.output[0] == Vec4{2, 6, 12, 20});
}

TEST_CASE("multiply-add reads a uniform through its wide second source") {
    auto unit = unit_with({(0x38u << 26) | (0x21u << 10) | (1u << 5), end}, {identity3});
    unit.float_uniforms[1] = {10, 10, 10, 10};
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {0.5f, 0.5f, 0.5f, 0.5f};
    unit.run(s);
    CHECK(s.output[0] == Vec4{10.5f, 20.5f, 30.5f, 40.5f});
}

TEST_CASE("inverted multiply-add reads a uniform through its third source") {
    auto unit = unit_with({(0x30u << 26) | (1u << 12) | (0x22u << 5), end}, {identity3});
    unit.float_uniforms[2] = {100, 100, 100, 100};
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {2, 2, 2, 2};
    unit.run(s);
    CHECK(s.output[0] == Vec4{102, 104, 106, 108});
}

TEST_CASE("a loop repeats its whole body") {
    auto unit = unit_with({(0x29u << 26) | (2u << 10), (0x10u << 21) | (0x10u << 12), (0x10u << 21) | (0x10u << 12),
                           (0x13u << 26) | (0x10u << 12), end},
                          {identity});
    unit.int_uniforms[0] = {2, 0, 1, 0};
    pica::ShaderState s;
    s.input[0] = {1, 1, 1, 1};
    unit.run(s);
    CHECK(s.output[0] == Vec4{6, 6, 6, 7});
}

TEST_CASE("jmpu can jump on a false boolean") {
    auto unit = unit_with({(0x2Du << 26) | (2u << 10) | 1, 0x13u << 26, end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 1, 1, 1};
    unit.run(s);
    CHECK(s.output[0] == Vec4{0, 0, 0, 0});
}

TEST_CASE("uploading a program decodes it again") {
    auto unit = unit_with({0x13u << 26, end}, {identity});
    pica::ShaderState s;
    s.input[0] = {1, 2, 3, 4};
    s.input[1] = {10, 20, 30, 40};
    unit.run(s);
    CHECK(s.output[0] == Vec4{1, 2, 3, 4});
    unit.program_write_offset = 0;
    unit.upload_program(1u << 7);
    unit.run(s);
    CHECK(s.output[0] == Vec4{11, 22, 33, 44});
}

TEST_CASE("zero times infinity is zero") {
    CHECK(pica::multiply(0.0f, std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(std::isnan(pica::multiply(std::numeric_limits<float>::quiet_NaN(), 1.0f)));
}

TEST_CASE("float24 decodes and encodes simple values") {
    CHECK(pica::decode_float24(63u << 16) == 1.0f);
    CHECK(pica::decode_float24(64u << 16) == 2.0f);
    CHECK(pica::decode_float24(62u << 16) == 0.5f);
    CHECK(pica::decode_float24((1u << 23) | (63u << 16)) == -1.0f);
    CHECK(pica::decode_float24(0) == 0.0f);
    for (float f : {1.0f, -2.5f, 0.125f, 1234.5f}) CHECK(pica::decode_float24(pica::encode_float24(f)) == f);
    CHECK(pica::encode_float24(1.0f) == 0x3F0000);
}

TEST_CASE("uniforms arrive packed in three words or wide in four") {
    pica::ShaderUnit unit;
    unit.set_float_uniform_index(5);
    // (x, y, z, w) = (4, 3, 2, 1) as float24, packed w first across three words
    std::uint32_t x = pica::encode_float24(4), y = pica::encode_float24(3), z = pica::encode_float24(2), w = pica::encode_float24(1);
    unit.upload_float_uniform(w << 8 | z >> 16);
    unit.upload_float_uniform((z & 0xFFFF) << 16 | y >> 8);
    unit.upload_float_uniform((y & 0xFF) << 24 | x);
    CHECK(unit.float_uniforms[5] == Vec4{4, 3, 2, 1});
    unit.set_float_uniform_index(0x80000000u | 7);
    for (float f : {8.0f, 7.0f, 6.0f, 5.0f}) unit.upload_float_uniform(std::bit_cast<std::uint32_t>(f));
    CHECK(unit.float_uniforms[7] == Vec4{5, 6, 7, 8});
}

TEST_CASE("HOME's DefaultShader places BannerBG's vertices") {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !std::filesystem::is_directory(dir)) return;
    auto load = [&](const char* name) {
        std::ifstream in(std::filesystem::path(dir) / "3D" / name, std::ios::binary);
        Bytes raw{std::istreambuf_iterator<char>(in), {}};
        return cgfx::File::load(raw, name);
    };
    auto shader_file = load("nwgfx_DefaultShader_LZ.bin");
    auto camera_file = load("BannerCamera_LZ.bin");
    auto banner = load("BannerBG_LZ.bin");
    auto shader = cgfx::read_shader(shader_file);
    auto& entry = shader.binary.entries[0];

    // section 5.5 of docs/research/camera-shader-nw4c.md, with TexMtx1 at
    // its rest scale and a made-up translation
    pica::ShaderUnit unit;
    for (auto w : shader.binary.code) unit.upload_program(w);
    for (auto w : shader.binary.descriptors) unit.upload_descriptor(w);
    unit.entry_point = entry.main;
    for (auto& c : entry.constants) {
        if (c.type == cgfx::ShaderConstant::Type::floating)
            unit.float_uniforms[c.reg] = {pica::decode_float24(c.raw[0]), pica::decode_float24(c.raw[1]),
                                          pica::decode_float24(c.raw[2]), pica::decode_float24(c.raw[3])};
        else if (c.type == cgfx::ShaderConstant::Type::integer)
            for (std::size_t k = 0; k < 4; ++k) unit.int_uniforms[c.reg][k] = static_cast<std::uint8_t>(c.raw[k]);
        else if (c.raw[0])
            unit.bool_uniforms |= static_cast<std::uint16_t>(1u << c.reg);
    }
    auto c = [&](const char* name) { return static_cast<std::size_t>(entry.uniform(name)->first - 0x10); };
    auto set_rows = [&](const char* name, const float* rows, std::size_t count) {
        for (std::size_t r = 0; r < count; ++r)
            unit.float_uniforms[c(name) + r] = {rows[r * 4], rows[r * 4 + 1], rows[r * 4 + 2], rows[r * 4 + 3]};
    };
    auto camera = cgfx::read_camera(camera_file);
    auto view = cgfx::view_matrix(camera);
    auto proj = cgfx::projection_matrix(camera, 1);
    auto identity34 = cgfx::Matrix34::identity();
    set_rows("WrldMtx", identity34.m.data(), 3);
    set_rows("UnivReg", identity34.m.data(), 3);
    set_rows("ViewMtx", view.m.data(), 3);
    set_rows("ProjMtx", proj.m.data(), 4);
    unit.float_uniforms[c("IrScale")] = {1, 0, 0, 1.0f / 255};
    unit.float_uniforms[c("IrScale") + 1] = {1, 0, 0, 0};
    unit.float_uniforms[c("IrScale") + 2] = {1, 1, 1, 1};
    unit.float_uniforms[c("MatAmbi")] = {1, 1, 1, 1};
    unit.float_uniforms[c("MatDiff")] = {1, 1, 1, 1};
    cgfx::Matrix34 tex1;
    tex1.at(0, 0) = 3;
    tex1.at(0, 3) = 0.25f;
    tex1.at(1, 1) = 6;
    tex1.at(1, 3) = -2.5f;
    tex1.at(2, 2) = 1;
    set_rows("TexMtx0", identity34.m.data(), 3);
    set_rows("TexMtx1", tex1.m.data(), 3);
    set_rows("TexMtx2", identity34.m.data(), 2);
    for (auto name : {"IsRgdSk", "IsVertA", "UvMap0", "UvMap1", "UvMap2", "IsTex1", "IsTex2"})
        unit.bool_uniforms |= static_cast<std::uint16_t>(1u << (entry.uniform(name)->first - 0x78));
    CHECK(unit.bool_uniforms == 0x6E85);

    auto model = cgfx::read_model(banner);
    auto& buffer = model.shapes[0].buffers[0];
    auto count = buffer.size / buffer.stride;
    REQUIRE(count > 0);
    for (std::size_t v = 0; v < count; ++v) {
        std::array<Vec4, pica::input_registers> in{};
        for (auto& a : buffer.attributes) {
            auto value = cgfx::attribute_value(banner, buffer, a, v);
            auto reg = shader.descriptions[0].inputs[static_cast<std::size_t>(a.usage)];
            REQUIRE(reg >= 0);
            // an input with fewer components gets the rest as (0, 0, 0, 1)
            Vec4 full{0, 0, 0, 1};
            for (std::size_t k = 0; k < a.components; ++k) full[k] = value[k];
            in[static_cast<std::size_t>(reg)] = full;
        }
        auto out = unit.run(in);
        // the position, ProjMtx · ViewMtx · p
        auto pv = cgfx::transform_point(view, {in[0][0], in[0][1], in[0][2]});
        for (int r = 0; r < 4; ++r) {
            float expected = proj.at(r, 0) * pv.x + proj.at(r, 1) * pv.y + proj.at(r, 2) * pv.z + proj.at(r, 3);
            CHECK(out[0][static_cast<std::size_t>(r)] == doctest::Approx(expected).epsilon(1e-5));
        }
        // the view vector, -p_view
        CHECK(out[2][0] == doctest::Approx(-pv.x).epsilon(1e-5));
        CHECK(out[2][2] == doctest::Approx(-pv.z).epsilon(1e-5));
        // no normals
        CHECK(out[1] == Vec4{0, 0, 0, 0});
        // the colour, over 255
        for (std::size_t k = 0; k < 4; ++k) CHECK(out[3][k] == doctest::Approx(in[3][k] / 255.0f).epsilon(1e-6));
        // texcoord 0 the UV as it is, texcoord 1 through matrix 1
        CHECK(out[4][0] == in[4][0]);
        CHECK(out[4][1] == in[4][1]);
        CHECK(out[5][0] == doctest::Approx(3 * in[4][0] + 0.25f).epsilon(1e-6));
        CHECK(out[5][1] == doctest::Approx(6 * in[4][1] - 2.5f).epsilon(1e-6));
        CHECK(out[6][0] == in[4][0]);
        CHECK(out[6][1] == in[4][1]);
    }
}
