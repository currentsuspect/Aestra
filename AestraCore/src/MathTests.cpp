// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include <AestraMath.h>

#include <cassert>
#include <cmath>
#include <iostream>

using namespace Aestra;

// Test helper
#define TEST_ASSERT(condition, message)                  \
    if (!(condition)) {                                  \
        std::cerr << "FAILED: " << message << std::endl; \
        return false;                                    \
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

    // Static constants
    TEST_ASSERT(Vector2::zero() == Vector2(0.0f, 0.0f), "Vector2::zero");
    TEST_ASSERT(Vector2::one() == Vector2(1.0f, 1.0f), "Vector2::one");
    TEST_ASSERT(Vector2::unitX() == Vector2(1.0f, 0.0f), "Vector2::unitX");
    TEST_ASSERT(Vector2::unitY() == Vector2(0.0f, 1.0f), "Vector2::unitY");

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

    // Scalar multiplication (commutative)
    Vector2 v5b = 2.0f * v1;
    TEST_ASSERT(v5b == v5, "Vector2 scalar*vector commutative");

    // Dot product
    float dot = v1.dot(v2);
    TEST_ASSERT(dot == 11.0f, "Vector2 dot product");

    // Length
    float len = v1.length();
    TEST_ASSERT(FLOAT_EQUAL(len, 5.0f), "Vector2 length");

    // Normalization
    Vector2 v6 = v1.normalized();
    TEST_ASSERT(FLOAT_EQUAL(v6.length(), 1.0f), "Vector2 normalization");

    // Operator== and !=
    Vector2 v7(3.0f, 4.0f);
    TEST_ASSERT(v1 == v7, "Vector2 equality");
    TEST_ASSERT(!(v1 != v7), "Vector2 inequality negation");
    TEST_ASSERT(v1 != v2, "Vector2 non-equality");

    std::cout << "  ✓ Vector2 tests passed" << std::endl;
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

    // Static constants
    TEST_ASSERT(Vector3::zero() == Vector3(0.0f, 0.0f, 0.0f), "Vector3::zero");
    TEST_ASSERT(Vector3::unitX() == Vector3(1.0f, 0.0f, 0.0f), "Vector3::unitX");
    TEST_ASSERT(Vector3::unitY() == Vector3(0.0f, 1.0f, 0.0f), "Vector3::unitY");
    TEST_ASSERT(Vector3::unitZ() == Vector3(0.0f, 0.0f, 1.0f), "Vector3::unitZ");

    // Addition
    Vector3 v2(4.0f, 5.0f, 6.0f);
    Vector3 v3 = v1 + v2;
    TEST_ASSERT(v3.x == 5.0f && v3.y == 7.0f && v3.z == 9.0f, "Vector3 addition");

    // Dot product
    float dot = v1.dot(v2);
    TEST_ASSERT(dot == 32.0f, "Vector3 dot product");

    // Cross product: X × Y = Z
    Vector3 v4(1.0f, 0.0f, 0.0f);
    Vector3 v5(0.0f, 1.0f, 0.0f);
    Vector3 cross = v4.cross(v5);
    TEST_ASSERT(cross == Vector3::unitZ(), "Vector3 cross product X×Y=Z");

    // Length
    Vector3 v6(3.0f, 4.0f, 0.0f);
    float len = v6.length();
    TEST_ASSERT(FLOAT_EQUAL(len, 5.0f), "Vector3 length");

    // Normalization
    Vector3 v7 = v6.normalized();
    TEST_ASSERT(FLOAT_EQUAL(v7.length(), 1.0f), "Vector3 normalization");

    // Operator== and !=
    TEST_ASSERT(Vector3::one() == Vector3(1.0f, 1.0f, 1.0f), "Vector3 equality");
    TEST_ASSERT(Vector3::unitX() != Vector3::unitY(), "Vector3 non-equality");

    std::cout << "  ✓ Vector3 tests passed" << std::endl;
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

    // Static constants
    TEST_ASSERT(Vector4::zero() == Vector4(0.0f, 0.0f, 0.0f, 0.0f), "Vector4::zero");
    TEST_ASSERT(Vector4::unitW() == Vector4(0.0f, 0.0f, 0.0f, 1.0f), "Vector4::unitW");

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

    // Normalization
    Vector4 v4n = v4.normalized();
    TEST_ASSERT(FLOAT_EQUAL(v4n.length(), 1.0f), "Vector4 normalization");

    // Operator== and !=
    TEST_ASSERT(v1 == Vector4(1.0f, 2.0f, 3.0f, 4.0f), "Vector4 equality");
    TEST_ASSERT(v1 != v2, "Vector4 non-equality");

    std::cout << "  ✓ Vector4 tests passed" << std::endl;
    return true;
}

