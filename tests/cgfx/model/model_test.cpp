// the model reader against the HOME Menu's own files, the values of
// docs/research/cgfx-model.md. KABEGAMI_ROMFS=<an extracted RomFS> runs it.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/model/model.hpp"

using namespace kabegami;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> romfs() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !fs::is_directory(dir)) return std::nullopt;
    return fs::path(dir);
}

cgfx::File load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    Bytes raw{std::istreambuf_iterator<char>(in), {}};
    return cgfx::File::load(raw, path.filename().string());
}

}  // namespace

TEST_CASE("BannerBG's model is what the spec decoded") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto model = cgfx::read_model(file);
    CHECK(model.name == "BannerBG");
    CHECK(model.type == 0x40000092);
    REQUIRE(model.groups.size() == 3);
    REQUIRE(model.group("MaterialAnimation"));
    CHECK(model.group("MaterialAnimation")->elements.size() == 27);
    CHECK(model.group("SkeletalAnimation")->elements.size() == 2);
    REQUIRE(model.meshes.size() == 1);
    CHECK(model.meshes[0].shape == 0);
    CHECK(model.materials.name(static_cast<std::size_t>(model.meshes[0].material)) == "mt_BG");
    CHECK(model.meshes[0].node_name == "BG");
    REQUIRE(model.shapes.size() == 1);
    auto& shape = model.shapes[0];
    CHECK(shape.box.size.x == doctest::Approx(56.458));
    CHECK(shape.box.centre.z == doctest::Approx(-12.7051));
    REQUIRE(shape.sub_meshes.size() == 1);
    CHECK(shape.sub_meshes[0].bone_indices == std::vector<std::int32_t>{1});
    CHECK(shape.sub_meshes[0].skinning == 0);
    auto& stream = shape.sub_meshes[0].primitives.at(0).streams.at(0);
    CHECK(stream.format == cgfx::gl::ubyte);
    CHECK(stream.count() == 444);
    CHECK(stream.data_at == 0x3280);
    REQUIRE(shape.buffers.size() == 1);
    auto& buffer = shape.buffers[0];
    CHECK(buffer.stride == 24);
    CHECK(buffer.data_at == 0x343C);
    CHECK(buffer.size / buffer.stride == 102);
    REQUIRE(buffer.attributes.size() == 3);
    CHECK(buffer.attributes[1].usage == cgfx::Usage::color);
    CHECK(buffer.attributes[1].scale == doctest::Approx(1.0 / 255));
    // every index names a vertex, and all 102 are used
    std::vector<bool> used(102);
    for (std::size_t i = 0; i < stream.count(); ++i) used.at(cgfx::index_at(file, stream, i)) = true;
    CHECK(std::all_of(used.begin(), used.end(), [](bool u) { return u; }));
    // the wall's x spans ±28.229
    float lowest = 0;
    for (std::size_t v = 0; v < 102; ++v) lowest = std::min(lowest, cgfx::attribute_value(file, buffer, buffer.attributes[0], v)[0]);
    CHECK(lowest == doctest::Approx(-28.229).epsilon(1e-4));
    REQUIRE(model.skeleton);
    auto& bones = model.skeleton->bones;
    REQUIRE(bones.size() == 2);
    CHECK(bones[0].name == "root");
    CHECK(bones[0].translation.z == doctest::Approx(44.786));
    CHECK(bones[1].name == "BG");
    CHECK(bones[1].parent == 0);
    CHECK(bones[1].flags == 0x1C4);
    // the stored local matrix is T · S of the stored transform
    auto local = cgfx::srt_matrix(bones[1].scale, bones[1].rotation, bones[1].translation);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) CHECK(local.at(r, c) == doctest::Approx(bones[1].local.at(r, c)));
}

TEST_CASE("BannerBG's textures are what the spec decoded") {
    auto dir = romfs();
    if (!dir) return;
    auto file = load(*dir / "3D/BannerBG_LZ.bin");
    auto cell = cgfx::find_texture(file, "BG_64_00");
    REQUIRE(cell);
    CHECK(cell->width == 64);
    CHECK(cell->height == 64);
    CHECK(cell->format == 7);
    CHECK(cell->data_at == 0x3E00);
    CHECK(cell->data_size == 0x1000);
    auto mask = cgfx::find_texture(file, "BG_CapMask_00");
    REQUIRE(mask);
    CHECK(mask->format == 11);
    CHECK(mask->width == 256);
    CHECK(mask->height == 512);
    CHECK(cgfx::find_texture(file, "BG_DefBase_00")->format == 12);
    CHECK_FALSE(cgfx::find_texture(file, "BG_Missing"));
}

TEST_CASE("every model and texture in 3D/ reads") {
    auto dir = romfs();
    if (!dir) return;
    int models = 0, textures = 0, skeletons = 0;
    for (auto& entry : fs::directory_iterator(*dir / "3D")) {
        auto file = load(entry.path());
        auto& dict = file.dict(cgfx::Slot::models);
        for (std::size_t i = 0; i < dict.size(); ++i) {
            auto model = cgfx::read_model(file, dict.value(i));
            ++models;
            skeletons += model.skeleton ? 1 : 0;
            for (auto& shape : model.shapes)
                for (auto& sub : shape.sub_meshes)
                    for (auto& primitive : sub.primitives)
                        for (auto& stream : primitive.streams) {
                            CHECK(stream.count() % 3 == 0);
                            auto vertices = shape.buffers.empty() ? 0u : shape.buffers[0].size / shape.buffers[0].stride;
                            for (std::size_t k = 0; k < stream.count(); ++k) CHECK(cgfx::index_at(file, stream, k) < vertices);
                        }
        }
        auto& tex = file.dict(cgfx::Slot::textures);
        for (std::size_t i = 0; i < tex.size(); ++i) {
            cgfx::read_texture(file, tex.value(i));
            ++textures;
        }
    }
    MESSAGE(models << " models, " << skeletons << " with skeletons, " << textures << " textures");
    CHECK(models == 15);
    CHECK(skeletons == 14);
    CHECK(textures == 87);
}
