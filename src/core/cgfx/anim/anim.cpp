#include "core/cgfx/anim/anim.hpp"

#include <algorithm>
#include <cmath>

namespace kabegami::cgfx {
namespace {

// how many components a member type has, and where the flags keep each
// component's "constant" and "not animated" bits.
struct Layout {
    std::size_t count;
    int constant_bit;
    int absent_bit;  // -1: the type has none
};

Layout layout_of(MemberType type) {
    switch (type) {
        case MemberType::float_: return {1, 0, -1};
        case MemberType::integer: return {1, 0, -1};
        case MemberType::boolean: return {1, 0, -1};
        case MemberType::vector2: return {2, 0, 2};
        case MemberType::vector3: return {3, 0, 3};
        case MemberType::transform: return {10, 6, 16};
        case MemberType::rgba: return {4, 0, 4};
        case MemberType::texture: return {1, 0, -1};
        default: return {0, 0, -1};
    }
}

struct Keys {
    std::vector<Key> keys;
    std::vector<float> search;  // what the key search compares t with
    bool integer_frames = false;
    bool sixteenths = false;  // UnifiedHermite48's frames, in 1/32
};

std::int32_t sign_extend(std::uint32_t v, int bits) {
    auto shift = 32 - bits;
    return static_cast<std::int32_t>(v << shift) >> shift;
}

// the keys of a keyed segment, decoded to floats, frames relative.
Keys read_keys(const Reader& r, std::size_t s, std::uint32_t format) {
    Keys out;
    auto count = r.u16(s + 0x0C);
    bool quantized = format == 1 || format == 2 || format == 4 || format == 5 || format == 7;
    float scale = quantized ? r.f32(s + 0x14) : 0, offset = quantized ? r.f32(s + 0x18) : 0;
    auto at = s + (quantized ? 0x1C : 0x14);
    out.integer_frames = format == 1 || format == 2 || format == 5 || format == 7;
    out.sixteenths = format == 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        Key k{};
        switch (format) {
            case 0:  // Hermite128
                k = {r.f32(at), r.f32(at + 4), r.f32(at + 8), r.f32(at + 12)};
                at += 16;
                break;
            case 1: {  // Hermite64
                auto word = r.u32(at);
                k.frame = static_cast<float>(word & 0xFFF);
                k.value = offset + static_cast<float>(word >> 12) * scale;
                k.in_slope = static_cast<float>(static_cast<std::int16_t>(r.u16(at + 4))) * (1.0f / 256);
                k.out_slope = static_cast<float>(static_cast<std::int16_t>(r.u16(at + 6))) * (1.0f / 256);
                at += 8;
                break;
            }
            case 2: {  // Hermite48
                k.frame = static_cast<float>(r.u8(at));
                k.value = offset + static_cast<float>(r.u16(at + 1)) * scale;
                std::uint32_t slopes = r.u8(at + 3) | std::uint32_t{r.u8(at + 4)} << 8 | std::uint32_t{r.u8(at + 5)} << 16;
                k.in_slope = static_cast<float>(sign_extend(slopes & 0xFFF, 12)) * (1.0f / 32);
                k.out_slope = static_cast<float>(sign_extend(slopes >> 12, 12)) * (1.0f / 32);
                at += 6;
                break;
            }
            case 3:  // UnifiedHermite96
                k = {r.f32(at), r.f32(at + 4), r.f32(at + 8), r.f32(at + 8)};
                at += 12;
                break;
            case 4: {  // UnifiedHermite48
                k.frame = static_cast<float>(r.u16(at)) * (1.0f / 32);
                k.value = offset + static_cast<float>(r.u16(at + 2)) * scale;
                k.in_slope = k.out_slope = static_cast<float>(static_cast<std::int16_t>(r.u16(at + 4))) * (1.0f / 256);
                at += 6;
                break;
            }
            case 5: {  // UnifiedHermite32
                k.frame = static_cast<float>(r.u8(at));
                std::uint32_t rest = r.u8(at + 1) | std::uint32_t{r.u8(at + 2)} << 8 | std::uint32_t{r.u8(at + 3)} << 16;
                k.value = offset + static_cast<float>(rest & 0xFFF) * scale;
                k.in_slope = k.out_slope = static_cast<float>(sign_extend(rest >> 12, 12)) * (1.0f / 32);
                at += 4;
                break;
            }
            case 6:  // StepLinear64
                k = {r.f32(at), r.f32(at + 4)};
                at += 8;
                break;
            case 7: {  // StepLinear32
                auto word = r.u32(at);
                k.frame = static_cast<float>(word & 0xFFF);
                k.value = offset + static_cast<float>(word >> 12) * scale;
                at += 4;
                break;
            }
        }
        out.keys.push_back(k);
    }
    if (out.keys.empty()) throw FormatError(r.what() + ": a keyed segment at " + Reader::hex(s) + " has no keys");
    return out;
}

float herp(float v0, float s0, float v1, float s1, float x, float d) {
    float w = x / d;
    float wm = w - 1.0f;
    return v0 + (v0 - v1) * (2.0f * w - 3.0f) * (w * w) + x * wm * (wm * s0 + w * s1);
}

float evaluate_segment(const Reader& r, std::size_t s, float f) {
    float start = r.f32(s);
    auto flags = r.u32(s + 0x08);
    f = std::max(f, start);
    if (flags & 1) return r.f32(s + 0x0C);
    float t = f - start;
    auto interpolation = (flags >> 2) & 7;
    auto format = (flags >> 5) & 7;
    auto decoded = read_keys(r, s, format);
    auto& keys = decoded.keys;
    // the key search compares integer frames with t truncated, and
    // UnifiedHermite48's 1/32 frames with t · 32 truncated
    auto place = [&](float frame) {
        if (decoded.integer_frames) return static_cast<double>(frame);
        if (decoded.sixteenths) return static_cast<double>(static_cast<std::int32_t>(frame * 32));
        return static_cast<double>(frame);
    };
    double at = decoded.integer_frames ? static_cast<double>(static_cast<std::uint32_t>(std::max(t, 0.0f)))
                : decoded.sixteenths ? static_cast<double>(static_cast<std::int32_t>(t * 32))
                                     : static_cast<double>(t);
    std::size_t k = 0;
    if (at <= place(keys.front().frame)) {
        k = 0;
    } else if (at >= place(keys.back().frame)) {
        k = keys.size() - 1;
    } else {
        while (k + 1 < keys.size() && place(keys[k + 1].frame) <= at) ++k;
    }
    auto& key = keys[k];
    if (key.frame == t || interpolation == 0 || k + 1 >= keys.size()) return key.value;
    auto& next = keys[k + 1];
    if (interpolation == 1) {
        float w = (t - key.frame) / (next.frame - key.frame);
        return key.value * (1.0f - w) + next.value * w;
    }
    return herp(key.value, key.out_slope, next.value, next.in_slope, t - key.frame, next.frame - key.frame);
}

// a frame outside a curve's span mapped in by its pre or post repeat method.
float repeat(float start, float end, std::uint8_t pre, std::uint8_t post, float f) {
    std::uint8_t method;
    if (f < start)
        method = pre;
    else if (f >= end)
        method = post;
    else
        return f;
    float len = end - start;
    switch (method) {
        case 1:
        case 3:  // Repeat and RelativeRepeat map frames the same way
            if (len <= 0) return start;
            return f - std::floor((f - start) / len) * len;
        case 2: {  // MirroredRepeat
            if (len <= 0) return start;
            float n = std::floor((f - start) / len);
            float g = f - n * len;
            if (static_cast<std::int64_t>(n) % 2 != 0) g = (start + end) - g;
            return g;
        }
        default:  // None
            return f < start ? start : std::min(f, end);
    }
}

}  // namespace

