// the container against the HOME Menu's own files: KABEGAMI_ROMFS=<an
// extracted RomFS> runs it, and it passes without one.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/cgfx.hpp"

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

}  // namespace

TEST_CASE("the wallpaper's dictionaries hold what the specs found") {
    auto dir = romfs();
    if (!dir) return;
    auto file = cgfx::File::load(read(*dir / "3D/BannerBG_LZ.bin"), "BannerBG_LZ.bin");
    CHECK(file.revision() == 0x05000000);
    CHECK(file.bytes().size() == 86080);
    auto& models = file.dict(cgfx::Slot::models);
    REQUIRE(models.size() == 1);
    CHECK(models.name(0) == "BannerBG");
    CHECK(models.value(0) == 0x1B4);
    auto& textures = file.dict(cgfx::Slot::textures);
    REQUIRE(textures.size() == 4);
    CHECK(textures.value_of("BG_DmyApp_00") == std::optional<std::size_t>(0x1710));
    CHECK(textures.value_of("BG_64_00") == std::optional<std::size_t>(0x15FC));
    CHECK_FALSE(textures.find("BG_Missing"));
    CHECK(file.dict(cgfx::Slot::skeletal_animations).size() == 2);
    auto& anims = file.dict(cgfx::Slot::material_animations);
    CHECK(anims.value_of("BannerBG_Loop") == std::optional<std::size_t>(0x298C));
    auto header = cgfx::object_header(file.reader(), 0x1B4);
    CHECK(header.magic == "CMDL");
    CHECK(header.type == 0x40000092);
    CHECK(header.name == "BannerBG");
}

TEST_CASE("every name of every DICT of every 3D file is found where it is") {
    auto dir = romfs();
    if (!dir) return;
    int files = 0, names = 0;
    for (auto& entry : fs::directory_iterator(*dir / "3D")) {
        auto file = cgfx::File::load(read(entry.path()), entry.path().filename().string());
        ++files;
        for (int slot = 0; slot < cgfx::slot_count; ++slot) {
            auto& dict = file.dict(static_cast<cgfx::Slot>(slot));
            for (std::size_t i = 0; i < dict.size(); ++i) {
                CHECK(dict.find(dict.name(i)) == std::optional(i));
                ++names;
            }
            if (!dict.empty()) CHECK_FALSE(dict.find(dict.name(0) + "x"));
        }
    }
    MESSAGE(files << " files, " << names << " names");
    CHECK(files >= 18);
}

TEST_CASE("an unedited file encodes back to its own size") {
    auto dir = romfs();
    if (!dir) return;
    auto raw = read(*dir / "3D/BannerBG_LZ.bin");
    auto file = cgfx::File::load(raw, "BannerBG_LZ.bin");
    auto again = file.encode(file.loaded_size());
    CHECK(again.size() == raw.size());
    CHECK(cgfx::File::load(again, "again").bytes() == file.bytes());
}
