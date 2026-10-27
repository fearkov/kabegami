// the edits a user makes, on the user's own BannerBG (KABEGAMI_ROMFS=<an
// extracted RomFS>), and recipes, which need no file.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/cgfx/anim/anim.hpp"
#include "core/edit/look.hpp"

using namespace kabegami;

namespace {

std::optional<cgfx::File> banner() {
    auto dir = std::getenv("KABEGAMI_ROMFS");
    if (!dir || !std::filesystem::is_directory(dir)) return std::nullopt;
    std::ifstream in(std::filesystem::path(dir) / "3D/BannerBG_LZ.bin", std::ios::binary);
    Bytes raw{std::istreambuf_iterator<char>(in), {}};
    return cgfx::File::load(raw, "BannerBG_LZ.bin");
}

image::Image checker() {
    image::Image cell(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            auto p = cell.at(x, y);
            p[0] = p[1] = p[2] = ((x / 8 + y / 8) % 2) ? 255 : 0;
            p[3] = 255;
        }
    return cell;
}

}  // namespace

TEST_CASE("HOME's own look") {
    auto file = banner();
    if (!file) return;
    auto look = edit::read_look(*file);
    CHECK(look.background[0] == doctest::Approx(0.8));
    CHECK(look.background[1] == doctest::Approx(0.8));
    CHECK(look.background[2] == doctest::Approx(0.85));
    CHECK(look.highlight == 1.0f);
    CHECK(look.gradient == doctest::Approx(0.75));
    CHECK(look.squares == doctest::Approx(0.25));
    CHECK(look.second_layer == 1.0f);
    // the cells move right and down a cell each 600-frame loop
    CHECK(look.drift == std::array{1, 1});
    CHECK(look.repeat == std::array{6.0f, 3.0f});
    CHECK(look.still_repeat == std::array{6.0f, 3.0f});
    CHECK(look.still_shift[0] == doctest::Approx(0));
    CHECK(look.still_shift[1] == doctest::Approx(0.06));
    CHECK(look.loop_frames == 600);
    REQUIRE(look.cell);
    CHECK(look.cell->width == 64);
}

TEST_CASE("applying the look a file has changes none of its bytes") {
    auto file = banner();
    if (!file) return;
    auto before = file->bytes();
    edit::apply(*file, edit::read_look(*file));
    CHECK((file->bytes() == before));
}

TEST_CASE("a new look reads back as it was written") {
    auto file = banner();
    if (!file) return;
    edit::Look look = edit::read_look(*file);
    look.background = {0.2f, 0.4f, 0.6f};
    look.highlight = 0.9f;
    look.gradient = 0.5f;
    look.squares = 0.6f;
    look.second_layer = 0.3f;
    look.drift = {2, -1};
    look.repeat = {2, 1.5f};
    look.still_repeat = {1, 1};
    look.still_shift = {0.25f, -0.5f};
    look.loop_frames = 900;
    look.cell = checker();
    edit::apply(*file, look);
    auto back = edit::read_look(*file);
    CHECK(back.still_shift[0] == doctest::Approx(0.25f));
    CHECK(back.still_shift[1] == doctest::Approx(-0.5f));
    back.still_shift = look.still_shift;
    CHECK(back == look);
    // the loop still starts where HOME's does, lasts 900 frames now, and has
    // no seam: two cells right and one up at the moving layer's repeats
    // (scale 1.5 down, u, and 2 right, v; 3ds Max matrices, so t0 = -1.5 x
    // and t1 = 2 y plus constants)
    auto loop = cgfx::find_animation(*file, cgfx::Slot::material_animations, "BannerBG_Loop");
    CHECK(loop->frames == 900);
    auto member = loop->find("Materials[\"mt_BG\"].TextureCoordinators[1].Translate");
    auto start = cgfx::evaluate(*file, *member, 0), end = cgfx::evaluate(*file, *member, 900);
    CHECK(start.values[0] == doctest::Approx(-0.357778));
    CHECK((end.values[1] - start.values[1]) * 2 == doctest::Approx(2).epsilon(1e-5));
    CHECK((end.values[0] - start.values[0]) * -1.5 == doctest::Approx(-1).epsilon(1e-5));
    // halfway through the loop, halfway there
    auto half = cgfx::evaluate(*file, *member, 450);
    CHECK((half.values[1] - start.values[1]) * 2 == doctest::Approx(1).epsilon(1e-4));
    // AppQuit puts the layer back at the new repeats and at the Loop's end
    auto quit = cgfx::find_animation(*file, cgfx::Slot::material_animations, "BannerBG_AppQuit");
    REQUIRE(quit);
    auto scale = cgfx::evaluate(*file, *quit->find("Materials[\"mt_BG\"].TextureCoordinators[1].Scale"), 0);
    CHECK(scale.values[0] == 1.5f);
    CHECK(scale.values[1] == 2.0f);
    auto put_back = cgfx::evaluate(*file, *quit->find("Materials[\"mt_BG\"].TextureCoordinators[1].Translate"), 0);
    CHECK(put_back.values[0] == end.values[0]);
    CHECK(put_back.values[1] == end.values[1]);
    // and the file compresses back to the size it had
    auto encoded = file->encode(file->loaded_size());
    CHECK(encoded.size() == file->loaded_size());
}

