// what a user changes in the wallpaper, and how each change lands in
// BannerBG_LZ.bin without moving anything in it:
//
//   the colours are mt_BG's own: the background is Constant3's colour, the
//   grey the squares and the gradient lighten to is Constant3's alpha, the
//   gradient's strength Constant2's alpha, the squares' opacity Constant1's
//   and the second layer's weight Constant5's. these are the seven floats
//   HOME copies out of the material when it starts and puts back whenever no
//   theme sets its own (cgfx-material-texture.md §3.3);
//   the two layers of cells are texture coordinators 1 and 2: how many cells
//   repeat over the wall is their scale, and where the still layer sits its
//   translation;
//   the drift is BannerBG_Loop's translation of coordinator 1, kept a whole
//   number of cells a loop so the loop has no seam, and the loop's length its
//   frame count. BannerBG_AppQuit puts coordinator 1 back when an app
//   closes, so its constants follow;
//   the cell is BG_64_00, the 64x64 L8 texture both layers repeat, the only
//   size HOME accepts there.
//
// the wall shows the texture turned a quarter counter-clockwise: its u runs
// up the screen and its v to the left. a Look is in the screen's terms, the
// cell upright and the cells counted right and down, and apply() turns them.
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/cgfx/cgfx.hpp"
#include "core/image/image.hpp"

namespace kabegami::edit {

// a Look's values start as HOME's own, which a recipe that leaves one out
// keeps.
struct Look {
    std::array<float, 3> background{0.8f, 0.8f, 0.85f};  // 0 to 1
    float highlight = 1;
    float gradient = 0.75f;
    float squares = 0.25f;
    float second_layer = 1;
    // the moving layer: the cells over a unit of the wall's texture
    // coordinates, right and down; and the cells it moves each loop, right
    // and down, whole numbers, 0, 0 holding it still
    std::array<float, 2> repeat{6, 3};
    std::array<int, 2> drift{1, 1};
    // the still layer: its repeats, and where it sits, in its cells right and down
    std::array<float, 2> still_repeat{6, 3};
    std::array<float, 2> still_shift{0, 0.06f};
    // the loop's length in HOME's frames, 600 for HOME's own
    float loop_frames = 600;
    // the cell as the screen shows it, 64x64 grey; nothing keeps the file's
    std::optional<image::Image> cell;

    bool operator==(const Look&) const = default;
};

// the look the file has now, its cell included
Look read_look(const cgfx::File& banner);
// the look written over the file's, in place. what doesn't change keeps its
// bytes. throws FormatError for a file whose wallpaper isn't laid out as
// HOME's is.
void apply(cgfx::File& banner, const Look& look);

// any picture as the cell: fitted to 64x64 by averaging, over black where it
// is see-through, and grey, the luminance of its colour
image::Image make_cell(const image::Image& picture);

// waves in place of squares: soft light ribbons, one layer flowing across
// the other, slowly
Look waves();
image::Image waves_cell();

// a recipe: a look as a short text a user can share, naming the SHA-256 of
// the BannerBG_LZ.bin it was made from. it holds no Nintendo data: the cell
// in it is the user's own picture.
struct Recipe {
    Look look;
    std::string original;  // hex, of the compressed file as the dump has it
};
std::string write_recipe(const Recipe& recipe);
// throws FormatError saying which line it can't read
Recipe read_recipe(std::string_view text);

std::string base64(std::span<const std::uint8_t> bytes);
Bytes unbase64(std::string_view text);

}  // namespace kabegami::edit
