// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// BufferConfigExceptionSafetyTest (#731)
// ─────────────────────────────────────────────────────────────────────────────
// Pins the exception-safety contract of AudioEngine::setBufferConfig:
//
//   1. When buffer configuration throws (allocation failure), the RTConfigAdmission
//      gate must be released on the way out — a stuck gate would refuse every
//      future processBlock, silencing the engine forever. The gate release is
//      RAII-owned by BufferConfigGateGuard, so no exception path can skip it.
//   2. Configuration values are published only after every allocation succeeds.
//      A throw mid-config must leave the engine fully on its previous config
//      (channel count, max block size, prepare bookkeeping) — not half-applied.
//   3. Repeated throwing attempts must never wedge the engine: it keeps
//      rendering silence afterwards (the documented no-content output).
//
// The allocation failure is injected through a test-only fault-injection flag
// on AudioEngine (setSimulateBufferConfigAllocFailure), so the exception path
// is deterministic on every platform and sanitizer-safe — astronomically-sized
// allocations would trip LSan's size interceptor and TSan's shadow allocator
// instead of throwing.

#include "Core/AudioEngine.h"

#include <cstdio>
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

#define CHECK_MSG(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__, msg);       \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

constexpr uint32_t kFrames = 256;
constexpr uint32_t kChannels = 2;

// Render one block into a kFrames*kChannels buffer pre-filled with a sentinel.
// The documented no-content output is silence, so any surviving sentinel or
// non-zero sample means the engine did not fully write the block. (Returns 0
// on both the admitted and denied paths, so admission itself is asserted via
// the setBufferConfig re-entry checks instead.)
void renderSilenceBlock(AudioEngine& engine) {
    std::vector<float> buffer(static_cast<size_t>(kFrames) * kChannels, 0.5f);
    engine.processBlock(buffer.data(), nullptr, kFrames, 0.0);
    bool clean = true;
    for (float s : buffer) {
        if (s != 0.0f) {
            clean = false;
            break;
        }
    }
    CHECK(clean);
}

// Attempt a config with the fault-injection flag set; assert it throws.
void attemptThrowingConfig(AudioEngine& engine) {
    engine.setSimulateBufferConfigAllocFailure(true);
    bool threw = false;
    try {
        engine.setBufferConfig(8192, 4);
    } catch (const std::exception&) {
        threw = true;
    }
    engine.setSimulateBufferConfigAllocFailure(false);
    if (!threw) {
        std::fprintf(stderr, "FAIL (line %d): %s\n", __LINE__,
                     "injected allocation failure must throw");
        ++g_failures;
    }
}

// 1. A throwing config releases the admission gate: the next setBufferConfig
//    succeeds and processBlock is admitted again.
void testThrowingConfigReleasesAdmissionGate() {
    AudioEngine engine;
    engine.setSampleRate(48000);
    CHECK_MSG(engine.setBufferConfig(kFrames, kChannels), "baseline config accepted");

    attemptThrowingConfig(engine);

    CHECK_MSG(engine.setBufferConfig(512, kChannels), "config succeeds again: gate released");
    renderSilenceBlock(engine);
    CHECK_MSG(engine.getSampleRate() == 48000, "sample rate untouched by failed config");
    std::puts("  throwing-config-releases-gate: ok");
}

// 2. The half-applied state is never published: after a throw, the engine's
//    channel width and max block size still match the last successful config.
//    (The width check here is behavioral — rendering a 2-channel block stays
//    in bounds. Under ASan/valgrind CI a poisoned channel count overruns.)
void testFailedConfigLeavesPreviousConfigIntact() {
    AudioEngine engine;
    engine.setSampleRate(44100);
    CHECK_MSG(engine.setBufferConfig(kFrames, kChannels), "baseline config accepted");

    attemptThrowingConfig(engine);

    renderSilenceBlock(engine);
    CHECK_MSG(engine.getSampleRate() == 44100, "sample rate unchanged");
    CHECK_MSG(engine.setBufferConfig(kFrames, kChannels), "re-applying the same config succeeds");
    renderSilenceBlock(engine);
    std::puts("  failed-config-keeps-old-config: ok");
}

// 3. Repeated throwing attempts never wedge the engine: gate stays free, and
//    rendering keeps producing clean silence between attempts.
void testRepeatedThrowsNeverWedgeEngine() {
    AudioEngine engine;
    engine.setSampleRate(48000);
    CHECK_MSG(engine.setBufferConfig(kFrames, kChannels), "baseline config accepted");

    for (int i = 0; i < 3; ++i) {
        attemptThrowingConfig(engine);
        renderSilenceBlock(engine);
        CHECK_MSG(engine.setBufferConfig(kFrames + static_cast<uint32_t>(i * 32), kChannels),
                  "recovery config accepted after throw");
        renderSilenceBlock(engine);
    }
    std::puts("  repeated-throws-recover: ok");
}

} // namespace

int main() {
    std::puts("BufferConfigExceptionSafetyTest:");
    testThrowingConfigReleasesAdmissionGate();
    testFailedConfigLeavesPreviousConfigIntact();
    testRepeatedThrowsNeverWedgeEngine();

    if (g_failures == 0) {
        std::puts("BufferConfigExceptionSafetyTest: PASS");
        return 0;
    }
    std::fprintf(stderr, "BufferConfigExceptionSafetyTest: FAILED (%d checks)\n", g_failures);
    return 1;
}