// =============================================================================
// Matrix4x4 Tests
// =============================================================================
bool testMatrix4x4() {
    std::cout << "Testing Matrix4x4..." << std::endl;

    // Identity matrix
    Matrix4x4 identity = Matrix4x4::identity();
    TEST_ASSERT(identity.m[0] == 1.0f && identity.m[5] == 1.0f && identity.m[10] == 1.0f && identity.m[15] == 1.0f,
                "Matrix4x4 identity");

    // Translation
    Matrix4x4 trans = Matrix4x4::translation(1.0f, 2.0f, 3.0f);
    Vector4 v1(0.0f, 0.0f, 0.0f, 1.0f);
    Vector4 v2 = trans * v1;
    TEST_ASSERT(FLOAT_EQUAL(v2.x, 1.0f) && FLOAT_EQUAL(v2.y, 2.0f) && FLOAT_EQUAL(v2.z, 3.0f), "Matrix4x4 translation");

    // Scale
    Matrix4x4 scale = Matrix4x4::scale(2.0f, 3.0f, 4.0f);
    Vector4 v3(1.0f, 1.0f, 1.0f, 1.0f);
    Vector4 v4 = scale * v3;
    TEST_ASSERT(FLOAT_EQUAL(v4.x, 2.0f) && FLOAT_EQUAL(v4.y, 3.0f) && FLOAT_EQUAL(v4.z, 4.0f), "Matrix4x4 scale");

    // Matrix multiplication
    Matrix4x4 result = trans * scale;
    TEST_ASSERT(result.m[0] == 2.0f && result.m[5] == 3.0f && result.m[10] == 4.0f, "Matrix4x4 multiplication");

    // Identity preserves vectors
    Vector4 testVec(1.5f, 2.5f, 3.5f, 1.0f);
    Vector4 transformed = identity * testVec;
    TEST_ASSERT(transformed == testVec, "Matrix4x4 identity preserves vector");

    // Matrix * Vector3 (homogeneous)
    Vector3 v3test(1.0f, 0.0f, 0.0f);
    Vector3 v3result = trans * v3test;
    TEST_ASSERT(FLOAT_EQUAL(v3result.x, 2.0f) && FLOAT_EQUAL(v3result.y, 2.0f) && FLOAT_EQUAL(v3result.z, 3.0f),
                "Matrix4x4 * Vector3 homogeneous");

    // Transpose
    Matrix4x4 t = Matrix4x4::translation(1.0f, 2.0f, 3.0f);
    Matrix4x4 tt = t.transposed();
    TEST_ASSERT(FLOAT_EQUAL(tt.m[12], 0.0f) && FLOAT_EQUAL(tt.m[1], 0.0f), "Matrix4x4 transpose");
    TEST_ASSERT(t == tt.transposed(), "Matrix4x4 double transpose = original");

    // Determinant of identity is 1
    float detIdentity = Matrix4x4::identity().determinant();
    TEST_ASSERT(FLOAT_EQUAL(detIdentity, 1.0f), "Matrix4x4 identity determinant is 1");

    // Determinant of scale matrix
    Matrix4x4 s = Matrix4x4::scale(2.0f, 3.0f, 4.0f);
    float detScale = s.determinant();
    TEST_ASSERT(FLOAT_EQUAL(detScale, 24.0f), "Matrix4x4 scale determinant (2*3*4=24)");

    // Inverse of identity is identity
    Matrix4x4 invIdentity = Matrix4x4::identity().inverted();
    TEST_ASSERT(invIdentity == Matrix4x4::identity(), "Matrix4x4 identity inverse");

    // Inverse: M * M^-1 ≈ I
    Matrix4x4 m = trans * scale;
    Matrix4x4 mInv = m.inverted();
    Matrix4x4 product = m * mInv;
    TEST_ASSERT(product == Matrix4x4::identity(), "Matrix4x4 * inverse = identity");

    // Singular matrix returns identity
    Matrix4x4 singular = Matrix4x4::scale(0.0f, 1.0f, 1.0f);
    Matrix4x4 singularInv = singular.inverted();
    TEST_ASSERT(singularInv == Matrix4x4::identity(), "Matrix4x4 singular matrix inverse returns identity");

    // Operator== and !=
    TEST_ASSERT(identity == Matrix4x4::identity(), "Matrix4x4 equality");
    TEST_ASSERT(trans != scale, "Matrix4x4 non-equality");

    std::cout << "  ✓ Matrix4x4 tests passed" << std::endl;
    return true;
}

