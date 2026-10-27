// the colour formats of colour buffers and display transfers, and the tiled
// layout the PICA200 keeps every image in: 8x8 tiles, row by row from the
// top, each a Morton curve. a port of Zakuro's zakuro-gpu format.rs.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace kabegami::pica {

enum class ColorFormat : std::uint8_t { rgba8, rgb8, rgb565, rgb5a1, rgba4 };

// the format a screen or a display transfer names
ColorFormat color_format_from_raw(std::uint32_t value);
// the format a colour buffer names, where 2 and 3 are the other way round
ColorFormat color_format_from_buffer(std::uint32_t value);
std::uint32_t color_buffer_raw(ColorFormat format);
std::uint32_t bytes_per_pixel(ColorFormat format);

using Rgba = std::array<std::uint8_t, 4>;

// one pixel to straight 8-bit RGBA, and back. the GPU keeps the components
// in ABGR order in memory.
Rgba decode_pixel(ColorFormat format, const std::uint8_t* bytes);
void encode_pixel(ColorFormat format, Rgba rgba, std::uint8_t* out);

// the place of (x, y) inside its 8x8 tile
constexpr std::uint32_t morton_interleave(std::uint32_t x, std::uint32_t y) {
    x &= 7;
    y &= 7;
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// the byte offset of pixel (x, y) in a tiled image width pixels across, (0,
// 0) its first pixel in memory
constexpr std::uint32_t morton_offset(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t bytes_per_pixel) {
    std::uint32_t tile = (y / 8) * (width / 8) + x / 8;
    return (tile * 64 + morton_interleave(x, y)) * bytes_per_pixel;
}

}  // namespace kabegami::pica
