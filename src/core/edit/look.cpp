#include "core/edit/look.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

#include "core/cgfx/anim/anim.hpp"
#include "core/cgfx/material/material.hpp"
#include "core/cgfx/model/model.hpp"
#include "core/pica/fragment/texture.hpp"

namespace kabegami::edit {
namespace {

constexpr std::string_view material_name = "mt_BG";
constexpr std::string_view translate_path = "Materials[\"mt_BG\"].TextureCoordinators[1].Translate";
constexpr std::string_view scale_path = "Materials[\"mt_BG\"].TextureCoordinators[1].Scale";
constexpr int cell_size = 64;

cgfx::Material material_of(const cgfx::File& file) {
    auto model = cgfx::read_model(file);
    auto entry = model.materials.find(material_name);
    if (!entry) throw FormatError(file.name() + " has no mt_BG, the wallpaper's material");
    return cgfx::read_material(file, model.materials.value(*entry));
}

struct Parts {
    cgfx::Material material;
    cgfx::Texture cell;
    cgfx::Animation loop;
    const cgfx::Member* translate;
    // what puts the moving layer back when an app closes
    std::optional<cgfx::Animation> quit;
};

Parts parts_of(const cgfx::File& file) {
    auto material = material_of(file);
    if (!material.mappers[1]) throw FormatError(file.name() + ": mt_BG has no cell texture");
    auto cell = cgfx::find_texture(file, material.mappers[1]->texture);
    if (!cell || cell->width != cell_size || cell->height != cell_size || cell->format != 7)
        throw FormatError(file.name() + ": the cell texture isn't a 64x64 L8 one, as HOME's is");
    auto loop = cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_Loop");
    if (!loop) throw FormatError(file.name() + " has no BannerBG_Loop, the drift");
    Parts p{material, *cell, *loop, nullptr, cgfx::find_animation(file, cgfx::Slot::material_animations, "BannerBG_AppQuit")};
    p.translate = p.loop.find(translate_path);
    if (!p.translate || p.translate->components.size() < 2) throw FormatError(file.name() + ": the Loop doesn't move the cells");
    for (std::size_t c = 0; c < 2; ++c)
        if (p.translate->components[c].kind != cgfx::Component::Kind::curve)
            throw FormatError(file.name() + ": the Loop's drift isn't a curve Kabegami can rewrite");
    return p;
}

// how a coordinator's translation moves the cells on the screen: a unit of
// its matrix's translation column t0 moves them a cell down, t1 a cell right
// (u runs up the screen, v left). the rows are t0 and t1, the columns
// Translate x and y.
using Jacobian = std::array<std::array<float, 2>, 2>;

Jacobian jacobian(std::uint32_t mode, cgfx::Vec2 scale, float rotate) {
    auto at = [&](cgfx::Vec2 t) {
        auto m = cgfx::coordinator_matrix(mode, scale, rotate, t);
        return std::array<float, 2>{m.at(0, 3), m.at(1, 3)};
    };
    auto origin = at({0, 0}), x = at({1, 0}), y = at({0, 1});
    return {{{x[0] - origin[0], y[0] - origin[0]}, {x[1] - origin[1], y[1] - origin[1]}}};
}

// cells right and down for a translation
std::array<float, 2> cells_of(const Jacobian& j, cgfx::Vec2 t) {
    return {j[1][0] * t.x + j[1][1] * t.y, j[0][0] * t.x + j[0][1] * t.y};
}

// the translation that moves the cells right and down as asked
cgfx::Vec2 translation_for(const Jacobian& j, std::array<float, 2> cells, const std::string& name) {
    auto det = j[0][0] * j[1][1] - j[0][1] * j[1][0];
    if (std::abs(det) < 1e-9f) throw FormatError(name + ": a layer of cells doesn't move with its translation");
    float down = cells[1], right = cells[0];
    return {(j[1][1] * down - j[0][1] * right) / det, (j[0][0] * right - j[1][0] * down) / det};
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// the Translate the Loop moves the moving layer by, from its start to its end
cgfx::Vec2 loop_motion(const cgfx::File& file, const Parts& p) {
    float moved[2]{};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        auto curve = p.translate->components[axis].curve;
        auto segments = cgfx::read_segments(file, curve);
        if (segments.empty()) continue;
        moved[axis] = cgfx::evaluate_curve(file, curve, segments.back().end) - cgfx::evaluate_curve(file, curve, segments.front().start);
    }
    return {moved[0], moved[1]};
}

// from where the Loop starts, the cells asked to its end, in a straight line
// the keys follow
void write_drift(cgfx::File& file, const Parts& p, std::array<int, 2> drift) {
    auto& c = p.material.coordinators[1];
    auto moved = translation_for(jacobian(c.mode, c.scale, c.rotate), {static_cast<float>(drift[0]), static_cast<float>(drift[1])}, file.name());
    float by_axis[2] = {moved.x, moved.y};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        auto curve = p.translate->components[axis].curve;
        auto segments = cgfx::read_segments(file, curve);
        if (segments.empty()) throw FormatError(file.name() + ": the Loop's drift has no keys");
        auto first = segments.front().start, last = segments.back().end;
        auto start = cgfx::evaluate_curve(file, curve, first);
        auto length = last - first;
        auto slope = length > 0 ? by_axis[axis] / length : 0.0f;
        for (auto& segment : segments) {
            if (segment.constant) throw FormatError(file.name() + ": the Loop's drift has a constant segment");
            auto keys = segment.keys;
            for (auto& key : keys) {
                key.value = start + slope * (segment.start + key.frame - first);
                key.in_slope = key.out_slope = slope;
            }
            cgfx::write_keys(file, segment, keys);
        }
    }
}

// a 64x64 picture a quarter turn round, the way the wall shows the texture
// (counter-clockwise) or the way that undoes it (clockwise)
image::Image turned(const image::Image& in, bool clockwise) {
    image::Image out(in.height, in.width);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            auto from = clockwise ? in.at(y, in.height - 1 - x) : in.at(in.width - 1 - y, x);
            std::copy_n(from, 4, out.at(x, y));
        }
    return out;
}

