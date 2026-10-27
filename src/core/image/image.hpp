// pictures in and out: the cell a user draws or imports, and the wallpaper
// saved as a PNG. 8-bit RGBA, rows top to bottom.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/bytes.hpp"

namespace kabegami::image {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {}

    bool operator==(const Image&) const = default;

    std::uint8_t* at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4; }
    const std::uint8_t* at(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
    }
};

// a PNG, JPEG, BMP, TGA or GIF from its bytes, whatever its channels, as RGBA.
// throws FormatError saying why it can't.
Image decode(ByteSpan file);
Image read(const std::filesystem::path& path);

Bytes encode_png(const Image& image);
void write_png(const std::filesystem::path& path, const Image& image);

}  // namespace kabegami::image
