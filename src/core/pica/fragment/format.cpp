#include "core/pica/fragment/format.hpp"

namespace kabegami::pica {
namespace {

constexpr std::uint8_t expand4(std::uint32_t v) { return static_cast<std::uint8_t>((v << 4) | v); }
constexpr std::uint8_t expand5(std::uint32_t v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
constexpr std::uint8_t expand6(std::uint32_t v) { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }

std::uint16_t le16(const std::uint8_t* b) { return static_cast<std::uint16_t>(b[0] | b[1] << 8); }
void put16(std::uint8_t* out, std::uint32_t v) {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
}

}  // namespace

ColorFormat color_format_from_raw(std::uint32_t value) {
    switch (value & 7) {
        case 0: return ColorFormat::rgba8;
        case 1: return ColorFormat::rgb8;
        case 2: return ColorFormat::rgb565;
        case 3: return ColorFormat::rgb5a1;
        default: return ColorFormat::rgba4;
    }
}

ColorFormat color_format_from_buffer(std::uint32_t value) {
    switch (value & 7) {
        case 2: return ColorFormat::rgb5a1;
        case 3: return ColorFormat::rgb565;
        default: return color_format_from_raw(value);
    }
}

std::uint32_t color_buffer_raw(ColorFormat format) {
    switch (format) {
        case ColorFormat::rgba8: return 0;
        case ColorFormat::rgb8: return 1;
        case ColorFormat::rgb5a1: return 2;
        case ColorFormat::rgb565: return 3;
        case ColorFormat::rgba4: return 4;
    }
    return 0;
}

std::uint32_t bytes_per_pixel(ColorFormat format) {
    switch (format) {
        case ColorFormat::rgba8: return 4;
        case ColorFormat::rgb8: return 3;
        default: return 2;
    }
}

Rgba decode_pixel(ColorFormat format, const std::uint8_t* b) {
    switch (format) {
        case ColorFormat::rgba8: return {b[3], b[2], b[1], b[0]};
        case ColorFormat::rgb8: return {b[2], b[1], b[0], 0xFF};
        case ColorFormat::rgb565: {
            auto v = le16(b);
            return {expand5((v >> 11) & 0x1F), expand6((v >> 5) & 0x3F), expand5(v & 0x1F), 0xFF};
        }
        case ColorFormat::rgb5a1: {
            auto v = le16(b);
            return {expand5((v >> 11) & 0x1F), expand5((v >> 6) & 0x1F), expand5((v >> 1) & 0x1F),
                    static_cast<std::uint8_t>(v & 1 ? 0xFF : 0)};
        }
        case ColorFormat::rgba4: {
            auto v = le16(b);
            return {expand4((v >> 12) & 0xF), expand4((v >> 8) & 0xF), expand4((v >> 4) & 0xF), expand4(v & 0xF)};
        }
    }
    return {};
}

void encode_pixel(ColorFormat format, Rgba c, std::uint8_t* out) {
    switch (format) {
        case ColorFormat::rgba8:
            out[0] = c[3];
            out[1] = c[2];
            out[2] = c[1];
            out[3] = c[0];
            break;
        case ColorFormat::rgb8:
            out[0] = c[2];
            out[1] = c[1];
            out[2] = c[0];
            break;
        case ColorFormat::rgb565: put16(out, (c[0] >> 3u) << 11 | (c[1] >> 2u) << 5 | c[2] >> 3u); break;
        case ColorFormat::rgb5a1: put16(out, (c[0] >> 3u) << 11 | (c[1] >> 3u) << 6 | (c[2] >> 3u) << 1 | (c[3] >= 0x80 ? 1u : 0u)); break;
        case ColorFormat::rgba4: put16(out, (c[0] >> 4u) << 12 | (c[1] >> 4u) << 8 | (c[2] >> 4u) << 4 | c[3] >> 4u); break;
    }
}

}  // namespace kabegami::pica
