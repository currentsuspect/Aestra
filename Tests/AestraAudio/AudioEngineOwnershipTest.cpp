// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AudioEngineOwnershipTest (R2)
// ─────────────────────────────────────────────────────────────────────────────
// The engine used to carry a process-wide instance pointer registered from the
// constructor (last-constructed-wins) and cleared from the destructor. That
// mechanism is gone: every AudioEngine is explicitly owned by its creator.
//
// These tests pin the ownership contract that replaces it:
//   1. Two simultaneously alive engines operate independently (config + render).
//   2. Destroying one engine does not affect another that is still alive.
//   3. Construction/destruction order can be interleaved arbitrarily — no
//      startup or shutdown step depends on any global registration.
//
// Rendering here goes through the real processBlock path with no track manager
// attached (silence), which is exactly what matters: the call must be safe,
// finite, and engine-local at every lifecycle stage.

#include "Core/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, #cond);     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

// Render one block and verify every sample is finite. Returns peak magnitude.
float renderBlock(AudioEngine& engine) {
    std::vector<float> buffer(static_cast<size_t>(kFrames) * kChannels, 0.5f);
    engine.processBlock(buffer.data(), nullptr, kFrames, 0.0);
    float peak = 0.0f;
    for (float s : buffer) {
        if (!std::isfinite(s)) {
            ++g_failures;
            std::fprintf(stderr, "FAIL: non-finite sample in rendered block\n");
            return peak;
        }
        peak = std::max(peak, std::fabs(s));
    }
    return peak;
}

void configure(AudioEngine& engine, uint32_t sampleRate, float bpm) {
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setBPM(bpm);
}

// 1. Two simultaneously alive engines keep independent config and both render.
void testTwoLiveEnginesAreIndependent() {
    AudioEngine a;
    AudioEngine b;
    configure(a, 48000, 120.0f);
    configure(b, 44100, 174.0f);

    CHECK(a.getSampleRate() == 48000);
    CHECK(b.getSampleRate() == 44100);
    CHECK(a.getBPM() == 120.0f);
    CHECK(b.getBPM() == 174.0f);

    // Mutating one must not leak into the other.
    a.setBPM(90.0f);
    CHECK(b.getBPM() == 174.0f);
    b.setSampleRate(96000);
    CHECK(a.getSampleRate() == 48000);

    renderBlock(a);
    renderBlock(b);

    // Both remain intact after both rendered.
    CHECK(a.getSampleRate() == 48000);
    CHECK(b.getSampleRate() == 96000);
    std::puts("  two-live-engines: ok");
}

// 2. Destroying one engine leaves the other fully functional.
void testDestructionIsolation() {
    auto a = std::make_unique<AudioEngine>();
    auto b = std::make_unique<AudioEngine>();
    configure(*a, 48000, 128.0f);
    configure(*b, 48000, 140.0f);
    renderBlock(*a);
    renderBlock(*b);

    // Destroy the LATER-constructed engine first: under the old registration
    // scheme this was the corrupting order (it owned the global slot).
    b.reset();
    CHECK(a->getBPM() == 128.0f);
    renderBlock(*a);

    // And the other way round on fresh engines.
    auto c = std::make_unique<AudioEngine>();
    auto d = std::make_unique<AudioEngine>();
    configure(*c, 44100, 100.0f);
    configure(*d, 44100, 101.0f);
    c.reset(); // destroy the EARLIER-constructed engine first
    CHECK(d->getBPM() == 101.0f);
    renderBlock(*d);
    std::puts("  destruction-isolation: ok");
}

// 3. Arbitrary interleaving of lifetimes: construct, destroy, reconstruct —
//    every stage renders. Nothing global is registered or required.
void testInterleavedLifetimes() {
    auto a = std::make_unique<AudioEngine>();
    configure(*a, 48000, 120.0f);
    renderBlock(*a);

    auto b = std::make_unique<AudioEngine>();
    configure(*b, 88200, 60.0f);
    a.reset();
    renderBlock(*b);

    auto c = std::make_unique<AudioEngine>();
    configure(*c, 22050, 200.0f);
    renderBlock(*b);
    renderBlock(*c);
    b.reset();
    renderBlock(*c);
    CHECK(c->getSampleRate() == 22050);
    c.reset();

    // A fresh engine after everything died: still self-sufficient.
    AudioEngine e;
    configure(e, 48000, 120.0f);
    renderBlock(e);
    std::puts("  interleaved-lifetimes: ok");
}

} // namespace

int main() {
    std::puts("AudioEngineOwnershipTest:");
    testTwoLiveEnginesAreIndependent();
    testDestructionIsolation();
    testInterleavedLifetimes();

    if (g_failures == 0) {
        std::puts("AudioEngineOwnershipTest: PASS");
        return 0;
    }
    std::fprintf(stderr, "AudioEngineOwnershipTest: FAILED (%d checks)\n", g_failures);
    return 1;
}
