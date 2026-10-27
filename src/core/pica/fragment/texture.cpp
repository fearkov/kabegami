#include "core/pica/fragment/texture.hpp"

#include <algorithm>
#include <stdexcept>

namespace kabegami::pica {
namespace {

constexpr std::uint8_t expand4(std::uint32_t v) { return static_cast<std::uint8_t>((v << 4) | v); }
constexpr std::uint8_t expand5(std::uint32_t v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
constexpr std::uint8_t expand6(std::uint32_t v) { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }

Rgba decode_bytes(TextureFormat format, const std::uint8_t* b) {
    auto le16 = [&] { return static_cast<std::uint32_t>(b[0] | b[1] << 8); };
    switch (format) {
        // textures keep their components in the reversed order colour buffers do
        case TextureFormat::rgba8: return {b[3], b[2], b[1], b[0]};
        case TextureFormat::rgb8: return {b[2], b[1], b[0], 0xFF};
        case TextureFormat::rgba5551: {
            auto v = le16();
            return {expand5((v >> 11) & 0x1F), expand5((v >> 6) & 0x1F), expand5((v >> 1) & 0x1F),
                    static_cast<std::uint8_t>(v & 1 ? 0xFF : 0)};
        }
        case TextureFormat::rgb565: {
            auto v = le16();
            return {expand5((v >> 11) & 0x1F), expand6((v >> 5) & 0x3F), expand5(v & 0x1F), 0xFF};
        }
        case TextureFormat::rgba4: {
            auto v = le16();
            return {expand4((v >> 12) & 0xF), expand4((v >> 8) & 0xF), expand4((v >> 4) & 0xF), expand4(v & 0xF)};
        }
        case TextureFormat::la8: return {b[1], b[1], b[1], b[0]};
        case TextureFormat::hilo8: return {0, b[1], b[0], 0xFF};
        case TextureFormat::l8: return {b[0], b[0], b[0], 0xFF};
        case TextureFormat::a8: return {0, 0, 0, b[0]};
        case TextureFormat::la4: {
            auto l = expand4(b[0] >> 4u), a = expand4(b[0] & 0xFu);
            return {l, l, l, a};
        }
        default: return {0, 0, 0, 0};
    }
}

Rgba decode_nibble(TextureFormat format, std::uint32_t nibble) {
    switch (format) {
        case TextureFormat::l4: {
            auto l = expand4(nibble);
            return {l, l, l, 0xFF};
        }
        // alpha alone reads with black colour, as in OpenGL ES
        case TextureFormat::a4: return {0, 0, 0, expand4(nibble)};
        default: return {0, 0, 0, 0};
    }
}

// the four deltas of each of ETC1's eight modifier tables
constexpr int etc1_modifiers[8][4] = {
    {2, 8, -2, -8},      {5, 17, -5, -17},    {9, 29, -9, -29},     {13, 42, -13, -42},
    {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183},
};

int sign_extend3(std::uint32_t v) { return v & 4 ? static_cast<int>(v) - 8 : static_cast<int>(v); }

std::uint64_t le64(const std::uint8_t* b) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[i];
    return v;
}

// one pixel of a plain 8-byte ETC1 block, which the PICA keeps little-endian
std::array<std::uint8_t, 3> decode_etc1_block(const std::uint8_t* block, std::uint32_t x, std::uint32_t y) {
    auto word = le64(block);
    auto low = static_cast<std::uint32_t>(word), high = static_cast<std::uint32_t>(word >> 32);
    bool flip = high & 1, diff = high & 2;
    std::array<int, 3> base0, base1;
    if (diff) {
        int r = (high >> 27) & 0x1F, g = (high >> 19) & 0x1F, b = (high >> 11) & 0x1F;
        int dr = sign_extend3((high >> 24) & 7), dg = sign_extend3((high >> 16) & 7), db = sign_extend3((high >> 8) & 7);
        auto e = [](int v) { return static_cast<int>(expand5(static_cast<std::uint32_t>(std::clamp(v, 0, 31)))); };
        base0 = {e(r), e(g), e(b)};
        base1 = {e(r + dr), e(g + dg), e(b + db)};
    } else {
        auto e = [&](int shift) { return static_cast<int>(expand4((high >> shift) & 0xF)); };
        base0 = {e(28), e(20), e(12)};
        base1 = {e(24), e(16), e(8)};
    }
    auto table0 = (high >> 5) & 7, table1 = (high >> 2) & 7;
    // an unflipped block splits into left and right halves, a flipped one
    // into top and bottom
    bool first = flip ? y < 2 : x < 2;
    auto& base = first ? base0 : base1;
    auto table = first ? table0 : table1;
    // the two index planes run down the columns
    auto pixel = x * 4 + y;
    auto msb = (low >> (pixel + 16)) & 1, lsb = (low >> pixel) & 1;
    int delta = etc1_modifiers[table][msb << 1 | lsb];
    auto c = [&](int v) { return static_cast<std::uint8_t>(std::clamp(v + delta, 0, 255)); };
    return {c(base[0]), c(base[1]), c(base[2])};
}

Rgba sample_etc1(std::span<const std::uint8_t> data, TextureFormat format, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    // 8x8 tiles row by row, each four 4x4 blocks in Z order
    std::size_t block_size = format == TextureFormat::etc1a4 ? 16 : 8;
    std::size_t tile = (y / 8) * std::max(width / 8, 1u) + x / 8;
    std::size_t sub = (x % 8) / 4 + 2 * ((y % 8) / 4);
    std::size_t at = (tile * 4 + sub) * block_size;
    if (at + block_size > data.size()) return {0, 0, 0, 0};
    auto block = data.data() + at;
    auto lx = x % 4, ly = y % 4;
    std::uint8_t alpha = 0xFF;
    if (format == TextureFormat::etc1a4) {
        alpha = expand4((le64(block) >> ((lx * 4 + ly) * 4)) & 0xF);
        block += 8;
    }
    auto rgb = decode_etc1_block(block, lx, ly);
    return {rgb[0], rgb[1], rgb[2], alpha};
}

}  // namespace

TextureFormat texture_format_from_raw(std::uint32_t value) {
    return value <= 13 ? static_cast<TextureFormat>(value) : TextureFormat::unknown;
}

std::uint32_t bits_per_pixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::rgba8: return 32;
        case TextureFormat::rgb8: return 24;
        case TextureFormat::rgba5551:
        case TextureFormat::rgb565:
        case TextureFormat::rgba4:
        case TextureFormat::la8:
        case TextureFormat::hilo8: return 16;
        case TextureFormat::l8:
        case TextureFormat::a8:
        case TextureFormat::la4: return 8;
        case TextureFormat::l4:
        case TextureFormat::a4: return 4;
        // a 4x4 block in 8 bytes, and ETC1A4 adds 4 bits of alpha a texel
        case TextureFormat::etc1: return 4;
        case TextureFormat::etc1a4: return 8;
        case TextureFormat::unknown: return 32;
    }
    return 32;
}

