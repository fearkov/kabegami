#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "core/md5.hpp"

using namespace kabegami;

namespace {
std::string hex(std::string_view text) {
    auto digest = md5({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    std::string out;
    for (auto b : digest) {
        constexpr char digits[] = "0123456789abcdef";
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}
}  // namespace

TEST_CASE("MD5 gives RFC 1321's digests") {
    CHECK(hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
}
