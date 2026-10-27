#include <doctest/doctest.h>

#include "core/image/image.hpp"

using namespace kabegami;

TEST_CASE("a picture comes back from its PNG") {
    image::Image picture(7, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x) {
            auto* p = picture.at(x, y);
            p[0] = static_cast<std::uint8_t>(x * 30);
            p[1] = static_cast<std::uint8_t>(y * 50);
            p[2] = static_cast<std::uint8_t>(x * y);
            p[3] = static_cast<std::uint8_t>(255 - x);
        }
    auto again = image::decode(image::encode_png(picture));
    CHECK(again.width == 7);
    CHECK(again.height == 5);
    CHECK(again.rgba == picture.rgba);
}

TEST_CASE("what is not a picture says so") {
    Bytes junk{1, 2, 3, 4, 5};
    CHECK_THROWS_AS(image::decode(junk), FormatError);
}
