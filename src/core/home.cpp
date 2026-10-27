#include "core/home.hpp"

#include <algorithm>
#include <functional>

namespace kabegami {
namespace fs = std::filesystem;

const std::vector<Region>& regions() {
    static const std::vector<Region> all{
        {"JPN", "JP_", 0x0004003000008202},
        {"USA", "US_", 0x0004003000008F02},
        {"EUR", "EU_", 0x0004003000009802},
        {"CHN", "CN_", 0x000400300000A102},
        {"KOR", "KR_", 0x000400300000A902},
        {"TWN", "TW_", 0x000400300000B102},
    };
    return all;
}

const Region* region_of_title(std::uint64_t title_id) {
    for (auto& r : regions())
        if (r.home_menu == title_id) return &r;
    return nullptr;
}

const Region* region_named(std::string_view name) {
    for (auto& r : regions())
        if (r.name == name) return &r;
    return nullptr;
}

namespace {

constexpr std::uint32_t none = 0xFFFFFFFF;
constexpr std::uint64_t media_unit = 0x200;

std::uint64_t align(std::uint64_t v, std::uint64_t to) { return (v + to - 1) / to * to; }

// the file a dump is in, read a range at a time: dumps of games can be
// gigabytes, the HOME Menu's a few.
class File {
public:
    explicit File(const fs::path& path) : in_(path, std::ios::binary), path_(path) {
        if (!in_) throw FormatError("can't open " + path.string());
        in_.seekg(0, std::ios::end);
        size_ = static_cast<std::uint64_t>(in_.tellg());
    }
    std::uint64_t size() const { return size_; }
    Bytes read(std::uint64_t at, std::uint64_t len) {
        if (at > size_ || len > size_ - at)
            throw FormatError(path_.string() + ": " + std::to_string(len) + " bytes at " + Reader::hex(at) +
                              " run past its end, the dump is cut short");
        Bytes out(len);
        in_.seekg(static_cast<std::streamoff>(at));
        in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(len));
        if (!in_) throw FormatError("can't read " + path_.string());
        return out;
    }

private:
    std::ifstream in_;
    fs::path path_;
    std::uint64_t size_ = 0;
};

std::string utf8(ByteSpan utf16) {
    std::string out;
    for (std::size_t i = 0; i + 1 < utf16.size(); i += 2) {
        std::uint32_t c = utf16[i] | std::uint32_t{utf16[i + 1]} << 8;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | c >> 12);
            out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::vector<std::string> split(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        if (end > start) parts.emplace_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}  // namespace

struct HomeFiles::Source {
    virtual ~Source() = default;
    virtual std::optional<Bytes> read(std::string_view path) = 0;
    virtual std::vector<std::string> list() = 0;
};

namespace {

struct Folder : HomeFiles::Source {
    fs::path root;
    explicit Folder(fs::path r) : root(std::move(r)) {}

    std::optional<Bytes> read(std::string_view path) override {
        auto file = root / fs::path(std::string(path));
        if (!fs::is_regular_file(file)) return std::nullopt;
        File f(file);
        return f.read(0, f.size());
    }

    std::vector<std::string> list() override {
        std::vector<std::string> out;
        for (auto& entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file()) out.push_back(fs::relative(entry.path(), root).generic_string());
        std::sort(out.begin(), out.end());
        return out;
    }
};

// the level 3 RomFS inside an NCCH: its directory and file tables, read once,
// and the file data read when asked for.
struct RomFs : HomeFiles::Source {
    File file;
    std::uint64_t data_at = 0;  // where the files' data starts, in the dump
    Bytes dirs, files;

    RomFs(const fs::path& path, std::uint64_t romfs_at, std::uint64_t romfs_size, bool says_plain) : file(path) {
        auto ivfc_bytes = file.read(romfs_at, 0x60);
        Reader ivfc(ivfc_bytes, "the RomFS");
        // some tools decrypt a dump without setting its NoCrypto flag, so the
        // header in the clear is what tells
        if (!ivfc.magic(0, "IVFC") || ivfc.u32(4) != 0x10000)
            throw FormatError(says_plain ? "the RomFS has no IVFC header, the dump is damaged"
                                         : "the dump is encrypted. In GodMode9, build the CIA from the HOME Menu's content, which decrypts it");
        auto master_hash = ivfc.u32(8);
        auto level3_block = std::uint64_t{1} << ivfc.u32(0x4C);
        auto level3_at = romfs_at + align(0x60 + master_hash, level3_block);
        auto header_bytes = file.read(level3_at, 0x28);
        Reader header(header_bytes, "the RomFS");
        if (header.u32(0) != 0x28) throw FormatError("the RomFS's level 3 header is not where IVFC says");
        dirs = file.read(level3_at + header.u32(0x0C), header.u32(0x10));
        files = file.read(level3_at + header.u32(0x1C), header.u32(0x20));
        data_at = level3_at + header.u32(0x24);
        if (data_at > romfs_at + romfs_size) throw FormatError("the RomFS's file data starts past its end");
    }

    std::string dir_name(std::uint32_t at) const {
        Reader r(dirs, "the RomFS directories");
        return utf8(r.bytes(at + 0x18, r.u32(at + 0x14)));
    }
    std::string file_name(std::uint32_t at) const {
        Reader r(files, "the RomFS files");
        return utf8(r.bytes(at + 0x20, r.u32(at + 0x1C)));
    }

    std::optional<Bytes> read(std::string_view path) override {
        Reader d(dirs, "the RomFS directories"), f(files, "the RomFS files");
        auto parts = split(path);
        if (parts.empty()) return std::nullopt;
        std::uint32_t dir = 0;  // the root
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            auto child = d.u32(dir + 0x08);
            while (child != none && dir_name(child) != parts[i]) child = d.u32(child + 0x04);
            if (child == none) return std::nullopt;
            dir = child;
        }
        for (auto at = d.u32(dir + 0x0C); at != none; at = f.u32(at + 0x04)) {
            if (file_name(at) == parts.back()) return file.read(data_at + f.u64(at + 0x08), f.u64(at + 0x10));
        }
        return std::nullopt;
    }

    std::vector<std::string> list() override {
        Reader d(dirs, "the RomFS directories"), f(files, "the RomFS files");
        std::vector<std::string> out;
        std::function<void(std::uint32_t, const std::string&)> walk = [&](std::uint32_t dir, const std::string& prefix) {
            for (auto at = d.u32(dir + 0x0C); at != none; at = f.u32(at + 0x04)) out.push_back(prefix + file_name(at));
            for (auto child = d.u32(dir + 0x08); child != none; child = d.u32(child + 0x04))
                walk(child, prefix + dir_name(child) + "/");
        };
        walk(0, "");
        std::sort(out.begin(), out.end());
        return out;
    }
};

struct Ncch {
    std::uint64_t at;
    std::uint64_t program_id;
    std::uint64_t romfs_at, romfs_size;
    bool says_plain;  // the NoCrypto flag GodMode9's decrypted dumps set
};

Ncch ncch_at(File& file, std::uint64_t at) {
    auto header_bytes = file.read(at, 0x200);
    Reader h(header_bytes, "the NCCH");
    if (!h.magic(0x100, "NCCH")) throw FormatError("no NCCH where the dump should have one");
    auto romfs = h.u32(0x1B0), romfs_size = h.u32(0x1B4);
    if (romfs == 0 || romfs_size == 0) throw FormatError("this NCCH has no RomFS");
    // flags[7] bit 2: no encryption
    bool plain = h.u8(0x188 + 7) & 0x4;
    return {at, h.u64(0x118), at + romfs * media_unit, romfs_size * media_unit, plain};
}

}  // namespace

HomeFiles::~HomeFiles() = default;
HomeFiles::HomeFiles(HomeFiles&&) noexcept = default;
HomeFiles& HomeFiles::operator=(HomeFiles&&) noexcept = default;

HomeFiles HomeFiles::open(const fs::path& path) {
    HomeFiles home;
    home.path_ = path;
    if (fs::is_directory(path)) {
        auto root = path;
        // a folder holding romfs/, as extraction tools write it, or the RomFS itself
        if (!fs::is_directory(root / "3D") && fs::is_directory(root / "romfs")) root /= "romfs";
        if (!fs::is_directory(root / "3D"))
            throw FormatError(path.string() + " has no 3D folder, so it is not the HOME Menu's RomFS");
        home.source_ = std::make_unique<Folder>(root);
        home.kind_ = "extracted RomFS";
        return home;
    }
    File file(path);
    if (file.size() < 0x200) throw FormatError(path.string() + " is too small to be a dump");
    auto head = file.read(0, 0x200);
    Reader h(head, path.string());
    Ncch ncch{};
    if (h.u32(0) == 0x2020) {
        // CIA: the header, certificates, ticket and TMD, each 64-byte aligned, then the contents
        auto certs_at = align(0x2020, 64);
        auto ticket_at = align(certs_at + h.u32(0x08), 64);
        auto tmd_at = align(ticket_at + h.u32(0x0C), 64);
        auto content_at = align(tmd_at + h.u32(0x10), 64);
        auto tmd_bytes = file.read(tmd_at, h.u32(0x10));
        Reader tmd(tmd_bytes, "the CIA's TMD");
        std::uint64_t signature;
        switch (tmd.be32(0)) {
            case 0x10003: signature = 0x23C; break;
            case 0x10004: signature = 0x13C; break;
            case 0x10005: signature = 0x7C; break;
            default: throw FormatError("the CIA's TMD has an unknown signature type");
        }
        auto body = 4 + signature;
        home.title_id_ = tmd.be64(body + 0x4C);
        home.version_ = tmd.be16(body + 0x9C);
        auto chunk = body + 0xC4 + 64 * 0x24;
        if (tmd.be16(chunk + 6) & 1)
            throw FormatError("the CIA's content is encrypted. In GodMode9, build the CIA again, which decrypts it");
        ncch = ncch_at(file, content_at);
        home.kind_ = "decrypted CIA";
    } else if (h.magic(0x100, "NCCH")) {
        ncch = ncch_at(file, 0);
        home.kind_ = "decrypted NCCH";
    } else if (h.magic(0x100, "NCSD")) {
        ncch = ncch_at(file, std::uint64_t{h.u32(0x120)} * media_unit);
        home.kind_ = "decrypted NCSD";
    } else {
        throw FormatError(path.string() + " is not a CIA, CXI or NCSD, nor a folder");
    }
    if (!home.title_id_) home.title_id_ = ncch.program_id;
    home.source_ = std::make_unique<RomFs>(path, ncch.romfs_at, ncch.romfs_size, ncch.says_plain);
    return home;
}

std::optional<Bytes> HomeFiles::read(std::string_view romfs_path) const { return source_->read(romfs_path); }

std::vector<std::string> HomeFiles::list() const { return source_->list(); }

const Region* HomeFiles::region() const {
    if (title_id_) return region_of_title(*title_id_);
    for (auto& path : list()) {
        if (!path.starts_with("message/")) continue;
        for (auto& r : regions())
            if (path.compare(8, r.prefix.size(), r.prefix) == 0) return &r;
    }
    return nullptr;
}

std::string HomeFiles::describe() const {
    std::string out = kind_ + " at " + path_.string();
    if (title_id_) out += ", title " + Reader::hex(*title_id_);
    if (version_) out += " v" + std::to_string(*version_);
    if (auto r = region()) out += ", " + std::string(r->name);
    return out;
}

}  // namespace kabegami
