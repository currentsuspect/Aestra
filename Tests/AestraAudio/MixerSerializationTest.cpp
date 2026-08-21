// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MixerSerializationTest — session-fidelity contract for mixer knob state.
//
// Extra Session 2026-08-21: "If a parameter changes the sound of a session,
// reopening the project must reproduce that sound state." Trim, pan and width
// are the mixer knobs this pins down.

#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"

#include <cmath>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

namespace {

constexpr double kBpm = 120.0;
constexpr uint32_t kSampleRate = 48000;

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "PASS: " << label << "\n";
    }
}

bool near(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

} // namespace

int main() {
    std::cout << "=== Mixer Serialization Tests ===\n";
    std::cout << "(Trim/Pan/Width survive save/reopen)\n\n";

    Aestra::Tests::ScopedTempDirectory dir{"MixerSerialization"};
    const std::string projectPath = (dir.path() / "mixer.aes").string();

    auto saved = std::make_shared<TrackManager>();
    saved->setOutputSampleRate(static_cast<double>(kSampleRate));
    MixerChannel* ch = saved->addChannel("Lead");
    check(ch != nullptr, "channel created");
    if (!ch) {
        return 1;
    }

    // Non-default knob positions (the values a producer would actually set).
    const float kTrimDb = -3.5f;
    const float kPan = 0.25f;
    const float kWidth = 1.75f;
    const float kVolume = 0.8f;
    ch->setTrimDb(kTrimDb);
    ch->setPan(kPan);
    ch->setWidth(kWidth);
    ch->setVolume(kVolume);

    const bool okSave = ProjectSerializer::save(projectPath, saved, kBpm, 0.0);
    check(okSave, "project saved");

    auto loaded = std::make_shared<TrackManager>();
    loaded->setOutputSampleRate(static_cast<double>(kSampleRate));
    const auto result = ProjectSerializer::load(projectPath, loaded);
    check(result.ok, "project loaded");
    if (result.ok) {
        check(loaded->getChannelCount() == 1, "one channel after load");
        auto* loadedChannel = loaded->getChannel(0);
        check(loadedChannel != nullptr, "loaded channel resolvable");
        if (loadedChannel) {
            check(near(loadedChannel->getTrimDb(), kTrimDb), "trim survives save/reopen");
            check(near(loadedChannel->getPan(), kPan), "pan survives save/reopen");
            check(near(loadedChannel->getWidth(), kWidth), "width survives save/reopen");
            check(near(loadedChannel->getVolume(), kVolume), "volume survives save/reopen");
        }
    }

    std::cout << "\n";
    if (g_failures > 0) {
        std::cout << "FAILED: " << g_failures << " assertion(s).\n";
        return 1;
    }
    std::cout << "All mixer serialization tests passed.\n";
    return 0;
}
