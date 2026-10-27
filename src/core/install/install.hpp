// where a mod of the HOME Menu goes, and putting it there. a mod is a RomFS
// tree, romfs/3D/BannerBG_LZ.bin and the like, that replaces the HOME Menu's
// own files when it reads them:
//
//   on a console, Luma3DS reads SD:/luma/titles/<title id>/romfs/ with "Enable
//   game patching" on (13.4 fixed the crashes it caused in the HOME Menu);
//   Azahar reads <user dir>/load/mods/<title id>/romfs/ when it boots the
//   HOME Menu like a game;
//   Zakuro reads <data dir>/mods/<title id>/romfs/, once it boots the HOME
//   Menu, which it can't yet.
//
// nothing is written to the NAND, and removing the folder undoes it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace kabegami::install {

namespace fs = std::filesystem;

// "0004003000008F02", the folder name all three use.
std::string title_folder(std::uint64_t title_id);

// Azahar's user folder: AZAHAR_USER_DIR when set, then the platform's data
// folder, ~/.local/share/azahar-emu on Linux. a portable install keeps it
// next to the program instead, which a user names with --azahar.
std::optional<fs::path> azahar_user_dir();
fs::path azahar_romfs(const fs::path& user_dir, std::uint64_t title_id);

// Zakuro's data folder, ZAKURO_DATA_DIR or ~/.local/share/zakuro.
std::optional<fs::path> zakuro_data_dir();
fs::path zakuro_romfs(const fs::path& data_dir, std::uint64_t title_id);

// an SD card, or a folder to copy onto one.
fs::path luma_romfs(const fs::path& sd_root, std::uint64_t title_id);

struct Written {
    fs::path path;
    std::optional<fs::path> backup;  // a different file that was there, kept as .orig
};

// writes data at romfs_root/relative, making the folders. a different file
// already there, another mod's, is kept once as <name>.orig, as moyou keeps
// lines.qrc.orig, and never overwritten after; Kabegami's own earlier file,
// which kabegami.txt beside romfs/ names by its SHA-256, is simply replaced.
// the write goes through a temporary file, so an interrupted one leaves the
// old file.
Written put(const fs::path& romfs_root, std::string_view relative, ByteSpan data);

// takes Kabegami's file out again, putting a .orig back when there is one,
// and removes the folders that are left empty up to and including the
// title's folder. a file that isn't Kabegami's stays. what it did, for the
// user.
std::vector<std::string> remove(const fs::path& romfs_root, std::string_view relative);

}  // namespace kabegami::install
