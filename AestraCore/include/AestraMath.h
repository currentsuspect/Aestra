// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Aestra {

// =============================================================================
// Constants
// =============================================================================

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float LN10_OVER_20 = 0.11512925464970228420089957273422f;
constexpr float DB_FLOOR = -90.0f; // Below this, gain is treated as 0

// =============================================================================
// Utility: approximate float equality
// =============================================================================

[[nodiscard]] constexpr bool approxEqual(float a, float b, float epsilon = 1e-6f) noexcept {
    float diff = (a > b) ? (a - b) : (b - a);
    return diff < epsilon;
}

// =============================================================================
// Vector2 - 2D Vector
// =============================================================================
struct Vector2 {
    float x, y;

    constexpr Vector2() noexcept : x(0.0f), y(0.0f) {}
    constexpr Vector2(float x, float y) noexcept : x(x), y(y) {}

    static constexpr Vector2 zero() noexcept { return Vector2(0.0f, 0.0f); }
    static constexpr Vector2 one() noexcept { return Vector2(1.0f, 1.0f); }
    static constexpr Vector2 unitX() noexcept { return Vector2(1.0f, 0.0f); }
    static constexpr Vector2 unitY() noexcept { return Vector2(0.0f, 1.0f); }

    [[nodiscard]] constexpr Vector2 operator+(const Vector2& other) const noexcept {
        return Vector2(x + other.x, y + other.y);
    }
    [[nodiscard]] constexpr Vector2 operator-(const Vector2& other) const noexcept {
        return Vector2(x - other.x, y - other.y);
    }
    [[nodiscard]] constexpr Vector2 operator*(float scalar) const noexcept {
        return Vector2(x * scalar, y * scalar);
    }
    [[nodiscard]] constexpr Vector2 operator/(float scalar) const noexcept {
        return Vector2(x / scalar, y / scalar);
    }

    constexpr Vector2& operator+=(const Vector2& other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }
    constexpr Vector2& operator-=(const Vector2& other) noexcept {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    constexpr Vector2& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    constexpr Vector2& operator/=(float scalar) noexcept {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Vector2& other) const noexcept {
        return approxEqual(x, other.x) && approxEqual(y, other.y);
    }
    [[nodiscard]] constexpr bool operator!=(const Vector2& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] constexpr float dot(const Vector2& other) const noexcept {
        return x * other.x + y * other.y;
    }
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y); }
    [[nodiscard]] constexpr float lengthSquared() const noexcept { return x * x + y * y; }
    [[nodiscard]] Vector2 normalized() const noexcept {
        float len = length();
        return len > 0 ? *this / len : Vector2();
    }
    void normalize() noexcept {
        float len = length();
        if (len > 0) {
            x /= len;
            y /= len;
        }
    }
};

[[nodiscard]] constexpr Vector2 operator*(float scalar, const Vector2& v) noexcept {
    return Vector2(scalar * v.x, scalar * v.y);
}

// =============================================================================
// Vector3 - 3D Vector
// =============================================================================
struct Vector3 {
    float x, y, z;

    constexpr Vector3() noexcept : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Vector3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}

    static constexpr Vector3 zero() noexcept { return Vector3(0.0f, 0.0f, 0.0f); }
    static constexpr Vector3 one() noexcept { return Vector3(1.0f, 1.0f, 1.0f); }
    static constexpr Vector3 unitX() noexcept { return Vector3(1.0f, 0.0f, 0.0f); }
    static constexpr Vector3 unitY() noexcept { return Vector3(0.0f, 1.0f, 0.0f); }
    static constexpr Vector3 unitZ() noexcept { return Vector3(0.0f, 0.0f, 1.0f); }

    [[nodiscard]] constexpr Vector3 operator+(const Vector3& other) const noexcept {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }
    [[nodiscard]] constexpr Vector3 operator-(const Vector3& other) const noexcept {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }
    [[nodiscard]] constexpr Vector3 operator*(float scalar) const noexcept {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }
    [[nodiscard]] constexpr Vector3 operator/(float scalar) const noexcept {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    constexpr Vector3& operator+=(const Vector3& other) noexcept {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    constexpr Vector3& operator-=(const Vector3& other) noexcept {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    constexpr Vector3& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
    constexpr Vector3& operator/=(float scalar) noexcept {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Vector3& other) const noexcept {
        return approxEqual(x, other.x) && approxEqual(y, other.y) && approxEqual(z, other.z);
    }
    [[nodiscard]] constexpr bool operator!=(const Vector3& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] constexpr float dot(const Vector3& other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }
    [[nodiscard]] constexpr Vector3 cross(const Vector3& other) const noexcept {
        return Vector3(y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x);
    }
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] constexpr float lengthSquared() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] Vector3 normalized() const noexcept {
        float len = length();
        return len > 0 ? *this / len : Vector3();
    }
    void normalize() noexcept {
        float len = length();
        if (len > 0) {
            x /= len;
            y /= len;
            z /= len;
        }
    }
};

