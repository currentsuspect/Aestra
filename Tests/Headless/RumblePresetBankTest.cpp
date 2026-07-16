// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Render-backed contract for the factory Rumble preset bank.

#include "RumblePresetBank.h"

#include "Plugin/PluginHost.h"
#include "RumbleInstance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

using Aestra::Audio::MidiBuffer;
using Aestra::Plugins::kRumbleFactoryPresets;
using Aestra::Plugins::RumbleFactoryPreset;
using Aestra::Plugins::RumbleInstance;

namespace {
struct RenderResult {
    std::vector<float> mono;
    bool finite = true;
    bool stereoMatched = true;
    float peak = 0.0f;
    double rms = 0.0;
};

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
    }
    return condition;
}

RenderResult renderPreset(const RumbleFactoryPreset& preset) {
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t blockSize = 127;
    constexpr uint64_t totalFrames = static_cast<uint64_t>(sampleRate * 1.25);
    constexpr uint64_t secondNoteFrame = static_cast<uint64_t>(sampleRate * 0.50);
    constexpr uint64_t secondNoteOffFrame = static_cast<uint64_t>(sampleRate * 0.88);
    constexpr uint64_t firstNoteOffFrame = static_cast<uint64_t>(sampleRate * 1.00);

    RumbleInstance rumble(RumbleInstance::TestLicense::GrantRumble);
    RenderResult result;
    if (!rumble.initialize(sampleRate, blockSize)) {
        result.finite = false;
        return result;
    }
    for (uint32_t parameterId = 0; parameterId < preset.values.size(); ++parameterId) {
        rumble.setParameter(parameterId, preset.values[parameterId]);
    }
    rumble.activate();

    result.mono.reserve(totalFrames);
    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* outputs[2] = {left.data(), right.data()};
    long double energy = 0.0;

    for (uint64_t frame = 0; frame < totalFrames; frame += blockSize) {
        const uint32_t framesThisBlock = static_cast<uint32_t>(std::min<uint64_t>(blockSize, totalFrames - frame));
        MidiBuffer midi;
        auto addEvent = [&](uint64_t eventFrame, bool noteOn, uint8_t note, uint8_t velocity) {
            if (eventFrame < frame || eventFrame >= frame + framesThisBlock) {
                return;
            }
            const uint32_t offset = static_cast<uint32_t>(eventFrame - frame);
            if (noteOn) {
                midi.addNoteOn(1, note, velocity, offset);
            } else {
                midi.addNoteOff(1, note, 0, offset);
            }
        };
        addEvent(0, true, 36, 112);
        addEvent(secondNoteFrame, true, 43, 96);
        addEvent(secondNoteOffFrame, false, 43, 0);
        addEvent(firstNoteOffFrame, false, 36, 0);

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        rumble.process(nullptr, outputs, 0, 2, framesThisBlock, &midi, nullptr);

        for (uint32_t i = 0; i < framesThisBlock; ++i) {
            result.finite = result.finite && std::isfinite(left[i]) && std::isfinite(right[i]);
            result.stereoMatched = result.stereoMatched && left[i] == right[i];
            result.peak = std::max(result.peak, std::abs(left[i]));
            energy += static_cast<long double>(left[i]) * static_cast<long double>(left[i]);
            result.mono.push_back(left[i]);
        }
    }
    result.rms = std::sqrt(energy / static_cast<long double>(result.mono.size()));
    return result;
}

double differenceRms(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    long double energy = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const long double delta = static_cast<long double>(a[i]) - static_cast<long double>(b[i]);
        energy += delta * delta;
    }
    return std::sqrt(energy / static_cast<long double>(a.size()));
}
} // namespace

int main() {
    bool ok = true;
    ok &= check(kRumbleFactoryPresets.size() >= 16, "factory bank has fewer than 16 presets");

    std::set<std::string> names;
    std::vector<RenderResult> renders;
    renders.reserve(kRumbleFactoryPresets.size());

    for (const auto& preset : kRumbleFactoryPresets) {
        ok &= check(!preset.name.empty(), "preset has an empty name");
        ok &= check(!preset.category.empty(), std::string(preset.name) + " has an empty category");
        ok &= check(!preset.description.empty(), std::string(preset.name) + " has an empty description");
        ok &= check(names.insert(std::string(preset.name)).second, std::string(preset.name) + " is duplicated");
        for (float value : preset.values) {
            ok &= check(std::isfinite(value) && value >= 0.0f && value <= 1.0f,
                        std::string(preset.name) + " has an invalid normalized parameter");
        }

        renders.push_back(renderPreset(preset));
        const auto& render = renders.back();
        ok &= check(render.finite, std::string(preset.name) + " produced NaN or Inf");
        ok &= check(render.stereoMatched, std::string(preset.name) + " is not exactly mono-compatible");
        ok &= check(render.peak > 1.0e-4f, std::string(preset.name) + " is silent");
        ok &= check(render.peak < 0.98f, std::string(preset.name) + " exceeded the safety ceiling");
        ok &= check(render.rms > 1.0e-5, std::string(preset.name) + " has negligible energy");
        std::cout << preset.category << " / " << preset.name << ": peak=" << render.peak << " rms=" << render.rms
                  << '\n';
    }

    for (size_t first = 0; first < kRumbleFactoryPresets.size(); ++first) {
        for (size_t second = first + 1; second < kRumbleFactoryPresets.size(); ++second) {
            const double delta = differenceRms(renders[first].mono, renders[second].mono);
            ok &= check(delta > 1.0e-4, std::string(kRumbleFactoryPresets[first].name) + " and " +
                                            std::string(kRumbleFactoryPresets[second].name) + " render too similarly");
        }
    }

    if (!ok) {
        return 1;
    }
    std::cout << "Rumble factory bank passed: 16 distinct, finite, audible, mono-safe presets.\n";
    return 0;
}
