// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleStateTest
// Verifies JSON preset round-trip and legacy state migration for Aestra Rumble.

#include "AestraJSON.h"
#include "RumbleInstance.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Plugins::RumbleInstance;

namespace {
constexpr uint32_t kExpectedParamCount = 25;
constexpr uint32_t kLegacyStateMagic = 0x524D424Cu; // 'RMBL'
constexpr uint32_t kLegacyStateVersionV2 = 2;

enum ParamIndex : uint32_t {
    kAmpDecay = 0,
    kDrive,
    kTone,
    kOutputGain,
    kPitchAmount,
    kPitchDecay,
    kPitchCurve,
    kAmpAttack,
    kResonance,
    kTransientAmount,
    kClickLevel,
    kClickDecay,
    kClickTone,
    kGlideTime,
    kGlideCurve,
    kGlideMode,
    kRetriggerMode,
    kFilterEnvAmount,
    kFilterKeytrack,
    kSatMode,
    kVelocityToAmp,
    kTune,
    kFine,
    kHarmonics,
    kSubClean,
};

struct LegacyStateBlobV2 {
    uint32_t magic;
    uint32_t version;
    float decay;
    float drive;
    float tone;
    float outputGain;
    float pitchAmount;
    float pitchDecay;
    float ampAttack;
    float resonance;
    float transientAmount;
};

bool nearlyEqual(float a, float b, float epsilon = 1.0e-6f) {
    return std::fabs(a - b) <= epsilon;
}

bool testParameterContract() {
    std::cout << "TEST: stable parameter ID contract... ";

    RumbleInstance rumble;
    const std::array<const char*, kExpectedParamCount> expectedNames = {
        "AmpDecay",       "Drive",
        "Tone",           "OutputGain",
        "PitchAmount",    "PitchDecay",
        "PitchCurve",     "AmpAttack",
        "Resonance",      "TransientAmount",
        "ClickLevel",     "ClickDecay",
        "ClickTone",      "GlideTime",
        "GlideCurve",     "GlideMode",
        "RetriggerMode",  "FilterEnvAmount",
        "FilterKeytrack", "SatMode",
        "VelocityToAmp",  "Tune",
        "Fine",           "Harmonics",
        "SubClean",
    };

    const auto parameters = rumble.getParameters();
    assert(parameters.size() == expectedNames.size());
    for (size_t i = 0; i < expectedNames.size(); ++i) {
        assert(parameters[i].id == i);
        assert(parameters[i].name == expectedNames[i]);
    }

    std::cout << "✅ PASS\n";
    return true;
}

void assertParameterEquals(RumbleInstance& rumble, uint32_t index, float expected, const char* label) {
    const float actual = rumble.getParameter(index);
    if (!nearlyEqual(actual, expected)) {
        std::cerr << "\nParameter mismatch for " << label << ": expected " << expected << ", got " << actual << "\n";
    }
    assert(nearlyEqual(actual, expected));
}

bool testJsonStateRoundTrip() {
    std::cout << "TEST: rumble JSON state round-trip... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));
    assert(rumble.getParameterCount() == kExpectedParamCount);

    const std::array<float, kExpectedParamCount> expected = {
        0.62f, // AmpDecay
        0.27f, // Drive
        0.41f, // Tone
        0.53f, // OutputGain
        0.71f, // PitchAmount
        0.18f, // PitchDecay
        0.66f, // PitchCurve
        0.09f, // AmpAttack
        0.81f, // Resonance
        0.44f, // TransientAmount
        0.91f, // ClickLevel
        0.37f, // ClickDecay
        0.58f, // ClickTone
        0.22f, // GlideTime
        0.48f, // GlideCurve
        1.00f, // GlideMode
        0.00f, // RetriggerMode
        0.73f, // FilterEnvAmount
        0.36f, // FilterKeytrack
        1.00f, // SatMode
        0.64f, // VelocityToAmp
        0.55f, // Tune
        0.47f, // Fine
        0.68f, // Harmonics
        0.79f, // SubClean
    };

    for (uint32_t i = 0; i < expected.size(); ++i) {
        rumble.setParameter(i, expected[i]);
    }

    const auto state = rumble.saveState();
    assert(!state.empty());
    const std::string stateString(state.begin(), state.end());

    Aestra::JSON json = Aestra::JSON::parse(stateString);
    assert(json.isObject());
    assert(json.has("schema"));
    assert(json["schema"].isString());
    assert(json["schema"].asString() == "rumble-preset");
    assert(json.has("version"));
    assert(json["version"].isNumber());
    assert(static_cast<int>(json["version"].asNumber()) == 1);
    assert(json.has("params"));
    assert(json["params"].isObject());

    RumbleInstance restored;
    assert(restored.initialize(48000.0, 512));
    assert(restored.loadState(state));

    for (uint32_t i = 0; i < expected.size(); ++i) {
        assert(nearlyEqual(restored.getParameter(i), expected[i]));
    }

    std::cout << "✅ PASS\n";
    return true;
}

bool testLegacyV2Migration() {
    std::cout << "TEST: legacy V2 blob migration... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    const LegacyStateBlobV2 legacy = {
        kLegacyStateMagic,
        kLegacyStateVersionV2,
        0.61f,          // decay -> AmpDecay
        0.19f,          // Drive
        0.42f,          // Tone
        0.57f,          // OutputGain
        0.33f,          // PitchAmount
        0.29f,          // PitchDecay
        0.04081632653f, // AmpAttack
        0.68f,          // Resonance
        0.24f,          // TransientAmount
    };

