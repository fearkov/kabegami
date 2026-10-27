// CGFX animations (CANM, revision 5) and how the HOME Menu plays them: the
// members and their curves, segments and keys, and the evaluator and frame
// controller as HOME's own code runs them. docs/research/cgfx-animation.md is
// what this follows.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/cgfx/cgfx.hpp"

namespace kabegami::cgfx {

enum class MemberType : std::uint32_t {
    float_ = 0,
    integer = 1,
    boolean = 2,
    vector2 = 3,
    vector3 = 4,
    transform = 5,  // ScaleX/Y/Z, RotateX/Y/Z, RotateW, TranslateX/Y/Z
    rgba = 6,
    texture = 7,
    quat_transform = 8,
    matrix_transform = 9,
};

// one component of a member: missing (the target keeps its own value), a
// constant, or a curve.
struct Component {
    enum class Kind { absent, constant, curve } kind = Kind::absent;
    float value = 0;
    std::size_t curve = 0;  // where the curve is, for Kind::curve
};

struct Member {
    std::size_t at = 0;
    std::uint32_t flags = 0;
    std::string path;    // what HOME binds by
    std::string object;  // the material or bone
    std::string member;  // inside it, "" for a bone
    MemberType type = MemberType::float_;
    std::vector<Component> components;
    std::vector<std::string> textures;  // a Texture member's list, by texture name
};

struct Animation {
    std::size_t at = 0;
    std::string name;
    std::string group;  // the animation group it is meant for
    bool loop = false;
    float frames = 0;
    std::vector<Member> members;

    const Member* find(std::string_view path) const;
};

Animation read_animation(const File& file, std::size_t at);
std::optional<Animation> find_animation(const File& file, Slot slot, std::string_view name);

// a float curve at a frame, as HOME evaluates it.
float evaluate_curve(const File& file, std::size_t curve, float frame);

struct Value {
    std::uint32_t written = 0;  // bit i: component i was written
    std::array<float, 10> values{};
    std::optional<std::string> texture;  // for a Texture member, the texture it binds
};

// what a member writes at a frame. components it does not write keep the
// target's own values.
Value evaluate(const File& file, const Member& member, float frame);

// HOME's frame controller: the frame moves by the step each update, wrapped
// for a looping animation and held at the ends for a one-time one. members
// are evaluated at the frame as it is, never rounded, so a 600-frame loop
// runs 0 to 599.
class FrameController {
public:
    FrameController(float frames, bool loop) : end_(frames), loop_(loop) {}
    float frame() const { return frame_; }
    void set_frame(float f) { frame_ = place(f); }
    void set_step(float step) { step_ = step; }
    void update() {
        if (step_ != 0) frame_ = place(frame_ + step_);
    }
    float place(float f) const;

private:
    float frame_ = 0, step_ = 1, start_ = 0, end_;
    bool loop_;
};

// a keyed segment's keys, for edits that keep the file's layout.
struct Key {
    float frame;
    float value;
    float in_slope = 0, out_slope = 0;
};
struct Segment {
    std::size_t at = 0;
    float start = 0, end = 0;
    bool constant = false;
    std::uint32_t interpolation = 0;  // 0 step, 1 linear, 2 hermite
    std::uint32_t format = 0;         // the key format, 0-7
    std::vector<Key> keys;            // frames relative to the segment's start
};
// the segments of a curve, in order.
std::vector<Segment> read_segments(const File& file, std::size_t curve);
// writes new keys over a keyed segment of format 0 (Hermite128), 3
// (UnifiedHermite96) or 6 (StepLinear64), the same number of them, and its
// 1 / (end − start). the first key has to stay at 0 and the last at end − start.
void write_keys(File& file, const Segment& segment, const std::vector<Key>& keys);

// a constant component of a member, written in place.
void set_constant(File& file, const Member& member, std::size_t component, float value);

// the animation made `frames` long: its frame count, and every curve's start,
// end, segments, key frames and slopes scaled to fit, so it plays the same
// motion over a different time. throws for key formats stored as integers.
void set_length(File& file, const Animation& animation, float frames);

}  // namespace kabegami::cgfx
