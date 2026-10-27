// little-endian reads and writes over byte spans, every one bounds-checked,
// since everything Kabegami reads comes from files a user hands it.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kabegami {

// a file that is not what it claims, and where.
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Bytes = std::vector<std::uint8_t>;

// std::byteswap is C++23.
template <typename T>
constexpr T swap_bytes(T v) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U u = static_cast<U>(v), r = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        r = static_cast<U>(static_cast<U>(r << 8) | (u & 0xFF));
        u = static_cast<U>(u >> 8);
    }
    return static_cast<T>(r);
}
using ByteSpan = std::span<const std::uint8_t>;

class Reader {
public:
    explicit Reader(ByteSpan data, std::string what = "file") : data_(data), what_(std::move(what)) {}

    std::size_t size() const { return data_.size(); }
    ByteSpan data() const { return data_; }
    const std::string& what() const { return what_; }

    // the bytes [at, at + len), or an error naming the file and the range.
    ByteSpan bytes(std::size_t at, std::size_t len) const {
        if (at > data_.size() || len > data_.size() - at)
            throw FormatError(what_ + ": " + std::to_string(len) + " bytes at " + hex(at) + " run past its end (" +
                              hex(data_.size()) + ")");
        return data_.subspan(at, len);
    }

    template <typename T>
    T get(std::size_t at) const {
        static_assert(std::is_trivially_copyable_v<T>);
        auto b = bytes(at, sizeof(T));
        T value;
        std::memcpy(&value, b.data(), sizeof(T));
        if constexpr (std::endian::native == std::endian::big && std::is_integral_v<T>) value = swap_bytes(value);
        return value;
    }

    std::uint8_t u8(std::size_t at) const { return get<std::uint8_t>(at); }
    std::uint16_t u16(std::size_t at) const { return get<std::uint16_t>(at); }
    std::uint32_t u32(std::size_t at) const { return get<std::uint32_t>(at); }
    std::int32_t s32(std::size_t at) const { return get<std::int32_t>(at); }
    std::uint64_t u64(std::size_t at) const { return get<std::uint64_t>(at); }
    float f32(std::size_t at) const { return std::bit_cast<float>(u32(at)); }

    // a big-endian u16 or u32, as the TMD keeps them.
    std::uint16_t be16(std::size_t at) const { return swap_bytes(u16(at)); }
    std::uint32_t be32(std::size_t at) const { return swap_bytes(u32(at)); }
    std::uint64_t be64(std::size_t at) const { return swap_bytes(u64(at)); }

    // where a self-relative offset stored at `at` points: the field's own
    // position plus its signed value, as CGFX files keep their links. zero
    // means nothing.
    std::size_t relative(std::size_t at) const {
        auto offset = s32(at);
        if (offset == 0) return 0;
        auto target = static_cast<std::int64_t>(at) + offset;
        if (target < 0 || static_cast<std::size_t>(target) >= data_.size())
            throw FormatError(what_ + ": the link at " + hex(at) + " points outside it, to " + hex(target));
        return static_cast<std::size_t>(target);
    }

    // a NUL-terminated string.
    std::string cstring(std::size_t at) const {
        if (at >= data_.size()) throw FormatError(what_ + ": a string at " + hex(at) + " is past its end");
        auto end = std::find(data_.begin() + static_cast<std::ptrdiff_t>(at), data_.end(), std::uint8_t{0});
        if (end == data_.end()) throw FormatError(what_ + ": the string at " + hex(at) + " never ends");
        return std::string(data_.begin() + static_cast<std::ptrdiff_t>(at), end);
    }

    bool magic(std::size_t at, std::string_view expected) const {
        if (at > data_.size() || expected.size() > data_.size() - at) return false;
        return std::memcmp(data_.data() + at, expected.data(), expected.size()) == 0;
    }

    static std::string hex(std::uint64_t v) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
        return buf;
    }
    static std::string hex(std::int64_t v) { return v < 0 ? "-" + hex(static_cast<std::uint64_t>(-v)) : hex(static_cast<std::uint64_t>(v)); }

private:
    ByteSpan data_;
    std::string what_;
};

// writes over bytes in place, for edits that keep a file's layout.
class Patcher {
public:
    explicit Patcher(Bytes& data) : data_(data) {}

    template <typename T>
    void put(std::size_t at, T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (at > data_.size() || sizeof(T) > data_.size() - at)
            throw FormatError("a write of " + std::to_string(sizeof(T)) + " bytes at " + Reader::hex(at) + " is past the end");
        if constexpr (std::endian::native == std::endian::big && std::is_integral_v<T>) value = swap_bytes(value);
        std::memcpy(data_.data() + at, &value, sizeof(T));
    }
    void u8(std::size_t at, std::uint8_t v) { put(at, v); }
    void u16(std::size_t at, std::uint16_t v) { put(at, v); }
    void u32(std::size_t at, std::uint32_t v) { put(at, v); }
    void f32(std::size_t at, float v) { put(at, std::bit_cast<std::uint32_t>(v)); }
    void bytes(std::size_t at, ByteSpan src) {
        if (at > data_.size() || src.size() > data_.size() - at)
            throw FormatError("a write of " + std::to_string(src.size()) + " bytes at " + Reader::hex(at) + " is past the end");
        std::memcpy(data_.data() + at, src.data(), src.size());
    }

private:
    Bytes& data_;
};

}  // namespace kabegami
