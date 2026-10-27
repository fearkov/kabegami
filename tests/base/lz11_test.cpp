#include <doctest/doctest.h>

#include "core/lz11.hpp"

using namespace kabegami;

namespace {

// bytes that repeat in runs and patterns, with noise between.
std::vector<std::uint8_t> sample(std::size_t len, std::uint32_t seed) {
    auto next = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    std::vector<std::uint8_t> out;
    while (out.size() < len) {
        switch (next() % 4) {
            case 0: out.push_back(static_cast<std::uint8_t>(next())); break;
            case 1: out.insert(out.end(), next() % 40, static_cast<std::uint8_t>(next())); break;
            default:
                if (out.size() > 8) {
                    std::size_t back = 1 + next() % std::min<std::size_t>(out.size(), 5000);
                    auto start = out.size() - back;
                    auto count = next() % 300;
                    for (std::size_t i = 0; i < count; ++i) out.push_back(out[start + i % back]);
                } else {
                    out.push_back(0);
                }
        }
    }
    out.resize(len);
    return out;
}

}  // namespace

TEST_CASE("what is compressed comes back") {
    for (auto [len, seed] : {std::pair<std::size_t, std::uint32_t>{0, 1}, {1, 2}, {2, 3}, {17, 4}, {4096, 5}, {70000, 6}, {200000, 7}}) {
        auto data = sample(len, seed);
        auto packed = lz11::compress(data);
        CHECK(packed.size() % 4 == 0);
        CHECK(lz11::decompress(packed) == data);
    }
}

TEST_CASE("every token size comes back") {
    for (std::size_t run : {3, 16, 17, 272, 273, 4096, 65808, 65809, 140000}) {
        std::vector<std::uint8_t> data{7, 8, 9};
        data.insert(data.end(), run, 5);
        data.insert(data.end(), {1, 2, 3});
        CHECK(lz11::decompress(lz11::compress(data)) == data);
    }
}

TEST_CASE("a hand-made stream decodes") {
    // 'a' 'b' then a copy of 4 from 2 back: "ababab"
    std::vector<std::uint8_t> stream{0x11, 6, 0, 0, 0b0010'0000, 'a', 'b', 0x30, 0x01};
    auto out = lz11::decompress(stream);
    CHECK(std::string(out.begin(), out.end()) == "ababab");
    CHECK(lz11::decompressed_size(stream) == 6);
}

TEST_CASE("a padded stream decodes the same") {
    auto data = sample(5000, 9);
    auto packed = lz11::compress(data);
    auto padded = lz11::pad_to(packed, packed.size() + 40);
    REQUIRE(padded);
    CHECK(padded->size() == packed.size() + 40);
    CHECK(lz11::decompress(*padded) == data);
    CHECK_FALSE(lz11::pad_to(packed, packed.size() - 1));
}

TEST_CASE("broken streams say why") {
    CHECK_THROWS_WITH_AS(lz11::decompress(std::vector<std::uint8_t>{0x10, 1, 0, 0}), "not LZ11, the first byte is 16", lz11::Error);
    CHECK_THROWS_WITH_AS(lz11::decompress(std::vector<std::uint8_t>{0x11, 4, 0, 0, 0, 'a'}), "the LZ11 stream ends early", lz11::Error);
    CHECK_THROWS_WITH_AS(lz11::decompress(std::vector<std::uint8_t>{0x11, 4, 0, 0, 0x80, 0x30, 0x05}),
                         "a copy at output byte 0 reaches 6 bytes back", lz11::Error);
}
