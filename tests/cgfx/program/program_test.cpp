// the camera and the shader program against docs/research/camera-shader-nw4c.md.
// KABEGAMI_ROMFS=<an extracted RomFS> runs them.
#include <doctest/doctest.h>

#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/program/program.hpp"

using namespace kabegami;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> romfs() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !fs::is_directory(dir)) return std::nullopt;
    return fs::path(dir);
}

Bytes read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

TEST_CASE("BannerCamera and its matrices are HOME's, bit for bit") {
    auto dir = romfs();
    if (!dir) return;
    auto file = cgfx::File::load(read(*dir / "3D/BannerCamera_LZ.bin"), "BannerCamera_LZ.bin");
    auto camera = cgfx::read_camera(file);
    CHECK(camera.name == "BannerCamera");
    CHECK(camera.view_type == 0);
    CHECK(camera.projection_type == 0);
    CHECK(camera.translation.z == std::bit_cast<float>(0x423324DDu));
    CHECK(camera.target.y == 1.0f);
    CHECK(camera.near == 26.5f);
    CHECK(camera.far == 1000.0f);
    CHECK(bits(camera.fovy) == 0x3F060A92u);
    CHECK(camera.wscale == 0.0f);
    auto view = cgfx::view_matrix(camera);
    CHECK(view.at(0, 0) == 1.0f);
    CHECK(view.at(1, 1) == 1.0f);
    CHECK(view.at(1, 3) == -1.0f);
    CHECK(view.at(2, 2) == 1.0f);
    CHECK(view.at(2, 3) == -camera.translation.z);
    auto flat = cgfx::projection_matrix(camera, 0);
    CHECK(bits(flat.at(0, 0)) == 0x400F4F87u);
    CHECK(bits(flat.at(1, 1)) == 0x406ED9E0u);
    CHECK(bits(flat.at(2, 2)) == 0x3F837BFDu);
    CHECK(bits(flat.at(2, 3)) == 0x41D9C55Bu);
    CHECK(flat.at(3, 2) == -1.0f);
    auto proj = cgfx::projection_matrix(camera, 1);
    CHECK(bits(proj.at(0, 1)) == 0x406ED9E0u);
    CHECK(bits(proj.at(1, 0)) == (0x400F4F87u | 0x80000000u));
    // the updater leaves negative zeros in the rest of row 1
    CHECK(bits(proj.at(1, 1)) == 0x80000000u);
    CHECK(bits(proj.at(1, 3)) == 0x80000000u);
    auto depth = cgfx::depth_map(camera);
    CHECK(depth.enable);
    CHECK(depth.scale == -1.0f);
}

TEST_CASE("DefaultShader's program is what the spec decoded") {
    auto dir = romfs();
    if (!dir) return;
    auto file = cgfx::File::load(read(*dir / "3D/nwgfx_DefaultShader_LZ.bin"), "nwgfx_DefaultShader_LZ.bin");
    auto shader = cgfx::read_shader(file);
    CHECK(shader.name == "DefaultShader");
    CHECK(shader.binary.code.size() == 481);
    CHECK(shader.binary.descriptors.size() == 86);
    REQUIRE(shader.binary.entries.size() == 4);
    REQUIRE(shader.descriptions.size() == 4);
    for (int i = 0; i < 4; ++i) CHECK(shader.descriptions[static_cast<std::size_t>(i)].vertex == i);
    CHECK(shader.descriptions[0].flags == 7);
    CHECK(shader.descriptions[0].inputs[0] == 0);
    CHECK(shader.descriptions[0].inputs[3] == 3);
    CHECK(shader.descriptions[0].inputs[4] == 4);
    CHECK(shader.descriptions[0].inputs[12] == -1);
    auto& e = shader.binary.entries[0];
    CHECK(e.main == 0);
    CHECK(e.endmain == 7);
    CHECK(e.input_mask == 0x0FFF);
    CHECK(e.output_mask == 0x007F);
    CHECK(e.constants.size() == 4);
    CHECK(e.uniforms.size() == 45);
    REQUIRE(e.uniform("ProjMtx"));
    CHECK(e.uniform("ProjMtx")->first == 0x66);
    CHECK(e.uniform("ProjMtx")->last == 0x69);
    CHECK(e.uniform("UnivReg")->first == 0x29);
    CHECK(e.uniform("IsRgdSk")->first == 0x7A);
    std::array<std::uint32_t, 7> o{0x03020100, 0x07060504, 0x1F141312, 0x0B0A0908, 0x1F100D0C, 0x1F1F0F0E, 0x1F1F1716};
    CHECK(e.outmap.total == 7);
    CHECK(e.outmap.regs == o);
    CHECK(e.outmap.mode == 1);
    CHECK(e.outmap.clock == 0x01010703);
    CHECK(shader.binary.entries[1].outmap.total == 6);
    CHECK(shader.binary.entries[1].outmap.clock == 0x01010303);
    CHECK(shader.binary.entries[3].outmap.total == 4);
    CHECK(shader.binary.entries[3].outmap.mode == 0);
    CHECK(shader.binary.entries[3].outmap.clock == 0x01000003);
    CHECK(shader.binary.entries[2].main == 0x1D8);
}

TEST_CASE("the standalone .shbin files parse") {
    auto dir = romfs();
    if (!dir) return;
    int files = 0;
    for (auto& entry : fs::directory_iterator(*dir / "shaders")) {
        auto data = read(entry.path());
        auto binary = cgfx::parse_shbin(data, entry.path().filename().string());
        CHECK(!binary.code.empty());
        CHECK(binary.entries.size() == 1);
        ++files;
    }
    CHECK(files == 5);
}
