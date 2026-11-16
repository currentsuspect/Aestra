// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-KEY1 (FD-76): the best guess of the key of a piece of audio (a sample, a loop, a recording).
//
// Pure data in, pure data out, like the rest of the harmony service: no project, no UI, no audio-thread code.
// It allocates and takes a few hundred milliseconds per minute of audio, so it runs on a worker and its result
// is cached by whoever asks. The answer is a guess: callers show `confidence` and let the user overrule it.
#pragma once

#include "Harmony/HarmonyAnalysis.h"

#include <array>
#include <cstdint>

namespace Aestra {
namespace Audio {
namespace Harmony {

struct AudioKeyResult {
    Key key;                         // invalid (tonic -1) for silence or audio too short to judge
    std::array<double, 12> chroma{}; // energy per pitch class (index 0 = C), normalised to sum 1; all 0 when invalid
    double tuningCents = 0.0;        // how far the audio sits from A = 440 Hz, -50..+50
    /**
     * 0..1: how much the audio is made of held pitches rather than noise or drums. A drum loop scores low and a
     * chord loop high. Below kPitchedThreshold the key is a coin toss and should not be shown as a key.
     */
    double pitchedness = 0.0;

    static constexpr double kPitchedThreshold = 0.25;
    bool pitched() const { return key.valid() && pitchedness >= kPitchedThreshold; }
};

/**
 * Detect the key of interleaved float audio.
 * Reads at most the first kMaxAnalysedSeconds; anything shorter than kMinAnalysedSeconds has no key.
 */
AudioKeyResult detectAudioKey(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate);

constexpr double kMinAnalysedSeconds = 0.25;
constexpr double kMaxAnalysedSeconds = 240.0;

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
