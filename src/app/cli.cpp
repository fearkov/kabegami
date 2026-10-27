// the command line: everything the window does, for scripts and for a look
// at what Kabegami makes without a window.
#include "app/cli.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>

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

const char* const usage = R"(usage: kabegami <command> <HOME Menu> [options]

<HOME Menu> is your console's own HOME Menu: the decrypted CIA GodMode9 builds
from SYSNAND CTRNAND > title > 00040030 > <id> > content, a decrypted CXI or
NCSD, or a folder with its extracted RomFS.

commands:
  info <home>                     what the dump is, and its wallpaper's look
  render <home> [look] [-o PNG] [--frame N] [--scene-in N]
                                  the top screen as HOME draws it, 400x240
  recipe <home> [look] [-o FILE]  the look as a recipe to keep or share
  export <home> [look] [-o FILE]  BannerBG_LZ.bin with the look in it
  install <home> [look] (--azahar [DIR] | --zakuro [DIR] | --sd DIR)
                                  the look as a mod Azahar, Zakuro or Luma3DS reads
  uninstall <home> (--azahar [DIR] | --zakuro [DIR] | --sd DIR)
                                  takes Kabegami's mod out again

a look starts from the dump's own and changes what these say:
  --waves              light waves in place of the squares, as a start
  --recipe FILE        a recipe, then any of these over it
  --background RRGGBB  the wall's colour
  --highlight X        the grey the squares and the gradient lighten to, 0 to 1
  --gradient X         the gradient's strength, 0 to 1
  --squares X          the squares' opacity, 0 to 1
  --second-layer X     the still layer's weight, 0 to 1
  --cell PICTURE       the cell both layers repeat, made 64x64 and grey, as
                       the screen shows it
  --repeat R,D         cells of the moving layer across and down the wall's
                       texture span, HOME's 6,3
  --drift R,D          cells it moves each loop, right and down, whole numbers
  --still-repeat R,D   the still layer's cells, HOME's 6,3
  --still-shift R,D    where the still layer sits, in its cells right and down
  --loop FRAMES        how long a loop lasts, HOME's 600 (10 s at 60 frames a second)
)";

struct Arguments {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // "--frame" -> "30"; flags given without a value map to ""
};

// options that take a value; --azahar and --zakuro take one only when the
// next word isn't an option
const std::vector<std::string> valued = {"-o",        "--frame",        "--scene-in", "--recipe", "--background", "--highlight",
                                         "--gradient", "--squares",     "--second-layer", "--drift", "--cell",   "--sd",
                                         "--repeat",  "--still-repeat", "--still-shift", "--loop"};
const std::vector<std::string> maybe_valued = {"--azahar", "--zakuro"};
const std::vector<std::string> flags = {"--waves"};

Arguments parse(const std::vector<std::string>& args) {
    Arguments a;
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto& word = args[i];
        if (word.starts_with("-")) {
            if (std::find(valued.begin(), valued.end(), word) != valued.end()) {
                if (i + 1 >= args.size()) throw FormatError(word + " needs a value");
                a.options[word] = args[++i];
            } else if (std::find(flags.begin(), flags.end(), word) != flags.end()) {
                a.options[word] = "";
            } else if (std::find(maybe_valued.begin(), maybe_valued.end(), word) != maybe_valued.end()) {
                a.options[word] = i + 1 < args.size() && !args[i + 1].starts_with("-") ? args[++i] : "";
            } else {
                throw FormatError("there is no option " + word);
            }
        } else if (a.command.empty()) {
            a.command = word;
        } else {
            a.positional.push_back(word);
        }
    }
    return a;
}

float fraction(const std::string& option, const std::string& value) {
    float f = 0;
    std::istringstream in(value);
    in.imbue(std::locale::classic());
    if (!(in >> f) || f < 0 || f > 1) throw FormatError(option + " takes a number from 0 to 1");
    return f;
}