    std::vector<uint8_t> state(sizeof(legacy));
    std::memcpy(state.data(), &legacy, sizeof(legacy));
    assert(rumble.loadState(state));

    assertParameterEquals(rumble, kAmpDecay, 0.61f, "AmpDecay");
    assertParameterEquals(rumble, kDrive, 0.19f, "Drive");
    assertParameterEquals(rumble, kTone, 0.42f, "Tone");
    assertParameterEquals(rumble, kOutputGain, 0.57f, "OutputGain");
    assertParameterEquals(rumble, kPitchAmount, 0.33f, "PitchAmount");
    assertParameterEquals(rumble, kPitchDecay, 0.29f, "PitchDecay");
    assertParameterEquals(rumble, kAmpAttack, 0.04081632653f, "AmpAttack");
    assertParameterEquals(rumble, kResonance, 0.68f, "Resonance");
    assertParameterEquals(rumble, kTransientAmount, 0.24f, "TransientAmount");

    assertParameterEquals(rumble, kPitchCurve, 0.42f, "PitchCurve default");
    assertParameterEquals(rumble, kClickLevel, 0.25f, "ClickLevel default");
    assertParameterEquals(rumble, kClickDecay, 0.20f, "ClickDecay default");
    assertParameterEquals(rumble, kClickTone, 0.35f, "ClickTone default");
    assertParameterEquals(rumble, kGlideTime, 0.15f, "GlideTime default");
    assertParameterEquals(rumble, kGlideCurve, 0.50f, "GlideCurve default");
    assertParameterEquals(rumble, kGlideMode, 0.0f, "GlideMode default");
    assertParameterEquals(rumble, kRetriggerMode, 0.0f, "RetriggerMode default");
    assertParameterEquals(rumble, kFilterEnvAmount, 0.50f, "FilterEnvAmount default");
    assertParameterEquals(rumble, kFilterKeytrack, 0.0f, "FilterKeytrack default");
    assertParameterEquals(rumble, kSatMode, 0.0f, "SatMode default");
    assertParameterEquals(rumble, kVelocityToAmp, 0.75f, "VelocityToAmp default");
    assertParameterEquals(rumble, kTune, 0.50f, "Tune default");
    assertParameterEquals(rumble, kFine, 0.50f, "Fine default");
    assertParameterEquals(rumble, kHarmonics, 0.22f, "Harmonics default");
    assertParameterEquals(rumble, kSubClean, 0.70f, "SubClean default");

    std::cout << "✅ PASS\n";
    return true;
}

bool testJsonV1AdditiveMigration() {
    std::cout << "TEST: JSON V1 additive migration... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));
    const std::string v1 = R"({"schema":"rumble-preset","version":1,"params":{"AmpDecay":0.61,"Drive":0.19}})";
    const std::vector<uint8_t> state(v1.begin(), v1.end());
    assert(rumble.loadState(state));
    assertParameterEquals(rumble, kAmpDecay, 0.61f, "AmpDecay from JSON V1");
    assertParameterEquals(rumble, kDrive, 0.19f, "Drive from JSON V1");
    assertParameterEquals(rumble, kHarmonics, 0.22f, "Harmonics additive default");
    assertParameterEquals(rumble, kSubClean, 0.70f, "SubClean additive default");

    std::cout << "✅ PASS\n";
    return true;
}

bool testRejectsInvalidJsonSchema() {
    std::cout << "TEST: reject invalid JSON schema... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    const std::string invalid = R"({"schema":"wrong","version":1,"params":{}})";
    const std::vector<uint8_t> state(invalid.begin(), invalid.end());
    assert(!rumble.loadState(state));

    std::cout << "✅ PASS\n";
    return true;
}

bool testRejectsCorruptStateSize() {
    std::cout << "TEST: reject corrupt state size... ";

    RumbleInstance rumble;
    assert(rumble.initialize(48000.0, 512));

    std::vector<uint8_t> invalid(3, 0xAA);
    assert(!rumble.loadState(invalid));

    std::cout << "✅ PASS\n";
    return true;
}
} // namespace

int main() {
    std::cout << "\n=== Aestra Rumble State Test ===\n";

    testParameterContract();
    testJsonStateRoundTrip();
    testLegacyV2Migration();
    testJsonV1AdditiveMigration();
    testRejectsInvalidJsonSchema();
    testRejectsCorruptStateSize();

    std::cout << "\nAll Rumble state tests passed.\n";
    return 0;
}
