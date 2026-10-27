#include "core/pica/fragment/tev.hpp"

#include <algorithm>

#include "core/pica/registers.hpp"

namespace kabegami::pica {
namespace {

using Source = TexEnv::Source;
using Operation = TexEnv::Operation;

Source source_from_raw(std::uint32_t value) {
    switch (value & 0xF) {
        case 0x0: return Source::primary;
        case 0x1: return Source::fragment_primary;
        case 0x2: return Source::fragment_secondary;
        case 0x3: return Source::texture0;
        case 0x4: return Source::texture1;
        case 0x5: return Source::texture2;
        case 0x6: return Source::texture3;
        case 0xD: return Source::buffer;
        case 0xE: return Source::constant;
        default: return Source::previous;
    }
}

Operation operation_from_raw(std::uint32_t value) {
    auto v = value & 0xF;
    return v <= 8 ? static_cast<Operation>(v) : Operation::add_multiply;
}

// how many of a stage's three inputs the operation reads
int inputs(Operation op) {
    switch (op) {
        case Operation::replace: return 1;
        case Operation::lerp:
        case Operation::multiply_add:
        case Operation::add_multiply: return 3;
        default: return 2;
    }
}

Color unpack(std::uint32_t value) {
    return {static_cast<float>(value & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
            static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 24) & 0xFF) / 255.0f};
}

// the raw field is a shift
float scale_factor(std::uint32_t raw) {
    switch (raw & 3) {
        case 1: return 2.0f;
        case 2: return 4.0f;
        default: return 1.0f;
    }
}

std::array<float, 3> color_operand(const Color& s, std::uint32_t operand) {
    switch (operand) {
        case 0x1: return {1 - s[0], 1 - s[1], 1 - s[2]};
        case 0x2: return {s[3], s[3], s[3]};
        case 0x3: return {1 - s[3], 1 - s[3], 1 - s[3]};
        case 0x4: return {s[0], s[0], s[0]};
        case 0x5: return {1 - s[0], 1 - s[0], 1 - s[0]};
        case 0x8: return {s[1], s[1], s[1]};
        case 0x9: return {1 - s[1], 1 - s[1], 1 - s[1]};
        case 0xC: return {s[2], s[2], s[2]};
        case 0xD: return {1 - s[2], 1 - s[2], 1 - s[2]};
        default: return {s[0], s[1], s[2]};
    }
}

float alpha_operand(const Color& s, std::uint32_t operand) {
    switch (operand) {
        case 0x0: return s[3];
        case 0x1: return 1 - s[3];
        case 0x2: return s[0];
        case 0x3: return 1 - s[0];
        case 0x4: return s[1];
        case 0x5: return 1 - s[1];
        case 0x6: return s[2];
        default: return 1 - s[2];
    }
}

std::array<float, 3> combine_rgb(Operation op, const std::array<std::array<float, 3>, 3>& v) {
    std::array<float, 3> r{};
    for (std::size_t i = 0; i < 3; ++i) {
        switch (op) {
            case Operation::replace: r[i] = v[0][i]; break;
            case Operation::modulate: r[i] = v[0][i] * v[1][i]; break;
            case Operation::add: r[i] = std::min(v[0][i] + v[1][i], 1.0f); break;
            case Operation::add_signed: r[i] = std::clamp(v[0][i] + v[1][i] - 0.5f, 0.0f, 1.0f); break;
            case Operation::lerp: r[i] = v[0][i] * v[2][i] + v[1][i] * (1 - v[2][i]); break;
            case Operation::subtract: r[i] = std::max(v[0][i] - v[1][i], 0.0f); break;
            case Operation::multiply_add: r[i] = std::min(v[0][i] * v[1][i] + v[2][i], 1.0f); break;
            case Operation::add_multiply: r[i] = std::min(v[0][i] + v[1][i], 1.0f) * v[2][i]; break;
            case Operation::dot3_rgb:
            case Operation::dot3_rgba: {
                // both inputs are signed values packed into 0 to 1, summed
                // as Rust sums floats, from -0
                float dot = -0.0f;
                for (std::size_t k = 0; k < 3; ++k) dot += (v[0][k] * 2 - 1) * (v[1][k] * 2 - 1);
                r.fill(std::clamp(dot, 0.0f, 1.0f));
                return r;
            }
        }
    }
    return r;
}

float combine_alpha(Operation op, const std::array<float, 3>& v) {
    switch (op) {
        case Operation::replace: return v[0];
        case Operation::modulate: return v[0] * v[1];
        case Operation::add: return std::min(v[0] + v[1], 1.0f);
        case Operation::add_signed: return std::clamp(v[0] + v[1] - 0.5f, 0.0f, 1.0f);
        case Operation::lerp: return v[0] * v[2] + v[1] * (1 - v[2]);
        case Operation::subtract: return std::max(v[0] - v[1], 0.0f);
        case Operation::dot3_rgb:
        case Operation::dot3_rgba: return v[0];
        case Operation::multiply_add: return std::min(v[0] * v[1] + v[2], 1.0f);
        case Operation::add_multiply: return std::min(v[0] + v[1], 1.0f) * v[2];
    }
    return v[0];
}

}  // namespace

