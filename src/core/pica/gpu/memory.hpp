// a window of the console's physical memory that the software GPU reads its
// command lists, vertices and textures from and draws into. addresses are
// physical, as the registers hold them; reads outside the window give zeros
// and writes there are dropped.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace kabegami::pica {

class Memory {
public:
    // VRAM's start by default, which keeps clear of address 0, the one a
    // texture unit takes as no texture
    explicit Memory(std::size_t size = 6 << 20, std::uint32_t base = 0x18000000) : base_(base), bytes_(size) {}

    std::uint32_t base() const { return base_; }
    std::size_t size() const { return bytes_.size(); }
    std::uint32_t end() const { return base_ + static_cast<std::uint32_t>(bytes_.size()); }

    // the bytes at addr, empty when they aren't all inside the window
    std::span<std::uint8_t> span(std::uint32_t addr, std::size_t length) {
        if (addr < base_ || addr - base_ > bytes_.size() || length > bytes_.size() - (addr - base_)) return {};
        return {bytes_.data() + (addr - base_), length};
    }
    std::span<const std::uint8_t> span(std::uint32_t addr, std::size_t length) const {
        return const_cast<Memory*>(this)->span(addr, length);
    }

    void read(std::uint32_t addr, std::span<std::uint8_t> out) const {
        for (std::size_t i = 0; i < out.size(); ++i) {
            auto a = static_cast<std::uint64_t>(addr) + i;
            out[i] = a >= base_ && a - base_ < bytes_.size() ? bytes_[static_cast<std::size_t>(a - base_)] : 0;
        }
    }
    void write(std::uint32_t addr, std::span<const std::uint8_t> in) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            auto a = static_cast<std::uint64_t>(addr) + i;
            if (a >= base_ && a - base_ < bytes_.size()) bytes_[static_cast<std::size_t>(a - base_)] = in[i];
        }
    }
    std::uint32_t read_u32(std::uint32_t addr) const {
        std::uint8_t b[4];
        read(addr, b);
        return static_cast<std::uint32_t>(b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24);
    }

    // room for size bytes, aligned, from the start of the window on: where
    // a scene puts its buffers. throws when the window is full.
    std::uint32_t allocate(std::size_t size, std::size_t align = 16) {
        auto at = (used_ + align - 1) / align * align;
        if (at + size > bytes_.size()) throw std::length_error("pica::Memory: the window is full");
        used_ = at + size;
        return base_ + static_cast<std::uint32_t>(at);
    }
    // bytes put somewhere new, and where
    std::uint32_t put(std::span<const std::uint8_t> data, std::size_t align = 16) {
        auto at = allocate(data.size(), align);
        write(at, data);
        return at;
    }
    std::uint32_t put_words(std::span<const std::uint32_t> words, std::size_t align = 16) {
        auto at = allocate(words.size() * 4, align);
        auto out = span(at, words.size() * 4);
        for (std::size_t i = 0; i < words.size(); ++i)
            for (std::size_t k = 0; k < 4; ++k) out[i * 4 + k] = static_cast<std::uint8_t>(words[i] >> (8 * k));
        return at;
    }
    // forgets what allocate gave out, keeping the bytes
    void reset_allocations(std::size_t to = 0) { used_ = to; }
    std::size_t allocated() const { return used_; }

private:
    std::uint32_t base_;
    std::vector<std::uint8_t> bytes_;
    std::size_t used_ = 0;
};

}  // namespace kabegami::pica
