// the animation reader and HOME's evaluator against the values of
// docs/research/cgfx-animation.md §4. KABEGAMI_ROMFS=<an extracted RomFS>
// runs the parts that need the files.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/anim/anim.hpp"

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

constexpr const char* translate = "Materials[\"mt_BG\"].TextureCoordinators[1].Translate";

}  // namespace

TEST_CASE("the frame controller wraps a loop and holds a one-time animation") {
    cgfx::FrameController loop(600, true);
    for (int i = 0; i < 599; ++i) loop.update();
    CHECK(loop.frame() == 599);
    loop.update();
    CHECK(loop.frame() == 0);
    loop.set_frame(-1);
    CHECK(loop.frame() == 599);
    cgfx::FrameController once(20, false);
    for (int i = 0; i < 30; ++i) once.update();
    CHECK(once.frame() == 20);
}

TEST_CASE("the Loop drifts the cells as the spec's table says") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto loop = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    REQUIRE(loop);
    CHECK(loop->at == 0x298C);
    CHECK(loop->loop);
    CHECK(loop->frames == 600);
    CHECK(loop->group == "MaterialAnimation");
    REQUIRE(loop->members.size() == 3);
    auto member = loop->find(translate);
    REQUIRE(member);
    std::array<std::array<float, 3>, 5> table{{{0, -0.3577780f, 0},
                                               {150, -0.4411085f, 0.0416500f},
                                               {300, -0.5244390f, 0.0833000f},
                                               {450, -0.6077695f, 0.1249500f},
                                               {599, -0.6905445f, 0.1663223f}}};
    for (auto [frame, x, y] : table) {
        auto v = cgfx::evaluate(file, *member, frame);
        CHECK(v.written == 3);
        CHECK(v.values[0] == doctest::Approx(x).epsilon(1e-6));
        CHECK(v.values[1] == doctest::Approx(y).epsilon(1e-6));
    }
    auto constant4 = cgfx::evaluate(file, *loop->find("Materials[\"mt_BG\"].MaterialColor.Constant4"), 37);
    CHECK(constant4.written == 8);
    CHECK(constant4.values[3] == 1.0f);
    auto texture = cgfx::evaluate(file, *loop->find("Materials[\"mt_BG\"].TextureMappers[1].Texture"), 37);
    REQUIRE(texture.texture);
    CHECK(*texture.texture == "BG_64_00");
}

TEST_CASE("SceneIn unfolds the wall from flat to its place") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto in = cgfx::find_animation(file, cgfx::Slot::skeletal_animations, "BannerBG_SceneIn");
    REQUIRE(in);
    CHECK_FALSE(in->loop);
    CHECK(in->frames == 20);
    auto bg = in->find("BG");
    REQUIRE(bg);
    CHECK(bg->type == cgfx::MemberType::transform);
    auto start = cgfx::evaluate(file, *bg, 0);
    // scales and TranslateY/Z, and nothing of the rotation or TranslateX
    CHECK(start.written == (0x7u | 0x300u));
    CHECK(start.values[0] == doctest::Approx(0.777129));
    CHECK(start.values[2] == doctest::Approx(0.001));
    CHECK(start.values[8] == doctest::Approx(0.223));
    CHECK(start.values[9] == doctest::Approx(-34.786));
    auto end = cgfx::evaluate(file, *bg, 20);
    CHECK(end.values[0] == doctest::Approx(1.0));
    CHECK(end.values[2] == doctest::Approx(1.0));
    CHECK(end.values[8] == doctest::Approx(0.0));
    CHECK(end.values[9] == doctest::Approx(-44.786));
}