[[nodiscard]] constexpr Vector3 operator*(float scalar, const Vector3& v) noexcept {
    return Vector3(scalar * v.x, scalar * v.y, scalar * v.z);
}

// =============================================================================
// Vector4 - 4D Vector
// =============================================================================
struct Vector4 {
    float x, y, z, w;

    constexpr Vector4() noexcept : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    constexpr Vector4(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}

    static constexpr Vector4 zero() noexcept { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }
    static constexpr Vector4 one() noexcept { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr Vector4 unitX() noexcept { return Vector4(1.0f, 0.0f, 0.0f, 0.0f); }
    static constexpr Vector4 unitY() noexcept { return Vector4(0.0f, 1.0f, 0.0f, 0.0f); }
    static constexpr Vector4 unitZ() noexcept { return Vector4(0.0f, 0.0f, 1.0f, 0.0f); }
    static constexpr Vector4 unitW() noexcept { return Vector4(0.0f, 0.0f, 0.0f, 1.0f); }

    [[nodiscard]] constexpr Vector4 operator+(const Vector4& other) const noexcept {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);
    }
    [[nodiscard]] constexpr Vector4 operator-(const Vector4& other) const noexcept {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);
    }
    [[nodiscard]] constexpr Vector4 operator*(float scalar) const noexcept {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);
    }
    [[nodiscard]] constexpr Vector4 operator/(float scalar) const noexcept {
        return Vector4(x / scalar, y / scalar, z / scalar, w / scalar);
    }

    constexpr Vector4& operator+=(const Vector4& other) noexcept {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }
    constexpr Vector4& operator-=(const Vector4& other) noexcept {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }
    constexpr Vector4& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }
    constexpr Vector4& operator/=(float scalar) noexcept {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Vector4& other) const noexcept {
        return approxEqual(x, other.x) && approxEqual(y, other.y) &&
               approxEqual(z, other.z) && approxEqual(w, other.w);
    }
    [[nodiscard]] constexpr bool operator!=(const Vector4& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] constexpr float dot(const Vector4& other) const noexcept {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y + z * z + w * w); }
    [[nodiscard]] constexpr float lengthSquared() const noexcept { return x * x + y * y + z * z + w * w; }
    [[nodiscard]] Vector4 normalized() const noexcept {
        float len = length();
        return len > 0 ? *this / len : Vector4();
    }
    void normalize() noexcept {
        float len = length();
        if (len > 0) {
            x /= len;
            y /= len;
            z /= len;
            w /= len;
        }
    }
};

[[nodiscard]] constexpr Vector4 operator*(float scalar, const Vector4& v) noexcept {
    return Vector4(scalar * v.x, scalar * v.y, scalar * v.z, scalar * v.w);
}

// =============================================================================
// Matrix4x4 - 4x4 Matrix (Column-Major)
// =============================================================================
struct Matrix4x4 {
    float m[16]; // Column-major order

    // Default constructor: zero-initialized
    // Note: In C++17, constexpr constructors can't have loop bodies, so we use
    // aggregate-style initialization with a static zero matrix.
    Matrix4x4() noexcept {
        for (int i = 0; i < 16; ++i)
            m[i] = 0.0f;
    }

    // Note: Matrix4x4 factory methods are NOT constexpr in C++17 because the
    // default constructor uses a loop (not allowed in constexpr constructors
    // until C++20).
    [[nodiscard]] static Matrix4x4 identity() noexcept {
        Matrix4x4 mat;
        mat.m[0] = mat.m[5] = mat.m[10] = mat.m[15] = 1.0f;
        return mat;
    }

    [[nodiscard]] static Matrix4x4 translation(float x, float y, float z) noexcept {
        Matrix4x4 mat = identity();
        mat.m[12] = x;
        mat.m[13] = y;
        mat.m[14] = z;
        return mat;
    }

    [[nodiscard]] static Matrix4x4 scale(float x, float y, float z) noexcept {
        Matrix4x4 mat = identity();
        mat.m[0] = x;
        mat.m[5] = y;
        mat.m[10] = z;
        return mat;
    }

    [[nodiscard]] static Matrix4x4 rotationX(float angle) noexcept {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[5] = c;
        mat.m[6] = s;
        mat.m[9] = -s;
        mat.m[10] = c;
        return mat;
    }

    [[nodiscard]] static Matrix4x4 rotationY(float angle) noexcept {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[2] = -s;
        mat.m[8] = s;
        mat.m[10] = c;
        return mat;
    }