const Member* Animation::find(std::string_view path) const {
    for (auto& m : members)
        if (m.path == path) return &m;
    return nullptr;
}

Animation read_animation(const File& file, std::size_t at) {
    auto r = file.reader();
    if (!r.magic(at, "CANM")) throw FormatError(file.name() + ": no animation at " + Reader::hex(at));
    Animation a;
    a.at = at;
    a.name = string_at(r, at + 0x08);
    a.group = string_at(r, at + 0x0C);
    a.loop = (r.u32(at + 0x10) & 0xFF) != 0;
    a.frames = r.f32(at + 0x14);
    auto members = dict_at(r, at + 0x18);
    for (std::size_t i = 0; i < members.size(); ++i) {
        auto m = members.value(i);
        Member member;
        member.at = m;
        member.flags = r.u32(m);
        member.path = string_at(r, m + 0x04);
        member.object = string_at(r, m + 0x08);
        member.member = string_at(r, m + 0x0C);
        member.type = static_cast<MemberType>(r.u32(m + 0x10));
        auto layout = layout_of(member.type);
        for (std::size_t c = 0; c < layout.count; ++c) {
            auto slot = m + 0x14 + 4 * c;
            Component component;
            bool absent = layout.absent_bit >= 0 && (member.flags >> (layout.absent_bit + static_cast<int>(c)) & 1);
            bool constant = member.flags >> (layout.constant_bit + static_cast<int>(c)) & 1;
            if (absent) {
                component.kind = Component::Kind::absent;
            } else if (constant) {
                component.kind = Component::Kind::constant;
                component.value = member.type == MemberType::integer ? static_cast<float>(r.s32(slot)) : r.f32(slot);
            } else {
                component.kind = Component::Kind::curve;
                component.curve = r.relative(slot);
                if (component.curve == 0) component.kind = Component::Kind::absent;
            }
            member.components.push_back(component);
        }
        if (member.type == MemberType::texture) {
            for (auto reference : object_list(r, m + 0x18)) member.textures.push_back(string_at(r, reference + 0x18));
        }
        a.members.push_back(std::move(member));
    }
    return a;
}

