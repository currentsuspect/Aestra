// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Analysis/ReferenceAnalysis.h"
#include "Models/ClipSource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief Everything Audition measures when a mix is set beside a reference (FD-26, V81-A): the loudness, stereo and
 * tonal numbers of each, the sections of each, the waveform of each on one scale, and how much the reference is
 * turned down (or up) so the two are heard at the same level. Pure: decoded audio in, numbers out, so it is
 * tested with generated signals and runs the same on a bounce, a render or a file.
 */
namespace AuditionCompare {

constexpr size_t kWaveColumns = 960;

struct Side {
    std::string path;
    std::shared_ptr<Audio::AudioBufferData> buffer;
    Audio::AudioAnalysis analysis;
    Audio::StereoDetail stereo;
    std::vector<Audio::Section> sections;
    std::vector<float> peaks;  ///< loudest sample per column, 0..1, on the same scale for both sides
    double seconds = 0.0;
};

struct Result {
    bool ok = false;
    Side mix, ref;
    double referenceGainDb = 0.0;  ///< applied to the reference so it matches the mix; negative when the reference is louder
    std::array<double, Audio::AudioAnalysis::kBandCount> tonalDeltaDb{};  ///< mix minus reference, per band
    double ppl(const Side& s) const { return Audio::peakToLoudnessRatioDb(s.analysis); }
};

/// Loudest absolute sample in each of @p columns equal stretches of the signal.
inline std::vector<float> columnPeaks(const std::vector<float>& interleaved, uint32_t channels, size_t columns = kWaveColumns) {
    std::vector<float> out(columns, 0.0f);
    if (channels == 0 || interleaved.empty() || columns == 0) return out;
    const size_t frames = interleaved.size() / channels;
    for (size_t f = 0; f < frames; ++f) {
        const size_t col = std::min(columns - 1, f * columns / frames);
        for (uint32_t c = 0; c < channels; ++c) out[col] = std::max(out[col], std::fabs(interleaved[f * channels + c]));
    }
    return out;
}

inline Side measure(const std::string& path, std::vector<float> samples, uint32_t channels, uint32_t rate) {
    Side s;
    s.path = path;
    s.peaks = columnPeaks(samples, channels);
    s.analysis = Audio::analyzeAudio(samples, channels, rate);
    const uint64_t frames = channels ? samples.size() / channels : 0;
    s.stereo = Audio::analyzeStereoDetail(samples.data(), frames, channels, rate);
    s.sections = Audio::detectSections(samples.data(), frames, channels, rate);
    s.seconds = rate ? static_cast<double>(frames) / rate : 0.0;
    auto buffer = std::make_shared<Audio::AudioBufferData>();
    buffer->sampleRate = rate;
    buffer->numChannels = channels;
    buffer->numFrames = frames;
    buffer->interleavedData = std::move(samples);
    s.buffer = std::move(buffer);
    return s;
}

/// Compare two decoded signals. The reference is turned to the mix's integrated loudness; nothing is changed in either.
inline Result compare(Side mix, Side ref) {
    Result r;
    r.mix = std::move(mix);
    r.ref = std::move(ref);
    r.ok = r.mix.buffer && r.ref.buffer && r.mix.analysis.integratedLufs > -150.0 && r.ref.analysis.integratedLufs > -150.0;
    // levelMatchGainDb is the gain that brings the mix to the reference; the reference moves the other way.
    r.referenceGainDb = r.ok ? -Audio::levelMatchGainDb(r.mix.analysis.integratedLufs, r.ref.analysis.integratedLufs) : 0.0;
    r.tonalDeltaDb = Audio::tonalBalanceDeltaDb(r.mix.analysis, r.ref.analysis);
    // Both waveforms are drawn at the level they are heard at: the reference after its match.
    if (r.ok) {
        const float g = static_cast<float>(std::pow(10.0, r.referenceGainDb / 20.0));
        for (auto& p : r.ref.peaks) p = std::min(1.0f, p * g);
    }
    return r;
}

} // namespace AuditionCompare
} // namespace Aestra
