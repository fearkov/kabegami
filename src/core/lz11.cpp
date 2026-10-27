#include "core/lz11.hpp"

#include <algorithm>
#include <string>

namespace kabegami::lz11 {
namespace {

constexpr std::size_t window = 0x1000;
constexpr std::size_t min_match = 3;
constexpr std::size_t max_match = 0xFFFF + 0x111;

struct Header {
    std::size_t size;
    std::size_t tokens_at;
};

Header header(std::span<const std::uint8_t> data) {
    if (data.empty()) throw Error("the LZ11 stream is empty");
    if (data[0] != 0x11) throw Error("not LZ11, the first byte is " + std::to_string(data[0]));
    if (data.size() < 4) throw Error("the LZ11 stream ends early");
    std::size_t size = data[1] | std::size_t{data[2]} << 8 | std::size_t{data[3]} << 16;
    if (size != 0) return {size, 4};
    if (data.size() < 8) throw Error("the LZ11 stream ends early");
    size = data[4] | std::size_t{data[5]} << 8 | std::size_t{data[6]} << 16 | std::size_t{data[7]} << 24;
    return {size, 8};
}

// hash chains over three-byte prefixes: the earlier positions with the same
// prefix, newest first.
class Matches {
public:
    explicit Matches(std::size_t len) : head_(std::size_t{1} << hash_bits, none), prev_(len, none) {}

    void insert(std::span<const std::uint8_t> data, std::size_t at) {
        if (at + min_match > data.size()) return;
        auto h = hash(data, at);
        prev_[at] = head_[h];
        head_[h] = static_cast<std::uint32_t>(at);
    }

    // the longest match for the bytes at pos, and how far back it is.
    std::pair<std::size_t, std::size_t> longest(std::span<const std::uint8_t> data, std::size_t pos) const {
        if (pos + min_match > data.size()) return {0, 0};
        auto limit = std::min(data.size() - pos, max_match);
        std::pair<std::size_t, std::size_t> best{0, 0};
        auto candidate = head_[hash(data, pos)];
        for (int step = 0; step < chain && candidate != none && pos - candidate <= window; ++step) {
            std::size_t from = candidate;
            std::size_t length = 0;
            while (length < limit && data[from + length] == data[pos + length]) ++length;
            if (length > best.first) {
                best = {length, pos - from};
                if (length == limit) break;
            }
            candidate = prev_[from];
        }
        return best;
    }

private:
    static constexpr std::uint32_t none = 0xFFFFFFFF;
    static constexpr int hash_bits = 15;
    // how many earlier positions one search looks at, enough to find nearly
    // every best match in files of this size
    static constexpr int chain = 256;

    static std::size_t hash(std::span<const std::uint8_t> data, std::size_t at) {
        std::uint32_t v = std::uint32_t{data[at]} << 16 | std::uint32_t{data[at + 1]} << 8 | data[at + 2];
        return (v * 0x9E3779B1u) >> (32 - hash_bits);
    }