TEST_CASE("a recipe keeps every value, the cell too") {
    edit::Recipe recipe;
    recipe.original = "27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711";
    recipe.look.background = {0.8f, 0.8f, 0.85f};
    recipe.look.highlight = 1;
    recipe.look.gradient = 0.75f;
    recipe.look.squares = 0.25f;
    recipe.look.second_layer = 1;
    recipe.look.drift = {1, -3};
    recipe.look.repeat = {1.5f, 0.75f};
    recipe.look.still_repeat = {2, 1};
    recipe.look.still_shift = {0.125f, -0.375f};
    recipe.look.loop_frames = 1800;
    recipe.look.cell = checker();
    auto text = edit::write_recipe(recipe);
    auto back = edit::read_recipe(text);
    CHECK(back.original == recipe.original);
    CHECK(back.look == recipe.look);
    CHECK_THROWS_AS(edit::read_recipe("hello\n"), FormatError);
    CHECK_THROWS_AS(edit::read_recipe("kabegami-recipe 1\ncolour 1\n"), FormatError);
}

TEST_CASE("any picture becomes a grey 64x64 cell, over black where it is see-through") {
    image::Image picture(128, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 128; ++x) {
            auto p = picture.at(x, y);
            p[0] = 255, p[1] = 0, p[2] = 0;
            p[3] = x < 64 ? 255 : 0;
        }
    auto cell = edit::make_cell(picture);
    CHECK(cell.width == 64);
    CHECK(cell.height == 64);
    CHECK(cell.at(0, 0)[0] == 76);  // red's luminance
    CHECK(cell.at(63, 63)[0] == 0);
    CHECK(cell.at(10, 10)[3] == 255);
}

TEST_CASE("base64 both ways") {
    std::string text = "Kabegami!";
    auto encoded = edit::base64({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    CHECK(encoded == "S2FiZWdhbWkh");
    auto back = edit::unbase64(encoded);
    CHECK(std::string(back.begin(), back.end()) == text);
    CHECK(edit::base64({reinterpret_cast<const std::uint8_t*>("ab"), 2}) == "YWI=");
}

TEST_CASE("the cell is turned so the screen shows it upright") {
    auto file = banner();
    if (!file) return;
    // a mark at the screen's top left of the cell
    image::Image cell(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            auto p = cell.at(x, y);
            p[0] = p[1] = p[2] = (x < 8 && y < 4) ? 255 : 0;
            p[3] = 255;
        }
    auto look = edit::read_look(*file);
    look.cell = cell;
    edit::apply(*file, look);
    // the wall shows the texture a quarter turn counter-clockwise, so the
    // texture holds the mark at its top right, turned
    auto back = edit::read_look(*file);
    CHECK(back.cell->at(0, 0)[0] == 255);
    CHECK(back.cell->at(7, 3)[0] == 255);
    CHECK(back.cell->at(8, 0)[0] == 0);
    CHECK(back.cell->at(0, 4)[0] == 0);
}

TEST_CASE("the waves preset applies and reads back") {
    auto file = banner();
    if (!file) return;
    auto waves = edit::waves();
    edit::apply(*file, waves);
    auto back = edit::read_look(*file);
    CHECK(back.drift == waves.drift);
    CHECK(back.repeat == waves.repeat);
    CHECK(back.loop_frames == 1200);
    CHECK(back.cell == edit::make_cell(*waves.cell));
}
