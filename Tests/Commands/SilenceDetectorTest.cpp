// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Delete Silence, detector half (founder request 2026-11-07). The synthetic
// speech is tone bursts over a -70 dBFS noise floor, so every expected range is
// known in advance: measured silence only, cut from the middle of the pause,
// short pauses left alone, the kept pause split evenly.

#include "Analysis/SilenceDetector.h"
#include "Models/ClipSource.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
constexpr uint32_t kRate = 48000;
constexpr uint64_t kFrame = 480; // one analysis frame (10 ms); the tolerance the spec allows

void require(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

struct Burst {
    double fromSec;
    double toSec;
    double db;
};

/// Deterministic noise floor at @p floorDb plus 1 kHz tone bursts. Silent channels get nothing but floor.
AudioBufferData makeSpeech(double totalSec, double floorDb, const std::vector<Burst>& bursts, uint32_t channels = 1,
                           const std::vector<bool>& channelHasTone = {}) {
    AudioBufferData b;
    b.sampleRate = kRate;
    b.numChannels = channels;
    b.numFrames = static_cast<uint64_t>(totalSec * kRate);
    b.interleavedData.assign(static_cast<size_t>(b.numFrames) * channels, 0.0f);
    uint32_t seed = 12345;
    // Uniform noise in [-a, a] has RMS a / sqrt(3).
    const double noiseAmp = std::sqrt(3.0) * std::pow(10.0, floorDb / 20.0);
    for (uint64_t f = 0; f < b.numFrames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) {
            seed = seed * 1664525u + 1013904223u;
            const double u = (static_cast<double>(seed >> 8) / 8388608.0) - 1.0;
            double s = u * noiseAmp;
            const bool toneHere = channelHasTone.empty() || channelHasTone[c];
            if (toneHere) {
                const double t = static_cast<double>(f) / kRate;
                for (const auto& burst : bursts) {
                    if (t >= burst.fromSec && t < burst.toSec) {
                        // Sine RMS = amp / sqrt(2).
                        s += std::sqrt(2.0) * std::pow(10.0, burst.db / 20.0) * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * t);
                    }
                }
            }
            b.interleavedData[static_cast<size_t>(f) * channels + c] = static_cast<float>(s);
        }
    }
    return b;
}

/// 0.6 s lead, 1.0 s word, 0.8 s pause, 0.8 s word, 0.3 s short pause, 0.5 s word, 0.8 s tail: 4.8 s.
const std::vector<Burst> kWords = {{0.6, 1.6, -20.0}, {2.4, 3.2, -20.0}, {3.5, 4.0, -20.0}};

bool near(uint64_t got, double wantSec) {
    const double want = wantSec * kRate;
    return std::fabs(static_cast<double>(got) - want) <= static_cast<double>(kFrame);
}

std::string show(const std::vector<SilenceRange>& r) {
    std::string s;
    for (const auto& x : r)
        s += " [" + std::to_string(x.startFrame / static_cast<double>(kRate)) + "," +
             std::to_string(x.endFrame / static_cast<double>(kRate)) + ")";
    return s;
}

void testRangesMatchKnownSilences() {
    std::printf("ranges: lead, pause and tail cut from the middle, kept pause honoured...\n");
    const AudioBufferData b = makeSpeech(4.8, -70.0, kWords);
    SilenceParams p; // defaults: auto threshold, 0.4 s minimum, 0.25 s kept, 10 ms fade
    const auto r = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p);
    require(r.size() == 3, "three cuts expected (lead, long pause, tail); got" + show(r));
    if (r.size() != 3)
        return;
    // Lead: audio starts at 0.6 s; half the 0.25 s pause stays next to the word, the rest goes from the edge.
    require(r[0].startFrame <= kFrame / 2 && near(r[0].endFrame, 0.475), "lead cut is [0, 0.475)" + show(r));
    // Pause 1.6..2.4 (0.8 s): 0.55 s removed from the middle, 0.125 s kept either side.
    require(near(r[1].startFrame, 1.725) && near(r[1].endFrame, 2.275), "pause cut is [1.725, 2.275)" + show(r));
    // Tail: word ends at 4.0 s, half the pause stays, the rest goes to the end.
    require(near(r[2].startFrame, 4.125) && r[2].endFrame >= static_cast<uint64_t>(4.8 * kRate) - kFrame,
            "tail cut is [4.125, end)" + show(r));
    // Even split: the pause keeps the same room each side of the cut.
    const double leftKept = static_cast<double>(r[1].startFrame) / kRate - 1.6;
    const double rightKept = 2.4 - static_cast<double>(r[1].endFrame) / kRate;
    require(std::fabs(leftKept - rightKept) < 0.011, "kept pause is split evenly either side");
    // Never cuts speech.
    for (const auto& x : r) {
        for (const auto& w : kWords) {
            const bool overlap = x.startFrame < static_cast<uint64_t>(w.toSec * kRate) &&
                                 x.endFrame > static_cast<uint64_t>(w.fromSec * kRate);
            require(!overlap, "a cut overlaps a word");
        }
    }
}

void testShortPauseIsKept() {
    std::printf("min silence: a 0.3 s pause is left alone, until the minimum is lowered...\n");
    const AudioBufferData b = makeSpeech(4.8, -70.0, kWords);
    SilenceParams p;
    auto r = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p);
    for (const auto& x : r)
        require(!(x.endFrame > static_cast<uint64_t>(3.2 * kRate) && x.startFrame < static_cast<uint64_t>(3.5 * kRate)),
                "the 0.3 s pause between words must survive");
    p.minSilenceSec = 0.2;
    r = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p);
    bool cutShort = false;
    for (const auto& x : r)
        cutShort = cutShort || (near(x.startFrame, 3.325) && near(x.endFrame, 3.375));
    require(cutShort, "with minSilence 0.2 the short pause loses its middle 0.05 s" + show(r));
}

