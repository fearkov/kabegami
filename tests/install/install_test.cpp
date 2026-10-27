#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "core/install/install.hpp"

using namespace kabegami;
namespace fs = std::filesystem;

namespace {

// a fresh folder under the system's temporary one, gone afterwards.
struct Scratch {
    fs::path dir;
    explicit Scratch(const char* name) : dir(fs::temp_directory_path() / name) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() { fs::remove_all(dir); }
};

Bytes read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace

TEST_CASE("mods go where Luma, Azahar and Zakuro look") {
    CHECK(install::title_folder(0x0004003000008F02) == "0004003000008F02");
    CHECK(install::luma_romfs("/sd", 0x0004003000009802) == fs::path("/sd/luma/titles/0004003000009802/romfs"));
    CHECK(install::azahar_romfs("/u", 0x0004003000008F02) == fs::path("/u/load/mods/0004003000008F02/romfs"));
    CHECK(install::zakuro_romfs("/z", 0x0004003000008202) == fs::path("/z/mods/0004003000008202/romfs"));
}

TEST_CASE("a file put over another mod's keeps that one, once, and removing puts it back") {
    Scratch scratch("kabegami-install-test");
    auto romfs = install::luma_romfs(scratch.dir, 0x0004003000008F02);
    Bytes theirs{1, 2, 3}, ours{4, 5, 6, 7}, again{8};
    auto path = romfs / "3D/BannerBG_LZ.bin";

    // another mod's file, written by something else
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(theirs.data()), 3);

    // Kabegami's goes over it, keeping that one as .orig
    auto first = install::put(romfs, "3D/BannerBG_LZ.bin", ours);
    REQUIRE(first.backup);
    CHECK(read(*first.backup) == theirs);
    CHECK(read(path) == ours);
    // and names its own file beside romfs/, not inside it
    CHECK(fs::exists(romfs.parent_path() / "kabegami.txt"));

    // Kabegami's own file is simply replaced, and the .orig stays theirs
    auto second = install::put(romfs, "3D/BannerBG_LZ.bin", again);
    CHECK_FALSE(second.backup);
    CHECK(read(path) == again);
    CHECK(read(romfs / "3D/BannerBG_LZ.bin.orig") == theirs);

    // removing puts theirs back, and forgets Kabegami's
    install::remove(romfs, "3D/BannerBG_LZ.bin");
    CHECK(read(path) == theirs);
    CHECK_FALSE(fs::exists(romfs / "3D/BannerBG_LZ.bin.orig"));
    CHECK_FALSE(fs::exists(romfs.parent_path() / "kabegami.txt"));

    // which then stays, not being Kabegami's
    auto lines = install::remove(romfs, "3D/BannerBG_LZ.bin");
    CHECK(read(path) == theirs);
    CHECK(lines.size() == 1);
}

TEST_CASE("Kabegami's own file goes with the folders it leaves empty") {
    Scratch scratch("kabegami-install-test-2");
    auto romfs = install::luma_romfs(scratch.dir, 0x0004003000008F02);
    Bytes ours{4, 5, 6, 7}, again{8};
    CHECK_FALSE(install::put(romfs, "3D/BannerBG_LZ.bin", ours).backup);
    CHECK_FALSE(install::put(romfs, "3D/BannerBG_LZ.bin", again).backup);
    CHECK_FALSE(fs::exists(romfs / "3D/BannerBG_LZ.bin.orig"));
    install::remove(romfs, "3D/BannerBG_LZ.bin");
    CHECK_FALSE(fs::exists(romfs));
    CHECK_FALSE(fs::exists(romfs.parent_path()));
    CHECK(fs::exists(scratch.dir / "luma" / "titles"));
}
