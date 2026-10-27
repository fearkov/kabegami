// LZ11, the compression of the HOME Menu's *_LZ.bin files.
//
// A header byte 0x11 and the decompressed size (24 bits, or 0 then 32 bits),
// then groups of a flag byte and eight tokens, the flag's high bit first. A
// clear bit is a literal byte, a set bit a copy of earlier output: 3 to 16
// bytes in two bytes, 17 to 272 in three, 273 to 65808 in four, from up to
// 4096 bytes back.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace kabegami::lz11 {

// a stream that can't be decompressed, and why.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// the size a stream decompresses to, read from its header.
std::size_t decompressed_size(std::span<const std::uint8_t> data);

std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> data);

// compresses as small as LZ11 allows, padded to a multiple of four bytes as
// Nintendo's files are.
std::vector<std::uint8_t> compress(std::span<const std::uint8_t> data);

// the stream padded with zeros to size, which decoders never read past the
// output, or nothing when it is already bigger. an edit that compresses no
// bigger than the file it replaces keeps that file's exact size this way.
std::optional<std::vector<std::uint8_t>> pad_to(std::vector<std::uint8_t> stream, std::size_t size);

}  // namespace kabegami::lz11
