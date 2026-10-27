// MD5 (RFC 1321), which the hash words of a CGFX material are made from.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace kabegami {

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data);

}  // namespace kabegami
