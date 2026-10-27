// the PICA200's texture formats and how a texel is read out of the tiled
// data, ETC1 and ETC1A4 included. a port of Zakuro's zakuro-gpu texture.rs.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/pica/fragment/format.hpp"

namespace kabegami::pica {

// GPUREG_TEXUNITi_TYPE's numbering
enum class TextureFormat : std::uint8_t {
    rgba8 = 0,
    rgb8 = 1,
    rgba5551 = 2,
    rgb565 = 3,
    rgba4 = 4,
    la8 = 5,
    hilo8 = 6,  // two 8-bit components, read as (0, g, r, 1)
    l8 = 7,
    a8 = 8,
    la4 = 9,
    l4 = 10,
    a4 = 11,
    etc1 = 12,
    etc1a4 = 13,
    unknown = 14,
};

TextureFormat texture_format_from_raw(std::uint32_t value);
std::uint32_t bits_per_pixel(TextureFormat format);
// the bytes a level of width x height takes
std::size_t texture_size(TextureFormat format, std::uint32_t width, std::uint32_t height);

// one texel as straight RGBA8, (0, 0) the first texel in memory, which is the
// top left of the picture. a texel past the end of data reads as zero.
Rgba sample_texel(std::span<const std::uint8_t> data, TextureFormat format, std::uint32_t width, std::uint32_t x, std::uint32_t y);

// a whole level, row by row from the top
std::vector<Rgba> decode_texture(std::span<const std::uint8_t> data, TextureFormat format, std::uint32_t width, std::uint32_t height);

// a picture in a texture format, the reverse of decode_texture, for the
// formats Kabegami writes: L8, A8, LA8, RGBA8, RGB8, RGB565, RGBA5551,
// RGBA4, LA4, L4 and A4. throws for the compressed ones.
std::vector<std::uint8_t> encode_texture(const std::vector<Rgba>& rows, TextureFormat format, std::uint32_t width, std::uint32_t height);

}  // namespace kabegami::pica
