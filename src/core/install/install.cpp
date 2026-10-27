#include "core/install/install.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

#include "core/sha256.hpp"

namespace kabegami::install {
namespace {

std::optional<fs::path> env(const char* name) {
    auto value = std::getenv(name);
    if (!value || !*value) return std::nullopt;
    return fs::path(value);
}

Bytes read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_all(const fs::path& path, ByteSpan data) {
    auto temporary = path;
    temporary += ".kabegami-new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) throw FormatError("can't write " + temporary.string());
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary);
        throw FormatError("can't put " + path.string() + " in place: " + error.message());
    }
}

// the platform's folder for program data.
std::optional<fs::path> data_home() {
#ifdef _WIN32
    return env("APPDATA");
#elif defined(__APPLE__)
    if (auto home = env("HOME")) return *home / "Library" / "Application Support";
    return std::nullopt;
#else
    if (auto xdg = env("XDG_DATA_HOME")) return xdg;
    if (auto home = env("HOME")) return *home / ".local" / "share";
    return std::nullopt;
#endif
}

}  // namespace

std::string title_folder(std::uint64_t title_id) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(title_id));
    return buf;
}

std::optional<fs::path> azahar_user_dir() {
    if (auto dir = env("AZAHAR_USER_DIR")) return dir;
    auto home = data_home();
    if (!home) return std::nullopt;
#ifdef _WIN32
    return *home / "Azahar";
#elif defined(__APPLE__)
    return *home / "Azahar";
#else
    return *home / "azahar-emu";
#endif
}

fs::path azahar_romfs(const fs::path& user_dir, std::uint64_t title_id) {
    return user_dir / "load" / "mods" / title_folder(title_id) / "romfs";
}

std::optional<fs::path> zakuro_data_dir() {
    if (auto dir = env("ZAKURO_DATA_DIR")) return dir;
    auto home = data_home();
    if (!home) return std::nullopt;
    return *home / "zakuro";
}

fs::path zakuro_romfs(const fs::path& data_dir, std::uint64_t title_id) {
    return data_dir / "mods" / title_folder(title_id) / "romfs";
}

fs::path luma_romfs(const fs::path& sd_root, std::uint64_t title_id) {
    return sd_root / "luma" / "titles" / title_folder(title_id) / "romfs";
}

namespace {

// what Kabegami wrote under a title's folder, beside romfs/ where neither
// LayeredFS reads: a line "<relative path> <SHA-256>" a file, so a file
// there is known for Kabegami's own and not another mod's
fs::path record_of(const fs::path& romfs_root) { return romfs_root.parent_path() / "kabegami.txt"; }

std::map<std::string, std::string> read_record(const fs::path& romfs_root) {
    std::map<std::string, std::string> out;
    std::ifstream in(record_of(romfs_root));
    std::string relative, hash;
    while (in >> relative >> hash) out[relative] = hash;
    return out;
}

void write_record(const fs::path& romfs_root, const std::map<std::string, std::string>& record) {
    auto path = record_of(romfs_root);
    if (record.empty()) {
        fs::remove(path);
        return;
    }
    std::string text;
    for (auto& [relative, hash] : record) text += relative + " " + hash + "\n";
    write_all(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

bool ours(const fs::path& romfs_root, std::string_view relative, const fs::path& path) {
    auto record = read_record(romfs_root);
    auto it = record.find(std::string(relative));
    return it != record.end() && fs::is_regular_file(path) && it->second == sha256_hex(read_all(path));
}

}  // namespace

Written put(const fs::path& romfs_root, std::string_view relative, ByteSpan data) {
    auto path = romfs_root / fs::path(std::string(relative));
    fs::create_directories(path.parent_path());
    Written written{path, std::nullopt};
    if (fs::is_regular_file(path) && !ours(romfs_root, relative, path)) {
        auto backup = path;
        backup += ".orig";
        auto old = read_all(path);
        bool same = old.size() == data.size() && std::equal(old.begin(), old.end(), data.begin());
        if (!same && !fs::exists(backup)) {
            fs::copy_file(path, backup);
            written.backup = backup;
        }
    }
    write_all(path, data);
    auto record = read_record(romfs_root);
    record[std::string(relative)] = sha256_hex(data);
    write_record(romfs_root, record);
    return written;
}

std::vector<std::string> remove(const fs::path& romfs_root, std::string_view relative) {
    std::vector<std::string> done;
    auto path = romfs_root / fs::path(std::string(relative));
    if (!ours(romfs_root, relative, path)) {
        if (fs::exists(path)) done.push_back(path.string() + " isn't Kabegami's, so it stays");
        return done;
    }
    auto record = read_record(romfs_root);
    record.erase(std::string(relative));
    write_record(romfs_root, record);
    auto backup = path;
    backup += ".orig";
    if (fs::exists(backup)) {
        fs::rename(backup, path);
        done.push_back("put back " + path.string() + " from its .orig");
    } else {
        fs::remove(path);
        done.push_back("removed " + path.string());
    }
    // empty folders up to the title's own, which holds romfs/
    auto stop = romfs_root.parent_path();
    for (auto dir = path.parent_path(); fs::exists(dir) && fs::is_empty(dir); dir = dir.parent_path()) {
        fs::remove(dir);
        done.push_back("removed the empty folder " + dir.string());
        if (dir == stop) break;
    }
    return done;
}

}  // namespace kabegami::install
