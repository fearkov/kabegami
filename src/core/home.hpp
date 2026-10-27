// the HOME Menu's files, from the user's own dump: a decrypted CIA (what
// GodMode9 builds from SYSNAND CTRNAND > title > 00040030 > <id> > content),
// a decrypted CXI or NCSD, or a folder holding an extracted RomFS.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace kabegami {

// the HOME Menu of each region. a theme or a mod goes to the one the console
// runs, so its title id names the folders installs write to.
struct Region {
    std::string_view name;       // "USA"
    std::string_view prefix;     // the message folders' prefix, "US_"
    std::uint64_t home_menu;     // 0004003000008F02
};

const std::vector<Region>& regions();
const Region* region_of_title(std::uint64_t title_id);
const Region* region_named(std::string_view name);

class HomeFiles {
public:
    // opens path, telling from its content what it is. throws FormatError
    // with a reason a user can act on, an encrypted dump for instance.
    static HomeFiles open(const std::filesystem::path& path);

    // a file of the RomFS by its path, "3D/BannerBG_LZ.bin", or nothing when
    // it is not there.
    std::optional<Bytes> read(std::string_view romfs_path) const;

    // the paths of every file in the RomFS, for listings.
    std::vector<std::string> list() const;

    // the HOME Menu's title id and version, when the dump says them. a folder
    // says neither, and its region is guessed from its message folders.
    std::optional<std::uint64_t> title_id() const { return title_id_; }
    std::optional<std::uint16_t> version() const { return version_; }
    const Region* region() const;
    std::string describe() const;

    // defined where Source is complete
    HomeFiles(HomeFiles&&) noexcept;
    HomeFiles& operator=(HomeFiles&&) noexcept;
    ~HomeFiles();

    // where the files come from, a folder or a dump's RomFS.
    struct Source;

private:
    HomeFiles() = default;
    std::unique_ptr<Source> source_;
    std::filesystem::path path_;
    std::string kind_;
    std::optional<std::uint64_t> title_id_;
    std::optional<std::uint16_t> version_;
};

}  // namespace kabegami
