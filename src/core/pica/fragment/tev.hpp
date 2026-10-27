// the texture environment: six combiner stages and the buffer that carries a
// result on to later ones. a port of Zakuro's zakuro-gpu tev.rs, which works
// in floats from 0 to 1. fragment lighting isn't modelled: its two colours
// read as the primary colour and (0, 0, 0, 1), as with lighting off.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace kabegami::pica {

using Color = std::array<float, 4>;

class TexEnv {
public:
    // the environment the registers set up
    static TexEnv read(std::span<const std::uint32_t> registers);

    // every stage for one fragment: the interpolated primary colour and the
    // samples of textures 0-3
    Color apply(const Color& primary, const std::array<Color, 4>& textures) const;

    // whether a stage reads the colours fragment lighting gives
    bool reads_lighting() const;

    enum class Source : std::uint8_t { primary, fragment_primary, fragment_secondary, texture0, texture1, texture2, texture3, buffer, constant, previous };
    enum class Operation : std::uint8_t { replace, modulate, add, add_signed, lerp, subtract, dot3_rgb, dot3_rgba, multiply_add, add_multiply };
    struct Stage {
        std::array<Source, 3> color_sources, alpha_sources;
        std::array<std::uint32_t, 3> color_operands, alpha_operands;
        Operation color_op, alpha_op;
        Color constant;
        float color_scale, alpha_scale;
        bool passthrough;
    };
    std::array<Stage, 6> stages{};
    std::array<bool, 4> update_color{}, update_alpha{};
    Color buffer_color{};
};

}  // namespace kabegami::pica
