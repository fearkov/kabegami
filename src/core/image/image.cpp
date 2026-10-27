#include "core/image/image.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace kabegami::image {

Image decode(ByteSpan file) {
    int w = 0, h = 0, channels = 0;
    if (file.size() > static_cast<std::size_t>(INT32_MAX)) throw FormatError("the picture is too big");
    auto* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &channels, 4);
    if (!pixels) throw FormatError(std::string("can't read the picture: ") + stbi_failure_reason());
    Image out(w, h);
    std::copy(pixels, pixels + out.rgba.size(), out.rgba.begin());
    stbi_image_free(pixels);
    return out;
}

Image read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FormatError("can't open " + path.string());
    Bytes data{std::istreambuf_iterator<char>(in), {}};
    try {
        return decode(data);
    } catch (const FormatError& e) {
        throw FormatError(path.string() + ": " + e.what());
    }
}

Bytes encode_png(const Image& image) {
    Bytes out;
    auto sink = [](void* context, void* data, int size) {
        auto* bytes = static_cast<Bytes*>(context);
        auto* p = static_cast<std::uint8_t*>(data);
        bytes->insert(bytes->end(), p, p + size);
    };
    if (!stbi_write_png_to_func(sink, &out, image.width, image.height, 4, image.rgba.data(), image.width * 4))
        throw FormatError("can't encode the picture");
    return out;
}

void write_png(const std::filesystem::path& path, const Image& image) {
    auto png = encode_png(image);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!out) throw FormatError("can't write " + path.string());
}

}  // namespace kabegami::image