    std::vector<std::uint32_t> head_;
    std::vector<std::uint32_t> prev_;
};

// in bits: a flag bit and the token's bytes.
std::uint32_t cost(std::size_t length) {
    if (length == 1) return 9;
    if (length <= 0x10) return 17;
    if (length <= 0x110) return 25;
    return 33;
}

struct Token {
    std::size_t length;    // 1 for a literal
    std::size_t distance;  // for a copy
};

// the cheapest tokens for data, found backwards: the cheapest way from each
// position to the end is a literal or a copy of some length, then the
// cheapest way from there. every length a match allows is tried up to a few
// hundred, past which only the longest is, as copies cost the same from 273
// bytes on.
std::vector<Token> parse(std::span<const std::uint8_t> data) {
    auto n = data.size();
    Matches finder(n);
    std::vector<std::pair<std::size_t, std::size_t>> longest(n);
    for (std::size_t pos = 0; pos < n; ++pos) {
        // inside a long copy the next position has the same copy one byte
        // shorter, and copies cost the same from 273 bytes on, so a long run
        // of zeros is not searched again at every byte
        if (pos > 0 && longest[pos - 1].first > 0x130 + 1) {
            longest[pos] = {longest[pos - 1].first - 1, longest[pos - 1].second};
        } else {
            longest[pos] = finder.longest(data, pos);
        }
        finder.insert(data, pos);
    }
    struct Best {
        std::uint32_t cost;
        Token token;
    };
    std::vector<Best> best(n + 1, Best{0, {1, 0}});
    for (std::size_t pos = n; pos-- > 0;) {
        Best choice{cost(1) + best[pos + 1].cost, {1, 0}};
        auto [length, distance] = longest[pos];
        if (length >= min_match) {
            auto consider = [&](std::size_t l) {
                auto total = cost(l) + best[pos + l].cost;
                if (total < choice.cost) choice = {total, {l, distance}};
            };
            for (std::size_t l = min_match; l <= std::min<std::size_t>(length, 0x130); ++l) consider(l);
            consider(length);
        }
        best[pos] = choice;
    }
    std::vector<Token> tokens;
    for (std::size_t pos = 0; pos < n; pos += best[pos].token.length) tokens.push_back(best[pos].token);
    return tokens;
}

}  // namespace

std::size_t decompressed_size(std::span<const std::uint8_t> data) {
    return header(data).size;
}

std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> data) {
    auto [size, pos] = header(data);
    std::vector<std::uint8_t> out;
    out.reserve(size);
    auto byte = [&]() -> std::size_t {
        if (pos >= data.size()) throw Error("the LZ11 stream ends early");
        return data[pos++];
    };
    while (out.size() < size) {
        auto flags = byte();
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (!(flags & (1u << bit))) {
                out.push_back(static_cast<std::uint8_t>(byte()));
                continue;
            }
            auto b0 = byte();
            std::size_t length, distance;
            switch (b0 >> 4) {
                case 0: {
                    auto b1 = byte(), b2 = byte();
                    length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11;
                    distance = ((b1 & 0xF) << 8 | b2) + 1;
                    break;
                }
                case 1: {
                    auto b1 = byte(), b2 = byte(), b3 = byte();
                    length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111;
                    distance = ((b2 & 0xF) << 8 | b3) + 1;
                    break;
                }
                default: {
                    auto b1 = byte();
                    length = (b0 >> 4) + 1;
                    distance = ((b0 & 0xF) << 8 | b1) + 1;
                }
            }
            if (distance > out.size())
                throw Error("a copy at output byte " + std::to_string(out.size()) + " reaches " +
                            std::to_string(distance) + " bytes back");
            // a copy may overlap what it writes, byte by byte as the console does
            auto start = out.size() - distance;
            auto count = std::min(length, size - out.size());
            for (std::size_t i = 0; i < count; ++i) out.push_back(out[start + i]);
        }
    }
    return out;
}

std::vector<std::uint8_t> compress(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out;
    out.reserve(data.size() / 2 + 16);
    out.push_back(0x11);
    auto put = [&](std::size_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    // a zero 24-bit size says a 32-bit one follows, so nothing needs that too
    if (!data.empty() && data.size() <= 0xFFFFFF) {
        put(data.size(), 3);
    } else {
        put(0, 3);
        put(data.size(), 4);
    }
    auto tokens = parse(data);
    std::size_t pos = 0;
    for (std::size_t t = 0; t < tokens.size();) {
        auto flag_at = out.size();
        out.push_back(0);
        for (int bit = 7; bit >= 0 && t < tokens.size(); --bit, ++t) {
            auto [length, distance] = tokens[t];
            if (length == 1) {
                out.push_back(data[pos++]);
                continue;
            }
            out[flag_at] |= static_cast<std::uint8_t>(1u << bit);
            auto d = distance - 1;
            if (length <= 0x10) {
                out.push_back(static_cast<std::uint8_t>((length - 1) << 4 | d >> 8));
                out.push_back(static_cast<std::uint8_t>(d));
            } else if (length <= 0x110) {
                auto l = length - 0x11;
                out.push_back(static_cast<std::uint8_t>(l >> 4));
                out.push_back(static_cast<std::uint8_t>((l & 0xF) << 4 | d >> 8));
                out.push_back(static_cast<std::uint8_t>(d));
            } else {
                auto l = length - 0x111;
                out.push_back(static_cast<std::uint8_t>(0x10 | l >> 12));
                out.push_back(static_cast<std::uint8_t>(l >> 4));
                out.push_back(static_cast<std::uint8_t>((l & 0xF) << 4 | d >> 8));
                out.push_back(static_cast<std::uint8_t>(d));
            }
            pos += length;
        }
    }
    while (out.size() % 4 != 0) out.push_back(0);
    return out;
}

std::optional<std::vector<std::uint8_t>> pad_to(std::vector<std::uint8_t> stream, std::size_t size) {
    if (stream.size() > size) return std::nullopt;
    stream.resize(size, 0);
    return stream;
}

}  // namespace kabegami::lz11
