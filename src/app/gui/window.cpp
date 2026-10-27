#include "app/gui/window.hpp"

#include <GLFW/glfw3.h>
#include <GL/glext.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "core/edit/look.hpp"
#include "core/home.hpp"
#include "core/image/image.hpp"
#include "core/install/install.hpp"
#include "core/scene/banner.hpp"
#include "core/sha256.hpp"

namespace kabegami::app {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view banner_path = "3D/BannerBG_LZ.bin";
// HOME steps the Loop by one a frame; the frames a second it draws at isn't
// in the notes, 60 is what the console's screens show
constexpr double loop_frames_per_second = 60.0;

// draws frames on a thread of its own, so the window keeps up while a frame
// takes its 50 ms. the window asks for the newest look and frame, and takes
// whatever was drawn last.
class Painter {
public:
    Painter() : thread_([this] { run(); }) {}
    ~Painter() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

    void set_scene(std::optional<scene::Banner> banner) {
        std::lock_guard lock(mutex_);
        scene_ = std::move(banner);
        ++scene_generation_;
    }
    void ask(const edit::Look& look, const scene::Pose& pose) {
        {
            std::lock_guard lock(mutex_);
            asked_ = Asked{look, pose};
        }
        wake_.notify_all();
    }
    // the newest picture, once, when there is one the window hasn't had
    std::optional<image::Image> take() {
        std::lock_guard lock(mutex_);
        auto out = std::move(drawn_);
        drawn_.reset();
        return out;
    }
    std::string error() {
        std::lock_guard lock(mutex_);
        return error_;
    }

private:
    struct Asked {
        edit::Look look;
        scene::Pose pose;
    };

    void run() {
        std::optional<scene::Banner> edited;
        std::optional<edit::Look> edited_look;
        std::uint64_t edited_generation = 0;
        for (;;) {
            Asked asked;
            std::optional<scene::Banner> fresh;
            std::uint64_t generation;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || asked_; });
                if (stop_) return;
                asked = std::move(*asked_);
                asked_.reset();
                generation = scene_generation_;
                if (!scene_) continue;
                // a copy of the scene to edit, only when the look or the dump changed
                if (!edited || generation != edited_generation || edited_look != asked.look) fresh = scene_;
            }
            try {
                if (fresh) {
                    edit::apply(fresh->file(), asked.look);
                    fresh->reload();
                    edited = std::move(fresh);
                    edited_look = asked.look;
                    edited_generation = generation;
                }
                auto picture = edited->render(asked.pose);
                std::lock_guard lock(mutex_);
                drawn_ = std::move(picture);
                error_.clear();
            } catch (const std::exception& e) {
                std::lock_guard lock(mutex_);
                error_ = e.what();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::optional<scene::Banner> scene_;
    std::uint64_t scene_generation_ = 0;
    std::optional<Asked> asked_;
    std::optional<image::Image> drawn_;
    std::string error_;
    std::thread thread_;  // last, so it starts once the rest is there
};

GLuint make_texture() {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    // the console's pixels, each a square of the screen's
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void upload(GLuint texture, const image::Image& picture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, picture.width, picture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture.rgba.data());
}

ImTextureID id_of(GLuint texture) { return static_cast<ImTextureID>(texture); }

// a path a text field edits, with room to type
struct PathField {
    char text[1024] = {};
    void set(const std::string& value) { std::snprintf(text, sizeof text, "%s", value.c_str()); }
    std::string get() const { return text; }
};

bool picture_file(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif" || ext == ".tga";
}

std::vector<std::string> dropped;

class Studio {
public:
    explicit Studio(const std::string& home) {
        preview_ = make_texture();
        cell_texture_ = make_texture();
        if (auto dir = install::azahar_user_dir()) azahar_.set(dir->string());
        if (auto dir = install::zakuro_data_dir()) zakuro_.set(dir->string());
        recipe_.set((fs::current_path() / "wallpaper.kabegami").string());
        picture_.set((fs::current_path() / "wallpaper.png").string());
        if (!home.empty()) open(home);
    }

    void open(const std::string& path) {
        try {
            auto home = HomeFiles::open(path);
            auto original = home.read(banner_path);
            if (!original) throw FormatError("this HOME Menu has no " + std::string(banner_path));
            auto banner = scene::Banner::load(home);
            home_look_ = edit::read_look(banner.file());
            look_ = home_look_;
            original_ = std::move(*original);
            title_ = home.title_id() ? *home.title_id() : home.region() ? home.region()->home_menu : 0;
            described_ = home.describe();
            painter_.set_scene(std::move(banner));
            home_.set(path);
            start_ = std::chrono::steady_clock::now();
            cell_dirty_ = true;
            say("opened " + path);
        } catch (const std::exception& e) {
            say(std::string("can't open it: ") + e.what());
        }
    }

