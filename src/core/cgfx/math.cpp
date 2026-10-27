#include "core/cgfx/math.hpp"

#include <cmath>
#include <numbers>

namespace kabegami::cgfx {

Matrix34 Matrix34::identity() {
    Matrix34 out;
    out.at(0, 0) = out.at(1, 1) = out.at(2, 2) = 1.0f;
    return out;
}

Matrix44 Matrix44::identity() {
    Matrix44 out;
    out.at(0, 0) = out.at(1, 1) = out.at(2, 2) = out.at(3, 3) = 1.0f;
    return out;
}

const std::array<std::array<float, 4>, 256>& sincos_table() {
    // entry i is sin and cos of 2πi/256 rounded to float, and the steps to
    // entry i + 1 taken in double and then rounded. three values a double
    // leaves at ±1e-16 are exact zeros in HOME's table
    static const auto table = [] {
        std::array<std::array<float, 4>, 256> t{};
        auto angle = [](int i) { return 2.0 * std::numbers::pi * i / 256.0; };
        auto sine = [&](int i) { return i % 256 == 128 ? 0.0 : std::sin(angle(i)); };
        auto cosine = [&](int i) { return i % 256 == 64 || i % 256 == 192 ? 0.0 : std::cos(angle(i)); };
        for (int i = 0; i < 256; ++i) {
            auto& e = t[static_cast<std::size_t>(i)];
            e[0] = static_cast<float>(sine(i));
            e[1] = static_cast<float>(cosine(i));
            e[2] = static_cast<float>(sine(i + 1) - sine(i));
            e[3] = static_cast<float>(cosine(i + 1) - cosine(i));
        }
        return t;
    }();
    return table;
}

SinCos sincos_fidx(float radians) {
    float fidx = radians * fidx_per_radian;
    float a = std::fabs(fidx);
    // past 65536 HOME first folds the angle, presumably with an fmod
    if (a >= 65536.0f) a = std::fmod(a, 65536.0f);
    auto whole = static_cast<std::uint32_t>(a);
    float frac = a - static_cast<float>(whole);
    auto& e = sincos_table()[whole & 0xFF];
    float step_sin = frac * e[2];
    float step_cos = frac * e[3];
    float s = e[0] + step_sin;
    float c = e[1] + step_cos;
    return {fidx < 0 ? -s : s, c};
}

Matrix34 multiply(const Matrix34& a, const Matrix34& b) {
    Matrix34 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = a.at(r, 0) * b.at(0, c) + a.at(r, 1) * b.at(1, c) + a.at(r, 2) * b.at(2, c);
            if (c == 3) v += a.at(r, 3);
            out.at(r, c) = v;
        }
    }
    return out;
}

Vec3 transform_point(const Matrix34& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}

Matrix34 srt_matrix(Vec3 scale, Vec3 rotation, Vec3 translation) {
    auto [sx, cx] = sincos_fidx(rotation.x);
    auto [sy, cy] = sincos_fidx(rotation.y);
    auto [sz, cz] = sincos_fidx(rotation.z);
    Matrix34 m;
    // Rz · Ry · Rx, then each column scaled
    m.at(0, 0) = cz * cy;
    m.at(0, 1) = cz * sy * sx - sz * cx;
    m.at(0, 2) = cz * sy * cx + sz * sx;
    m.at(1, 0) = sz * cy;
    m.at(1, 1) = sz * sy * sx + cz * cx;
    m.at(1, 2) = sz * sy * cx - cz * sx;
    m.at(2, 0) = -sy;
    m.at(2, 1) = cy * sx;
    m.at(2, 2) = cy * cx;
    for (int r = 0; r < 3; ++r) {
        m.at(r, 0) *= scale.x;
        m.at(r, 1) *= scale.y;
        m.at(r, 2) *= scale.z;
    }
    m.at(0, 3) = translation.x;
    m.at(1, 3) = translation.y;
    m.at(2, 3) = translation.z;
    return m;
}

}  // namespace kabegami::cgfx
