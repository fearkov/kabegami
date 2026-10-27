// the PICA200 in software: the register file command lists write, the shader
// unit those writes configure, the fixed attributes and immediate-mode
// vertices, the memory fill engine, and the draws, which go to raster.hpp.
// a port of Zakuro's zakuro-gpu lib.rs, by the same author (MIT).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/image/image.hpp"
#include "core/pica/fragment/format.hpp"
#include "core/pica/gpu/memory.hpp"
#include "core/pica/gpu/raster.hpp"
#include "core/pica/shader/shader.hpp"

namespace kabegami::pica {

class Gpu {
public:
    Gpu();

    Registers registers{};
    ShaderUnit vertex_shader, geometry_shader;
    std::array<Vec4, 16> fixed_attributes;

    // runs size bytes of commands from paddr, following jumps, and draws
    void run_command_list(Memory& memory, std::uint32_t paddr, std::uint32_t size);
    // one register write with its effects, as a command makes it
    void write(Memory& memory, std::size_t reg, std::uint32_t value, std::uint32_t mask = 0xF);

    // the PSC fill engine: a pattern of width 2 or 3 bytes, else 4, over
    // [start, end)
    static void memory_fill(Memory& memory, std::uint32_t start, std::uint32_t end, std::uint32_t value, std::uint32_t width);

    // what the draws did
    std::vector<DrawStats> draws;

private:
    void flush_immediate(Memory& memory);
    void configure_shader(ShaderUnit& unit, std::size_t offset, std::uint32_t value);

    std::array<std::uint32_t, 3> fixed_staging_{};
    std::size_t fixed_words_ = 0;
    std::array<Vec4, 16> immediate_attributes_{};
    std::size_t immediate_next_ = 0;
    std::vector<std::array<Vec4, 16>> immediate_;
    bool jump_pending_ = false;
    std::uint32_t jump_address_ = 0, jump_size_ = 0;
};

// a tiled buffer as a picture, its first row in memory the top
image::Image read_color_buffer(const Memory& memory, std::uint32_t addr, std::uint32_t width, std::uint32_t height, ColorFormat format);

// a picture turned a quarter counter-clockwise, which takes what the console
// draws for its sideways screens to the picture people see
image::Image rotate_counter_clockwise(const image::Image& picture);

}  // namespace kabegami::pica