bool near(float a, float b) { return std::abs(a - b) <= 1e-6f * std::max(1.0f, std::abs(a)); }

}  // namespace

Look read_look(const cgfx::File& file) {
    auto p = parts_of(file);
    auto& c = p.material.colors;
    auto constant = [&](int n) { return c[static_cast<std::size_t>(5 + n)]; };
    Look look;
    look.background = {constant(3).x, constant(3).y, constant(3).z};
    look.highlight = constant(3).w;
    look.gradient = constant(2).w;
    look.squares = constant(1).w;
    look.second_layer = constant(5).w;

    // the layers: the scale's x counts the cells down (u), y right (v)
    auto& moving = p.material.coordinators[1];
    auto& still = p.material.coordinators[2];
    look.repeat = {moving.scale.y, moving.scale.x};
    look.still_repeat = {still.scale.y, still.scale.x};
    look.still_shift = cells_of(jacobian(still.mode, still.scale, still.rotate), still.translate);

    // the drift: the Loop's motion from its start to its end, in cells
    auto cells = cells_of(jacobian(moving.mode, moving.scale, moving.rotate), loop_motion(file, p));
    look.drift = {static_cast<int>(std::lround(cells[0])), static_cast<int>(std::lround(cells[1]))};
    look.loop_frames = p.loop.frames;

    auto data = std::span(file.bytes()).subspan(p.cell.data_at, p.cell.data_size);
    auto texels = pica::decode_texture(data, pica::TextureFormat::l8, cell_size, cell_size);
    image::Image texture(cell_size, cell_size);
    for (std::size_t i = 0; i < texels.size(); ++i)
        std::copy(texels[i].begin(), texels[i].end(), texture.rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
    look.cell = turned(texture, false);
    return look;
}

void apply(cgfx::File& file, const Look& look) {
    auto before = read_look(file);
    // the colours, each read again after the last was set, so the hash word
    // set_color keeps counts every one
    auto set = [&](int constant, auto change) {
        auto material = material_of(file);
        auto which = static_cast<cgfx::Color>(static_cast<int>(cgfx::Color::constant0) + constant);
        auto value = material.colors[static_cast<std::size_t>(which)];
        change(value);
        cgfx::set_color(file, material, which, value);
    };
    set(3, [&](cgfx::Vec4& v) {
        v = {clamp01(look.background[0]), clamp01(look.background[1]), clamp01(look.background[2]), clamp01(look.highlight)};
    });
    set(2, [&](cgfx::Vec4& v) { v.w = clamp01(look.gradient); });
    set(1, [&](cgfx::Vec4& v) { v.w = clamp01(look.squares); });
    set(5, [&](cgfx::Vec4& v) { v.w = clamp01(look.second_layer); });

    // the layers' repeats and the still one's place; what is as it was keeps
    // its bytes, as the stored matrices are the exporter's, not ours
    for (auto& r : {look.repeat, look.still_repeat})
        if (!(r[0] > 0) || !(r[1] > 0)) throw FormatError("a layer has to repeat its cell a positive number of times");
    bool rescaled = look.repeat != before.repeat;
    if (rescaled) {
        auto material = material_of(file);
        auto& c = material.coordinators[1];
        cgfx::set_coordinator(file, material, 1, {look.repeat[1], look.repeat[0]}, c.rotate, c.translate);
    }
    if (look.still_repeat != before.still_repeat || !near(look.still_shift[0], before.still_shift[0]) ||
        !near(look.still_shift[1], before.still_shift[1])) {
        auto material = material_of(file);
        auto& c = material.coordinators[2];
        cgfx::Vec2 scale{look.still_repeat[1], look.still_repeat[0]};
        auto t = translation_for(jacobian(c.mode, scale, c.rotate), look.still_shift, file.name());
        cgfx::set_coordinator(file, material, 2, scale, c.rotate, t);
    }

    // the loop's length, then the drift: a whole number of cells at the
    // moving layer's repeats, so it is written again when they change. a
    // drift as the file has it keeps the file's own keys
    if (!(look.loop_frames >= 1)) throw FormatError("the loop has to last a frame at least");
    bool stretched = look.loop_frames != before.loop_frames;
    if (stretched) cgfx::set_length(file, parts_of(file).loop, look.loop_frames);
    bool drifted = rescaled || look.drift != before.drift;
    if (drifted) write_drift(file, parts_of(file), look.drift);

    // AppQuit, which puts the moving layer back at the Loop's end and at its
    // repeats when an app closes, follows
    if (rescaled || stretched || drifted) {
        auto p = parts_of(file);
        if (p.quit) {
            if (auto m = p.quit->find(scale_path); m && m->components.size() >= 2) {
                auto& c = p.material.coordinators[1];
                float values[2] = {c.scale.x, c.scale.y};
                for (std::size_t k = 0; k < 2; ++k)
                    if (m->components[k].kind == cgfx::Component::Kind::constant) cgfx::set_constant(file, *m, k, values[k]);
            }
            if (auto m = p.quit->find(translate_path); m && m->components.size() >= 2) {
                for (std::size_t k = 0; k < 2; ++k) {
                    if (m->components[k].kind != cgfx::Component::Kind::constant) continue;
                    auto curve = p.translate->components[k].curve;
                    auto segments = cgfx::read_segments(file, curve);
                    if (!segments.empty()) cgfx::set_constant(file, *m, k, cgfx::evaluate_curve(file, curve, segments.back().end));
                }
            }
        }
    }

    if (look.cell) {
        auto texture = turned(make_cell(*look.cell), true);
        std::vector<pica::Rgba> rows(static_cast<std::size_t>(cell_size * cell_size));
        for (std::size_t i = 0; i < rows.size(); ++i)
            std::copy_n(texture.rgba.begin() + static_cast<std::ptrdiff_t>(i * 4), 4, rows[i].begin());
        auto data = pica::encode_texture(rows, pica::TextureFormat::l8, cell_size, cell_size);
        auto p = parts_of(file);
        Patcher patch(file.bytes());
        for (std::size_t i = 0; i < data.size() && i < p.cell.data_size; ++i) patch.u8(p.cell.data_at + i, data[i]);
    }
}

image::Image waves_cell() {
    // soft ribbons across the cell, each a gaussian around a sine with whole
    // periods, so the cell repeats without a seam either way
    struct Ribbon {
        double periods, amplitude, phase, centre, width, gain;
    };
    constexpr Ribbon ribbons[] = {{1, 9, 0.0, 26, 2.2, 1.0}, {2, 5, 1.3, 40, 1.6, 0.7}, {1, 6, 3.0, 50, 3.5, 0.45}};
    image::Image cell(cell_size, cell_size);
    for (int y = 0; y < cell_size; ++y) {
        for (int x = 0; x < cell_size; ++x) {
            double t = 2 * 3.14159265358979323846 * x / cell_size, v = 0;
            for (auto& r : ribbons) {
                double centre = r.centre + r.amplitude * std::sin(r.periods * t + r.phase);
                double d = std::remainder(y - centre, static_cast<double>(cell_size));
                v += r.gain * std::exp(-(d * d) / (2 * r.width * r.width));
            }
            auto p = cell.at(x, y);
            p[0] = p[1] = p[2] = static_cast<std::uint8_t>(std::lround(std::min(1.0, v) * 255));
            p[3] = 255;
        }
    }
    return cell;
}

Look waves() {
    Look look;
    look.background = {15 / 255.0f, 42 / 255.0f, 107 / 255.0f};
    look.highlight = 1;
    look.gradient = 0.2f;
    look.squares = 0.7f;
    look.second_layer = 0.45f;
    look.repeat = {1, 1};
    look.drift = {1, 0};
    look.still_repeat = {1, 1};
    look.still_shift = {0.11f, -0.37f};
    look.loop_frames = 1200;
    look.cell = waves_cell();
    return look;
}

image::Image make_cell(const image::Image& picture) {
    if (picture.width <= 0 || picture.height <= 0) throw FormatError("the cell picture is empty");
    image::Image cell(cell_size, cell_size);
    for (int y = 0; y < cell_size; ++y) {
        for (int x = 0; x < cell_size; ++x) {
            // the picture's pixels under this one, averaged, each over black
            int x0 = x * picture.width / cell_size, x1 = std::max(x0 + 1, (x + 1) * picture.width / cell_size);
            int y0 = y * picture.height / cell_size, y1 = std::max(y0 + 1, (y + 1) * picture.height / cell_size);
            double sum = 0;
            int n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    auto p = picture.at(sx, sy);
                    double a = p[3] / 255.0;
                    sum += (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) * a;
                    ++n;
                }
            }
            auto grey = static_cast<std::uint8_t>(std::clamp(std::lround(sum / n), 0L, 255L));
            auto out = cell.at(x, y);
            out[0] = out[1] = out[2] = grey;
            out[3] = 255;
        }
    }
    return cell;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t v = static_cast<std::uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) v |= bytes[i + 2];
        out += digits[(v >> 18) & 63];
        out += digits[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? digits[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? digits[v & 63] : '=';
    }
    return out;
}

