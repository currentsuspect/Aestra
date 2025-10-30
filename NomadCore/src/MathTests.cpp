// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadMath.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace Nomad;

// Test helper
#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
    }

#define FLOAT_EPSILON 0.0001f
#define FLOAT_EQUAL(a, b) (std::abs((a) - (b)) < FLOAT_EPSILON)

// =============================================================================
// Vector2 Tests
// =============================================================================
bool testVector2() {
    std::cout << "Testing Vector2..." << std::endl;

    // Construction
    Vector2 v1(3.0f, 4.0f);
    TEST_ASSERT(v1.x == 3.0f && v1.y == 4.0f, "Vector2 construction");

    // Addition
    Vector2 v2(1.0f, 2.0f);
    Vector2 v3 = v1 + v2;
    TEST_ASSERT(v3.x == 4.0f && v3.y == 6.0f, "Vector2 addition");

    // Subtraction
    Vector2 v4 = v1 - v2;
    TEST_ASSERT(v4.x == 2.0f && v4.y == 2.0f, "Vector2 subtraction");

    // Scalar multiplication
    Vector2 v5 = v1 * 2.0f;
    TEST_ASSERT(v5.x == 6.0f && v5.y == 8.0f, "Vector2 scalar multiplication");

    // Dot product
    float dot = v1.dot(v2);
    TEST_ASSERT(dot == 11.0f, "Vector2 dot product");

    // Length
    float len = v1.length();
    TEST_ASSERT(FLOAT_EQUAL(len, 5.0f), "Vector2 length");

    // Normalization
    Vector2 v6 = v1.normalized();
    TEST_ASSERT(FLOAT_EQUAL(v6.length(), 1.0f), "Vector2 normalization");

    std::cout << "  âœ“ Vector2 tests passed" << std::endl;
    return true;
}

// =============================================================================
// Vector3 Tests
// =============================================================================
bool testVector3() {
    std::cout << "Testing Vector3..." << std::endl;

    // Construction
    Vector3 v1(1.0f, 2.0f, 3.0f);
    TEST_ASSERT(v1.x == 1.0f && v1.y == 2.0f && v1.z == 3.0f, "Vector3 construction");

    // Addition
    Vector3 v2(4.0f, 5.0f, 6.0f);
    Vector3 v3 = v1 + v2;
    TEST_ASSERT(v3.x == 5.0f && v3.y == 7.0f && v3.z == 9.0f, "Vector3 addition");

    // Dot product
    float dot = v1.dot(v2);
    TEST_ASSERT(dot == 32.0f, "Vector3 dot product");

    // Cross product
    Vector3 v4(1.0f, 0.0f, 0.0f);
    Vector3 v5(0.0f, 1.0f, 0.0f);
    Vector3 cross = v4.cross(v5);
    TEST_ASSERT(FLOAT_EQUAL(cross.x, 0.0f) && FLOAT_EQUAL(cross.y, 0.0f) && FLOAT_EQUAL(cross.z, 1.0f), 
                "Vector3 cross product");

    // Length
    Vector3 v6(3.0f, 4.0f, 0.0f);
    float len = v6.length();
    TEST_ASSERT(FLOAT_EQUAL(len, 5.0f), "Vector3 length");

    // Normalization
    Vector3 v7 = v6.normalized();
    TEST_ASSERT(FLOAT_EQUAL(v7.length(), 1.0f), "Vector3 normalization");

    std::cout << "  âœ“ Vector3 tests passed" << std::endl;
    return true;
}

