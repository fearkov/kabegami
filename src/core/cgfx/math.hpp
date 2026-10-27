// the maths NW4C does inside the HOME Menu, as it does it: matrices of three
// rows of four with column vectors and the translation in column 3, as the
// files keep them, and SinCosFIdx, the table sine every angle goes through.
// docs/research/camera-shader-nw4c.md §3.7.6 is where the table comes from.
#pragma once

#include <array>
#include <cstdint>

namespace kabegami::cgfx {

struct Vec2 {
    float x = 0, y = 0;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

// three rows of four: p' = M · (p, 1).
struct Matrix34 {
    std::array<float, 12> m{};
    float& at(int row, int column) { return m[static_cast<std::size_t>(row * 4 + column)]; }
    float at(int row, int column) const { return m[static_cast<std::size_t>(row * 4 + column)]; }
    static Matrix34 identity();
};

struct Matrix44 {
    std::array<float, 16> m{};
    float& at(int row, int column) { return m[static_cast<std::size_t>(row * 4 + column)]; }
    float at(int row, int column) const { return m[static_cast<std::size_t>(row * 4 + column)]; }
    static Matrix44 identity();
};

// 256 / 2π as HOME has it, the bits 0x4222F983: radians times this index the
// table.
constexpr float fidx_per_radian = 40.743664f;

struct SinCos {
    float sin, cos;
};

// HOME's SinCosFIdx: the table entry of the angle's whole part, plus its
// fraction times the step to the next, each product rounded before the sum.
SinCos sincos_fidx(float radians);

// the table: per entry sin, cos, and the steps to the next entry's sin and
// cos. built by the rule that reproduces HOME's bit for bit.
const std::array<std::array<float, 4>, 256>& sincos_table();

Matrix34 multiply(const Matrix34& a, const Matrix34& b);
Vec3 transform_point(const Matrix34& m, Vec3 p);

// T · Rz · Ry · Rx · S, the local matrix of a bone or a node, X turned first.
Matrix34 srt_matrix(Vec3 scale, Vec3 rotation, Vec3 translation);

}  // namespace kabegami::cgfx
