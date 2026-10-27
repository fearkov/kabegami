// the output merger: blending, or a logic op, and the tests a fragment goes
// through before it (alpha, stencil, depth). a port of Zakuro's zakuro-gpu
// blend.rs and the fragment operations of raster.rs.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "core/pica/fragment/tev.hpp"

namespace kabegami::pica {

class Blend {
public:
    // the blend configuration, none when the output merger is in logic-op mode
    static std::optional<Blend> read(std::span<const std::uint32_t> registers);
    // a fragment over the pixel under it
    Color apply(const Color& source, const Color& dest) const;

    std::uint32_t color_equation = 0, alpha_equation = 0;
    std::uint32_t color_source = 1, color_dest = 0, alpha_source = 1, alpha_dest = 0;
    Color constant{};
};

enum class LogicOp : std::uint8_t {
    clear, and_, and_reverse, copy, set, copy_inverted, noop, invert, nand, or_, nor, xor_, equivalent, and_inverted, or_reverse, or_inverted,
};
// the logic op in use, none when the merger blends or plainly copies
std::optional<LogicOp> read_logic_op(std::span<const std::uint32_t> registers);
std::uint8_t apply(LogicOp op, std::uint8_t source, std::uint8_t dest);

enum class Compare : std::uint8_t { never, always, equal, not_equal, less, less_equal, greater, greater_equal };
Compare compare_from_raw(std::uint32_t value);
template <typename T>
bool passes(Compare c, T value, T reference) {
    switch (c) {
        case Compare::never: return false;
        case Compare::always: return true;
        case Compare::equal: return value == reference;
        case Compare::not_equal: return value != reference;
        case Compare::less: return value < reference;
        case Compare::less_equal: return value <= reference;
        case Compare::greater: return value > reference;
        case Compare::greater_equal: return value >= reference;
    }
    return true;
}

struct AlphaTest {
    Compare function;
    std::uint8_t reference;
    static std::optional<AlphaTest> read(std::span<const std::uint32_t> registers);
    bool passes(std::uint8_t alpha) const { return pica::passes(function, alpha, reference); }
};

enum class StencilOp : std::uint8_t { keep, zero, replace, increment_saturate, decrement_saturate, invert, increment_wrap, decrement_wrap };

// GPUREG_STENCIL_TEST and _OP
struct Stencil {
    Compare function;
    std::uint8_t reference, compare_mask, write_mask;
    // when the stencil test fails, when it passes and the depth test fails,
    // and when both pass
    StencilOp fail, depth_fail, pass;

    static std::optional<Stencil> read(std::span<const std::uint32_t> registers);
    // the reference is on the left of the comparison
    bool passes(std::uint8_t stored) const {
        return pica::passes(function, static_cast<std::uint8_t>(reference & compare_mask), static_cast<std::uint8_t>(stored & compare_mask));
    }
    std::uint8_t update(std::uint8_t stored, StencilOp op) const;
};

}  // namespace kabegami::pica
