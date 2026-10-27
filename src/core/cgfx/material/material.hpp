// CGFX materials (MTOB): the colours, the texture coordinators and mappers,
// the TEV stages and the PICA command blocks they carry, and what HOME does
// with them when it draws. docs/research/cgfx-material-texture.md is the
// layout this follows, camera-shader-nw4c.md §5.3 for the coordinators.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/cgfx/cgfx.hpp"
#include "core/cgfx/math.hpp"

namespace kabegami::cgfx {

// one register write a command block makes.
struct RegisterWrite {
    std::uint16_t reg;
    std::uint8_t mask;  // bit i enables byte i
    std::uint32_t value;
};

// the writes of a raw PICA command block: a parameter, a header (register,
// byte mask, extra parameter count, consecutive flag), the extra parameters,
// and a padding word after a command of odd length.
std::vector<RegisterWrite> decode_commands(std::span<const std::uint32_t> words);

// the material colours, in the order the file keeps them.
enum class Color : int {
    emission = 0,
    ambient,
    diffuse,
    specular0,
    specular1,
    constant0,
    constant1,
    constant2,
    constant3,
    constant4,
    constant5,
};
constexpr int color_count = 11;

// the byte form the file keeps next to each float colour, as Nintendo's
// exporter wrote it: clamped, times 255, plus a half, truncated, in double.
std::uint32_t pack_color(Vec4 color);
// the same as HOME's own packer does it, in float, which rounds a value that
// lands on x.5 up where the exporter rounded it down. HOME repacks Constant1,
// 2, 3 and 5 of the wallpaper this way whenever it applies their colours.
std::uint32_t pack_color_home(Vec4 color);

struct TextureCoordinator {
    std::size_t at = 0;
    std::int32_t source = 0;          // the vertex UV set
    std::uint32_t mapping = 0;        // 0 UV, 1 camera cube env, 2 camera sphere env, 3 projection, 4 shadow, 5 shadow box
    std::int32_t reference_camera = -1;
    std::uint32_t mode = 0;           // the matrix mode: 0 Maya, 1 SoftImage, 2 3ds Max
    Vec2 scale{1, 1};
    float rotate = 0;
    Vec2 translate;
    Matrix34 matrix;                  // what the file stores, kept in step with the above
};

// the coordinator's matrix as HOME computes it from scale, rotate and
// translate in each matrix mode.
Matrix34 coordinator_matrix(std::uint32_t mode, Vec2 scale, float rotate, Vec2 translate);

struct TextureMapper {
    std::size_t at = 0;
    std::string texture;  // the name of a texture in the same file
    std::uint32_t min_filter = 0;
    Vec4 border_color;
    float lod_bias = 0;
    std::array<std::uint32_t, 14> commands{};  // a template HOME fills in when it binds a texture
    std::int32_t commands_length = 0;          // in bytes
};

// what a mapper's commands become when HOME binds an image texture to it: the
// type, size, mip count and address filled in, and the run cut to the five
// registers from BORDER_COLOR to ADDR1, which only then fits units 1 and 2.
std::vector<std::uint32_t> bind_texture(const TextureMapper& mapper, std::uint32_t format, int width, int height, int levels,
                                        std::uint32_t physical_address);

struct TevStage {
    std::uint32_t constant = 0;  // 0-5 Constant0-5, 6-10 Emission, Ambient, Diffuse, Specular0, Specular1
    std::array<std::uint32_t, 6> commands{};
};

struct FragmentShader {
    std::size_t at = 0;
    Vec4 buffer_color;
    std::uint32_t lighting_flags = 0;
    std::uint32_t lighting_config = 0;
    std::uint32_t fresnel = 0;
    std::int32_t bump_texture = 0;
    std::uint32_t bump_mode = 0;
    bool bump_renormalize = false;
    std::array<std::size_t, 6> luts{};  // links to the fragment lighting LUT references, 0 for none
    std::array<TevStage, 6> stages;
    std::array<std::uint32_t, 2> alpha_test{};
    std::array<std::uint32_t, 6> commands{};  // buffer colour, buffer update, lighting
};

struct Material {
    std::size_t at = 0;
    std::string name;
    std::uint32_t flags = 0;  // bit 0 fragment lighting, 1 vertex lighting, 2 hemisphere, 3 hemisphere occlusion, 4 fog
    std::uint32_t texcoord_config = 0;
    std::uint32_t translucency = 0;
    std::array<Vec4, color_count> colors;
    std::array<std::uint32_t, color_count> packed_colors{};
    bool polygon_offset = false;
    std::uint32_t face_culling = 0;
    float polygon_offset_unit = 0;
    std::array<std::uint32_t, 2> culling_commands{};
    std::uint32_t depth_flags = 0;  // bit 0 test, bit 1 write
    std::array<std::uint32_t, 4> depth_commands{};
    std::uint32_t blend_mode = 0;  // 0 none, 1 blend, 2 separate, 3 logic op
    Vec4 blend_color;
    std::array<std::uint32_t, 6> blend_commands{};
    std::array<std::uint32_t, 4> stencil_commands{};
    std::int32_t used_texcoords = 0;
    std::array<TextureCoordinator, 3> coordinators;
    std::array<std::optional<TextureMapper>, 3> mappers;
    std::string shader;  // "DefaultShader"
    std::int32_t program_index = 0;
    FragmentShader fragment;
    std::array<std::uint32_t, 14> hashes{};

    // the colour stage i's TEXENVi_COLOR takes: the packed material colour
    // its Constant names. the file's own COLOR words are placeholders.
    std::uint32_t stage_color(int stage) const;
};

Material read_material(const File& file, std::size_t at);

// SPICA's HashBuffer, which every hash word of a material is: the MD5 of the
// bytes, byte i XORed into bits (i mod 4)·8, and 0 made 1.
std::uint32_t hash_buffer(std::span<const std::uint8_t> bytes);
// MaterialColorsHash (+0x2B4): the colours as floats, Emission, Specular0 and
// Specular1 without their alpha.
std::uint32_t colors_hash(const std::array<Vec4, color_count>& colors);

// edits in place, keeping what depends on them in step: both forms of a
// colour and the colours' hash word, a coordinator's stored matrix.
void set_color(File& file, const Material& material, Color which, Vec4 value);
void set_coordinator(File& file, const Material& material, int index, Vec2 scale, float rotate, Vec2 translate);

}  // namespace kabegami::cgfx