// =============================================================================
// DSP Math Tests
// =============================================================================
bool testDSPMath() {
    std::cout << "Testing DSP Math functions..." << std::endl;

    // Lerp
    float l1 = lerp(0.0f, 10.0f, 0.5f);
    TEST_ASSERT(FLOAT_EQUAL(l1, 5.0f), "lerp midpoint");
    TEST_ASSERT(FLOAT_EQUAL(lerp(0.0f, 10.0f, 0.0f), 0.0f), "lerp t=0");
    TEST_ASSERT(FLOAT_EQUAL(lerp(0.0f, 10.0f, 1.0f), 10.0f), "lerp t=1");

    // Clamp
    float c1 = clamp(15.0f, 0.0f, 10.0f);
    TEST_ASSERT(FLOAT_EQUAL(c1, 10.0f), "clamp max");
    float c2 = clamp(-5.0f, 0.0f, 10.0f);
    TEST_ASSERT(FLOAT_EQUAL(c2, 0.0f), "clamp min");
    float c3 = clamp(5.0f, 0.0f, 10.0f);
    TEST_ASSERT(FLOAT_EQUAL(c3, 5.0f), "clamp in range");

    // Smoothstep
    float s1 = smoothstep(0.0f, 1.0f, 0.5f);
    TEST_ASSERT(s1 > 0.4f && s1 < 0.6f, "smoothstep midpoint");
    TEST_ASSERT(FLOAT_EQUAL(smoothstep(0.0f, 1.0f, 0.0f), 0.0f), "smoothstep edge0");
    TEST_ASSERT(FLOAT_EQUAL(smoothstep(0.0f, 1.0f, 1.0f), 1.0f), "smoothstep edge1");
    // Equal edges: should not produce NaN
    float s2 = smoothstep(1.0f, 1.0f, 0.5f);
    TEST_ASSERT(FLOAT_EQUAL(s2, 0.0f), "smoothstep equal edges: x < edge0 returns 0.0f");
    float s3 = smoothstep(1.0f, 1.0f, 2.0f);
    TEST_ASSERT(FLOAT_EQUAL(s3, 1.0f), "smoothstep equal edges: x >= edge0 returns 1.0f");

    // Map
    float m1 = map(5.0f, 0.0f, 10.0f, 0.0f, 100.0f);
    TEST_ASSERT(FLOAT_EQUAL(m1, 50.0f), "map");
    // Div-by-zero guard
    float m2 = map(5.0f, 10.0f, 10.0f, 0.0f, 100.0f);
    TEST_ASSERT(FLOAT_EQUAL(m2, 0.0f), "map equal input ranges returns outMin");

    // DB conversion
    float gain = dbToGain(0.0f);
    TEST_ASSERT(FLOAT_EQUAL(gain, 1.0f), "dbToGain 0dB");
    float db = gainToDb(1.0f);
    TEST_ASSERT(FLOAT_EQUAL(db, 0.0f), "gainToDb unity");

    // DB floor guards
    float gainFloor = dbToGain(-90.0f);
    TEST_ASSERT(FLOAT_EQUAL(gainFloor, 0.0f), "dbToGain -90dB = 0");
    float gainBelow = dbToGain(-120.0f);
    TEST_ASSERT(FLOAT_EQUAL(gainBelow, 0.0f), "dbToGain -120dB = 0");

    // gainToDb floor guard (was producing -inf)
    float dbZero = gainToDb(0.0f);
    TEST_ASSERT(FLOAT_EQUAL(dbZero, -90.0f), "gainToDb 0.0f returns DB_FLOOR, not -inf");
    float dbNeg = gainToDb(-1.0f);
    TEST_ASSERT(FLOAT_EQUAL(dbNeg, -90.0f), "gainToDb -1.0f returns DB_FLOOR, not NaN");

    std::cout << "  ✓ DSP Math tests passed" << std::endl;
    return true;
}