    void frame() {
        for (auto& path : std::exchange(dropped, {})) take_drop(path);

        // the frame of the loop now, and the scene coming in at the start
        auto now = std::chrono::steady_clock::now();
        if (playing_) {
            auto seconds = std::chrono::duration<double>(now - start_).count();
            pose_.loop = static_cast<float>(std::fmod(seconds * loop_frames_per_second, 600.0));
            pose_.scene_in = static_cast<float>(std::min(20.0, seconds * loop_frames_per_second));
        }
        if (!original_.empty()) painter_.ask(look_, pose_);
        if (auto picture = painter_.take()) {
            upload(preview_, *picture);
            last_picture_ = std::move(*picture);
        }
        if (cell_dirty_ && look_.cell) {
            upload(cell_texture_, *look_.cell);
            cell_dirty_ = false;
        }

        auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Kabegami", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        float panel = 430;
        ImGui::BeginChild("preview", {io.DisplaySize.x - panel - 24, 0});
        preview();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("panel", {0, 0});
        controls();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void say(std::string message) { status_ = std::move(message); }

    void take_drop(const std::string& path) {
        fs::path p(path);
        if (picture_file(p)) {
            load_cell(path);
        } else if (p.extension() == ".kabegami") {
            recipe_.set(path);
            load_recipe();
        } else {
            open(path);
        }
    }

    void load_cell(const std::string& path) {
        try {
            look_.cell = edit::make_cell(image::read(path));
            cell_dirty_ = true;
            say("the cell is now " + fs::path(path).filename().string());
        } catch (const std::exception& e) {
            say(std::string("can't use that picture: ") + e.what());
        }
    }

    void load_recipe() {
        try {
            std::ifstream in(recipe_.get(), std::ios::binary);
            if (!in) throw FormatError("can't read " + recipe_.get());
            std::string text{std::istreambuf_iterator<char>(in), {}};
            auto recipe = edit::read_recipe(text);
            auto cell = look_.cell;
            look_ = recipe.look;
            if (!look_.cell) look_.cell = cell;
            cell_dirty_ = true;
            std::string note;
            if (!recipe.original.empty() && !original_.empty() && recipe.original != sha256_hex(original_))
                note = " (it was made for another BannerBG)";
            say("loaded " + recipe_.get() + note);
        } catch (const std::exception& e) {
            say(std::string("can't load the recipe: ") + e.what());
        }
    }

    void save_recipe() {
        try {
            auto text = edit::write_recipe({look_, original_.empty() ? std::string() : sha256_hex(original_)});
            std::ofstream out(recipe_.get(), std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) throw FormatError("can't write " + recipe_.get());
            say("saved " + recipe_.get());
        } catch (const std::exception& e) {
            say(std::string("can't save the recipe: ") + e.what());
        }
    }

    // the edited file, as HOME reads it, the size the original was
    Bytes edited() const {
        auto file = cgfx::File::load(original_, std::string(banner_path));
        edit::apply(file, look_);
        return file.encode(original_.size());
    }

    void install_to(const fs::path& romfs, const std::string& where) {
        try {
            auto written = install::put(romfs, banner_path, edited());
            auto message = "installed for " + where + ": " + written.path.string();
            if (written.backup) message += ", what was there kept as " + written.backup->filename().string();
            say(message);
        } catch (const std::exception& e) {
            say("can't install for " + where + ": " + e.what());
        }
    }

    void uninstall_from(const fs::path& romfs, const std::string& where) {
        try {
            auto lines = install::remove(romfs, banner_path);
            say(lines.empty() ? "nothing of Kabegami's was there for " + where : "took it out of " + where);
        } catch (const std::exception& e) {
            say("can't take it out of " + where + ": " + e.what());
        }
    }

    void preview() {
        auto avail = ImGui::GetContentRegionAvail();
        float scale = std::max(1.0f, std::floor(std::min(avail.x / scene::screen_width, (avail.y - 90) / scene::screen_height)));
        if (original_.empty()) {
            ImGui::TextWrapped("Open your HOME Menu: the decrypted CIA GodMode9 makes from SYSNAND CTRNAND > title > 00040030 > "
                               "<id> > content, a decrypted CXI, or a folder with its extracted RomFS. Drop it on the window, "
                               "or type its path on the right.");
            return;
        }
        ImGui::Image(id_of(preview_), {scene::screen_width * scale, scene::screen_height * scale});
        if (ImGui::Button(playing_ ? "Pause" : "Play")) {
            playing_ = !playing_;
            // carry on from where the slider is
            start_ = std::chrono::steady_clock::now() - std::chrono::microseconds(static_cast<long long>(pose_.loop / loop_frames_per_second * 1e6));
            pose_.scene_in = 20;
        }
        ImGui::SameLine();
        if (ImGui::Button("Replay the start")) {
            playing_ = true;
            start_ = std::chrono::steady_clock::now();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260);
        if (ImGui::SliderFloat("loop frame", &pose_.loop, 0, 599, "%.0f")) {
            playing_ = false;
            pose_.scene_in = 20;
        }
        ImGui::TextDisabled("%s", described_.c_str());
        if (auto error = painter_.error(); !error.empty()) ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", error.c_str());
    }

    void controls() {
        ImGui::SeparatorText("HOME Menu");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##home", home_.text, sizeof home_.text);
        ImGui::SameLine();
        if (ImGui::Button("Open")) open(home_.get());
        if (original_.empty()) {
            status();
            return;
        }

        ImGui::SeparatorText("Look");
        bool changed = false;
        changed |= ImGui::ColorEdit3("background", look_.background.data());
        changed |= ImGui::SliderFloat("highlight", &look_.highlight, 0, 1);
        ImGui::SetItemTooltip("the grey the squares and the gradient lighten to");
        changed |= ImGui::SliderFloat("gradient", &look_.gradient, 0, 1);
        changed |= ImGui::SliderFloat("squares", &look_.squares, 0, 1);
        ImGui::SetItemTooltip("how opaque the squares are");
        changed |= ImGui::SliderFloat("second layer", &look_.second_layer, 0, 1);
        ImGui::SetItemTooltip("how much the still layer of squares shows");

        ImGui::SeparatorText("Layers");
        changed |= ImGui::DragFloat2("repeat", look_.repeat.data(), 0.01f, 0.25f, 16, "%.2f");
        ImGui::SetItemTooltip("cells of the moving layer across and down the wall; HOME's 6, 3");
        if (ImGui::InputInt2("drift", look_.drift.data())) {
            for (auto& d : look_.drift) d = std::clamp(d, -8, 8);
            changed = true;
        }
        ImGui::SetItemTooltip("cells the moving layer moves each loop, right and down; 0, 0 holds it still");
        float seconds = look_.loop_frames / 60.0f;
        if (ImGui::SliderFloat("loop", &seconds, 1, 120, "%.1f s")) {
            look_.loop_frames = std::max(1.0f, std::round(seconds * 60));
            changed = true;
        }
        ImGui::SetItemTooltip("how long a loop lasts, at 60 frames a second; HOME's 10 s (600 frames)");
        changed |= ImGui::DragFloat2("still repeat", look_.still_repeat.data(), 0.01f, 0.25f, 16, "%.2f");
        ImGui::SetItemTooltip("cells of the still layer across and down the wall; HOME's 6, 3");
        changed |= ImGui::DragFloat2("still shift", look_.still_shift.data(), 0.005f, -4, 4, "%.3f");
        ImGui::SetItemTooltip("where the still layer sits, in its cells right and down");
        for (auto* r : {&look_.repeat, &look_.still_repeat})
            for (auto& v : *r) v = std::clamp(v, 0.25f, 16.0f);
        (void)changed;

        ImGui::Image(id_of(cell_texture_), {128, 128});
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextWrapped("The cell both layers repeat, 64x64 and grey, as the screen shows it: light is square, dark is wall.");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##cell", cell_.text, sizeof cell_.text);
        ImGui::SameLine();
        if (ImGui::Button("Use")) load_cell(cell_.get());
        ImGui::TextDisabled("or drop a picture on the window");
        ImGui::EndGroup();
        if (ImGui::Button("HOME's own look")) {
            look_ = home_look_;
            cell_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Waves")) {
            look_ = edit::waves();
            cell_dirty_ = true;
        }
        ImGui::SetItemTooltip("light waves in place of the squares, one layer flowing across the other");

        ImGui::SeparatorText("Recipe");
        ImGui::SetNextItemWidth(-110);
        ImGui::InputText("##recipe", recipe_.text, sizeof recipe_.text);
        ImGui::SameLine();
        if (ImGui::Button("Save")) save_recipe();
        ImGui::SameLine();
        if (ImGui::Button("Load")) load_recipe();

        ImGui::SeparatorText("Install");
        if (title_ == 0) {
            ImGui::TextWrapped("This dump doesn't say its title. Open the CIA or CXI to install.");
        } else {
            ImGui::Text("for %s", install::title_folder(title_).c_str());
            install_row("Azahar", azahar_, [&](const fs::path& dir) { return install::azahar_romfs(dir, title_); });
            install_row("Zakuro", zakuro_, [&](const fs::path& dir) { return install::zakuro_romfs(dir, title_); });
            install_row("SD card", sd_, [&](const fs::path& dir) { return install::luma_romfs(dir, title_); });
            ImGui::TextDisabled("On a console, Luma3DS reads it with \"Enable game patching\" on.");
        }

        ImGui::SeparatorText("Picture");
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##picture", picture_.text, sizeof picture_.text);
        ImGui::SameLine();
        if (ImGui::Button("Save##png")) {
            try {
                image::write_png(picture_.get(), last_picture_);
                say("saved " + picture_.get());
            } catch (const std::exception& e) {
                say(std::string("can't save the picture: ") + e.what());
            }
        }
        status();
    }

    template <typename Romfs>
    void install_row(const char* name, PathField& field, Romfs romfs) {
        ImGui::PushID(name);
        ImGui::SetNextItemWidth(-170);
        ImGui::InputTextWithHint("##dir", name, field.text, sizeof field.text);
        ImGui::SameLine();
        bool has = field.text[0] != 0;
        ImGui::BeginDisabled(!has);
        if (ImGui::Button("Install")) install_to(romfs(field.get()), name);
        ImGui::SameLine();
        if (ImGui::Button("Uninstall")) uninstall_from(romfs(field.get()), name);
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    void status() {
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_.c_str());
    }

    Painter painter_;
    GLuint preview_ = 0, cell_texture_ = 0;
    PathField home_, cell_, recipe_, picture_, azahar_, zakuro_, sd_;
    Bytes original_;
    std::uint64_t title_ = 0;
    std::string described_, status_ = "Drop your HOME Menu dump on the window.";
    edit::Look look_, home_look_;
    bool cell_dirty_ = false, playing_ = true;
    scene::Pose pose_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    image::Image last_picture_;
};

// what the window shows, drawn into a framebuffer of its own: the window
// stays hidden and the picture goes to a PNG. for checking the window, and
// pictures of it, without one on the screen.
class Capture {
public:
    Capture(int width, int height) : width_(width), height_(height) {
        auto load = [](const char* name) { return glfwGetProcAddress(name); };
        gen_framebuffers_ = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(load("glGenFramebuffers"));
        bind_framebuffer_ = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(load("glBindFramebuffer"));
        framebuffer_texture_ = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(load("glFramebufferTexture2D"));
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gen_framebuffers_(1, &framebuffer_);
        bind();
        framebuffer_texture_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    }
    void bind() { bind_framebuffer_(GL_FRAMEBUFFER, framebuffer_); }
    void save(const std::string& path) {
        bind();
        image::Image picture(width_, height_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, picture.rgba.data());
        // OpenGL's rows go up from the bottom
        image::Image upright(width_, height_);
        for (int y = 0; y < height_; ++y) std::copy_n(picture.at(0, height_ - 1 - y), width_ * 4, upright.at(0, y));
        image::write_png(path, upright);
    }

private:
    int width_, height_;
    GLuint texture_ = 0, framebuffer_ = 0;
    PFNGLGENFRAMEBUFFERSPROC gen_framebuffers_;
    PFNGLBINDFRAMEBUFFERPROC bind_framebuffer_;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture_;
};

}  // namespace

int run_window(const std::string& home) {
    auto capture_to = std::getenv("KABEGAMI_SCREENSHOT");
    if (!glfwInit()) {
        std::fprintf(stderr, "kabegami: no window system to open a window on\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (capture_to) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = glfwCreateWindow(1320, 760, "Kabegami", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "kabegami: can't open an OpenGL 3.3 window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(capture_to ? 0 : 1);
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; ++i) dropped.emplace_back(paths[i]);
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    {
        Studio studio(home);
        std::optional<Capture> capture;
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (capture_to) capture.emplace(width, height);
        auto started = std::chrono::steady_clock::now();
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            studio.frame();
            ImGui::Render();
            if (capture) capture->bind();
            else glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.1f, 0.1f, 0.12f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (capture) {
                // long enough for the scene to come in and the first frames to draw
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(2)) {
                    capture->save(capture_to);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            glfwSwapBuffers(window);
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace kabegami::app