std::size_t texture_size(TextureFormat format, std::uint32_t width, std::uint32_t height) {
    return (static_cast<std::size_t>(width) * height * bits_per_pixel(format) + 7) / 8;
}

Rgba sample_texel(std::span<const std::uint8_t> data, TextureFormat format, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    if (format == TextureFormat::etc1 || format == TextureFormat::etc1a4) return sample_etc1(data, format, width, x, y);
    auto bits = bits_per_pixel(format);
    if (bits >= 8) {
        auto bytes = bits / 8;
        std::size_t at = morton_offset(x, y, width, bytes);
        if (at + bytes > data.size()) return {0, 0, 0, 0};
        return decode_bytes(format, data.data() + at);
    }
    // two texels a byte, the low nibble first
    auto bit = morton_offset(x, y, width, bits);
    std::size_t at = bit / 8;
    if (at >= data.size()) return {0, 0, 0, 0};
    auto byte = data[at];
    return decode_nibble(format, bit % 8 == 0 ? byte & 0xFu : byte >> 4u);
}

std::vector<Rgba> decode_texture(std::span<const std::uint8_t> data, TextureFormat format, std::uint32_t width, std::uint32_t height) {
    std::vector<Rgba> rows(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) rows[static_cast<std::size_t>(y) * width + x] = sample_texel(data, format, width, x, y);
    return rows;
}

std::vector<std::uint8_t> encode_texture(const std::vector<Rgba>& rows, TextureFormat format, std::uint32_t width, std::uint32_t height) {
    if (format == TextureFormat::etc1 || format == TextureFormat::etc1a4 || format == TextureFormat::hilo8 ||
        format == TextureFormat::unknown)
        throw std::invalid_argument("encode_texture: Kabegami doesn't write this format");
    if (rows.size() != static_cast<std::size_t>(width) * height) throw std::invalid_argument("encode_texture: the picture isn't width x height");
    std::vector<std::uint8_t> out(texture_size(format, width, height));
    auto bits = bits_per_pixel(format);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            auto c = rows[static_cast<std::size_t>(y) * width + x];
            // the luminance of a grey picture is its red, as decoding gives back
            auto l = c[0];
            if (bits < 8) {
                auto bit = morton_offset(x, y, width, bits);
                auto nibble = static_cast<std::uint8_t>((format == TextureFormat::a4 ? c[3] : l) >> 4);
                auto& byte = out[bit / 8];
                byte = static_cast<std::uint8_t>(bit % 8 == 0 ? (byte & 0xF0) | nibble : (byte & 0x0F) | nibble << 4);
                continue;
            }
            auto at = out.data() + morton_offset(x, y, width, bits / 8);
            auto put16 = [&](std::uint32_t v) {
                at[0] = static_cast<std::uint8_t>(v);
                at[1] = static_cast<std::uint8_t>(v >> 8);
            };
            switch (format) {
                case TextureFormat::rgba8: std::copy(c.rbegin(), c.rend(), at); break;
                case TextureFormat::rgb8: std::copy(c.rbegin() + 1, c.rend(), at); break;
                case TextureFormat::rgba5551: put16((c[0] >> 3u) << 11 | (c[1] >> 3u) << 6 | (c[2] >> 3u) << 1 | (c[3] >= 0x80 ? 1u : 0u)); break;
                case TextureFormat::rgb565: put16((c[0] >> 3u) << 11 | (c[1] >> 2u) << 5 | c[2] >> 3u); break;
                case TextureFormat::rgba4: put16((c[0] >> 4u) << 12 | (c[1] >> 4u) << 8 | (c[2] >> 4u) << 4 | c[3] >> 4u); break;
                case TextureFormat::la8:
                    at[0] = c[3];
                    at[1] = l;
                    break;
                case TextureFormat::l8: at[0] = l; break;
                case TextureFormat::a8: at[0] = c[3]; break;
                case TextureFormat::la4: at[0] = static_cast<std::uint8_t>((l >> 4) << 4 | c[3] >> 4); break;
                default: break;
            }
        }
    }
    return out;
}

}  // namespace kabegami::pica