Bytes unbase64(std::string_view text) {
    Bytes out;
    std::uint32_t v = 0;
    int bits = 0;
    for (char ch : text) {
        int d;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
        else if (ch == '+') d = 62;
        else if (ch == '/') d = 63;
        else if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
        else throw FormatError("the cell in the recipe isn't base64");
        v = v << 6 | static_cast<std::uint32_t>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(v >> bits));
        }
    }
    return out;
}

std::string write_recipe(const Recipe& recipe) {
    auto& l = recipe.look;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // every float as it is, so a recipe makes the same file again
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "kabegami-recipe 1\n";
    if (!recipe.original.empty()) out << "original " << recipe.original << "\n";
    out << "background " << l.background[0] << " " << l.background[1] << " " << l.background[2] << "\n";
    out << "highlight " << l.highlight << "\n";
    out << "gradient " << l.gradient << "\n";
    out << "squares " << l.squares << "\n";
    out << "second-layer " << l.second_layer << "\n";
    out << "repeat " << l.repeat[0] << " " << l.repeat[1] << "\n";
    out << "drift " << l.drift[0] << " " << l.drift[1] << "\n";
    out << "still-repeat " << l.still_repeat[0] << " " << l.still_repeat[1] << "\n";
    out << "still-shift " << l.still_shift[0] << " " << l.still_shift[1] << "\n";
    out << "loop " << l.loop_frames << "\n";
    if (l.cell) {
        auto png = image::encode_png(make_cell(*l.cell));
        out << "cell " << base64(png) << "\n";
    }
    return out.str();
}

