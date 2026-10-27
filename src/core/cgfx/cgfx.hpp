// the CGFX container (NintendoWare for CTR's graphics files, revision
// 0x05000000 as the HOME Menu has them): the header, the DATA block's 15
// dictionary slots, DICTs, self-relative links and the header most objects
// start with. what is inside the objects lives in model/, anim/ and program/.
// docs/research/cgfx-model.md sections 2-7 is the layout this follows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace kabegami::cgfx {

// the DATA block's dictionary slots, in file order.
enum class Slot : int {
    models = 0,
    textures = 1,
    luts = 2,
    materials = 3,
    shaders = 4,
    cameras = 5,
    lights = 6,
    fogs = 7,
    environments = 8,
    skeletal_animations = 9,
    material_animations = 10,
    visibility_animations = 11,
    camera_animations = 12,
    light_animations = 13,
    emitters = 14,
};
constexpr int slot_count = 15;

// a DICT: a Patricia tree over names, whose entries keep their file order.
// indices stored elsewhere (a mesh's material index, a bone's) count entries
// in that order.
class Dict {
public:
    Dict() = default;
    // the DICT at `at`, which must start with its magic.
    Dict(const Reader& file, std::size_t at);

    std::size_t size() const { return names_.size(); }
    bool empty() const { return names_.empty(); }
    std::size_t offset() const { return at_; }

    const std::string& name(std::size_t entry) const { return names_.at(entry); }
    // where entry's value is, as an offset in the file, 0 for none.
    std::size_t value(std::size_t entry) const { return values_.at(entry); }

    // an entry by name, walking the tree as the runtime does: down while the
    // reference bits fall, then the one candidate is compared whole.
    std::optional<std::size_t> find(std::string_view name) const;
    std::optional<std::size_t> value_of(std::string_view name) const {
        auto entry = find(name);
        return entry ? std::optional(values_[*entry]) : std::nullopt;
    }

private:
    struct Node {
        std::uint32_t reference_bit;
        std::uint16_t left, right;
    };
    std::size_t at_ = 0;
    std::vector<Node> nodes_;  // node 0 is the root, node i + 1 entry i
    std::vector<std::string> names_;
    std::vector<std::size_t> values_;
};

// the header most objects start with: a type word, the magic, a revision, a
// name and their user data.
struct ObjectHeader {
    std::uint32_t type;
    std::string magic;
    std::uint32_t revision;
    std::string name;
};
ObjectHeader object_header(const Reader& file, std::size_t at);

// a list: a u32 count at `at`, then a link to an array of that many links,
// each from its own slot. the offsets of what they point to.
std::vector<std::size_t> object_list(const Reader& file, std::size_t at);

// a count at `at` followed by a link to a DICT, empty when the count is 0.
Dict dict_at(const Reader& file, std::size_t at);

// a string a link at `at` points to, "" for a null link.
std::string string_at(const Reader& file, std::size_t at);

// one CGFX file, decompressed, holding its bytes for reads and for edits that
// keep its layout.
class File {
public:
    // LZ11-compressed as the HOME Menu keeps them, or already plain.
    static File load(ByteSpan raw, std::string name);

    const std::string& name() const { return name_; }
    const Bytes& bytes() const { return bytes_; }
    // the bytes to edit in place. every Reader made before stays valid, as
    // nothing here grows or moves them.
    Bytes& bytes() { return bytes_; }
    Reader reader() const { return Reader(bytes_, name_); }

    std::uint32_t revision() const { return revision_; }
    const Dict& dict(Slot slot) const { return dicts_[static_cast<int>(slot)]; }

    // the file as the HOME Menu reads it: compressed again, and padded to
    // keep_size when it fits, so an edit can leave the file's size as it was.
    Bytes encode(std::optional<std::size_t> keep_size = std::nullopt) const;
    // the size the file had when it was loaded, compressed.
    std::size_t loaded_size() const { return loaded_size_; }

private:
    std::string name_;
    Bytes bytes_;
    std::uint32_t revision_ = 0;
    std::vector<Dict> dicts_;
    std::size_t loaded_size_ = 0;
};

}  // namespace kabegami::cgfx
