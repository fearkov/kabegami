// the wallpaper drawn from the user's own files (KABEGAMI_ROMFS=<an extracted
// RomFS>), and KABEGAMI_DUMP=<a folder> to look at what came out.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "core/image/image.hpp"
#include "core/pica/gpu/gpu.hpp"
#include "core/scene/banner.hpp"

using namespace kabegami;

namespace {

std::optional<scene::Banner> banner() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !std::filesystem::is_directory(dir)) return std::nullopt;
    auto load = [&](const char* name) {
        std::ifstream in(std::filesystem::path(dir) / "3D" / name, std::ios::binary);
        Bytes raw{std::istreambuf_iterator<char>(in), {}};
        return cgfx::File::load(raw, name);
    };
    return scene::Banner(load("BannerBG_LZ.bin"), load("BannerCamera_LZ.bin"), load("nwgfx_DefaultShader_LZ.bin"));
}

void dump(const image::Image& picture, const std::string& name) {
    if (auto dir = std::getenv("KABEGAMI_DUMP")) image::write_png(std::filesystem::path(dir) / name, picture);
}

}  // namespace

TEST_CASE("the wallpaper covers the screen, in the scene's own colours") {
    auto b = banner();
    if (!b) return;
    auto frame = b->build();
    pica::Gpu gpu;
    gpu.run_command_list(frame.memory, frame.commands_at, static_cast<std::uint32_t>(frame.commands.size() * 4));
    REQUIRE(gpu.draws.size() == 1);
    auto& stats = gpu.draws[0];
    MESSAGE("vertices " << stats.vertices << ", triangles " << stats.triangles << ", clipped away " << stats.clipped_away << ", culled "
                        << stats.culled << ", written " << stats.written);
    // every pixel of the 240x400 buffer, once
    CHECK(stats.written == 240 * 400);

    auto picture = b->render();
    REQUIRE(picture.width == scene::screen_width);
    REQUIRE(picture.height == scene::screen_height);
    dump(picture, "banner-0.png");
    std::set<std::uint32_t> colors;
    for (int y = 0; y < picture.height; ++y)
        for (int x = 0; x < picture.width; ++x) {
            auto p = picture.at(x, y);
            colors.insert(static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16));
            CHECK(p[3] == 255);
        }
    MESSAGE(colors.size() << " colours");
    CHECK(colors.size() > 8);
}

TEST_CASE("the cells drift through the loop") {
    auto b = banner();
    if (!b) return;
    auto first = b->render({0, 20});
    auto later = b->render({150, 20});
    dump(later, "banner-150.png");
    CHECK((first.rgba != later.rgba));
    // the loop comes round to where it started, and SceneIn stays at its end
    CHECK((b->render({600, 20}).rgba == first.rgba));
    CHECK((b->render({0, 45}).rgba == first.rgba));
    // the wall unfolding as HOME starts
    dump(b->render({0, 0}), "banner-scene-in-0.png");
    dump(b->render({0, 10}), "banner-scene-in-10.png");
}
