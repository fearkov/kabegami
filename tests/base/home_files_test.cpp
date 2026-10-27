// checks against a real HOME Menu RomFS, which never ships with the code:
// KABEGAMI_ROMFS=<an extracted RomFS> runs them, and they pass without it.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/lz11.hpp"

using namespace kabegami;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> romfs() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !fs::is_directory(dir)) return std::nullopt;
    return fs::path(dir);
}

std::vector<std::uint8_t> read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

TEST_CASE("every compressed HOME file round-trips, no bigger than Nintendo's") {
    auto dir = romfs();
    if (!dir) return;
    std::size_t theirs = 0, ours = 0, files = 0;
    for (auto& entry : fs::recursive_directory_iterator(*dir)) {
        auto name = entry.path().filename().string();
        if (!entry.is_regular_file() || !name.ends_with("_LZ.bin")) continue;
        auto packed = read(entry.path());
        auto data = lz11::decompress(packed);
        CHECK(data.size() == lz11::decompressed_size(packed));
        auto again = lz11::compress(data);
        CHECK_MESSAGE(lz11::decompress(again) == data, name);
        theirs += packed.size();
        ours += again.size();
        ++files;
    }
    CHECK(files > 0);
    MESSAGE("Nintendo's " << theirs << " bytes, ours " << ours << " in " << files << " files");
    CHECK(ours <= theirs);
}

TEST_CASE("the wallpaper recompressed fits its old size") {
    auto dir = romfs();
    if (!dir) return;
    auto packed = read(*dir / "3D/BannerBG_LZ.bin");
    auto data = lz11::decompress(packed);
    auto again = lz11::compress(data);
    MESSAGE("BannerBG: Nintendo's " << packed.size() << " bytes, ours " << again.size());
    auto same = lz11::pad_to(again, packed.size());
    REQUIRE(same);
    CHECK(lz11::decompress(*same) == data);
}

#include "core/home.hpp"

TEST_CASE("an extracted RomFS opens as the HOME Menu and says its region") {
    auto dir = romfs();
    if (!dir) return;
    auto home = HomeFiles::open(*dir);
    auto banner = home.read("3D/BannerBG_LZ.bin");
    REQUIRE(banner);
    CHECK(*banner == read(*dir / "3D/BannerBG_LZ.bin"));
    CHECK_FALSE(home.read("3D/NoSuchFile_LZ.bin"));
    REQUIRE(home.region());
    MESSAGE(home.describe());
    auto files = home.list();
    CHECK(std::find(files.begin(), files.end(), "3D/nwgfx_DefaultShader_LZ.bin") != files.end());
}

// a decrypted game dump has an NCCH and a RomFS like the HOME Menu's:
// KABEGAMI_NCCH_DUMP=<a decrypted .3ds, .cci or .cxi> reads it.
TEST_CASE("a decrypted dump's RomFS lists and reads") {
    auto path = std::getenv("KABEGAMI_NCCH_DUMP");
    if (!path) return;
    auto home = HomeFiles::open(path);
    auto files = home.list();
    REQUIRE(!files.empty());
    MESSAGE(home.describe() << ", " << files.size() << " files, first " << files.front());
    auto first = home.read(files.front());
    REQUIRE(first);
}