std::optional<Animation> find_animation(const File& file, Slot slot, std::string_view name) {
    auto at = file.dict(slot).value_of(name);
    if (!at) return std::nullopt;
    return read_animation(file, *at);
}

float evaluate_curve(const File& file, std::size_t c, float frame) {
    auto r = file.reader();
    float start = r.f32(c), end = r.f32(c + 4);
    frame = repeat(start, end, r.u8(c + 8), r.u8(c + 9), frame);
    auto flags = r.u32(c + 0x0C);
    if (flags & 1) return evaluate_curve(file, r.relative(c + 0x10), frame) + evaluate_curve(file, r.relative(c + 0x14), frame);
    if (flags & 2) return r.f32(c + 0x10);
    std::size_t segment;
    if (flags & 4) {
        segment = r.relative(c + 0x14);
    } else {
        auto count = r.u32(c + 0x10);
        if (count == 0) throw FormatError(file.name() + ": a curve at " + Reader::hex(c) + " has no segments");
        segment = r.relative(c + 0x14 + 4 * (count - 1));
        for (std::uint32_t i = 0; i < count; ++i) {
            auto s = r.relative(c + 0x14 + 4 * i);
            if (r.f32(s + 4) > frame) {
                segment = s;
                break;
            }
        }
    }
    return evaluate_segment(r, segment, frame);
}

Value evaluate(const File& file, const Member& member, float frame) {
    Value out;
    for (std::size_t c = 0; c < member.components.size(); ++c) {
        auto& component = member.components[c];
        if (component.kind == Component::Kind::absent) continue;
        out.values[c] = component.kind == Component::Kind::constant ? component.value : evaluate_curve(file, component.curve, frame);
        out.written |= 1u << c;
    }
    if (member.type == MemberType::texture && (out.written & 1)) {
        // HOME truncates toward zero and ignores what is not in the list
        auto index = static_cast<std::int32_t>(out.values[0]);
        out.written = 0;
        if (index >= 0 && static_cast<std::size_t>(index) < member.textures.size()) {
            out.texture = member.textures[static_cast<std::size_t>(index)];
            out.written = 1;
        }
    }
    return out;
}

float FrameController::place(float f) const {
    if (!loop_) return std::clamp(f, start_, end_);
    float len = end_ - start_;
    if (len == 0) return start_;
    float o = f - start_;
    if (o < 0) o += (std::floor(-o / len) + 1.0f) * len;
    return start_ + std::fmod(o, len);
}