Recipe read_recipe(std::string_view text) {
    Recipe recipe;
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string line;
    int number = 0;
    bool header = false;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        words.imbue(std::locale::classic());
        std::string key;
        words >> key;
        auto fail = [&](const std::string& why) {
            return FormatError("recipe line " + std::to_string(number) + ": " + why);
        };
        auto number_of = [&](float& out) {
            if (!(words >> out)) throw fail("\"" + key + "\" needs a number");
        };
        if (!header) {
            int version = 0;
            if (key != "kabegami-recipe" || !(words >> version)) throw fail("this isn't a Kabegami recipe");
            if (version != 1) throw fail("a recipe of a later Kabegami");
            header = true;
        } else if (key == "original") {
            words >> recipe.original;
        } else if (key == "background") {
            for (auto& c : recipe.look.background) number_of(c);
        } else if (key == "highlight") {
            number_of(recipe.look.highlight);
        } else if (key == "gradient") {
            number_of(recipe.look.gradient);
        } else if (key == "squares") {
            number_of(recipe.look.squares);
        } else if (key == "second-layer") {
            number_of(recipe.look.second_layer);
        } else if (key == "repeat") {
            for (auto& c : recipe.look.repeat) number_of(c);
        } else if (key == "still-repeat") {
            for (auto& c : recipe.look.still_repeat) number_of(c);
        } else if (key == "still-shift") {
            for (auto& c : recipe.look.still_shift) number_of(c);
        } else if (key == "loop") {
            number_of(recipe.look.loop_frames);
        } else if (key == "drift") {
            if (!(words >> recipe.look.drift[0] >> recipe.look.drift[1])) throw fail("\"drift\" needs two whole numbers");
        } else if (key == "cell") {
            std::string data;
            words >> data;
            recipe.look.cell = make_cell(image::decode(unbase64(data)));
        } else {
            throw fail("\"" + key + "\" is nothing Kabegami knows");
        }
    }
    if (!header) throw FormatError("the recipe is empty");
    return recipe;
}

}  // namespace kabegami::edit
