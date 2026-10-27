#include <doctest/doctest.h>

#include <bit>
#include <cstring>
#include <vector>

#include "core/cgfx/math.hpp"
#include "core/sha256.hpp"

using namespace kabegami;

TEST_CASE("the sine table is HOME's, bit for bit") {
    auto& table = cgfx::sincos_table();
    std::vector<std::uint8_t> bytes(sizeof(float) * 4 * 256);
    std::memcpy(bytes.data(), table.data(), bytes.size());
    // the SHA-256 of HOME 11.17's table at VA 0x30C7D4 (camera-shader-nw4c.md §3.7.6)
    CHECK(sha256_hex(bytes) == "f2433a2419e313eb7d18fc5f2682c7be465840bf0f120094ab2b00c79dcca7a8");
}

TEST_CASE("SinCosFIdx gives what HOME gives for half of 30 degrees") {
    // fovy/2 of BannerCamera, 0.5235988 / 2
    float half = std::bit_cast<float>(0x3F060A92u) * 0.5f;
    auto [s, c] = cgfx::sincos_fidx(half);
    CHECK(s == doctest::Approx(0.25880191f).epsilon(1e-7));
    CHECK(c == doctest::Approx(0.96586114f).epsilon(1e-7));
    CHECK(std::bit_cast<std::uint32_t>(c / s) == 0x406ED9E0u);
    auto [ns, nc] = cgfx::sincos_fidx(-half);
    CHECK(ns == -s);
    CHECK(nc == c);
}

TEST_CASE("an SRT matrix with no turn is the scale and the translation") {
    auto m = cgfx::srt_matrix({0.969094f, 0.969094f, 0.861467f}, {}, {0, 0.0309238f, -43.3993f});
    CHECK(m.at(0, 0) == 0.969094f);
    CHECK(m.at(2, 2) == 0.861467f);
    CHECK(m.at(1, 3) == 0.0309238f);
    CHECK(m.at(2, 3) == -43.3993f);
    CHECK(m.at(0, 1) == 0.0f);
    auto root = cgfx::srt_matrix({1, 1, 1}, {}, {0, 0, 44.786f});
    auto bg = cgfx::srt_matrix({1, 1, 1}, {}, {0, 0, -44.786f});
    auto world = cgfx::multiply(root, bg);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) CHECK(world.at(r, c) == cgfx::Matrix34::identity().at(r, c));
}
