#include <doctest/doctest.h>

#include <string_view>

#include "core/sha256.hpp"

using namespace kabegami;

namespace {
std::string hash(std::string_view s) {
    return sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}
}  // namespace

TEST_CASE("sha256 gives the published digests") {
    CHECK(hash("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hash("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
