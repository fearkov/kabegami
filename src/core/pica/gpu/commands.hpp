// a PICA command list as a program builds one: register writes, runs of
// consecutive registers, bursts into a port, and blocks of commands copied in
// as a file keeps them; then the padding and GPUREG_FINALIZE a list ends with.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/pica/registers.hpp"

namespace kabegami::pica {

class CommandList {
public:
    void write(std::size_t reg_index, std::uint32_t value, std::uint32_t mask = 0xF) { command(reg_index, mask, false, {&value, 1}); }
    // values to reg and the ones after it
    void run(std::size_t reg_index, std::span<const std::uint32_t> values, std::uint32_t mask = 0xF) {
        for (std::size_t i = 0; i < values.size(); i += 256) command(reg_index + i, mask, true, values.subspan(i, std::min<std::size_t>(256, values.size() - i)));
    }
    // values all to one register, a port that takes them in turn
    void port(std::size_t reg_index, std::span<const std::uint32_t> values) {
        for (std::size_t i = 0; i < values.size(); i += 256) command(reg_index, 0xF, false, values.subspan(i, std::min<std::size_t>(256, values.size() - i)));
    }
    // commands as a file holds them, whole
    void raw(std::span<const std::uint32_t> commands) { words.insert(words.end(), commands.begin(), commands.end()); }

    // float vectors into a shader unit's uniforms from register c`first`, as
    // four float32 words each, w first
    void float_uniforms(std::size_t block, std::size_t first, std::span<const std::array<float, 4>> vectors) {
        if (vectors.empty()) return;
        write(block + reg::shader::float_uniform_index, 0x80000000u | static_cast<std::uint32_t>(first));
        std::vector<std::uint32_t> data;
        for (auto& v : vectors)
            for (int c = 3; c >= 0; --c) data.push_back(std::bit_cast<std::uint32_t>(v[static_cast<std::size_t>(c)]));
        port(block + reg::shader::float_uniform_data0, data);
    }

    // the list padded to 16 bytes with GPUREG_FINALIZE in its last 8, as the
    // console needs it
    std::vector<std::uint32_t> finish() const {
        auto out = words;
        while ((out.size() + 2) % 4 != 0) {
            // a write that writes nothing
            out.push_back(0);
            out.push_back(0);
        }
        out.push_back(0x12345678);
        out.push_back(static_cast<std::uint32_t>(reg::finalize) | 0xFu << 16);
        return out;
    }

    std::vector<std::uint32_t> words;

private:
    void command(std::size_t reg_index, std::uint32_t mask, bool consecutive, std::span<const std::uint32_t> values) {
        auto extra = static_cast<std::uint32_t>(values.size() - 1);
        words.push_back(values[0]);
        words.push_back(static_cast<std::uint32_t>(reg_index) | (mask & 0xF) << 16 | extra << 20 | (consecutive ? 0x80000000u : 0u));
        words.insert(words.end(), values.begin() + 1, values.end());
        if (extra % 2) words.push_back(0);
    }
};

}  // namespace kabegami::pica
