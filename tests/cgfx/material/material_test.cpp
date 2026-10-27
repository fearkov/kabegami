// the material reader against the HOME Menu's own files, the values of
// docs/research/cgfx-material-texture.md. KABEGAMI_ROMFS=<an extracted RomFS>
// runs the parts that need them.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/material/material.hpp"

using namespace kabegami;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> romfs() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !fs::is_directory(dir)) return std::nullopt;
    return fs::path(dir);
}

cgfx::File load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    Bytes raw{std::istreambuf_iterator<char>(in), {}};
    return cgfx::File::load(raw, path.filename().string());
}

// the material DICT of the first model, as the model reader finds it
std::size_t material_at(const cgfx::File& file, std::string_view name) {
    auto r = file.reader();
    auto model = file.dict(cgfx::Slot::models).value(0);
    auto materials = cgfx::dict_at(r, model + 0xBC);
    return *materials.value_of(name);
}

}  // namespace

TEST_CASE("a command block's writes come out in order, padding skipped") {
    // one write, then a consecutive run of four, as mt_BG's blend block
    std::vector<std::uint32_t> block{0x00E40100, 0x803F0100, 0x01010000, 0x00000000, 0xFF000000, 0};
    auto writes = cgfx::decode_commands(block);
    REQUIRE(writes.size() == 4);
    CHECK(writes[0].reg == 0x100);
    CHECK(writes[0].value == 0x00E40100);
    CHECK(writes[3].reg == 0x103);
    CHECK(writes[3].value == 0xFF000000);
    CHECK(writes[1].mask == 0xF);
    std::vector<std::uint32_t> two{0x10, 0x00010107, 0x01000000, 0x00080126};
    auto depth = cgfx::decode_commands(two);
    REQUIRE(depth.size() == 2);
    CHECK(depth[1].reg == 0x126);
    CHECK(depth[1].mask == 0x8);
}

TEST_CASE("colours pack the way the exporter and HOME pack them") {
    CHECK(cgfx::pack_color({0.40f, 0.45f, 0.50f, 0.0f}) == 0x00807366);
    CHECK(cgfx::pack_color({0.8f, 0.8f, 0.85f, 1.0f}) == 0xFFD9CCCC);
    CHECK(cgfx::pack_color({2.0f, -1.0f, 0.0f, 1.0f}) == 0xFF0000FF);
    // 0.9f · 255 is 229.4999939: 229 in double, but 229.5 in float, so 230
    CHECK((cgfx::pack_color({0, 0, 0, 0.9f}) >> 24) == 229);
    CHECK((cgfx::pack_color_home({0, 0, 0, 0.9f}) >> 24) == 230);
}

TEST_CASE("mt_BG is what the spec decoded") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto m = cgfx::read_material(file, material_at(file, "mt_BG"));
    CHECK(m.at == 0xC68);
    CHECK(m.name == "mt_BG");
    CHECK(m.packed_colors[static_cast<int>(cgfx::Color::constant0)] == 0x00807366);
    CHECK(m.packed_colors[static_cast<int>(cgfx::Color::constant3)] == 0xFFD9CCCC);
    CHECK(m.colors[static_cast<int>(cgfx::Color::constant2)].w == doctest::Approx(0.75));
    auto culling = cgfx::decode_commands(m.culling_commands);
    REQUIRE(culling.size() == 1);
    CHECK(culling[0].reg == 0x040);
    CHECK(culling[0].value == 1);
    CHECK(m.used_texcoords == 3);
    CHECK(m.coordinators[1].mode == 2);
    CHECK(m.coordinators[1].scale.x == 3.0f);
    CHECK(m.coordinators[1].scale.y == 6.0f);
    CHECK(m.coordinators[2].translate.x == doctest::Approx(-0.02));
    REQUIRE(m.mappers[0]);
    CHECK(m.mappers[0]->texture == "BG_DmyApp_00");
    CHECK(m.mappers[1]->texture == "BG_64_00");
    CHECK(m.mappers[2]->texture == "BG_64_00");
    CHECK(m.shader == "DefaultShader");
    CHECK(m.program_index == 0);
    std::array<std::uint32_t, 6> constants{0, 5, 1, 2, 3, 4};
    std::array<std::uint32_t, 6> sources{0x0E0403EE, 0x0EE50EFF, 0x0EF40F0E, 0x0EEE0FE0, 0x0EEE0FEE, 0x0EED0EDF};
    std::array<std::uint32_t, 6> colors{0x00807366, 0xFF000000, 0x40331A00, 0xBF000000, 0xFFD9CCCC, 0xFF000000};
    for (int i = 0; i < 6; ++i) {
        auto& stage = m.fragment.stages[static_cast<std::size_t>(i)];
        CHECK(stage.constant == constants[static_cast<std::size_t>(i)]);
        CHECK(stage.commands[0] == sources[static_cast<std::size_t>(i)]);
        CHECK(m.stage_color(i) == colors[static_cast<std::size_t>(i)]);
    }
    // binding BG_64_00 (L8, 64x64) to mapper 1, as HOME's binder does
    auto bound = cgfx::bind_texture(*m.mappers[1], 7, 64, 64, 1, 0x18000000);
    REQUIRE(bound.size() == 8);
    CHECK(bound[1] == 0x000F0096);
    CHECK(bound[3] == 0x804F0091);
    CHECK(bound[4] == 0x00400040);
    CHECK(bound[5] == 0x00002206);
    CHECK(bound[6] == 0);
    CHECK(bound[7] == 0x18000000 >> 3);
    auto writes = cgfx::decode_commands(bound);
    REQUIRE(writes.size() == 6);
    CHECK(writes[5].reg == 0x095);
}