void testKeepPauseParameter() {
    std::printf("keep pause: a longer kept pause removes less...\n");
    const AudioBufferData b = makeSpeech(4.8, -70.0, kWords);
    SilenceParams p;
    p.keepPauseSec = 0.5;
    const auto r = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p);
    bool found = false;
    for (const auto& x : r)
        found = found || (near(x.startFrame, 1.85) && near(x.endFrame, 2.15));
    require(found, "keep 0.5 s leaves [1.85, 2.15) to remove from the 0.8 s pause" + show(r));
}

void testAutoThresholdUsesNoiseFloor() {
    std::printf("auto threshold: -70 dBFS floor, -20 dBFS speech...\n");
    const AudioBufferData b = makeSpeech(4.8, -70.0, kWords);
    SilenceParams p;
    double used = 0.0;
    const auto autoRanges = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p, &used);
    require(used > -65.0 && used < -59.0, "auto threshold sits about 8 dB over the floor (got " + std::to_string(used) + ")");
    p.autoThreshold = false;
    p.thresholdDb = -50.0;
    const auto manualRanges = SilenceDetector::findRemovableRanges(b, 0, b.numFrames, p, &used);
    require(used == -50.0, "manual threshold is used as given");
    require(manualRanges.size() == autoRanges.size(), "manual -50 dB agrees with auto on this clip");

    // A quiet whisper at -45 dBFS inside the long pause is above the auto threshold: it is not silence.
    std::vector<Burst> withWhisper = kWords;
    withWhisper.push_back({1.9, 2.1, -45.0});
    const AudioBufferData w = makeSpeech(4.8, -70.0, withWhisper);
    SilenceParams ap;
    const auto autoW = SilenceDetector::findRemovableRanges(w, 0, w.numFrames, ap);
    for (const auto& x : autoW)
        require(!(x.endFrame > static_cast<uint64_t>(1.9 * kRate) && x.startFrame < static_cast<uint64_t>(2.1 * kRate)),
                "auto threshold never cuts the -45 dBFS whisper");
    // A hotter manual threshold treats it as silence, which proves the whisper really is measured, not skipped.
    SilenceParams hot;
    hot.autoThreshold = false;
    hot.thresholdDb = -40.0;
    const auto hotW = SilenceDetector::findRemovableRanges(w, 0, w.numFrames, hot);
    bool cutsWhisper = false;
    for (const auto& x : hotW)
        cutsWhisper = cutsWhisper || (x.endFrame > static_cast<uint64_t>(1.9 * kRate) &&
                                      x.startFrame < static_cast<uint64_t>(2.1 * kRate));
    require(cutsWhisper, "a -40 dB manual threshold does treat the whisper as silence");
}

void testNothingToSeparate() {
    std::printf("auto threshold: noise only, or a steady tone, has nothing to cut...\n");
    const AudioBufferData noise = makeSpeech(3.0, -70.0, {});
    require(SilenceDetector::findRemovableRanges(noise, 0, noise.numFrames, SilenceParams{}).empty(),
            "pure noise floor yields no cuts");
    const AudioBufferData tone = makeSpeech(3.0, -70.0, {{0.0, 3.0, -20.0}});
    require(SilenceDetector::findRemovableRanges(tone, 0, tone.numFrames, SilenceParams{}).empty(),
            "continuous speech yields no cuts");
}

void testStereoUsesLoudestChannel() {
    std::printf("channels: the loudest channel decides...\n");
    const AudioBufferData mono = makeSpeech(4.8, -70.0, kWords, 1);
    // Tone on the right channel only; the left is floor throughout.
    const AudioBufferData stereo = makeSpeech(4.8, -70.0, kWords, 2, {false, true});
    const auto a = SilenceDetector::findRemovableRanges(mono, 0, mono.numFrames, SilenceParams{});
    const auto b = SilenceDetector::findRemovableRanges(stereo, 0, stereo.numFrames, SilenceParams{});
    require(a.size() == 3 && b.size() == 3, "mono and one-sided stereo find the same three cuts");
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const auto d = [](uint64_t x, uint64_t y) { return x > y ? x - y : y - x; };
        require(d(a[i].startFrame, b[i].startFrame) <= kFrame && d(a[i].endFrame, b[i].endFrame) <= kFrame,
                "stereo cut " + std::to_string(i) + " matches the mono cut");
    }
}

void testWindowIsRelative() {
    std::printf("window: ranges are relative to the region start...\n");
    const AudioBufferData b = makeSpeech(4.8, -70.0, kWords);
    // The region opens inside the first word (1.0 s) and runs to the end of the buffer.
    const uint64_t start = static_cast<uint64_t>(1.0 * kRate);
    const auto r = SilenceDetector::findRemovableRanges(b, start, b.numFrames - start, SilenceParams{});
    require(!r.empty() && near(r[0].startFrame, 1.725 - 1.0), "region starting at 1.0 s sees the pause at 0.725 s" + show(r));
}

} // namespace

int main() {
    testRangesMatchKnownSilences();
    testShortPauseIsKept();
    testKeepPauseParameter();
    testAutoThresholdUsesNoiseFloor();
    testNothingToSeparate();
    testStereoUsesLoudestChannel();
    testWindowIsRelative();
    if (g_failures == 0) {
        std::printf("SilenceDetectorTest: all passed\n");
        return 0;
    }
    std::printf("SilenceDetectorTest: %d failure(s)\n", g_failures);
    return 1;
}