TexEnv TexEnv::read(std::span<const std::uint32_t> registers) {
    TexEnv env;
    for (std::size_t i = 0; i < 6; ++i) {
        auto base = reg::texenv_stages[i];
        auto source = registers[base], operand = registers[base + 1], combiner = registers[base + 2];
        auto constant = registers[base + 3], scale = registers[base + 4];
        auto& s = env.stages[i];
        s.color_sources = {source_from_raw(source), source_from_raw(source >> 4), source_from_raw(source >> 8)};
        s.alpha_sources = {source_from_raw(source >> 16), source_from_raw(source >> 20), source_from_raw(source >> 24)};
        s.color_operands = {operand & 0xF, (operand >> 4) & 0xF, (operand >> 8) & 0xF};
        s.alpha_operands = {(operand >> 12) & 0x7, (operand >> 16) & 0x7, (operand >> 20) & 0x7};
        s.color_op = operation_from_raw(combiner);
        s.alpha_op = operation_from_raw(combiner >> 16);
        s.constant = unpack(constant);
        s.color_scale = scale_factor(scale);
        s.alpha_scale = scale_factor(scale >> 16);
        // replace from the previous stage at a scale of one changes nothing
        s.passthrough = s.color_op == Operation::replace && s.alpha_op == Operation::replace &&
                        s.color_sources[0] == Source::previous && s.alpha_sources[0] == Source::previous &&
                        s.color_operands[0] == 0 && s.alpha_operands[0] == 0 && s.color_scale == 1 && s.alpha_scale == 1;
    }
    // bits 8-11 say which of stages 0-3 write their colour into the buffer,
    // bits 12-15 their alpha
    auto update = registers[reg::texenv_update_buffer];
    for (std::size_t i = 0; i < 4; ++i) {
        env.update_color[i] = update & (0x100u << i);
        env.update_alpha[i] = update & (0x1000u << i);
    }
    env.buffer_color = unpack(registers[reg::texenv_buffer_color]);
    return env;
}

bool TexEnv::reads_lighting() const {
    auto lit = [](Source s) { return s == Source::fragment_primary || s == Source::fragment_secondary; };
    for (auto& s : stages) {
        if (s.passthrough) continue;
        for (int i = 0; i < inputs(s.color_op); ++i)
            if (lit(s.color_sources[static_cast<std::size_t>(i)])) return true;
        if (s.color_op != Operation::dot3_rgba)
            for (int i = 0; i < inputs(s.alpha_op); ++i)
                if (lit(s.alpha_sources[static_cast<std::size_t>(i)])) return true;
    }
    return false;
}

Color TexEnv::apply(const Color& primary, const std::array<Color, 4>& textures) const {
    Color previous = primary;
    // the buffer lags a stage behind: the first stage reads zero, the second
    // the buffer colour, and a stage's update reaches the stage after next
    Color buffer{};
    Color next_buffer = buffer_color;
    for (std::size_t index = 0; index < 6; ++index) {
        auto& stage = stages[index];
        if (!stage.passthrough) {
            auto pick = [&](Source source) -> Color {
                switch (source) {
                    case Source::primary:
                    case Source::fragment_primary: return primary;
                    // without fragment lighting there is no specular term
                    case Source::fragment_secondary: return {0, 0, 0, 1};
                    case Source::texture0: return textures[0];
                    case Source::texture1: return textures[1];
                    case Source::texture2: return textures[2];
                    case Source::texture3: return textures[3];
                    case Source::buffer: return buffer;
                    case Source::constant: return stage.constant;
                    case Source::previous: return previous;
                }
                return previous;
            };
            std::array<std::array<float, 3>, 3> rgb;
            std::array<float, 3> alpha;
            for (std::size_t i = 0; i < 3; ++i) {
                rgb[i] = color_operand(pick(stage.color_sources[i]), stage.color_operands[i]);
                alpha[i] = alpha_operand(pick(stage.alpha_sources[i]), stage.alpha_operands[i]);
            }
            auto combined = combine_rgb(stage.color_op, rgb);
            // dot3 RGBA writes the product to alpha too, past the alpha combiner
            float a = stage.color_op == Operation::dot3_rgba ? combined[0] : combine_alpha(stage.alpha_op, alpha);
            previous = {std::clamp(combined[0] * stage.color_scale, 0.0f, 1.0f), std::clamp(combined[1] * stage.color_scale, 0.0f, 1.0f),
                        std::clamp(combined[2] * stage.color_scale, 0.0f, 1.0f), std::clamp(a * stage.alpha_scale, 0.0f, 1.0f)};
        }
        buffer = next_buffer;
        if (index < 4) {
            if (update_color[index]) std::copy_n(previous.begin(), 3, next_buffer.begin());
            if (update_alpha[index]) next_buffer[3] = previous[3];
        }
    }
    return previous;
}

}  // namespace kabegami::pica