// =============================================================================
// Free Function Tests (distance, reflect, project, lerp, clamp for vectors)
// =============================================================================
bool testFreeFunctions() {
    std::cout << "Testing free functions..." << std::endl;

    // Distance
    Vector2 a2(0.0f, 0.0f), b2(3.0f, 4.0f);
    TEST_ASSERT(FLOAT_EQUAL(distance(a2, b2), 5.0f), "Vector2 distance");

    Vector3 a3(0.0f, 0.0f, 0.0f), b3(1.0f, 2.0f, 2.0f);
    TEST_ASSERT(FLOAT_EQUAL(distance(a3, b3), 3.0f), "Vector3 distance");

    // Reflect: 45° vector off Y normal
    Vector3 v45(1.0f, 1.0f, 0.0f);
    Vector3 refl45 = reflect(v45, Vector3::unitY());
    TEST_ASSERT(FLOAT_EQUAL(refl45.x, 1.0f) && FLOAT_EQUAL(refl45.y, -1.0f), "Reflect 45° off Y normal");

    // Project
    Vector3 vp(3.0f, 4.0f, 0.0f);
    Vector3 proj = project(vp, Vector3::unitX());
    TEST_ASSERT(proj == Vector3(3.0f, 0.0f, 0.0f), "Project onto X axis");

    Vector3 projZero = project(vp, Vector3::zero());
    TEST_ASSERT(projZero == Vector3::zero(), "Project onto zero vector returns zero");

    // Vector lerp
    Vector3 la(0.0f, 0.0f, 0.0f), lb(10.0f, 20.0f, 30.0f);
    Vector3 lm = lerp(la, lb, 0.5f);
    TEST_ASSERT(FLOAT_EQUAL(lm.x, 5.0f) && FLOAT_EQUAL(lm.y, 10.0f) && FLOAT_EQUAL(lm.z, 15.0f), "Vector3 lerp");
    TEST_ASSERT(lerp(la, lb, 0.0f) == la, "Vector3 lerp t=0");
    TEST_ASSERT(lerp(la, lb, 1.0f) == lb, "Vector3 lerp t=1");

    // Vector clamp (component-wise)
    Vector3 vc(5.0f, -3.0f, 15.0f);
    Vector3 vclamped = clamp(vc, Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 10.0f, 10.0f));
    TEST_ASSERT(vclamped == Vector3(5.0f, 0.0f, 10.0f), "Vector3 component-wise clamp");

    // Scalar clamp for vectors
    Vector3 vcs = clamp(vc, 0.0f, 10.0f);
    TEST_ASSERT(vcs == Vector3(5.0f, 0.0f, 10.0f), "Vector3 scalar clamp");

    std::cout << "  ✓ Free functions tests passed" << std::endl;
    return true;
}

// =============================================================================
// Constants Tests
// =============================================================================
bool testConstants() {
    std::cout << "Testing constants..." << std::endl;

    TEST_ASSERT(FLOAT_EQUAL(PI, 3.14159265358979323846f), "PI");
    TEST_ASSERT(FLOAT_EQUAL(TWO_PI, 6.28318530717958647692f), "TWO_PI");
    TEST_ASSERT(FLOAT_EQUAL(LN10_OVER_20, 0.11512925464970228420089957273422f), "LN10_OVER_20");
    TEST_ASSERT(FLOAT_EQUAL(DB_FLOOR, -90.0f), "DB_FLOOR");

    std::cout << "  ✓ Constants tests passed" << std::endl;
    return true;
}

// =============================================================================
// Approximate Equality Tests
// =============================================================================
bool testApproxEqual() {
    std::cout << "Testing approxEqual..." << std::endl;

    TEST_ASSERT(approxEqual(1.0f, 1.0f), "exact equality");
    TEST_ASSERT(approxEqual(1.0f, 1.0000001f), "within default epsilon");
    TEST_ASSERT(!approxEqual(1.0f, 1.1f), "outside default epsilon");
    TEST_ASSERT(approxEqual(1.0f, 1.01f, 0.1f), "within custom epsilon");

    std::cout << "  ✓ approxEqual tests passed" << std::endl;
    return true;
}

// =============================================================================
// constexpr Tests (compile-time verification)
// =============================================================================
static_assert(Vector2(1.0f, 2.0f).x == 1.0f, "constexpr Vector2 construction");
static_assert(Vector3::unitZ().z == 1.0f, "constexpr Vector3::unitZ");
static_assert(Vector4::zero().w == 0.0f, "constexpr Vector4::zero");
static_assert(lerp(0.0f, 10.0f, 0.5f) == 5.0f, "constexpr lerp");

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  AestraCore Math Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testVector2();
    allPassed &= testVector3();
    allPassed &= testVector4();
    allPassed &= testMatrix4x4();
    allPassed &= testDSPMath();
    allPassed &= testFreeFunctions();
    allPassed &= testConstants();
    allPassed &= testApproxEqual();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  ✓ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  ✗ SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
