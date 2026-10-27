#include "core/pica/fragment/blend.hpp"

#include <algorithm>
#include <cmath>

#include "core/pica/registers.hpp"

namespace kabegami::pica {
namespace {

// 0 add, 1 subtract, 2 reverse subtract, 3 min, 4 max; 5-7 add, as the
// hardware has them
float equation(std::uint32_t e, float source, float source_factor, float dest, float dest_factor) {
    float s = source * source_factor, d = dest * dest_factor, r;
    switch (e & 7) {
        case 1: r = s - d; break;
        case 2: r = d - s; break;
        // min and max ignore the factors, as in OpenGL; Rust's min and max
        // take the other value when one is NaN
        case 3: r = std::isnan(source) ? dest : std::isnan(dest) ? source : std::min(source, dest); break;
        case 4: r = std::isnan(source) ? dest : std::isnan(dest) ? source : std::max(source, dest); break;
        default: r = s + d;
    }
    return std::clamp(r, 0.0f, 1.0f);
}

float factor(std::uint32_t f, std::size_t channel, const Color& source, const Color& dest, const Color& constant) {
    switch (f & 0xF) {
        case 0: return 0;
        case 1: return 1;
        case 2: return source[channel];
        case 3: return 1 - source[channel];
        case 4: return dest[channel];
        case 5: return 1 - dest[channel];
        case 6: return source[3];
        case 7: return 1 - source[3];
        case 8: return dest[3];
        case 9: return 1 - dest[3];
        case 10: return constant[channel];
        case 11: return 1 - constant[channel];
        case 12: return constant[3];
        case 13: return 1 - constant[3];
        case 14: return channel == 3 ? 1.0f : std::min(source[3], 1 - dest[3]);
        // 15 is undocumented, taken as one
        default: return 1;
    }
}

StencilOp stencil_op(std::uint32_t value) { return static_cast<StencilOp>(value & 7); }

}  // namespace

std::optional<Blend> Blend::read(std::span<const std::uint32_t> registers) {
    // bit 8 of the colour operation picks blending over the logic op
    if (!(registers[reg::color_operation] & 0x100)) return std::nullopt;
    auto config = registers[reg::blend_func], constant = registers[reg::blend_color];
    Blend b;
    b.color_equation = config & 7;
    b.alpha_equation = (config >> 8) & 7;
    b.color_source = (config >> 16) & 0xF;
    b.color_dest = (config >> 20) & 0xF;
    b.alpha_source = (config >> 24) & 0xF;
    b.alpha_dest = (config >> 28) & 0xF;
    for (std::size_t i = 0; i < 4; ++i) b.constant[i] = static_cast<float>((constant >> (8 * i)) & 0xFF) / 255.0f;
    return b;
}

Color Blend::apply(const Color& source, const Color& dest) const {
    Color out{};
    for (std::size_t c = 0; c < 3; ++c)
        out[c] = equation(color_equation, source[c], factor(color_source, c, source, dest, constant), dest[c],
                          factor(color_dest, c, source, dest, constant));
    out[3] = equation(alpha_equation, source[3], factor(alpha_source, 3, source, dest, constant), dest[3],
                      factor(alpha_dest, 3, source, dest, constant));
    return out;
}

std::optional<LogicOp> read_logic_op(std::span<const std::uint32_t> registers) {
    if (registers[reg::color_operation] & 0x100) return std::nullopt;
    auto op = static_cast<LogicOp>(registers[reg::logic_op] & 0xF);
    if (op == LogicOp::copy) return std::nullopt;
    return op;
}

std::uint8_t apply(LogicOp op, std::uint8_t s, std::uint8_t d) {
    unsigned r;
    switch (op) {
        case LogicOp::clear: r = 0; break;
        case LogicOp::and_: r = s & d; break;
        case LogicOp::and_reverse: r = s & ~d; break;
        case LogicOp::copy: r = s; break;
        case LogicOp::set: r = 0xFF; break;
        case LogicOp::copy_inverted: r = ~s; break;
        case LogicOp::noop: r = d; break;
        case LogicOp::invert: r = ~d; break;
        case LogicOp::nand: r = ~(s & d); break;
        case LogicOp::or_: r = s | d; break;
        case LogicOp::nor: r = ~(s | d); break;
        case LogicOp::xor_: r = s ^ d; break;
        case LogicOp::equivalent: r = ~(s ^ d); break;
        case LogicOp::and_inverted: r = ~s & d; break;
        case LogicOp::or_reverse: r = s | ~d; break;
        default: r = ~s | d; break;
    }
    return static_cast<std::uint8_t>(r);
}

Compare compare_from_raw(std::uint32_t value) { return static_cast<Compare>(value & 7); }

std::optional<AlphaTest> AlphaTest::read(std::span<const std::uint32_t> registers) {
    auto config = registers[reg::alpha_test];
    if (!(config & 1)) return std::nullopt;
    return AlphaTest{compare_from_raw(config >> 4), static_cast<std::uint8_t>(config >> 8)};
}

std::optional<Stencil> Stencil::read(std::span<const std::uint32_t> registers) {
    auto test = registers[reg::stencil_test];
    if (!(test & 1)) return std::nullopt;
    auto op = registers[reg::stencil_op];
    return Stencil{compare_from_raw(test >> 4),  static_cast<std::uint8_t>(test >> 16), static_cast<std::uint8_t>(test >> 24),
                   static_cast<std::uint8_t>(test >> 8), stencil_op(op), stencil_op(op >> 4), stencil_op(op >> 8)};
}

std::uint8_t Stencil::update(std::uint8_t stored, StencilOp op) const {
    unsigned v;
    switch (op) {
        case StencilOp::keep: v = stored; break;
        case StencilOp::zero: v = 0; break;
        case StencilOp::replace: v = reference; break;
        case StencilOp::increment_saturate: v = stored == 0xFF ? 0xFF : stored + 1u; break;
        case StencilOp::decrement_saturate: v = stored == 0 ? 0 : stored - 1u; break;
        case StencilOp::invert: v = ~static_cast<unsigned>(stored); break;
        case StencilOp::increment_wrap: v = stored + 1u; break;
        default: v = stored - 1u; break;
    }
    return static_cast<std::uint8_t>((stored & ~write_mask) | (v & write_mask));
}

}  // namespace kabegami::pica