TEST_CASE("setting a colour keeps both its forms and the colours' hash in step") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto at = cgfx::dict_at(file.reader(), file.dict(cgfx::Slot::models).value(0) + 0xBC).value(0);
    auto before = cgfx::read_material(file, at);
    CHECK(before.hashes[5] == 0xCE15886F);
    cgfx::set_color(file, before, cgfx::Color::constant3, {0.5f, 0.25f, 0.125f, 1.0f});
    auto after = cgfx::read_material(file, at);
    CHECK(after.colors[8].y == 0.25f);
    CHECK(after.packed_colors[8] == cgfx::pack_color({0.5f, 0.25f, 0.125f, 1.0f}));
    CHECK(after.hashes[5] == cgfx::colors_hash(after.colors));
    CHECK(after.hashes[5] != before.hashes[5]);
}

TEST_CASE("every HOME material reads, its colours pack, its coordinators compute") {
    auto dir = romfs();
    if (!dir) return;
    int materials = 0, coordinators = 0;
    double worst = 0;
    for (auto& entry : fs::directory_iterator(*dir / "3D")) {
        auto file = load(entry.path());
        auto r = file.reader();
        auto& models = file.dict(cgfx::Slot::models);
        for (std::size_t mi = 0; mi < models.size(); ++mi) {
            auto dict = cgfx::dict_at(r, models.value(mi) + 0xBC);
            for (std::size_t i = 0; i < dict.size(); ++i) {
                auto m = cgfx::read_material(file, dict.value(i));
                ++materials;
                for (int c = 0; c < cgfx::color_count; ++c) {
                    auto packed = cgfx::pack_color(m.colors[static_cast<std::size_t>(c)]);
                    auto stored = m.packed_colors[static_cast<std::size_t>(c)];
                    if (c == static_cast<int>(cgfx::Color::ambient)) packed &= 0xFFFFFF, stored &= 0xFFFFFF;
                    CHECK(packed == stored);
                }
                for (int k = 0; k < m.used_texcoords; ++k) {
                    auto& t = m.coordinators[static_cast<std::size_t>(k)];
                    auto computed = cgfx::coordinator_matrix(t.mode, t.scale, t.rotate, t.translate);
                    for (std::size_t e = 0; e < 12; ++e) worst = std::max(worst, static_cast<double>(std::fabs(computed.m[e] - t.matrix.m[e])));
                    ++coordinators;
                }
                for (auto& stage : m.fragment.stages) CHECK(cgfx::decode_commands(stage.commands).size() == 5);
                // MaterialColorsHash, and the empty ShaderParamsHash
                CHECK(cgfx::colors_hash(m.colors) == m.hashes[5]);
                CHECK(m.hashes[1] == cgfx::hash_buffer({}));
            }
        }
    }
    MESSAGE(materials << " materials, " << coordinators << " coordinators, worst matrix difference " << worst);
    CHECK(materials == 53);
    CHECK(coordinators == 89);
    CHECK(worst < 1e-6);
}
