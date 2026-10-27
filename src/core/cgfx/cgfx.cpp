#include "core/cgfx/cgfx.hpp"

#include "core/lz11.hpp"

namespace kabegami::cgfx {
namespace {

// bit b of a name: bit (b & 7) from the least significant of byte b >> 3,
// zero past the end.
int bit(std::string_view name, std::uint32_t b) {
    auto byte = b >> 3;
    if (byte >= name.size()) return 0;
    return (static_cast<unsigned char>(name[byte]) >> (b & 7)) & 1;
}

constexpr std::size_t dict_header = 0x0C;
constexpr std::size_t node_size = 0x10;

}  // namespace

Dict::Dict(const Reader& file, std::size_t at) : at_(at) {
    if (!file.magic(at, "DICT")) throw FormatError(file.what() + ": no DICT at " + Reader::hex(at));
    auto count = file.u32(at + 8);
    if (file.u32(at + 4) != dict_header + node_size * (count + 1))
        throw FormatError(file.what() + ": the DICT at " + Reader::hex(at) + " has the wrong size for its entries");
    for (std::uint32_t i = 0; i <= count; ++i) {
        auto node = at + dict_header + node_size * i;
        nodes_.push_back({file.u32(node), file.u16(node + 4), file.u16(node + 6)});
        if (i == 0) continue;
        names_.push_back(string_at(file, node + 8));
        values_.push_back(file.relative(node + 12));
    }
    for (auto& n : nodes_)
        if (n.left > count || n.right > count)
            throw FormatError(file.what() + ": the DICT at " + Reader::hex(at) + " links outside its nodes");
}

std::optional<std::size_t> Dict::find(std::string_view name) const {
    if (names_.empty()) return std::nullopt;
    auto parent = nodes_[0];
    std::size_t i = parent.left;
    // a node whose bit is not below its parent's is an upward link, and
    // where the walk stops. there are at most as many steps as nodes
    for (std::size_t steps = 0; nodes_[i].reference_bit < parent.reference_bit && steps <= nodes_.size(); ++steps) {
        parent = nodes_[i];
        i = bit(name, parent.reference_bit) ? parent.right : parent.left;
    }
    if (i == 0 || names_[i - 1] != name) return std::nullopt;
    return i - 1;
}

ObjectHeader object_header(const Reader& file, std::size_t at) {
    return {file.u32(at), std::string(reinterpret_cast<const char*>(file.bytes(at + 4, 4).data()), 4), file.u32(at + 8),
            string_at(file, at + 0x0C)};
}

std::vector<std::size_t> object_list(const Reader& file, std::size_t at) {
    auto count = file.u32(at);
    std::vector<std::size_t> out;
    if (count == 0) return out;
    auto array = file.relative(at + 4);
    if (array == 0) throw FormatError(file.what() + ": the list at " + Reader::hex(at) + " has items but no array");
    for (std::uint32_t i = 0; i < count; ++i) out.push_back(file.relative(array + 4 * i));
    return out;
}

Dict dict_at(const Reader& file, std::size_t at) {
    if (file.u32(at) == 0) return {};
    auto dict = file.relative(at + 4);
    if (dict == 0) return {};
    return Dict(file, dict);
}

std::string string_at(const Reader& file, std::size_t at) {
    auto target = file.relative(at);
    return target == 0 ? std::string() : file.cstring(target);
}

File File::load(ByteSpan raw, std::string name) {
    File f;
    f.name_ = std::move(name);
    f.loaded_size_ = raw.size();
    f.bytes_ = !raw.empty() && raw[0] == 0x11 ? lz11::decompress(raw) : Bytes(raw.begin(), raw.end());
    Reader r(f.bytes_, f.name_);
    if (!r.magic(0, "CGFX")) throw FormatError(f.name_ + " is not a CGFX file");
    if (r.u16(4) != 0xFEFF) throw FormatError(f.name_ + " is big-endian, which no 3DS file is");
    auto header = r.u16(6);
    f.revision_ = r.u32(8);
    if (r.u32(0x0C) != f.bytes_.size())
        throw FormatError(f.name_ + " says it is " + std::to_string(r.u32(0x0C)) + " bytes but holds " + std::to_string(f.bytes_.size()));
    if (!r.magic(header, "DATA")) throw FormatError(f.name_ + " has no DATA block after its header");
    for (int slot = 0; slot < slot_count; ++slot) f.dicts_.push_back(dict_at(r, header + 8 + 8 * static_cast<std::size_t>(slot)));
    return f;
}

Bytes File::encode(std::optional<std::size_t> keep_size) const {
    auto packed = lz11::compress(bytes_);
    if (keep_size) {
        if (auto same = lz11::pad_to(packed, *keep_size)) return std::move(*same);
    }
    return packed;
}

}  // namespace kabegami::cgfx
