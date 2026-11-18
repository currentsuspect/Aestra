// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleStateTest
// Pins what a saved Rumble sound is (FD-80): fifteen settings with stable ids and saved names, plus the click
// grain, in a "rumble-sound" JSON document. Changing any of these breaks every project that holds a Rumble.

#include "RumbleInstance.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Plugins::RumbleInstance;
using Aestra::Plugins::RumbleSound;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}
} // namespace

int main() {
    static const char* const kKeys[] = {"Punch",  "Tail",  "Weight",   "Tone",  "Dirt",        "Slide",    "DropTime", "Hold",
                                        "Curve", "Second", "Third", "ClipType", "Click", "ClickLength", "ClickTone", "Velocity"};
    RumbleInstance rumble;
    check(rumble.getParameterCount() == 16, "fifteen sound settings and Velocity");
    const auto parameters = rumble.getParameters();
    for (uint32_t id = 0; id < 16; ++id) {
        check(std::string(RumbleInstance::parameterKey(id)) == kKeys[id], std::string("setting ") + std::to_string(id) + " keeps its saved name " + kKeys[id]);
        check(parameters[id].id == id, "ids are their positions");
    }

    RumbleSound sound = rumble.sound();
    for (uint32_t id = 0; id < 15; ++id) {
        sound[id] = (id + 1) / 17.0;
    }
    sound.seed = 4242u;
    rumble.setSound(sound);
    const auto saved = rumble.saveState();
    const std::string text(saved.begin(), saved.end());
    check(text.find("\"rumble-sound\"") != std::string::npos, "saved as a rumble-sound document");
    for (const char* key : kKeys) {
        check(text.find(std::string("\"") + key + "\"") != std::string::npos, std::string("the document names ") + key);
    }

    RumbleInstance loaded;
    check(loaded.loadState(saved), "the document loads");
    for (uint32_t id = 0; id < 15; ++id) {
        check(std::fabs(loaded.getParameter(id) - static_cast<float>((id + 1) / 17.0)) < 1.0e-6f, std::string(kKeys[id]) + " round-trips");
    }
    check(loaded.sound().seed == 4242u, "the click grain round-trips");

    const std::string missing = R"({"schema":"rumble-sound","version":2,"params":{"Tail":0.9}})";
    RumbleInstance partial;
    check(partial.loadState(std::vector<uint8_t>(missing.begin(), missing.end())), "a document with some settings loads");
    check(std::fabs(partial.getParameter(RumbleSound::Tail) - 0.9f) < 1.0e-6f, "the given setting is used");
    check(std::fabs(partial.getParameter(RumbleSound::Punch) - 0.42f) < 1.0e-6f, "missing settings take Balanced 808's");
    check(std::fabs(partial.getParameter(15) - RumbleInstance::kDefaultVelocity) < 1.0e-6f, "a missing Velocity takes the default");

    for (const std::string bad : {std::string("not json"), std::string(R"({"schema":"rumble-preset","params":{}})"), std::string()}) {
        check(!partial.loadState(std::vector<uint8_t>(bad.begin(), bad.end())), "anything else is refused");
    }

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleStateTest passed\n";
    return 0;
}