std::vector<Segment> read_segments(const File& file, std::size_t c) {
    auto r = file.reader();
    auto flags = r.u32(c + 0x0C);
    std::vector<std::size_t> at;
    if (flags & 3) return {};  // composite or constant curves have no segments of their own
    if (flags & 4) {
        at.push_back(r.relative(c + 0x14));
    } else {
        for (std::uint32_t i = 0; i < r.u32(c + 0x10); ++i) at.push_back(r.relative(c + 0x14 + 4 * i));
    }
    std::vector<Segment> out;
    for (auto s : at) {
        Segment segment;
        segment.at = s;
        segment.start = r.f32(s);
        segment.end = r.f32(s + 4);
        auto sflags = r.u32(s + 0x08);
        segment.constant = sflags & 1;
        segment.interpolation = (sflags >> 2) & 7;
        segment.format = (sflags >> 5) & 7;
        if (segment.constant)
            segment.keys.push_back({0, r.f32(s + 0x0C)});
        else
            segment.keys = read_keys(r, s, segment.format).keys;
        out.push_back(std::move(segment));
    }
    return out;
}

void write_keys(File& file, const Segment& segment, const std::vector<Key>& keys) {
    if (segment.constant) throw FormatError("a constant segment has no keys to write");
    if (keys.size() != segment.keys.size()) throw FormatError("a segment's keys can only be rewritten as many as it has");
    if (keys.front().frame != 0 || keys.back().frame != segment.end - segment.start)
        throw FormatError("the first key has to stay at 0 and the last at the segment's length");
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (keys[i].frame < keys[i - 1].frame) throw FormatError("keys have to stay in order");
    std::size_t size;
    switch (segment.format) {
        case 0: size = 16; break;
        case 3: size = 12; break;
        case 6: size = 8; break;
        default: throw FormatError("Kabegami only rewrites keys stored as floats");
    }
    Patcher p(file.bytes());
    p.f32(segment.at + 0x10, 1.0f / (segment.end - segment.start));
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto at = segment.at + 0x14 + size * i;
        p.f32(at, keys[i].frame);
        p.f32(at + 4, keys[i].value);
        if (segment.format == 0) {
            p.f32(at + 8, keys[i].in_slope);
            p.f32(at + 12, keys[i].out_slope);
        } else if (segment.format == 3) {
            p.f32(at + 8, keys[i].in_slope);
        }
    }
}

void set_constant(File& file, const Member& member, std::size_t component, float value) {
    if (component >= member.components.size() || member.components[component].kind != Component::Kind::constant)
        throw FormatError("only a constant component of " + member.path + " can be set");
    Patcher p(file.bytes());
    if (member.type == MemberType::integer)
        p.u32(member.at + 0x14 + 4 * component, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
    else
        p.f32(member.at + 0x14 + 4 * component, value);
}

namespace {

void stretch_curve(File& file, std::size_t curve, float k) {
    auto r = file.reader();
    Patcher p(file.bytes());
    p.f32(curve, r.f32(curve) * k);
    p.f32(curve + 4, r.f32(curve + 4) * k);
    auto flags = r.u32(curve + 0x0C);
    if (flags & 1) {
        stretch_curve(file, r.relative(curve + 0x10), k);
        stretch_curve(file, r.relative(curve + 0x14), k);
        return;
    }
    for (auto& segment : read_segments(file, curve)) {
        float start = segment.start * k, end = segment.end * k;
        p.f32(segment.at, start);
        p.f32(segment.at + 4, end);
        if (segment.constant) continue;
        std::size_t size;
        switch (segment.format) {
            case 0: size = 16; break;
            case 3: size = 12; break;
            case 6: size = 8; break;
            default: throw FormatError("Kabegami only stretches keys stored as floats");
        }
        p.f32(segment.at + 0x10, end > start ? 1.0f / (end - start) : 0.0f);
        for (std::size_t i = 0; i < segment.keys.size(); ++i) {
            auto at = segment.at + 0x14 + size * i;
            auto& key = segment.keys[i];
            p.f32(at, key.frame * k);
            // a slope is a change a frame, and there are k times as many frames
            if (segment.format == 0) {
                p.f32(at + 8, key.in_slope / k);
                p.f32(at + 12, key.out_slope / k);
            } else if (segment.format == 3) {
                p.f32(at + 8, key.in_slope / k);
            }
        }
    }
}

}  // namespace

void set_length(File& file, const Animation& animation, float frames) {
    if (!(frames > 0) || !(animation.frames > 0)) throw FormatError("an animation has to last some frames");
    float k = frames / animation.frames;
    Patcher(file.bytes()).f32(animation.at + 0x14, frames);
    for (auto& member : animation.members)
        for (auto& component : member.components)
            if (component.kind == Component::Kind::curve) stretch_curve(file, component.curve, k);
}

}  // namespace kabegami::cgfx
