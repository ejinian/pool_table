#ifndef VEC3_HPP
#define VEC3_HPP

#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-8f ? a * (1.f / l) : Vec3{};
}

// 3x3 rotation matrix (column-major)
struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    Vec3 col(int i) const { return {m[i * 3], m[i * 3 + 1], m[i * 3 + 2]}; }
    void setCol(int i, Vec3 v) {
        m[i * 3] = v.x;
        m[i * 3 + 1] = v.y;
        m[i * 3 + 2] = v.z;
    }
};

inline Vec3 operator*(const Mat3& M, Vec3 v) {
    return M.col(0) * v.x + M.col(1) * v.y + M.col(2) * v.z;
}

inline Mat3 operator*(const Mat3& A, const Mat3& B) {
    Mat3 R;
    for (int c = 0; c < 3; c++) R.setCol(c, A * B.col(c));
    return R;
}

inline void orthonormalize(Mat3& M) {
    Vec3 x = normalize(M.col(0));
    Vec3 y = normalize(M.col(1) - x * dot(x, M.col(1)));
    Vec3 z = cross(x, y);
    M.setCol(0, x);
    M.setCol(1, y);
    M.setCol(2, z);
}

#endif