    [[nodiscard]] static Matrix4x4 rotationZ(float angle) noexcept {
        Matrix4x4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[1] = s;
        mat.m[4] = -s;
        mat.m[5] = c;
        return mat;
    }

    [[nodiscard]] Matrix4x4 operator*(const Matrix4x4& other) const noexcept {
        Matrix4x4 result;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                result.m[col * 4 + row] = m[0 * 4 + row] * other.m[col * 4 + 0] +
                                          m[1 * 4 + row] * other.m[col * 4 + 1] +
                                          m[2 * 4 + row] * other.m[col * 4 + 2] + m[3 * 4 + row] * other.m[col * 4 + 3];
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Vector4 operator*(const Vector4& v) const noexcept {
        return Vector4(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
    }

    // Transform a Vector3 using homogeneous coordinates (w=1 for translation)
    [[nodiscard]] Vector3 operator*(const Vector3& v) const noexcept {
        return Vector3(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]);
    }

    [[nodiscard]] Matrix4x4 transposed() const noexcept {
        Matrix4x4 result;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                result.m[row * 4 + col] = m[col * 4 + row];
        return result;
    }

    [[nodiscard]] float determinant() const noexcept {
        // Laplace expansion along first row (sufficient for 4x4)
        return m[0] * (m[5] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[9] * m[15] - m[11] * m[13]) + m[7] * (m[9] * m[14] - m[10] * m[13])) -
               m[1] * (m[4] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[14] - m[10] * m[12])) +
               m[2] * (m[4] * (m[9] * m[15] - m[11] * m[13]) - m[5] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[13] - m[9] * m[12])) -
               m[3] * (m[4] * (m[9] * m[14] - m[10] * m[13]) - m[5] * (m[8] * m[14] - m[10] * m[12]) + m[6] * (m[8] * m[13] - m[9] * m[12]));
    }

    // Inverse using adjugate matrix. Returns identity if matrix is singular.
    [[nodiscard]] Matrix4x4 inverted() const noexcept {
        float inv[16];
        float det;

        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

        constexpr float kDetEpsilon = 1e-8f;
        if (approxEqual(det, 0.0f, kDetEpsilon)) {
            return identity(); // Singular matrix, return identity
        }

        float invDet = 1.0f / det;
        Matrix4x4 result;
        for (int i = 0; i < 16; ++i) {
            result.m[i] = inv[i] * invDet;
        }
        return result;
    }

    [[nodiscard]] constexpr bool operator==(const Matrix4x4& other) const noexcept {
        for (int i = 0; i < 16; ++i) {
            if (!approxEqual(m[i], other.m[i])) return false;
        }
        return true;
    }
    [[nodiscard]] constexpr bool operator!=(const Matrix4x4& other) const noexcept {
        return !(*this == other);
    }
};

// =============================================================================
// Free Functions: Distance, Reflection, Projection, Interpolation
// =============================================================================

// Euclidean distance between two vectors
[[nodiscard]] inline float distance(const Vector2& a, const Vector2& b) noexcept {
    return (a - b).length();
}
[[nodiscard]] inline float distance(const Vector3& a, const Vector3& b) noexcept {
    return (a - b).length();
}
[[nodiscard]] inline float distance(const Vector4& a, const Vector4& b) noexcept {
    return (a - b).length();
}

// Reflect a vector off a surface with the given normal (normal must be normalized)
[[nodiscard]] constexpr Vector2 reflect(const Vector2& v, const Vector2& n) noexcept {
    return v - n * (2.0f * v.dot(n));
}
[[nodiscard]] constexpr Vector3 reflect(const Vector3& v, const Vector3& n) noexcept {
    return v - n * (2.0f * v.dot(n));
}
[[nodiscard]] constexpr Vector4 reflect(const Vector4& v, const Vector4& n) noexcept {
    return v - n * (2.0f * v.dot(n));
}

// Project vector `v` onto vector `onto`
[[nodiscard]] constexpr Vector2 project(const Vector2& v, const Vector2& onto) noexcept {
    float d = onto.dot(onto);
    if (d == 0.0f) return Vector2::zero();
    return onto * (v.dot(onto) / d);
}
[[nodiscard]] constexpr Vector3 project(const Vector3& v, const Vector3& onto) noexcept {
    float d = onto.dot(onto);
    if (d == 0.0f) return Vector3::zero();
    return onto * (v.dot(onto) / d);
}
[[nodiscard]] constexpr Vector4 project(const Vector4& v, const Vector4& onto) noexcept {
    float d = onto.dot(onto);
    if (d == 0.0f) return Vector4::zero();
    return onto * (v.dot(onto) / d);
}