Bytes read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FormatError("can't read " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_file(const fs::path& path, ByteSpan data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw FormatError("can't write " + path.string());
}

struct Opened {
    HomeFiles home;
    Bytes original;  // BannerBG_LZ.bin as the dump has it
    cgfx::File file;  // the same, to edit
};

Opened open_home(const Arguments& a) {
    if (a.positional.empty()) throw FormatError("name your HOME Menu dump or its extracted RomFS");
    auto home = HomeFiles::open(a.positional[0]);
    auto original = home.read(banner_path);
    if (!original) throw FormatError("this HOME Menu has no " + std::string(banner_path));
    auto file = cgfx::File::load(*original, std::string(banner_path));
    return {std::move(home), std::move(*original), std::move(file)};
}

// the look the options make, from the file's own
edit::Look look_of(const Arguments& a, const Opened& o) {
    auto look = edit::read_look(o.file);
    auto option = [&](const char* name) -> const std::string* {
        auto it = a.options.find(name);
        return it == a.options.end() ? nullptr : &it->second;
    };
    if (option("--waves")) look = edit::waves();
    if (auto path = option("--recipe")) {
        auto text = read_file(*path);
        auto recipe = edit::read_recipe({reinterpret_cast<const char*>(text.data()), text.size()});
        auto ours = sha256_hex(o.original);
        if (!recipe.original.empty() && recipe.original != ours)
            std::cerr << "note: the recipe was made for another BannerBG_LZ.bin (" << recipe.original.substr(0, 12)
                      << "…), this one is " << ours.substr(0, 12) << "…\n";
        auto cell = recipe.look.cell;
        look = recipe.look;
        // a recipe without a cell keeps the file's
        if (!cell) look.cell = edit::read_look(o.file).cell;
    }
    if (auto v = option("--background")) {
        auto hex = v->starts_with("#") ? v->substr(1) : *v;
        unsigned rgb = 0;
        auto [end, error] = std::from_chars(hex.data(), hex.data() + hex.size(), rgb, 16);
        if (error != std::errc() || end != hex.data() + hex.size() || hex.size() != 6) throw FormatError("--background takes RRGGBB");
        look.background = {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                           static_cast<float>(rgb & 0xFF) / 255.0f};
    }
    if (auto v = option("--highlight")) look.highlight = fraction("--highlight", *v);
    if (auto v = option("--gradient")) look.gradient = fraction("--gradient", *v);
    if (auto v = option("--squares")) look.squares = fraction("--squares", *v);
    if (auto v = option("--second-layer")) look.second_layer = fraction("--second-layer", *v);
    if (auto v = option("--drift")) {
        int x = 0, y = 0;
        char comma = 0;
        std::istringstream in(*v);
        if (!(in >> x >> comma >> y) || comma != ',') throw FormatError("--drift takes two whole numbers, R,D");
        look.drift = {x, y};
    }
    auto pair = [&](const char* name, std::array<float, 2>& out, bool positive) {
        if (auto v = option(name)) {
            float x = 0, y = 0;
            char comma = 0;
            std::istringstream in(*v);
            in.imbue(std::locale::classic());
            if (!(in >> x >> comma >> y) || comma != ',' || (positive && (!(x > 0) || !(y > 0))))
                throw FormatError(std::string(name) + " takes two numbers, R,D" + (positive ? ", above 0" : ""));
            out = {x, y};
        }
    };
    pair("--repeat", look.repeat, true);
    pair("--still-repeat", look.still_repeat, true);
    pair("--still-shift", look.still_shift, false);
    if (auto v = option("--loop")) {
        std::istringstream in(*v);
        in.imbue(std::locale::classic());
        float frames = 0;
        if (!(in >> frames) || frames < 1) throw FormatError("--loop takes the frames a loop lasts, 1 or more");
        look.loop_frames = frames;
    }
    if (auto v = option("--cell")) look.cell = edit::make_cell(image::read(*v));
    return look;
}

// the title the mod goes to: the dump's own, or its region's HOME Menu
std::uint64_t title_of(const Opened& o) {
    if (auto id = o.home.title_id()) return *id;
    if (auto r = o.home.region()) return r->home_menu;
    throw FormatError("can't tell which region's HOME Menu this is; a CIA or CXI says it");
}

fs::path romfs_of(const Arguments& a, std::uint64_t title) {
    if (auto it = a.options.find("--sd"); it != a.options.end()) return install::luma_romfs(it->second, title);
    if (auto it = a.options.find("--azahar"); it != a.options.end()) {
        auto dir = it->second.empty() ? install::azahar_user_dir() : std::optional<fs::path>(it->second);
        if (!dir) throw FormatError("can't find Azahar's user folder; name it with --azahar DIR");
        return install::azahar_romfs(*dir, title);
    }
    if (auto it = a.options.find("--zakuro"); it != a.options.end()) {
        auto dir = it->second.empty() ? install::zakuro_data_dir() : std::optional<fs::path>(it->second);
        if (!dir) throw FormatError("can't find Zakuro's data folder; name it with --zakuro DIR");
        return install::zakuro_romfs(*dir, title);
    }
    throw FormatError("say where: --azahar, --zakuro or --sd DIR");
}

std::string hex_color(const std::array<float, 3>& c) {
    char buf[8];
    auto byte = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    std::snprintf(buf, sizeof buf, "%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2]));
    return buf;
}

Bytes edited_file(const Arguments& a, Opened& o) {
    edit::apply(o.file, look_of(a, o));
    // the size it had, so nothing else in the RomFS has to make room
    return o.file.encode(o.original.size());
}

int info(const Arguments& a) {
    auto o = open_home(a);
    auto look = edit::read_look(o.file);
    std::cout << o.home.describe() << "\n";
    std::cout << "BannerBG_LZ.bin: " << o.original.size() << " bytes, SHA-256 " << sha256_hex(o.original) << "\n";
    std::cout << "background " << hex_color(look.background) << ", highlight " << look.highlight << ", gradient " << look.gradient
              << ", squares " << look.squares << ", second layer " << look.second_layer << "\n";
    std::cout << "moving layer: repeat " << look.repeat[0] << "," << look.repeat[1] << ", drift " << look.drift[0] << ","
              << look.drift[1] << " a loop of " << look.loop_frames << " frames; still layer: repeat " << look.still_repeat[0] << ","
              << look.still_repeat[1] << ", shift " << look.still_shift[0] << "," << look.still_shift[1] << "\n";
    return 0;
}

int render(const Arguments& a) {
    auto o = open_home(a);
    auto banner = scene::Banner::load(o.home);
    edit::apply(banner.file(), look_of(a, o));
    banner.reload();
    scene::Pose pose;
    auto number = [&](const char* name, float& out) {
        if (auto it = a.options.find(name); it != a.options.end()) {
            std::istringstream in(it->second);
            in.imbue(std::locale::classic());
            if (!(in >> out)) throw FormatError(std::string(name) + " takes a number");
        }
    };
    number("--frame", pose.loop);
    number("--scene-in", pose.scene_in);
    auto output = a.options.count("-o") ? a.options.at("-o") : std::string("wallpaper.png");
    image::write_png(output, banner.render(pose));
    std::cout << "drew " << output << "\n";
    return 0;
}

int recipe(const Arguments& a) {
    auto o = open_home(a);
    edit::Recipe r{look_of(a, o), sha256_hex(o.original)};
    auto text = edit::write_recipe(r);
    if (auto it = a.options.find("-o"); it != a.options.end()) {
        write_file(it->second, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
        std::cout << "wrote " << it->second << "\n";
    } else {
        std::cout << text;
    }
    return 0;
}

int export_file(const Arguments& a) {
    auto o = open_home(a);
    auto data = edited_file(a, o);
    auto output = a.options.count("-o") ? a.options.at("-o") : std::string("BannerBG_LZ.bin");
    write_file(output, data);
    std::cout << "wrote " << output << ", " << data.size() << " bytes\n";
    return 0;
}

int install_mod(const Arguments& a) {
    auto o = open_home(a);
    auto title = title_of(o);
    auto root = romfs_of(a, title);
    auto data = edited_file(a, o);
    auto written = install::put(root, banner_path, data);
    std::cout << "put " << written.path.string() << "\n";
    if (written.backup) std::cout << "kept what was there as " << written.backup->string() << "\n";
    return 0;
}

int uninstall_mod(const Arguments& a) {
    auto o = open_home(a);
    for (auto& line : install::remove(romfs_of(a, title_of(o)), banner_path)) std::cout << line << "\n";
    return 0;
}

}  // namespace

int run_cli(const std::vector<std::string>& args) {
    try {
        auto a = parse(args);
        if (a.command.empty() || a.command == "help" || a.command == "--help" || a.command == "-h") {
            std::cout << usage;
            return a.command.empty() ? 2 : 0;
        }
        if (a.command == "info") return info(a);
        if (a.command == "render") return render(a);
        if (a.command == "recipe") return recipe(a);
        if (a.command == "export") return export_file(a);
        if (a.command == "install") return install_mod(a);
        if (a.command == "uninstall") return uninstall_mod(a);
        std::cerr << "kabegami: there is no command " << a.command << "\n\n" << usage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "kabegami: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace kabegami::app
