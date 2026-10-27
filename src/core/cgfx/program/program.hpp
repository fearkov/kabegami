// the camera (CCAM) and the shader program (SHDR and the DVLB inside it), and
// the matrices and register values NW4C makes of them inside the HOME Menu.
// docs/research/camera-shader-nw4c.md is what this follows.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/cgfx/cgfx.hpp"
#include "core/cgfx/math.hpp"

namespace kabegami::cgfx {

struct Camera {
    std::size_t at = 0;
    std::string name;
    Vec3 scale, rotation, translation;  // the translation is the eye
    std::uint32_t view_type = 0;        // 0 aim, 1 look-at, 2 rotate
    std::uint32_t projection_type = 0;  // 0 perspective, 1 frustum, 2 orthographic
    std::uint32_t view_flags = 0;
    Vec3 target;         // aim and look-at
    float twist = 0;     // aim
    Vec3 up;             // look-at
    Vec3 view_rotation;  // rotate
    float near = 0, far = 0, aspect = 0;
    float fovy = 0;    // perspective, the full vertical angle in radians
    float height = 0;  // frustum and orthographic
    Vec2 centre;
    float wscale = 0;
};

Camera read_camera(const File& file, std::string_view name = {});

// the view matrix HOME's view updaters build, with the eye at the camera's
// translation.
Matrix34 view_matrix(const Camera& camera);

// the projection HOME's projection updaters build: NW4C's depth, 0 at near
// and -1 at far, and the pivot that turns the picture onto the 3DS's
// sideways screens, 1 for BannerCamera.
Matrix44 projection_matrix(const Camera& camera, int pivot);

// what HOME writes to DEPTHMAP_ENABLE, _SCALE and _OFFSET for the camera,
// with polygon offset off: a z-buffer with scale -1 when WScale is 0.
struct DepthMap {
    bool enable;
    float scale, offset;
};
DepthMap depth_map(const Camera& camera);

// a constant a DVLE gives a register.
struct ShaderConstant {
    enum class Type : std::uint8_t { boolean = 0, integer = 1, floating = 2 } type;
    std::uint8_t reg;                  // within its kind: b0-b15, i0-i3, c0-c95
    std::array<std::uint32_t, 4> raw;  // float24 in 32-bit words for floats
};

struct ShaderOutput {
    std::uint16_t type;  // 0 position, 1 normal quaternion, 2 colour, 3 texcoord0, 4 texcoord0 w, 5 texcoord1, 6 texcoord2, 8 view
    std::uint16_t reg;
    std::uint16_t mask;
};

struct ShaderUniform {
    std::string name;
    std::uint16_t first, last;  // 0x00-0x0F v, 0x10-0x6F c, 0x70-0x73 i, 0x78-0x87 b
};

// the output map registers made of a DVLE's outputs, by libctru's rule.
struct OutputMap {
    std::uint32_t total = 0;                 // GPUREG_SH_OUTMAP_TOTAL
    std::array<std::uint32_t, 7> regs{};     // GPUREG_SH_OUTMAP_O0-O6
    std::uint32_t mode = 0;                  // GPUREG_SH_OUTATTR_MODE
    std::uint32_t clock = 0;                 // GPUREG_SH_OUTATTR_CLOCK
    std::uint32_t mask = 0;                  // the output registers written
};

struct ShaderEntry {
    std::uint8_t type = 0;  // 0 vertex, 1 geometry
    std::uint32_t main = 0, endmain = 0;
    std::uint16_t input_mask = 0, output_mask = 0;
    std::vector<ShaderConstant> constants;
    std::vector<ShaderOutput> outputs;
    std::vector<ShaderUniform> uniforms;
    OutputMap outmap;

    const ShaderUniform* uniform(std::string_view name) const;
};

struct ShaderBinary {
    std::vector<std::uint32_t> code;
    std::vector<std::uint32_t> descriptors;  // operand descriptors
    std::vector<ShaderEntry> entries;
};

// a DVLB: a SHDR's, or a standalone .shbin file.
ShaderBinary parse_shbin(ByteSpan dvlb, const std::string& what);

struct ProgramDescription {
    std::uint32_t flags = 0;
    std::int32_t vertex = 0, geometry = -1;  // DVLE indices
    std::array<std::int8_t, 22> inputs{};    // the input register of each attribute usage, -1 unbound
};

struct ShaderProgram {
    std::string name;
    ShaderBinary binary;
    std::vector<ProgramDescription> descriptions;
};

ShaderProgram read_shader(const File& file, std::string_view name = {});

OutputMap generate_outmap(const std::vector<ShaderOutput>& outputs);

}  // namespace kabegami::cgfx
