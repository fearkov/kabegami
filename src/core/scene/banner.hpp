// the HOME Menu's own wallpaper: the BannerBG scene drawn as HOME draws it.
// the model, its material and textures from BannerBG_LZ.bin, the camera from
// BannerCamera_LZ.bin and the vertex program from nwgfx_DefaultShader_LZ.bin,
// the animations evaluated as HOME plays them, and every register NW4C would
// set made into one command list for the software PICA200. what a draw
// sends follows docs/research/camera-shader-nw4c.md part 3 and
// cgfx-material-texture.md part 3.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/cgfx/anim/anim.hpp"
#include "core/cgfx/cgfx.hpp"
#include "core/cgfx/material/material.hpp"
#include "core/cgfx/model/model.hpp"
#include "core/cgfx/program/program.hpp"
#include "core/home.hpp"
#include "core/image/image.hpp"
#include "core/pica/gpu/commands.hpp"
#include "core/pica/gpu/memory.hpp"

namespace kabegami::scene {

// the frames of the scene's animations
struct Pose {
    float loop = 0;       // BannerBG_Loop, which runs 0 to 600 and round
    float scene_in = 20;  // BannerBG_SceneIn, 20 its end, where HOME holds it
};

// the top screen's size, the picture's
constexpr int screen_width = 400, screen_height = 240;

class Banner {
public:
    Banner(cgfx::File banner, cgfx::File camera, cgfx::File shader);
    // the three files from the user's HOME Menu
    static Banner load(const HomeFiles& home);

    // the top screen as HOME shows it at that pose, upright, 400x240
    image::Image render(const Pose& pose = {}) const;

    // the command list a frame is, and the memory it reads, for tests and tools
    struct Frame {
        pica::Memory memory;
        std::vector<std::uint32_t> commands;
        std::uint32_t commands_at = 0, color_at = 0, depth_at = 0;
    };
    Frame build(const Pose& pose = {}) const;

    // the BannerBG file, to edit; reload() reads the edits back
    cgfx::File& file() { return banner_; }
    const cgfx::File& file() const { return banner_; }
    void reload();

    const cgfx::Model& model() const { return model_; }
    const std::vector<cgfx::Material>& materials() const { return materials_; }
    const cgfx::Camera& camera() const { return camera_; }
    const cgfx::ShaderProgram& shader() const { return shader_; }
    const std::optional<cgfx::Animation>& loop() const { return loop_; }
    const std::optional<cgfx::Animation>& scene_in() const { return scene_in_; }

private:
    cgfx::File banner_, camera_file_, shader_file_;
    cgfx::Model model_;
    std::vector<cgfx::Material> materials_;  // the model's, in its DICT's order
    cgfx::Camera camera_;
    cgfx::ShaderProgram shader_;
    std::optional<cgfx::Animation> loop_, scene_in_;
};

}  // namespace kabegami::scene
