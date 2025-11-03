// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>
#include <algorithm>

namespace Nomad {

// =============================================================================
// Vector2 - 2D Vector
// =============================================================================
struct Vector2 {
    float x, y;

    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return Vector2(x / scalar, y / scalar); }

    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
    Vector2& operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }
    Vector2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    Vector2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }

    float dot(const Vector2& other) const { return x * other.x + y * other.y; }
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }
    Vector2 normalized() const { float len = length(); return len > 0 ? *this / len : Vector2(); }
    void normalize() { float len = length(); if (len > 0) { x /= len; y /= len; } }
};

// =============================================================================
// Vector3 - 3D Vector
// =============================================================================
struct Vector3 {
    float x, y, z;

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
    Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
    Vector3 operator*(float scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
    Vector3 operator/(float scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }

    Vector3& operator+=(const Vector3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vector3& operator-=(const Vector3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vector3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vector3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

    float dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
    Vector3 cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }
    Vector3 normalized() const { float len = length(); return len > 0 ? *this / len : Vector3(); }
    void normalize() { float len = length(); if (len > 0) { x /= len; y /= len; z /= len; } }
};

// =============================================================================
// Vector4 - 4D Vector
// =============================================================================
struct Vector4 {
    float x, y, z, w;

    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Vector4 operator+(const Vector4& other) const { return Vector4(x + other.x, y + other.y, z + other.z, w + other.w); }
    Vector4 operator-(const Vector4& other) const { return Vector4(x - other.x, y - other.y, z - other.z, w - other.w); }
    Vector4 operator*(float scalar) const { return Vector4(x * scalar, y * scalar, z * scalar, w * scalar); }
    Vector4 operator/(float scalar) const { return Vector4(x / scalar, y / scalar, z / scalar, w / scalar); }

    Vector4& operator+=(const Vector4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    Vector4& operator-=(const Vector4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
    Vector4& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this; }
    Vector4& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; w /= scalar; return *this; }

    float dot(const Vector4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float lengthSquared() const { return x * x + y * y + z * z + w * w; }
    Vector4 normalized() const { float len = length(); return len > 0 ? *this / len : Vector4(); }
    void normalize() { float len = length(); if (len > 0) { x /= len; y /= len; z /= len; w /= len; } }
};

// =============================================================================
// Matrix4x4 - 4x4 Matrix (Column-Major)
// =============================================================================
struct Matrix4x4 {
    float m[16]; // Column-major order

    Matrix4x4() {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    }

    static Matrix4x4 identity() {
        Matrix4x4 mat;
        mat.m[0] = mat.m[5] = mat.m[10] = mat.m[15] = 1.0f;
        return mat;
    }

    static Matrix4x4 translation(float x, float y, float z) {
        Matrix4x4 mat = identity();
        mat.m[12] = x;
        mat.m[13] = y;
        mat.m[14] = z;
        return mat;
    }

    static Matrix4x4 scale(float x, float y, float z) {
        Matrix4x4 mat = identity();
        mat.m[0] = x;
        mat.m[5] = y;
        mat.m[10] = z;
        return mat;
    }

    static Matrix4x4 rotationX(float angle) {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[5] = c;
        mat.m[6] = s;
        mat.m[9] = -s;
        mat.m[10] = c;
        return mat;
    }

    static Matrix4x4 rotationY(float angle) {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[2] = -s;
        mat.m[8] = s;
        mat.m[10] = c;
        return mat;
    }

    static Matrix4x4 rotationZ(float angle) {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[1] = s;
        mat.m[4] = -s;
        mat.m[5] = c;
        return mat;
    }

    Matrix4x4 operator*(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                result.m[col * 4 + row] = 
                    m[0 * 4 + row] * other.m[col * 4 + 0] +
                    m[1 * 4 + row] * other.m[col * 4 + 1] +
                    m[2 * 4 + row] * other.m[col * 4 + 2] +
                    m[3 * 4 + row] * other.m[col * 4 + 3];
            }
        }
        return result;
    }

    Vector4 operator*(const Vector4& v) const {
        return Vector4(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
        );
    }
};

// =============================================================================
// DSP Math Functions
// =============================================================================

// Linear interpolation
inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Clamp value between min and max
inline float clamp(float value, float min, float max) {
    return std::max(min, std::min(value, max));
}

// Smooth Hermite interpolation
inline float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Map value from one range to another
inline float map(float value, float inMin, float inMax, float outMin, float outMax) {
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

// Decibels to linear gain
inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Linear gain to decibels
inline float gainToDb(float gain) {
    return 20.0f * std::log10(gain);
}

} // namespace Nomad
