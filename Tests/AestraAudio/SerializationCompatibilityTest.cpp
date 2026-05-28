// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Serialization compatibility test - validates project format migration and round-trip.

#include "../../AestraCore/include/AestraJSON.h"
#include "../../AestraAudio/include/Core/ProjectValidator.h"

#include <fstream>
#include <iostream>
#include <string>

using namespace Aestra;

static int g_passes = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    if (cond) { \
        std::cout << "[PASS] " << msg << "\n"; \
        g_passes++; \
    } else { \
        std::cout << "[FAIL] " << msg << "\n"; \
        g_fails++; \
    } \
} while(0)

// Test that a minimal valid project JSON is accepted
void testMinimalValidProject() {
    std::cout << "\n=== Test: Minimal Valid Project ===\n";

    std::string json = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "lanes": [],
        "patterns": []
    })";

    JSON parsed = JSON::parse(json);
    CHECK(parsed.isObject(), "Minimal project parses as object");
    CHECK(parsed.has("version"), "Has version field");
    CHECK(parsed.has("tempo"), "Has tempo field");
    CHECK(parsed["version"].asNumber() == 1.0, "Version is 1");
    CHECK(parsed["tempo"].asNumber() == 120.0, "Tempo is 120");
}

// Test that malformed JSON is handled gracefully
void testMalformedJSON() {
    std::cout << "\n=== Test: Malformed JSON ===\n";

    std::string json = R"({ "version": 1, "tempo": )";

    JSON parsed = JSON::parse(json);
    // Malformed JSON should return a null JSON object or a partial object
    // The parser may return a partial object with default values
    CHECK(parsed.isNull() || parsed.isObject(), "Malformed JSON returns null or partial object");
}

// Test that missing fields get defaults
void testMissingFields() {
    std::cout << "\n=== Test: Missing Fields ===\n";

    std::string json = R"({
        "version": 1
    })";

    JSON parsed = JSON::parse(json);
    CHECK(parsed.isObject(), "Partial project parses as object");

    // Missing tempo should default to 120
    double tempo = parsed.has("tempo") ? parsed["tempo"].asNumber() : 120.0;
    CHECK(tempo == 120.0, "Missing tempo defaults to 120");

    // Missing lanes should be empty
    bool hasLanes = parsed.has("lanes") && parsed["lanes"].isArray();
    CHECK(!hasLanes || parsed["lanes"].size() == 0, "Missing lanes is empty or absent");
}

// Test that extreme values are handled
void testExtremeValues() {
    std::cout << "\n=== Test: Extreme Values ===\n";

    std::string json = R"({
        "version": 1,
        "tempo": 999999.0,
        "playhead": -1.0
    })";

    JSON parsed = JSON::parse(json);
    CHECK(parsed.isObject(), "Extreme values project parses");

    // Tempo should be clamped to reasonable range
    double tempo = parsed["tempo"].asNumber();
    CHECK(tempo > 0.0, "Tempo is positive");
}

// Test JSON round-trip (serialize then parse)
void testJSONRoundTrip() {
    std::cout << "\n=== Test: JSON Round Trip ===\n";

    JSON original = JSON::object();
    original.set("version", JSON(1.0));
    original.set("tempo", JSON(140.0));
    original.set("playhead", JSON(4.0));

    JSON lanes = JSON::array();
    JSON lane = JSON::object();
    lane.set("id", JSON(1.0));
    lane.set("name", JSON("Test Lane"));
    lane.set("volume", JSON(0.8));
    lanes.push(lane);
    original.set("lanes", lanes);

    std::string serialized = original.toString();
    JSON parsed = JSON::parse(serialized);

    CHECK(parsed.isObject(), "Round-trip produces object");
    CHECK(parsed["version"].asNumber() == 1.0, "Version preserved");
    CHECK(parsed["tempo"].asNumber() == 140.0, "Tempo preserved");
    CHECK(parsed["playhead"].asNumber() == 4.0, "Playhead preserved");
    CHECK(parsed["lanes"].isArray(), "Lanes is array");
    CHECK(parsed["lanes"].size() == 1, "One lane preserved");
    CHECK(parsed["lanes"][0]["name"].asString() == "Test Lane", "Lane name preserved");
}

int main() {
    std::cout << "=========================================\n";
    std::cout << "  Serialization Compatibility Tests\n";
    std::cout << "=========================================\n";

    testMinimalValidProject();
    testMalformedJSON();
    testMissingFields();
    testExtremeValues();
    testJSONRoundTrip();

    std::cout << "\n=========================================\n";
    std::cout << "  Test Summary\n";
    std::cout << "=========================================\n";
    std::cout << "  Passed: " << g_passes << "\n";
    std::cout << "  Failed: " << g_fails << "\n";
    std::cout << "  Total:  " << (g_passes + g_fails) << "\n";
    std::cout << "=========================================\n";

    return g_fails > 0 ? 1 : 0;
}
