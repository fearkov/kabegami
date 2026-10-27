// SHA-256, which names the original file a recipe of edits was made for, so
// a recipe is only applied to the same file of the HOME Menu.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace kabegami {

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
std::string sha256_hex(std::span<const std::uint8_t> data);

}  // namespace kabegami
