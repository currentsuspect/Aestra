// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-KEY1 (FD-76): key detection for audio. See AudioKeyDetect.h for the contract.
//
// Pipeline: mix to mono -> low-pass and decimate to about 11 kHz (the notes that decide a key sit below 2 kHz)
//   -> Hann-windowed frames, each measured with a Goertzel filter at three points per semitone from C2 to B6
//   -> the three points per semitone give the tuning offset from A = 440 Hz
//   -> fold the magnitudes into twelve pitch classes (the chroma)
//   -> the same Krumhansl-Schmuckler scorer the MIDI path uses (keyFromPitchClassWeights).
// Goertzel instead of an FFT because only 180 frequencies are needed and they are not on an FFT's grid.
// Not real-time code: it allocates and is meant for a worker thread.
#include "Harmony/AudioKeyDetect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Harmony {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTargetRate = 11025.0;
constexpr int kFrame = 8192; // 0.74 s at 11 kHz: fine enough to tell C2 from C#2
constexpr int kHop = 4096;
constexpr int kLowestMidi = 36; // C2
constexpr int kSemitones = 60;  // C2..B6
constexpr int kPointsPerSemitone = 3;
constexpr int kPoints = kSemitones * kPointsPerSemitone;
constexpr double kPointCents[kPointsPerSemitone] = {-100.0 / 3.0, 0.0, 100.0 / 3.0};

/** Mono, low-passed and decimated copy of the first kMaxAnalysedSeconds. */
std::vector<float> decimatedMono(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate,
                                 double& outRate) {
    const int factor = std::max(1, static_cast<int>(std::lround(sampleRate / kTargetRate)));
    outRate = static_cast<double>(sampleRate) / factor;
    const uint64_t maxFrames = static_cast<uint64_t>(kMaxAnalysedSeconds * sampleRate);
    frames = std::min(frames, maxFrames);

    std::vector<float> out;
    out.reserve(static_cast<size_t>(frames / factor) + 1);

    // Four one-pole low-passes in a row at 3 kHz: about 40 dB down where content would fold back under 2 kHz.
    const double a = factor > 1 ? 1.0 - std::exp(-2.0 * kPi * 3000.0 / sampleRate) : 1.0;
    double z[4] = {0, 0, 0, 0};
    const double invCh = 1.0 / channels;
    for (uint64_t i = 0; i < frames; ++i) {
        double x = 0;
        const float* f = interleaved + i * channels;
        for (uint32_t c = 0; c < channels; ++c)
            x += f[c];
        x *= invCh;
        if (!std::isfinite(x))
            x = 0;
        for (double& s : z) {
            s += a * (x - s);
            x = s;
        }
        if (i % factor == 0)
            out.push_back(static_cast<float>(x));
    }
    return out;
}

} // namespace

AudioKeyResult detectAudioKey(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate) {
    AudioKeyResult result;
    if (!interleaved || channels == 0 || sampleRate < 8000 ||
        static_cast<double>(frames) < kMinAnalysedSeconds * sampleRate)
        return result;

    double rate = 0;
    const std::vector<float> mono = decimatedMono(interleaved, frames, channels, sampleRate, rate);
    const int total = static_cast<int>(mono.size());
    const int frameLen = std::min(kFrame, total); // a short one-shot is one frame of its own length
    if (frameLen < 256)
        return result;

    std::vector<double> coeff(kPoints);
    for (int p = 0; p < kPoints; ++p) {
        const double midi = kLowestMidi + p / kPointsPerSemitone + kPointCents[p % kPointsPerSemitone] / 100.0;
        const double hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        coeff[p] = 2.0 * std::cos(2.0 * kPi * hz / rate);
    }
    std::vector<double> window(frameLen);
    for (int i = 0; i < frameLen; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (frameLen - 1));

    // First pass: every frame's magnitudes and its level, so quiet tails can be dropped against the loudest frame.
    struct Frame {
        std::vector<float> mag;
        double level = 0;
    };
    std::vector<Frame> measured;
    std::vector<double> x(frameLen);
    double loudest = 0;
    for (int start = 0; start + frameLen <= total; start += kHop) {
        double level = 0;
        for (int i = 0; i < frameLen; ++i) {
            x[i] = mono[start + i] * window[i];
            level += x[i] * x[i];
        }
        Frame fr;
        fr.level = level;
        fr.mag.resize(kPoints);
        for (int p = 0; p < kPoints; ++p) {
            const double c = coeff[p];
            double s1 = 0, s2 = 0;
            for (int i = 0; i < frameLen; ++i) {
                const double s0 = x[i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            fr.mag[p] = static_cast<float>(std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)));
        }
        loudest = std::max(loudest, level);
        measured.push_back(std::move(fr));
    }
    if (measured.empty() || loudest <= 1e-12)
        return result;

    // Sum the frames that are within 50 dB of the loudest one.
    std::vector<double> sum(kPoints, 0.0);
    double pitchedSum = 0, pitchedWeight = 0;
    std::vector<float> sorted(kPoints);
    for (const Frame& fr : measured) {
        if (fr.level < loudest * 1e-5)
            continue;
        double frameTotal = 0;
        for (int p = 0; p < kPoints; ++p) {
            sum[p] += fr.mag[p];
            frameTotal += static_cast<double>(fr.mag[p]) * fr.mag[p];
        }
        // Held pitches put most of a frame's energy into a few points; noise and drums spread it over all of them.
        sorted = fr.mag;
        const int top = kPoints / 10;
        std::nth_element(sorted.begin(), sorted.begin() + top, sorted.end(), std::greater<float>());
        double topEnergy = 0;
        for (int i = 0; i < top; ++i)
            topEnergy += static_cast<double>(sorted[i]) * sorted[i];
        const double share = frameTotal > 0 ? topEnergy / frameTotal : 0;
        const double w = std::sqrt(fr.level);
        pitchedSum += w * std::clamp((share - 0.35) / 0.5, 0.0, 1.0);
        pitchedWeight += w;
    }
    result.pitchedness = pitchedWeight > 0 ? pitchedSum / pitchedWeight : 0.0;

    // Tuning: which of the three points per semitone holds the energy, as an angle on a 100-cent circle.
    double re = 0, im = 0;
    for (int p = 0; p < kPoints; ++p) {
        const double angle = 2.0 * kPi * kPointCents[p % kPointsPerSemitone] / 100.0;
        const double e = sum[p] * sum[p];
        re += e * std::cos(angle);
        im += e * std::sin(angle);
    }
    result.tuningCents = (re == 0 && im == 0) ? 0.0 : std::atan2(im, re) / (2.0 * kPi) * 100.0;

    // Fold into pitch classes around the measured tuning.
    std::array<double, 12> chroma{};
    for (int p = 0; p < kPoints; ++p) {
        const double cents = (kLowestMidi + p / kPointsPerSemitone) * 100.0 + kPointCents[p % kPointsPerSemitone];
        const long semitone = std::lround((cents - result.tuningCents) / 100.0);
        chroma[static_cast<size_t>(((semitone % 12) + 12) % 12)] += sum[p];
    }
    double chromaTotal = 0;
    for (double v : chroma)
        chromaTotal += v;
    if (chromaTotal <= 0)
        return result;
    for (double& v : chroma)
        v /= chromaTotal;

    result.chroma = chroma;
    result.key = keyFromPitchClassWeights(chroma);
    return result;
}

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