TEST_CASE("segments that start away from frame 0 use relative key frames") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto pause = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_AppPause");
    auto texture = pause->find("Materials[\"mt_BG\"].TextureMappers[1].Texture");
    REQUIRE(texture);
    CHECK(*cgfx::evaluate(file, *texture, 0).texture == "BG_CapMask_00");
    CHECK(*cgfx::evaluate(file, *texture, 18).texture == "BG_CapMask_00");
    CHECK(*cgfx::evaluate(file, *texture, 19).texture == "BG_64_00");
    auto restart = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_AppRestart");
    auto constant0 = restart->find("Materials[\"mt_BG\"].MaterialColor.Constant0");
    CHECK(cgfx::evaluate(file, *constant0, 20).values[0] == doctest::Approx(0.4));
    CHECK(cgfx::evaluate(file, *constant0, 30).values[0] == doctest::Approx(0.7));
    CHECK(cgfx::evaluate(file, *constant0, 40).values[0] == doctest::Approx(1.0));
    auto quit = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_AppQuit");
    auto scale = quit->find("Materials[\"mt_BG\"].TextureCoordinators[0].Scale");
    CHECK(cgfx::evaluate(file, *scale, 10).values[0] == doctest::Approx(0.87));
    CHECK(cgfx::evaluate(file, *scale, 20).values[0] == doctest::Approx(1.0));
}

TEST_CASE("rewriting the Loop's keys changes the drift and keeps the layout") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto size = file.bytes().size();
    auto loop = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    auto member = loop->find(translate);
    auto segments = cgfx::read_segments(file, member->components[0].curve);
    REQUIRE(segments.size() == 1);
    CHECK(segments[0].format == 6);
    CHECK(segments[0].interpolation == 1);
    REQUIRE(segments[0].keys.size() == 2);
    CHECK(segments[0].keys[1].frame == 600);
    // twice the distance in x
    auto keys = segments[0].keys;
    keys[1].value = keys[0].value + 2 * (keys[1].value - keys[0].value);
    cgfx::write_keys(file, segments[0], keys);
    CHECK(file.bytes().size() == size);
    auto again = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    CHECK(cgfx::evaluate(file, *again->find(translate), 300).values[0] == doctest::Approx(-0.357778 - 0.333322).epsilon(1e-5));
    CHECK_THROWS(cgfx::write_keys(file, segments[0], {keys[0]}));
}

TEST_CASE("every animation in 3D/ reads and evaluates to finite values") {
    auto dir = romfs();
    if (!dir) return;
    int animations = 0, members = 0;
    for (auto& entry : fs::directory_iterator(*dir / "3D")) {
        auto file = load(entry.path());
        for (auto slot : {cgfx::Slot::skeletal_animations, cgfx::Slot::material_animations}) {
            auto& dict = file.dict(slot);
            for (std::size_t i = 0; i < dict.size(); ++i) {
                auto a = cgfx::read_animation(file, dict.value(i));
                ++animations;
                for (auto& m : a.members) {
                    ++members;
                    for (float f = -2; f <= a.frames + 2; f += 0.5f) {
                        auto v = cgfx::evaluate(file, m, f);
                        for (auto x : v.values) CHECK(std::isfinite(x));
                    }
                }
            }
        }
    }
    MESSAGE(animations << " animations, " << members << " members");
    CHECK(animations == 37);
}

TEST_CASE("stretching the Loop plays the same drift over more frames") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto loop = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    REQUIRE(loop);
    auto member = loop->find(translate);
    auto before300 = cgfx::evaluate(file, *member, 300), before600 = cgfx::evaluate(file, *member, 600);
    cgfx::set_length(file, *loop, 1200);
    auto stretched = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    CHECK(stretched->frames == 1200);
    auto at600 = cgfx::evaluate(file, *stretched->find(translate), 600), at1200 = cgfx::evaluate(file, *stretched->find(translate), 1200);
    for (std::size_t c = 0; c < 2; ++c) {
        CHECK(at600.values[c] == doctest::Approx(before300.values[c]).epsilon(1e-6));
        CHECK(at1200.values[c] == before600.values[c]);
    }
    // a constant member is left as it is
    CHECK(cgfx::evaluate(file, *stretched->find("Materials[\"mt_BG\"].MaterialColor.Constant4"), 900).values[3] == 1.0f);
}
