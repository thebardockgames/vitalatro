#pragma once
#include <cmath>
#include <cstdint>

namespace vl {

constexpr double PI = 3.14159265358979323846;  // math.pi

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 &operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
    // Same convention as love::Vector2::getNormal(scale).
    Vec2 normal(float s) const { return {-y * s, x * s}; }
    static float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    static float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    uint32_t packed() const;  // RGBA8 in memory order r,g,b,a
};

inline uint32_t Color::packed() const {
    // love::toColor32 clamps then rounds: (uint8)(c*255 + 0.5)
    auto u8 = [](float v) -> uint32_t {
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        return (uint32_t)(v * 255.0f + 0.5f);
    };
    return u8(r) | (u8(g) << 8) | (u8(b) << 16) | (u8(a) << 24);
}

// 2D affine transform with the same composition rules as love::Matrix4 restricted to 2D.
//   x' = a*x + c*y + tx
//   y' = b*x + d*y + ty
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Vec2 apply(float x, float y) const { return {a * x + c * y + tx, b * x + d * y + ty}; }

    // this = this * o
    Affine operator*(const Affine &o) const {
        Affine r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    void translate(float x, float y) {
        tx += a * x + c * y;
        ty += b * x + d * y;
    }
    void scale(float sx, float sy) {
        a *= sx; b *= sx;
        c *= sy; d *= sy;
    }
    void rotate(float r) {
        float cs = std::cos(r), sn = std::sin(r);
        float na = a * cs + c * sn, nb = b * cs + d * sn;
        float nc = -a * sn + c * cs, nd = -b * sn + d * cs;
        a = na; b = nb; c = nc; d = nd;
    }
    // love::Matrix4::setTransformation(x, y, angle, sx, sy, ox, oy) with no shear.
    static Affine from_transformation(float x, float y, float r, float sx, float sy, float ox, float oy) {
        float cs = std::cos(r), sn = std::sin(r);
        Affine m;
        m.a = cs * sx;
        m.b = sn * sx;
        m.c = -sn * sy;
        m.d = cs * sy;
        m.tx = x - ox * m.a - oy * m.c;
        m.ty = y - ox * m.b - oy * m.d;
        return m;
    }
};

}  // namespace vl