// =============================================================================
// Vector4 Tests
// =============================================================================
bool testVector4() {
    std::cout << "Testing Vector4..." << std::endl;

    // Construction
    Vector4 v1(1.0f, 2.0f, 3.0f, 4.0f);
    TEST_ASSERT(v1.x == 1.0f && v1.y == 2.0f && v1.z == 3.0f && v1.w == 4.0f, "Vector4 construction");

    // Addition
    Vector4 v2(5.0f, 6.0f, 7.0f, 8.0f);
    Vector4 v3 = v1 + v2;
    TEST_ASSERT(v3.x == 6.0f && v3.y == 8.0f && v3.z == 10.0f && v3.w == 12.0f, "Vector4 addition");

    // Dot product
    float dot = v1.dot(v2);
    TEST_ASSERT(dot == 70.0f, "Vector4 dot product");

    // Length
    Vector4 v4(2.0f, 2.0f, 1.0f, 0.0f);
    float len = v4.length();
    TEST_ASSERT(FLOAT_EQUAL(len, 3.0f), "Vector4 length");

    std::cout << "  âœ“ Vector4 tests passed" << std::endl;
    return true;
}

// =============================================================================
// Matrix4x4 Tests
// =============================================================================
bool testMatrix4x4() {
    std::cout << "Testing Matrix4x4..." << std::endl;

    // Identity matrix
    Matrix4x4 identity = Matrix4x4::identity();
    TEST_ASSERT(identity.m[0] == 1.0f && identity.m[5] == 1.0f && 
                identity.m[10] == 1.0f && identity.m[15] == 1.0f, "Matrix4x4 identity");

    // Translation
    Matrix4x4 trans = Matrix4x4::translation(1.0f, 2.0f, 3.0f);
    Vector4 v1(0.0f, 0.0f, 0.0f, 1.0f);
    Vector4 v2 = trans * v1;
    TEST_ASSERT(FLOAT_EQUAL(v2.x, 1.0f) && FLOAT_EQUAL(v2.y, 2.0f) && FLOAT_EQUAL(v2.z, 3.0f), 
                "Matrix4x4 translation");

    // Scale
    Matrix4x4 scale = Matrix4x4::scale(2.0f, 3.0f, 4.0f);
    Vector4 v3(1.0f, 1.0f, 1.0f, 1.0f);
    Vector4 v4 = scale * v3;
    TEST_ASSERT(FLOAT_EQUAL(v4.x, 2.0f) && FLOAT_EQUAL(v4.y, 3.0f) && FLOAT_EQUAL(v4.z, 4.0f), 
                "Matrix4x4 scale");

    // Matrix multiplication
    Matrix4x4 result = trans * scale;
    TEST_ASSERT(result.m[0] == 2.0f && result.m[5] == 3.0f && result.m[10] == 4.0f, 
                "Matrix4x4 multiplication");

    std::cout << "  âœ“ Matrix4x4 tests passed" << std::endl;
    return true;
}

// =============================================================================
// DSP Math Tests
// =============================================================================
bool testDSPMath() {
    std::cout << "Testing DSP Math functions..." << std::endl;

    // Lerp
    float l1 = lerp(0.0f, 10.0f, 0.5f);
    TEST_ASSERT(FLOAT_EQUAL(l1, 5.0f), "lerp");

    // Clamp
    float c1 = clamp(15.0f, 0.0f, 10.0f);
    TEST_ASSERT(FLOAT_EQUAL(c1, 10.0f), "clamp max");
    float c2 = clamp(-5.0f, 0.0f, 10.0f);
    TEST_ASSERT(FLOAT_EQUAL(c2, 0.0f), "clamp min");

    // Smoothstep
    float s1 = smoothstep(0.0f, 1.0f, 0.5f);
    TEST_ASSERT(s1 > 0.4f && s1 < 0.6f, "smoothstep");

    // Map
    float m1 = map(5.0f, 0.0f, 10.0f, 0.0f, 100.0f);
    TEST_ASSERT(FLOAT_EQUAL(m1, 50.0f), "map");

    // DB conversion
    float gain = dbToGain(0.0f);
    TEST_ASSERT(FLOAT_EQUAL(gain, 1.0f), "dbToGain 0dB");
    float db = gainToDb(1.0f);
    TEST_ASSERT(FLOAT_EQUAL(db, 0.0f), "gainToDb unity");

    std::cout << "  âœ“ DSP Math tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  NomadCore Math Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testVector2();
    allPassed &= testVector3();
    allPassed &= testVector4();
    allPassed &= testMatrix4x4();
    allPassed &= testDSPMath();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
