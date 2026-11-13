// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Your 808s (SPEC-025 R7): Keep writes the sound to the producer's own presets, exactly (the click grain too), under a
// name that never overwrites another; the shelf lists them; a keep can be undone; any project opens them.

#include "RumbleInstance.h"
#include "RumbleKept.h"
#include "RumblePresetBank.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Aestra::Plugins;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

bool same(const RumbleSound& a, const RumbleSound& b) {
    return a.v == b.v && a.seed == b.seed;
}

fs::path freshRoot() {
    const fs::path root = fs::temp_directory_path() / "aestra_rumble_kept_test";
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

RumbleSound aSound(uint32_t seed) {
    RumbleSound s = rumbleSoundOf(kRumbleFactoryPresets[2]);
    s[RumbleSound::Tail] = 0.4321;
    s.seed = seed;
    return s;
}

void keepWritesTheSoundExactly() {
    RumbleKept kept(freshRoot());
    check(kept.list().empty(), "nothing kept to start");
    const auto k = kept.keep(aSound(77), "Long dirty");
    check(k.has_value(), "keeping works");
    if (!k) return;
    check(k->name == "Long dirty" && fs::exists(k->path), "under its own name, as a file");
    const auto all = kept.list();
    check(all.size() == 1 && same(all[0].sound, aSound(77)), "the shelf lists it with the exact sound and grain");
}

void namesNeverOverwrite() {
    RumbleKept kept(freshRoot());
    kept.keep(aSound(1), "Long dirty");
    const auto second = kept.keep(aSound(2), "Long dirty");
    check(second && second->name == "Long dirty 2", "a second sound with the same name gets a number");
    const auto third = kept.keep(aSound(3), "Long dirty");
    check(third && third->name == "Long dirty 3", "and the next one the next number");
    check(kept.list().size() == 3, "all three are kept");
}

void aKeepCanBeUndone() {
    RumbleKept kept(freshRoot());
    const auto k = kept.keep(aSound(5), "Short knock");
    check(k && kept.unkeep(*k), "undo removes it");
    check(kept.list().empty() && !fs::exists(k->path), "and its file");
}

void anyRumbleOpensAKeptSound() {
    RumbleKept kept(freshRoot());
    const auto k = kept.keep(aSound(9), "Deep clean");
    RumbleInstance rumble;
    check(k && rumble.loadState(k->state), "the kept state loads into an instrument");
    // The instrument stores settings as floats, so compare at float precision.
    bool close = rumble.sound().seed == aSound(9).seed;
    for (size_t id = 0; id < RumbleSound::Count; ++id) close = close && std::fabs(rumble.sound()[id] - aSound(9)[id]) < 1.0e-6;
    check(close, "and plays the same sound");
    const float velocity = 0.9f;
    rumble.setParameter(RumbleInstance::kVelocity, velocity);
    rumble.loadState(k->state);
    check(rumble.getParameter(RumbleInstance::kVelocity) == velocity,
          "keeping a sound does not carry how hard it is played: Velocity stays the instrument's");
}

void otherPluginsPresetsAreNotListed() {
    const fs::path root = freshRoot();
    RumbleKept kept(root);
    fs::create_directories(root / "Verb");
    std::ofstream(root / "Verb" / "Hall.aepreset")
        << R"({"format":1,"name":"Hall","pluginId":"com.Aestrastudios.verb","params":[]})";
    kept.keep(aSound(4), "Mine");
    const auto all = kept.list();
    check(all.size() == 1 && all[0].name == "Mine", "only Rumble's sounds are on the shelf");
}
} // namespace

int main() {
    keepWritesTheSoundExactly();
    namesNeverOverwrite();
    aKeepCanBeUndone();
    anyRumbleOpensAKeptSound();
    otherPluginsPresetsAreNotListed();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleKeptTest passed\n";
    return 0;
}