// Linear interpolation for vectors
[[nodiscard]] constexpr Vector2 lerp(const Vector2& a, const Vector2& b, float t) noexcept {
    return a + (b - a) * t;
}
[[nodiscard]] constexpr Vector3 lerp(const Vector3& a, const Vector3& b, float t) noexcept {
    return a + (b - a) * t;
}
[[nodiscard]] constexpr Vector4 lerp(const Vector4& a, const Vector4& b, float t) noexcept {
    return a + (b - a) * t;
}

// Component-wise clamp for vectors
[[nodiscard]] constexpr Vector2 clamp(const Vector2& v, const Vector2& minVal, const Vector2& maxVal) noexcept {
    return Vector2(
        v.x < minVal.x ? minVal.x : (v.x > maxVal.x ? maxVal.x : v.x),
        v.y < minVal.y ? minVal.y : (v.y > maxVal.y ? maxVal.y : v.y));
}
[[nodiscard]] constexpr Vector3 clamp(const Vector3& v, const Vector3& minVal, const Vector3& maxVal) noexcept {
    return Vector3(
        v.x < minVal.x ? minVal.x : (v.x > maxVal.x ? maxVal.x : v.x),
        v.y < minVal.y ? minVal.y : (v.y > maxVal.y ? maxVal.y : v.y),
        v.z < minVal.z ? minVal.z : (v.z > maxVal.z ? maxVal.z : v.z));
}
[[nodiscard]] constexpr Vector4 clamp(const Vector4& v, const Vector4& minVal, const Vector4& maxVal) noexcept {
    return Vector4(
        v.x < minVal.x ? minVal.x : (v.x > maxVal.x ? maxVal.x : v.x),
        v.y < minVal.y ? minVal.y : (v.y > maxVal.y ? maxVal.y : v.y),
        v.z < minVal.z ? minVal.z : (v.z > maxVal.z ? maxVal.z : v.z),
        v.w < minVal.w ? minVal.w : (v.w > maxVal.w ? maxVal.w : v.w));
}

// Scalar clamp for all components
[[nodiscard]] constexpr Vector2 clamp(const Vector2& v, float minVal, float maxVal) noexcept {
    return Vector2(
        v.x < minVal ? minVal : (v.x > maxVal ? maxVal : v.x),
        v.y < minVal ? minVal : (v.y > maxVal ? maxVal : v.y));
}
[[nodiscard]] constexpr Vector3 clamp(const Vector3& v, float minVal, float maxVal) noexcept {
    return Vector3(
        v.x < minVal ? minVal : (v.x > maxVal ? maxVal : v.x),
        v.y < minVal ? minVal : (v.y > maxVal ? maxVal : v.y),
        v.z < minVal ? minVal : (v.z > maxVal ? maxVal : v.z));
}
[[nodiscard]] constexpr Vector4 clamp(const Vector4& v, float minVal, float maxVal) noexcept {
    return Vector4(
        v.x < minVal ? minVal : (v.x > maxVal ? maxVal : v.x),
        v.y < minVal ? minVal : (v.y > maxVal ? maxVal : v.y),
        v.z < minVal ? minVal : (v.z > maxVal ? maxVal : v.z),
        v.w < minVal ? minVal : (v.w > maxVal ? maxVal : v.w));
}

// =============================================================================
// DSP Math Functions
// =============================================================================

// Linear interpolation
[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
    return a + t * (b - a);
}

// Clamp value between min and max
[[nodiscard]] constexpr float clamp(float value, float min, float max) noexcept {
    return value < min ? min : (value > max ? max : value);
}

// Smooth Hermite interpolation
[[nodiscard]] inline float smoothstep(float edge0, float edge1, float x) noexcept {
    float range = edge1 - edge0;
    if (approxEqual(range, 0.0f)) return x < edge0 ? 0.0f : 1.0f;
    float t = clamp((x - edge0) / range, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Map value from one range to another. Returns outMin if ranges are equal.
[[nodiscard]] inline float map(float value, float inMin, float inMax, float outMin, float outMax) noexcept {
    float range = inMax - inMin;
    if (approxEqual(range, 0.0f)) return outMin;
    return outMin + (value - inMin) * (outMax - outMin) / range;
}

// Decibels to linear gain. Returns 0.0f for db <= DB_FLOOR.
[[nodiscard]] inline float dbToGain(float db) noexcept {
    if (db <= DB_FLOOR)
        return 0.0f;
    return std::exp(db * LN10_OVER_20);
}

// Linear gain to decibels. Returns DB_FLOOR for gain <= 0.
[[nodiscard]] inline float gainToDb(float gain) noexcept {
    if (gain <= 0.0f)
        return DB_FLOOR;
    return 20.0f * std::log10(gain);
}

} // namespace Aestra
