// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The outline of a Rumble sound and its worker (SPEC-025 R3): bins equal to the sketch's own peaks(), the
// picture's scale, and a worker that makes each sound once, newest first, never on the caller's thread.

#include "RumbleOutline.h"
#include "RumblePresetBank.h"
#include "RumbleVoice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "RumbleOutlineGolden.inc"

using namespace Aestra::Plugins;
using Clock = std::chrono::steady_clock;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

RumbleSound soundOf(const RumbleOutlineGoldenCase& c) {
    RumbleSound sound;
    for (size_t i = 0; i < RumbleSound::Count; ++i) {
        sound[i] = c.s[i];
    }
    sound.seed = c.seed;
    return sound;
}

// A spread of distinct sounds for the worker tests.
RumbleSound nthSound(int n) {
    RumbleSound sound = rumbleSoundOf(kRumbleFactoryPresets[static_cast<size_t>(n) % kRumbleFactoryPresets.size()]);
    sound[RumbleSound::Tail] = 0.05 + 0.9 * ((n * 37) % 100) / 100.0;
    sound.seed = 5000u + static_cast<uint32_t>(n);
    return sound;
}

double millisSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void drawsExactlyLikeTheSketch() {
    double worst = 0.0;
    for (const auto& c : rumbleOutlineGoldenCases()) {
        const RumbleOutline outline = RumbleOutline::of(soundOf(c), c.rate);
        check(c.bins.size() == RumbleOutline::kBins, "the golden case has every bin");
        for (size_t j = 0; j < RumbleOutline::kBins && j < c.bins.size(); ++j) {
            worst = std::max(worst, std::fabs(static_cast<double>(outline.bins[j]) - c.bins[j]));
        }
    }
    std::cout << rumbleOutlineGoldenCases().size() << " sketch outlines, worst bin difference " << worst << '\n';
    check(worst < 1.0e-5, "outlines match the sketch's peaks() bin for bin (worst " + std::to_string(worst) + ")");
}

void carriesTheLevelAndLength() {
    const RumbleSound sound = rumbleSoundOf(kRumbleFactoryPresets[0]);
    const RumbleOutline outline = RumbleOutline::of(sound, 48000.0);
    const double raw = RumbleVoice::rawPeak(sound, RumbleOutline::kPictureNote, 48000.0);
    check(std::fabs(outline.rawPeak - raw) < 1.0e-12, "the outline carries the sound's level at the picture note");
    check(std::fabs(outline.lengthSeconds - RumbleVoice::lengthSeconds(sound)) < 1.0e-12, "and its length");
    const float top = *std::max_element(outline.bins.begin(), outline.bins.end());
    // The sketch reads every third sample, so the drawn top can sit a little under the true peak (0.83 to 0.85
    // over the golden cases): close to the one levelled peak, never over it.
    check(top <= RumbleVoice::kLevelledPeak + 1.0e-6 && top > RumbleVoice::kLevelledPeak * 0.97,
          "every outline reaches close to the one levelled peak (" + std::to_string(top) + ")");
}

void theScaleIsTheSketchs() {
    check(RumbleOutline::xOf(0.0) == 0.0 && std::fabs(RumbleOutline::xOf(RumbleOutline::kSeconds) - 1.0) < 1e-12,
          "the picture runs from 0 to 3.9 s");
    check(std::fabs(RumbleOutline::xOf(0.39) - std::sqrt(0.1)) < 1e-12, "on a square-root scale");
    check(RumbleOutline::xOf(-1.0) == 0.0 && RumbleOutline::xOf(10.0) == 1.0, "and stays inside it");
    check(std::fabs(RumbleOutline::secondsAt(RumbleOutline::xOf(1.234)) - 1.234) < 1e-12, "secondsAt undoes xOf");
    check(std::fabs(RumbleOutline::xOfBin(65) - 0.5) < 1e-12, "bin 65 of 260 sits half way across");

    const RumbleSound sound = rumbleSoundOf(kRumbleFactoryPresets[0]);
    const double drop = RumbleVoice::dropSemitones(sound[RumbleSound::Punch]);
    check(std::fabs(RumbleOutline::pitchAbove(sound, 0.0) - drop) < 1e-12, "the pitch line starts at the full drop");
    const double t = RumbleVoice::dropSeconds(sound);
    check(std::fabs(RumbleOutline::pitchAbove(sound, t) - drop * std::exp(-1.0)) < 1e-12,
          "and falls as the voice's pitch does");
    check(RumbleOutline::pitchAbove(sound, RumbleVoice::lengthSeconds(sound)) < 0.1, "landing on the note");
}

