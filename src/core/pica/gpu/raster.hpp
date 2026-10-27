// a draw: the vertices fetched from the arrays the loaders describe (or sent
// one at a time), through the vertex shader, put together into triangles,
// clipped, culled and filled, each fragment textured, combined, tested and
// blended into the colour buffer. a port of the software path of Zakuro's
// zakuro-gpu raster.rs, without the parts nothing in the HOME Menu's banner
// draws use: geometry shaders, fragment lighting, fog and procedural
// textures. textures sample level 0 with the magnification filter, as there.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/pica/fragment/tev.hpp"
#include "core/pica/gpu/memory.hpp"
#include "core/pica/registers.hpp"
#include "core/pica/shader/shader.hpp"

namespace kabegami::pica {

using Registers = std::array<std::uint32_t, reg::count>;

// a vertex after shading: its clip-space position and what the rasterizer
// interpolates
struct Vertex {
    Vec4 clip{};
    Color color{};
    std::array<std::array<float, 2>, 3> texcoords{};

    Vertex lerp(const Vertex& other, float t) const;
};

// what happened to a draw's fragments
struct DrawStats {
    std::uint32_t vertices = 0, triangles = 0, clipped_away = 0, culled = 0;
    std::uint32_t written = 0, depth_failed = 0, stencil_failed = 0, alpha_failed = 0;
};

// a draw of the arrays the registers describe, the vertices in order or as
// the index buffer picks them
DrawStats draw(const Registers& registers, ShaderUnit& vertex_shader, const std::array<Vec4, 16>& fixed_attributes, Memory& memory,
               bool indexed);
// a draw of vertices sent through the fixed attribute registers, each its
// attributes in order
DrawStats draw_immediate(const Registers& registers, ShaderUnit& vertex_shader, const std::vector<std::array<Vec4, 16>>& vertices,
                         Memory& memory);
// shaded vertices, three a triangle (or a strip or fan, as the registers
// say), into the buffers the registers name
DrawStats rasterize(const Registers& registers, std::span<const Vertex> vertices, Memory& memory);

// the varyings of a vertex out of its shader outputs, as the output map and
// the output mask place them
std::vector<Vertex> place_outputs(const Registers& registers, std::span<const std::array<Vec4, output_registers>> outputs);

}  // namespace kabegami::pica
