// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Holds the Rumble voice to the sketch the founder approved (FD-80, SPEC-025 R2): golden samples generated
// from the sketch's own code, the levelling, the streaming path, and the one-shot behaviour.

#include "RumbleVoice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "RumbleVoiceGolden.inc"

using Aestra::Plugins::RumbleSound;
using Aestra::Plugins::RumbleVoice;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

RumbleSound soundOf(const RumbleGoldenCase& c) {
    RumbleSound sound;
    for (size_t i = 0; i < RumbleSound::Count; ++i) {
        sound[i] = c.s[i];
    }
    sound.seed = c.seed;
    return sound;
}

void soundsExactlyLikeTheSketch() {
    double worst = 0.0;
    size_t compared = 0;
    for (const auto& c : rumbleGoldenCases()) {
        const RumbleSound sound = soundOf(c);
        const auto out = RumbleVoice::render(sound, c.note, c.rate);
        check(out.size() == c.length, "same length as the sketch");
        for (size_t k = 0; k < c.samples.size() && k * c.stride < out.size(); ++k) {
            worst = std::max(worst, static_cast<double>(std::fabs(out[k * c.stride] - c.samples[k])));
            ++compared;
        }
    }
    std::cout << rumbleGoldenCases().size() << " sounds, " << compared << " samples against the sketch; worst difference "
              << worst << '\n';
    check(worst < 1.0e-5, "every sample within 1e-5 of the sketch (about -100 dB)");
}

void levellingFindsTheTruePeak() {
    for (const auto& c : rumbleGoldenCases()) {
        const double head = RumbleVoice::rawPeak(soundOf(c), c.note, c.rate);
        check(std::fabs(head - c.peak) < 1.0e-9 * std::max(1.0, c.peak), "the peak is at the start of the sound");
    }
    // And over many more sounds than the golden set, against a full render's peak.
    uint32_t state = 77;
    auto unit = [&] { state = state * 1664525u + 1013904223u; return (state >> 8) / 16777216.0; };
    int missed = 0;
    for (int i = 0; i < 400; ++i) {
        RumbleSound sound;
        for (size_t k = 0; k < RumbleSound::Count; ++k) {
            sound[k] = unit();
        }
        sound.seed = state;
        const int note = 21 + static_cast<int>(unit() * 28);
        const auto full = RumbleVoice::render(sound, note, 48000.0);
        // render() levels to 0.85, so the raw peak is right exactly when the loudest output sample is 0.85.
        double loudest = 0.0;
        for (float v : full) {
            loudest = std::max(loudest, static_cast<double>(std::fabs(v)));
        }
        RumbleVoice voice;
        voice.prepare(48000.0);
        voice.start(sound, note, RumbleVoice::rawPeak(sound, note, 48000.0));
        double streamed = 0.0;
        while (voice.active()) {
            streamed = std::max(streamed, static_cast<double>(std::fabs(voice.next())));
        }
        if (std::fabs(streamed - loudest) > 1.0e-4) {
            ++missed;
        }
    }
    check(missed == 0, "streamed sounds peak exactly where the levelled render does (" + std::to_string(missed) + " of 400 missed)");
}

void streamingMatchesTheRender() {
    for (const auto& c : rumbleGoldenCases()) {
        const RumbleSound sound = soundOf(c);
        const auto rendered = RumbleVoice::render(sound, c.note, c.rate);
        RumbleVoice voice;
        voice.prepare(c.rate);
        voice.start(sound, c.note, c.peak);
        double worst = 0.0;
        size_t i = 0;
        while (voice.active()) {
            const float v = voice.next();
            check(std::isfinite(v), "finite output");
            if (i < rendered.size()) {
                worst = std::max(worst, static_cast<double>(std::fabs(v - rendered[i])));
            }
            ++i;
        }
        check(i == rendered.size(), "a note rings for exactly its own length");
        check(worst < 1.0e-6, "the instrument plays the same samples as the render");
    }
}

void slideBendsAndCutsAreClean() {
    const RumbleSound sound = soundOf(rumbleGoldenCases()[6]); // the sketch's Glide lead start
    RumbleVoice voice;
    voice.prepare(48000.0);
    voice.start(sound, 29, RumbleVoice::rawPeak(sound, 29, 48000.0));
    for (int i = 0; i < 4800; ++i) {
        voice.next();
    }
    voice.slideTo(36);
    float biggestStep = 0.0f;
    float last = voice.next();
    for (int i = 0; i < 24000 && voice.active(); ++i) {
        const float v = voice.next();
        check(std::isfinite(v) && std::fabs(v) <= 0.86f, "a slide stays finite and levelled");
        biggestStep = std::max(biggestStep, std::fabs(v - last));
        last = v;
    }
    check(biggestStep < 0.2f, "a slide has no jumps");
    voice.stop();
    int left = 0;
    while (voice.active()) {
        voice.next();
        ++left;
    }
    check(left > 0 && left < 2000, "a cut-off note fades out quickly");
}
} // namespace

int main() {
    soundsExactlyLikeTheSketch();
    levellingFindsTheTruePeak();
    streamingMatchesTheRender();
    slideBendsAndCutsAreClean();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleVoiceTest passed\n";
    return 0;
}