void theWorkerMakesItOffTheCallersThread() {
    RumbleOutlineWorker worker(48000.0);
    const auto start = Clock::now();
    for (int n = 0; n < 7; ++n) {
        worker.request(nthSound(n));
    }
    const double asked = millisSince(start);
    check(asked < 5.0, "asking for seven outlines costs the caller nothing (" + std::to_string(asked) + " ms)");
    check(worker.waitUntilIdle(30000), "the worker finishes");
    for (int n = 0; n < 7; ++n) {
        const auto made = worker.find(nthSound(n));
        check(made != nullptr, "outline " + std::to_string(n) + " is made");
        if (made) {
            const RumbleOutline direct = RumbleOutline::of(nthSound(n), 48000.0);
            check(made->bins == direct.bins && made->rawPeak == direct.rawPeak, "and it is the sound's outline");
        }
    }
    check(worker.find(nthSound(99)) == nullptr, "a sound never asked for has no outline");
}

void eachSoundIsMadeOnce() {
    RumbleOutlineWorker worker(48000.0);
    for (int i = 0; i < 5; ++i) {
        worker.request(nthSound(1));
    }
    worker.waitUntilIdle(30000);
    worker.request(nthSound(1));
    worker.waitUntilIdle(30000);
    check(worker.rendersDone() == 1, "asking six times renders once (" + std::to_string(worker.rendersDone()) + ")");
    RumbleSound other = nthSound(1);
    other.seed += 1;
    worker.request(other);
    worker.waitUntilIdle(30000);
    check(worker.rendersDone() == 2, "a different click grain is a different sound");
}

void theNewestArrivesFirst() {
    RumbleOutlineWorker worker(48000.0);
    for (int n = 0; n < 30; ++n) {
        worker.request(nthSound(n));
    }
    worker.request(nthSound(100)); // the card the producer just looked at
    const auto start = Clock::now();
    while (!worker.find(nthSound(100)) && millisSince(start) < 30000.0) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    check(worker.find(nthSound(100)) != nullptr, "the newest outline arrives");
    // At most the one already being made, and the newest, can be done when the newest arrives.
    check(worker.rendersDone() <= 2, "before the older ones (" + std::to_string(worker.rendersDone()) + " made)");
    worker.waitUntilIdle(30000);
}

void aNewRoundDropsTheOldRequests() {
    RumbleOutlineWorker worker(48000.0);
    for (int n = 0; n < 40; ++n) {
        worker.request(nthSound(n));
    }
    worker.forgetPending();
    check(worker.waitUntilIdle(30000), "idle after forgetting");
    check(worker.rendersDone() <= 1, "only the one already started is made (" + std::to_string(worker.rendersDone()) + ")");
}

void keepsABoundedNumber() {
    RumbleOutlineWorker worker(48000.0, 8);
    for (int n = 0; n < 12; ++n) {
        worker.request(nthSound(n));
        worker.waitUntilIdle(30000);
    }
    int kept = 0;
    for (int n = 0; n < 12; ++n) {
        kept += worker.find(nthSound(n)) ? 1 : 0;
    }
    check(kept == 8, "keeps at most its capacity (" + std::to_string(kept) + ")");
    check(worker.find(nthSound(11)) != nullptr && worker.find(nthSound(0)) == nullptr, "dropping the least used first");
}

void closesPromptly() {
    const auto start = Clock::now();
    {
        RumbleOutlineWorker worker(48000.0);
        for (int n = 0; n < 200; ++n) {
            worker.request(nthSound(n));
        }
    }
    const double took = millisSince(start);
    check(took < 500.0, "closing with work queued does not wait for it (" + std::to_string(took) + " ms)");
}
} // namespace

int main() {
    drawsExactlyLikeTheSketch();
    carriesTheLevelAndLength();
    theScaleIsTheSketchs();
    theWorkerMakesItOffTheCallersThread();
    eachSoundIsMadeOnce();
    theNewestArrivesFirst();
    aNewRoundDropsTheOldRequests();
    keepsABoundedNumber();
    closesPromptly();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleOutlineTest passed\n";
    return 0;
}
