// CGFX models and textures: what gets a model's geometry and pictures to the
// GPU. docs/research/cgfx-model.md is the layout this follows, and
// cgfx-material-texture.md §2.11 for textures.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/cgfx/cgfx.hpp"
#include "core/cgfx/math.hpp"

namespace kabegami::cgfx {

// what a vertex attribute feeds (SPICA's PICAAttributeName).
enum class Usage : std::uint32_t {
    position = 0,
    normal = 1,
    tangent = 2,
    color = 3,
    texcoord0 = 4,
    texcoord1 = 5,
    texcoord2 = 6,
    bone_index = 7,
    bone_weight = 8,
    user0 = 9,  // up to user11 = 20
    interleave = 21,
};

// the GL enums the files name component types with.
namespace gl {
constexpr std::uint32_t byte = 0x1400, ubyte = 0x1401, short_ = 0x1402, ushort = 0x1403, float_ = 0x1406;
}

// the PICA attribute type of a GL component type: 0 byte, 1 ubyte, 2 short,
// 3 float. throws for anything else.
std::uint32_t pica_attribute_type(std::uint32_t gl_format);
std::uint32_t component_size(std::uint32_t gl_format);

struct Attribute {
    Usage usage;
    std::uint32_t format;      // a gl:: value
    std::uint32_t components;  // 1 to 4
    float scale;               // what the shader multiplies the raw value by
    std::uint32_t offset;      // in the vertex
};

// a buffer of interleaved vertices in IMAG.
struct VertexBuffer {
    std::size_t at = 0;
    std::size_t data_at = 0;  // where its bytes are in the file
    std::uint32_t size = 0;
    std::uint32_t stride = 0;
    std::vector<Attribute> attributes;
};

// one value for the whole shape.
struct FixedAttribute {
    std::size_t at = 0;
    Usage usage;
    std::uint32_t format;
    std::uint32_t components;
    float scale;
    std::vector<float> values;
};

struct IndexStream {
    std::size_t at = 0;
    std::uint32_t format = 0;  // gl::ubyte or gl::ushort
    std::uint8_t mode = 0;     // 0 triangles
    bool visible = true;
    std::size_t data_at = 0;
    std::uint32_t size = 0;  // in bytes
    std::size_t count() const { return format == gl::ushort ? size / 2 : size; }
};

struct Primitive {
    std::vector<IndexStream> streams;
};

struct SubMesh {
    std::vector<std::int32_t> bone_indices;
    std::uint32_t skinning = 0;  // 0 none, 1 rigid, 2 smooth
    std::vector<Primitive> primitives;
};

// the oriented bounding box: centre, the rows of its orientation (whose
// columns are the box's axes), and its full size.
struct Box {
    Vec3 centre;
    std::array<float, 9> orientation{};
    Vec3 size;
};

struct Shape {
    std::size_t at = 0;
    std::uint32_t flags = 0;
    Box box;
    Vec3 position_offset;
    std::vector<SubMesh> sub_meshes;
    std::vector<VertexBuffer> buffers;
    std::vector<FixedAttribute> fixed;
};

struct Mesh {
    std::size_t at = 0;
    std::string name;
    std::int32_t shape = 0;
    std::int32_t material = 0;  // an entry of the model's material DICT
    bool visible = true;
    std::uint8_t render_priority = 0;
    std::int16_t mesh_node = -1;
    std::string node_name;
};

struct Bone {
    std::size_t at = 0;
    std::string name;
    std::uint32_t flags = 0;
    std::int32_t index = 0;
    std::int32_t parent = -1;
    Vec3 scale, rotation, translation;
    Matrix34 local, world, inverse_bind;
    std::uint32_t billboard = 0;
};

struct Skeleton {
    std::size_t at = 0;
    std::vector<Bone> bones;  // in DICT order, which is each bone's index
    std::uint32_t scaling_rule = 0;
    std::uint32_t flags = 0;
};

// a member animations may drive, in one of the model's animation groups.
struct GroupElement {
    std::size_t at = 0;
    std::uint32_t type = 0;
    std::string path;    // what animations bind by, "Materials[\"mt_BG\"].TextureCoordinators[1].Translate"
    std::string object;  // a bone's or a material's name
    std::uint32_t member_offset = 0;
    std::uint32_t member_size = 0;
    std::uint32_t object_type = 0;  // 0 bone, 1 material colour, 2 sampler, 3 mapper, 4 blend, 5 coordinator, 6 model, 7 mesh
};

struct AnimationGroup {
    std::size_t at = 0;
    std::string name;  // "SkeletalAnimation", "MaterialAnimation", ...
    std::uint32_t member_type = 0;
    std::vector<GroupElement> elements;
};

struct Model {
    std::size_t at = 0;
    std::uint32_t type = 0;
    std::string name;
    std::vector<AnimationGroup> groups;
    Vec3 scale, rotation, translation;
    Matrix34 local, world;
    std::vector<Mesh> meshes;
    Dict materials;
    std::vector<Shape> shapes;
    std::uint32_t flags = 0;
    std::optional<Skeleton> skeleton;

    const AnimationGroup* group(std::string_view name) const;
};

Model read_model(const File& file, std::size_t at);
// the first model of the file, or the one named.
Model read_model(const File& file, std::string_view name = {});

// what a vertex buffer and a fixed attribute hold, for one vertex: the raw
// component values, before the attribute's scale.
std::array<float, 4> attribute_value(const File& file, const VertexBuffer& buffer, const Attribute& attribute, std::size_t vertex);
std::uint32_t index_at(const File& file, const IndexStream& stream, std::size_t i);

// the world matrix of every bone from their local matrices, down the parent
// chain: world(b) = world(parent) · local(b).
std::vector<Matrix34> world_matrices(const Skeleton& skeleton, const std::vector<Matrix34>& locals);

// TXOB
struct Texture {
    std::size_t at = 0;
    std::uint32_t type = 0;  // 0x20000011 image, 0x20000009 cube
    std::string name;
    int width = 0, height = 0;
    std::uint32_t gl_format = 0, gl_type = 0;
    int levels = 1;
    std::uint32_t format = 0;  // the PICA texture format (HwFormat)
    std::size_t image_at = 0;  // the image data object
    std::size_t data_at = 0;   // the texels, in IMAG
    std::uint32_t data_size = 0;
    int bits_per_pixel = 0;
};

Texture read_texture(const File& file, std::size_t at);
// a texture of the file's DICT by name.
std::optional<Texture> find_texture(const File& file, std::string_view name);

// bits per texel of a PICA texture format, 0 for one that does not exist.
int texture_format_bits(std::uint32_t format);

}  // namespace kabegami::cgfx
