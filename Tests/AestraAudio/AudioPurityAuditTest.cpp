// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioPurityAuditTest — deterministic truth harness for transparent-path, summing,
// buffer-size, clipping, and SRC behavior.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "DSP/SampleRateConverter.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kChannels = 2;
constexpr uint32_t kDefaultSampleRate = 48000;
constexpr uint32_t kDefaultBlockSize = 256;
constexpr double kDurationSeconds = 1.0;
constexpr double kTau = 6.28318530717958647692;
constexpr double kPanLawCenterGain = 0.7071067811865476;
constexpr size_t kDefaultTrimStartFrames = 768;
constexpr size_t kDefaultTrimEndFrames = 256;
constexpr double kIdentityMaxAbs = 1.0e-5;
constexpr double kIdentityRms = 1.0e-7;
constexpr double kBufferVarianceRms = 1.0e-7;
constexpr double kSilenceMaxAbs = 1.0e-12;
constexpr uint32_t kDeterministicSeed = 0x00A3572A;

double linearToDb(double value) {
    if (value <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(value);
}

double dbToLinear(double db) {
    return std::pow(10.0, db / 20.0);
}

double finiteOrZero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

std::string formatDouble(double value, int precision = 6) {
    if (std::isinf(value)) {
        return value < 0.0 ? "-inf" : "inf";
    }
    if (std::isnan(value)) {
        return "nan";
    }
    std::ostringstream os;
    os << std::setprecision(precision) << std::scientific << value;
    return os.str();
}

std::string jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

void writeJsonNumber(std::ostream& os, const char* name, double value, bool comma = true) {
    os << "      \"" << name << "\": ";
    if (std::isfinite(value)) {
        os << std::setprecision(12) << value;
    } else {
        os << "null";
    }
    if (comma) {
        os << ',';
    }
    os << '\n';
}

struct AudioPurityMetrics {
    double maxAbsError = 0.0;
    double rmsError = 0.0;
    double rmsErrorDbFS = -std::numeric_limits<double>::infinity();
    double inputRmsDbFS = -std::numeric_limits<double>::infinity();
    double outputRmsDbFS = -std::numeric_limits<double>::infinity();
    double gainDeltaDb = 0.0;
    double peakDeltaDb = 0.0;
    double truePeakDeltaDb = std::numeric_limits<double>::quiet_NaN();
    double dcOffsetInput = 0.0;
    double dcOffsetOutput = 0.0;
    double leftRightBalanceDeltaDb = 0.0;
    double correlation = 0.0;
    bool polarityMatch = true;
    int sampleDelay = 0;
    double thdNDb = std::numeric_limits<double>::quiet_NaN();
    double sinadDb = std::numeric_limits<double>::quiet_NaN();
    double frequencyResponseDeltaDb = std::numeric_limits<double>::quiet_NaN();
    double phaseDeltaDegrees = std::numeric_limits<double>::quiet_NaN();
    double crestFactorInputDb = 0.0;
    double crestFactorOutputDb = 0.0;
    bool hasNaN = false;
    bool hasInf = false;
    bool lengthMismatch = false;
    uint64_t clippedSamples = 0;
};

struct AudioPurityResult {
    std::string name;
    std::string status;
    std::string classification;
    AudioPurityMetrics metrics;
    std::string notes;
};

struct TrackFixture {
    std::string name;
    std::vector<float> interleaved;
    float volume = 1.0f;
    float pan = 0.0f;
};

struct RenderResult {
    std::vector<float> interleaved;
    uint32_t sampleRate = kDefaultSampleRate;
    uint32_t blockSize = kDefaultBlockSize;
};

std::vector<float> makeSilence(uint32_t frames) {
    return std::vector<float>(static_cast<size_t>(frames) * kChannels, 0.0f);
}

std::vector<float> makeStereoSine(uint32_t frames, uint32_t sampleRate, double frequency, double amplitude,
                                  double rightScale = 1.0) {
    std::vector<float> data(static_cast<size_t>(frames) * kChannels, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const double s = std::sin(kTau * frequency * static_cast<double>(i) / static_cast<double>(sampleRate)) *
                         amplitude;
        data[static_cast<size_t>(i) * 2] = static_cast<float>(s);
        data[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(s * rightScale);
    }
    return data;
}

std::vector<float> makeImpulse(uint32_t frames, uint32_t impulseFrame, double amplitude) {
    std::vector<float> data(static_cast<size_t>(frames) * kChannels, 0.0f);
    if (impulseFrame < frames) {
        data[static_cast<size_t>(impulseFrame) * 2] = static_cast<float>(amplitude);
        data[static_cast<size_t>(impulseFrame) * 2 + 1] = static_cast<float>(amplitude);
    }
    return data;
}

std::vector<float> makeLeftOnly(uint32_t frames, uint32_t sampleRate, double frequency, double amplitude) {
    std::vector<float> data(static_cast<size_t>(frames) * kChannels, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const double s = std::sin(kTau * frequency * static_cast<double>(i) / static_cast<double>(sampleRate)) *
                         amplitude;
        data[static_cast<size_t>(i) * 2] = static_cast<float>(s);
    }
    return data;
}

std::vector<float> makeNoise(uint32_t frames, double amplitude) {
    std::mt19937 rng(kDeterministicSeed);
    std::uniform_real_distribution<float> dist(static_cast<float>(-amplitude), static_cast<float>(amplitude));
    std::vector<float> data(static_cast<size_t>(frames) * kChannels, 0.0f);
    for (float& sample : data) {
        sample = dist(rng);
    }
    return data;
}

std::vector<float> scaleBuffer(const std::vector<float>& input, double scale) {
    std::vector<float> out(input.size(), 0.0f);
    for (size_t i = 0; i < input.size(); ++i) {
        out[i] = static_cast<float>(static_cast<double>(input[i]) * scale);
    }
    return out;
}

std::vector<float> sumExpected(const std::vector<TrackFixture>& tracks, double panLawGain = kPanLawCenterGain) {
    size_t samples = 0;
    for (const auto& track : tracks) {
        samples = std::max(samples, track.interleaved.size());
    }
    std::vector<float> expected(samples, 0.0f);
    for (const auto& track : tracks) {
        const double scale = static_cast<double>(track.volume) * panLawGain;
        for (size_t i = 0; i < track.interleaved.size(); ++i) {
            expected[i] += static_cast<float>(static_cast<double>(track.interleaved[i]) * scale);
        }
    }
    return expected;
}

RenderResult renderThroughAestraEngine(const std::vector<TrackFixture>& tracks, uint32_t sampleRate,
                                       uint32_t blockSize) {
    const uint32_t totalFrames = tracks.empty() || tracks.front().interleaved.empty()
                                     ? static_cast<uint32_t>(sampleRate * kDurationSeconds)
                                     : static_cast<uint32_t>(tracks.front().interleaved.size() / kChannels);
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(sampleRate));
    trackManager->getPlaylistModel().setProjectSampleRate(static_cast<double>(sampleRate));
    trackManager->getPlaylistModel().setBPM(120.0);

    for (size_t t = 0; t < tracks.size(); ++t) {
        const auto& fixture = tracks[t];
        auto* channel = trackManager->addChannel(fixture.name);
        if (channel) {
            channel->setVolume(fixture.volume);
            channel->setPan(fixture.pan);
        }

        auto buffer = std::make_shared<AudioBufferData>();
        buffer->sampleRate = sampleRate;
        buffer->numChannels = kChannels;
        buffer->numFrames = totalFrames;
        buffer->interleavedData = fixture.interleaved;
        if (buffer->interleavedData.size() < static_cast<size_t>(totalFrames) * kChannels) {
            buffer->interleavedData.resize(static_cast<size_t>(totalFrames) * kChannels, 0.0f);
        }

        const std::string stem = "audio_purity_" + std::to_string(t);
        const std::string fakePath = (std::filesystem::temp_directory_path() / (stem + ".wav")).string();
        const ClipSourceID sourceId = trackManager->getSourceManager().createRecordedSource(fakePath, stem, buffer);

        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = static_cast<double>(totalFrames) / static_cast<double>(sampleRate);
        payload.slices.push_back({0.0,
                                  payload.durationSeconds,
                                  0.0,
                                  static_cast<double>(totalFrames)});

        const PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane(stem);
        if (auto* lane = trackManager->getPlaylistModel().getLane(laneId)) {
            lane->volume = fixture.volume;
            lane->pan = fixture.pan;
        }
        const PatternID patternId =
            trackManager->getPatternManager().createAudioPattern(stem, payload.durationSeconds * 2.0, payload);
        if (channel) {
            trackManager->getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
        }
        const ClipInstanceID clipId =
            trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, payload.durationSeconds * 2.0);
        if (!trackManager->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
            std::cerr << "Failed to configure unity-gain audio-purity fixture\n";
            return {};
        }
    }

    AudioEngine engine;
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(blockSize, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(120.0f);
    engine.setSafetyLimiterEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.initialize();
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    std::vector<float> output;
    output.reserve(static_cast<size_t>(totalFrames) * kChannels);
    std::vector<float> block(static_cast<size_t>(blockSize) * kChannels, 0.0f);
    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        const uint32_t framesThisBlock = std::min(blockSize, totalFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, framesThisBlock, 0.0);
        output.insert(output.end(), block.begin(), block.begin() + static_cast<ptrdiff_t>(framesThisBlock * kChannels));
        rendered += framesThisBlock;
    }
    engine.setTransportPlaying(false);

    return {std::move(output), sampleRate, blockSize};
}

AudioPurityMetrics measureDifference(const std::vector<float>& reference, const std::vector<float>& output,
                                      size_t trimStartFrames = kDefaultTrimStartFrames,
                                      size_t trimEndFrames = kDefaultTrimEndFrames) {
    AudioPurityMetrics metrics;
    if (reference.empty() || output.empty() || reference.size() != output.size()) {
        metrics.lengthMismatch = true;
        metrics.maxAbsError = std::numeric_limits<double>::infinity();
        metrics.rmsError = std::numeric_limits<double>::infinity();
        metrics.rmsErrorDbFS = std::numeric_limits<double>::infinity();
        return metrics;
    }

    const size_t frameCount = std::min(reference.size(), output.size()) / kChannels;
    const size_t startFrame = std::min(trimStartFrames, frameCount);
    const size_t endFrame = frameCount > trimEndFrames ? frameCount - trimEndFrames : frameCount;
    if (startFrame >= endFrame) {
        metrics.lengthMismatch = true;
        metrics.maxAbsError = std::numeric_limits<double>::infinity();
        metrics.rmsError = std::numeric_limits<double>::infinity();
        metrics.rmsErrorDbFS = std::numeric_limits<double>::infinity();
        return metrics;
    }

    double errSq = 0.0;
    double inSq = 0.0;
    double outSq = 0.0;
    double inSum = 0.0;
    double outSum = 0.0;
    double peakIn = 0.0;
    double peakOut = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    double sumYY = 0.0;
    double leftSq = 0.0;
    double rightSq = 0.0;
    double inputLeftSq = 0.0;
    double inputRightSq = 0.0;
    uint64_t count = 0;

    for (size_t frame = startFrame; frame < endFrame; ++frame) {
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            const size_t idx = frame * kChannels + ch;
            const double x = static_cast<double>(reference[idx]);
            const double y = static_cast<double>(output[idx]);
            metrics.hasNaN = metrics.hasNaN || std::isnan(y);
            metrics.hasInf = metrics.hasInf || std::isinf(y);
            const double yf = finiteOrZero(y);
            const double diff = yf - x;
            metrics.maxAbsError = std::max(metrics.maxAbsError, std::abs(diff));
            errSq += diff * diff;
            inSq += x * x;
            outSq += yf * yf;
            inSum += x;
            outSum += yf;
            peakIn = std::max(peakIn, std::abs(x));
            peakOut = std::max(peakOut, std::abs(yf));
            sumXY += x * yf;
            sumXX += x * x;
            sumYY += yf * yf;
            if (ch == 0) {
                inputLeftSq += x * x;
                leftSq += yf * yf;
            } else {
                inputRightSq += x * x;
                rightSq += yf * yf;
            }
            if (std::abs(yf) >= 0.999999) {
                ++metrics.clippedSamples;
            }
            ++count;
        }
    }

    if (count == 0) {
        return metrics;
    }

    const double invCount = 1.0 / static_cast<double>(count);
    metrics.rmsError = std::sqrt(errSq * invCount);
    metrics.rmsErrorDbFS = linearToDb(metrics.rmsError);
    metrics.inputRmsDbFS = linearToDb(std::sqrt(inSq * invCount));
    metrics.outputRmsDbFS = linearToDb(std::sqrt(outSq * invCount));
    metrics.gainDeltaDb = metrics.outputRmsDbFS - metrics.inputRmsDbFS;
    metrics.peakDeltaDb = linearToDb(peakOut) - linearToDb(peakIn);
    metrics.dcOffsetInput = inSum * invCount;
    metrics.dcOffsetOutput = outSum * invCount;
    metrics.correlation = (sumXX > 0.0 && sumYY > 0.0) ? sumXY / std::sqrt(sumXX * sumYY) : 0.0;
    metrics.polarityMatch = metrics.correlation >= 0.0;
    metrics.leftRightBalanceDeltaDb = (linearToDb(std::sqrt(leftSq / static_cast<double>(endFrame - startFrame))) -
                                       linearToDb(std::sqrt(rightSq / static_cast<double>(endFrame - startFrame)))) -
                                      (linearToDb(std::sqrt(inputLeftSq / static_cast<double>(endFrame - startFrame))) -
                                       linearToDb(std::sqrt(inputRightSq / static_cast<double>(endFrame - startFrame))));
    metrics.crestFactorInputDb = linearToDb(peakIn) - metrics.inputRmsDbFS;
    metrics.crestFactorOutputDb = linearToDb(peakOut) - metrics.outputRmsDbFS;

    double bestCorr = -std::numeric_limits<double>::infinity();
    int bestDelay = 0;
    for (int delay = -32; delay <= 32; ++delay) {
        double corr = 0.0;
        double refEnergy = 0.0;
        double outEnergy = 0.0;
        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            const int64_t shifted = static_cast<int64_t>(frame) + delay;
            if (shifted < static_cast<int64_t>(startFrame) || shifted >= static_cast<int64_t>(endFrame)) {
                continue;
            }
            const size_t refIdx = frame * kChannels;
            const size_t outIdx = static_cast<size_t>(shifted) * kChannels;
            const double x = 0.5 * (static_cast<double>(reference[refIdx]) + static_cast<double>(reference[refIdx + 1]));
            const double y = 0.5 * (static_cast<double>(output[outIdx]) + static_cast<double>(output[outIdx + 1]));
            corr += x * y;
            refEnergy += x * x;
            outEnergy += y * y;
        }
        if (refEnergy > 0.0 && outEnergy > 0.0) {
            const double normalized = corr / std::sqrt(refEnergy * outEnergy);
            if (normalized > bestCorr) {
                bestCorr = normalized;
                bestDelay = delay;
            }
        }
    }
    metrics.sampleDelay = bestDelay;

    return metrics;
}

AudioPurityResult makeComparedResult(const std::string& name, const std::vector<float>& reference,
                                     const std::vector<float>& output, const std::string& classification,
                                     const std::string& notes, double maxAbsTolerance = kIdentityMaxAbs,
                                     double rmsTolerance = kIdentityRms,
                                     size_t trimStartFrames = kDefaultTrimStartFrames,
                                     size_t trimEndFrames = kDefaultTrimEndFrames) {
    AudioPurityResult result;
    result.name = name;
    result.classification = classification;
    result.notes = notes;
    result.metrics = measureDifference(reference, output, trimStartFrames, trimEndFrames);
    const bool pass = !result.metrics.lengthMismatch && !result.metrics.hasNaN && !result.metrics.hasInf &&
                      result.metrics.maxAbsError <= maxAbsTolerance && result.metrics.rmsError <= rmsTolerance &&
                      result.metrics.sampleDelay == 0;
    result.status = pass ? "PASS" : "FAIL";
    return result;
}

AudioPurityResult makeInfoResult(const std::string& name, const std::string& classification,
                                 const std::string& notes, AudioPurityMetrics metrics = {}) {
    return {name, "INFO", classification, metrics, notes};
}

AudioPurityResult runSilencePurity() {
    const uint32_t frames = static_cast<uint32_t>(kDefaultSampleRate * kDurationSeconds);
    const auto silence = makeSilence(frames);
    const auto rendered = renderThroughAestraEngine({{"silence", silence}}, kDefaultSampleRate, kDefaultBlockSize);
    auto result = makeComparedResult("Silence_TrackToMaster_48k", silence, rendered.interleaved, "transparent",
                                     "silence through real track->master route", kSilenceMaxAbs, kSilenceMaxAbs);
    if (result.metrics.maxAbsError <= kSilenceMaxAbs && !result.metrics.hasNaN && !result.metrics.hasInf) {
        result.status = "PASS";
    }
    return result;
}

std::vector<AudioPurityResult> runIdentityAndSumming() {
    std::vector<AudioPurityResult> results;
    const uint32_t frames = static_cast<uint32_t>(kDefaultSampleRate * kDurationSeconds);

    const auto sine = makeStereoSine(frames, kDefaultSampleRate, 1000.0, dbToLinear(-12.0));
    {
        const std::vector<TrackFixture> tracks{{"stereo_sine", sine}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("TransparentStereoTrack_48k", expected, rendered.interleaved,
                                             "transparent",
                                             "expected includes Aestra equal-power center pan gain"));
    }

    {
        const auto leftOnly = makeLeftOnly(frames, kDefaultSampleRate, 1000.0, dbToLinear(-12.0));
        const std::vector<TrackFixture> tracks{{"left_only", leftOnly}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("LeftOnly_ChannelIsolation_48k", expected, rendered.interleaved,
                                             "transparent", "right channel should remain silent"));
    }

    {
        const auto impulse = makeImpulse(frames, 4096, 0.75);
        const std::vector<TrackFixture> tracks{{"impulse", impulse}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("Impulse_PhasePolarity_48k", expected, rendered.interleaved,
                                             "transparent", "impulse placed after startup ramp"));
    }

    {
        const auto noise = makeNoise(frames, 0.2);
        const std::vector<TrackFixture> tracks{{"noise_a", noise}, {"noise_b_inverted", scaleBuffer(noise, -1.0)}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("TwoTrackNullCancel_48k", expected, rendered.interleaved,
                                             "summing", "opposite polarity tracks should null"));
    }

    {
        const std::vector<TrackFixture> tracks{{"sine_a", sine}, {"sine_b", sine}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("TwoTrackDoubleSum_48k", expected, rendered.interleaved,
                                             "summing", "two identical tracks should sum to exactly 2x"));
    }

    {
        std::vector<TrackFixture> tracks;
        const auto quietSine = makeStereoSine(frames, kDefaultSampleRate, 440.0, dbToLinear(-30.0));
        for (int i = 0; i < 8; ++i) {
            tracks.push_back({"eight_track_" + std::to_string(i), quietSine});
        }
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
        results.push_back(makeComparedResult("EightTrackSumming_48k", expected, rendered.interleaved,
                                             "summing", "eight low-level tracks should follow linear addition"));
    }

    return results;
}

std::vector<AudioPurityResult> runBufferSizeMatrix() {
    std::vector<AudioPurityResult> results;
    const uint32_t frames = static_cast<uint32_t>(kDefaultSampleRate * kDurationSeconds);
    const auto noise = makeNoise(frames, 0.12);
    const std::vector<TrackFixture> tracks{{"buffer_noise", noise}};
    const auto referenceRender = renderThroughAestraEngine(tracks, kDefaultSampleRate, 256);
    for (uint32_t blockSize : {16u, 32u, 64u, 128u, 512u, 1024u}) {
        const auto render = renderThroughAestraEngine(tracks, kDefaultSampleRate, blockSize);
        auto result = makeComparedResult("BufferSizeInvariance_" + std::to_string(blockSize), referenceRender.interleaved,
                                         render.interleaved, "buffer-size",
                                         "compared against 256-frame block render after click-prevention trims",
                                         kIdentityMaxAbs, kBufferVarianceRms, 2048, 512);
        results.push_back(result);
    }
    return results;
}

AudioPurityResult runNoHiddenLimiter() {
    const uint32_t frames = static_cast<uint32_t>(kDefaultSampleRate * kDurationSeconds);
    const auto hotSine = makeStereoSine(frames, kDefaultSampleRate, 997.0, 0.9);
    const std::vector<TrackFixture> tracks{{"hot_a", hotSine}, {"hot_b", hotSine}};
    const auto expected = sumExpected(tracks);
    const auto rendered = renderThroughAestraEngine(tracks, kDefaultSampleRate, kDefaultBlockSize);
    auto result = makeComparedResult("MasterLimiterDisabled_NearFullScale", expected, rendered.interleaved,
                                     "hidden-limiter-check",
                                     "safety limiter disabled; final output should reveal whether any clamp remains",
                                     kIdentityMaxAbs, kIdentityRms);
    const double expectedPeakDb = result.metrics.inputRmsDbFS + result.metrics.crestFactorInputDb;
    const double actualPeakDb = result.metrics.outputRmsDbFS + result.metrics.crestFactorOutputDb;
    if (actualPeakDb < expectedPeakDb - 0.05) {
        result.status = "FAIL";
        result.notes += "; hard clipping/peak shaving detected in output stage";
    }
    return result;
}

std::vector<AudioPurityResult> runSameRateMatrix() {
    std::vector<AudioPurityResult> results;
    for (uint32_t rate : {44100u, 48000u, 96000u}) {
        const uint32_t frames = static_cast<uint32_t>(rate * kDurationSeconds);
        const auto sine = makeStereoSine(frames, rate, 1000.0, dbToLinear(-12.0));
        const std::vector<TrackFixture> tracks{{"same_rate_" + std::to_string(rate), sine}};
        const auto expected = sumExpected(tracks);
        const auto rendered = renderThroughAestraEngine(tracks, rate, kDefaultBlockSize);
        results.push_back(makeComparedResult("SameRateTransparent_" + std::to_string(rate), expected,
                                             rendered.interleaved, "sample-rate",
                                             "same-rate path should not invoke SRC; reference includes pan law"));
    }
    return results;
}

AudioPurityResult runSrcPassthrough() {
    const uint32_t frames = 4096;
    const auto input = makeStereoSine(frames, kDefaultSampleRate, 1000.0, dbToLinear(-12.0));
    SampleRateConverter src;
    src.configure(kDefaultSampleRate, kDefaultSampleRate, kChannels, SRCQuality::Sinc16);
    std::vector<float> output(input.size(), 0.0f);
    const uint32_t written = src.process(input.data(), frames, output.data(), frames);
    output.resize(static_cast<size_t>(written) * kChannels);
    auto result = makeComparedResult("SRC_SameRatePassthrough_48k", input, output, "resampler",
                                     "same-rate SRC path must copy exactly", 1.0e-7, 1.0e-8, 0, 0);
    if (written != frames) {
        result.status = "FAIL";
        result.notes += "; frame count mismatch expected " + std::to_string(frames) + " got " + std::to_string(written);
    }
    return result;
}

AudioPurityResult runSrcSineQuality(uint32_t inputRate, uint32_t outputRate, double frequency) {
    constexpr double kInputPeakDb = -12.0;
    constexpr double kExpectedSineRmsDbFS = kInputPeakDb - 3.010299956639812;
    const uint32_t inputFrames = inputRate;
    const auto input = makeStereoSine(inputFrames, inputRate, frequency, dbToLinear(kInputPeakDb));
    SampleRateConverter src;
    src.configure(inputRate, outputRate, kChannels, SRCQuality::Sinc16);
    const uint32_t capacity = estimateOutputFrames(inputFrames, inputRate, outputRate, src.getLatency()) + 2048;
    std::vector<float> output(static_cast<size_t>(capacity) * kChannels, 0.0f);
    const uint32_t written = src.process(input.data(), inputFrames, output.data(), capacity);
    output.resize(static_cast<size_t>(written) * kChannels);

    AudioPurityMetrics metrics;
    for (float sample : output) {
        metrics.hasNaN = metrics.hasNaN || std::isnan(sample);
        metrics.hasInf = metrics.hasInf || std::isinf(sample);
    }
    const AudioPurityMetrics outputStats = measureDifference(output, output, 256, 256);
    metrics.outputRmsDbFS = outputStats.outputRmsDbFS;
    metrics.crestFactorOutputDb = outputStats.crestFactorOutputDb;
    metrics.clippedSamples = outputStats.clippedSamples;
    metrics.frequencyResponseDeltaDb = std::abs(outputStats.outputRmsDbFS - kExpectedSineRmsDbFS);

    const bool severe = metrics.hasNaN || metrics.hasInf || written == 0 || metrics.frequencyResponseDeltaDb > 3.0;
    const bool warn = !severe && metrics.frequencyResponseDeltaDb > 0.05;
    AudioPurityResult result;
    result.name = "SRC_" + std::to_string(inputRate) + "_to_" + std::to_string(outputRate) + "_" +
                  std::to_string(static_cast<int>(frequency)) + "Hz";
    result.status = severe ? "FAIL" : (warn ? "WARN" : "PASS");
    result.classification = "resampler";
    result.metrics = metrics;
    result.notes = "direct Sinc16 SRC sine gain/finite-output smoke; outputFrames=" + std::to_string(written);
    return result;
}

AudioPurityResult runSrcPhaseZeroImpulse() {
    const uint32_t inputRate = 44100;
    const uint32_t outputRate = 48000;
    const uint32_t frames = 4096;
    AudioPurityMetrics metrics;
    std::vector<double> peaks;
    for (uint32_t offset : {256u, 257u, 258u, 259u}) {
        const auto input = makeImpulse(frames, offset, 0.75);
        SampleRateConverter src;
        src.configure(inputRate, outputRate, kChannels, SRCQuality::Sinc16);
        const uint32_t capacity = estimateOutputFrames(frames, inputRate, outputRate, src.getLatency()) + 2048;
        std::vector<float> output(static_cast<size_t>(capacity) * kChannels, 0.0f);
        const uint32_t written = src.process(input.data(), frames, output.data(), capacity);
        output.resize(static_cast<size_t>(written) * kChannels);
        double peak = 0.0;
        for (float sample : output) {
            metrics.hasNaN = metrics.hasNaN || std::isnan(sample);
            metrics.hasInf = metrics.hasInf || std::isinf(sample);
            if (std::isfinite(sample)) {
                peak = std::max(peak, std::abs(static_cast<double>(sample)));
            }
        }
        peaks.push_back(peak);
    }

    const auto [minIt, maxIt] = std::minmax_element(peaks.begin(), peaks.end());
    metrics.peakDeltaDb = linearToDb(*maxIt) - linearToDb(*minIt);
    AudioPurityResult result;
    result.name = "SRC_Phase0_ImpulseContinuity";
    result.classification = "resampler";
    result.metrics = metrics;
    result.notes = "compares adjacent impulse offsets to catch unique phase-zero gain jumps";
    result.status = (metrics.hasNaN || metrics.hasInf) ? "FAIL"
                                                        : (metrics.peakDeltaDb <= 0.01 ? "PASS"
                                                                                       : (metrics.peakDeltaDb <= 3.0 ? "WARN" : "FAIL"));
    return result;
}

bool writeTextReport(const std::filesystem::path& path, const std::vector<AudioPurityResult>& results) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "============================================================\n";
    out << "Aestra Audio Purity Audit\n";
    out << "============================================================\n\n";
    for (const auto& result : results) {
        out << result.name << ":\n";
        out << "  " << result.status << "  maxAbsError=" << formatDouble(result.metrics.maxAbsError)
            << "  rmsErrorDbFS=" << formatDouble(result.metrics.rmsErrorDbFS, 4)
            << "  gainDeltaDb=" << formatDouble(result.metrics.gainDeltaDb, 4)
            << "  peakDeltaDb=" << formatDouble(result.metrics.peakDeltaDb, 4)
            << "  sampleDelay=" << result.metrics.sampleDelay << "\n";
        if (!result.notes.empty()) {
            out << "  notes: " << result.notes << "\n";
        }
        out << "\n";
    }
    out.flush();
    return static_cast<bool>(out);
}

bool writeJsonReport(const std::filesystem::path& path, const std::vector<AudioPurityResult>& results) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "{\n";
    out << "  \"audit\": \"AudioPurityAuditTest\",\n";
    out << "  \"engine\": \"Aestra\",\n";
    out << "  \"sampleRate\": " << kDefaultSampleRate << ",\n";
    out << "  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(result.name) << "\",\n";
        out << "      \"status\": \"" << result.status << "\",\n";
        out << "      \"classification\": \"" << result.classification << "\",\n";
        out << "      \"notes\": \"" << jsonEscape(result.notes) << "\",\n";
        writeJsonNumber(out, "maxAbsError", result.metrics.maxAbsError);
        writeJsonNumber(out, "rmsError", result.metrics.rmsError);
        writeJsonNumber(out, "rmsErrorDbFS", result.metrics.rmsErrorDbFS);
        writeJsonNumber(out, "inputRmsDbFS", result.metrics.inputRmsDbFS);
        writeJsonNumber(out, "outputRmsDbFS", result.metrics.outputRmsDbFS);
        writeJsonNumber(out, "gainDeltaDb", result.metrics.gainDeltaDb);
        writeJsonNumber(out, "peakDeltaDb", result.metrics.peakDeltaDb);
        writeJsonNumber(out, "truePeakDeltaDb", result.metrics.truePeakDeltaDb);
        writeJsonNumber(out, "dcOffsetInput", result.metrics.dcOffsetInput);
        writeJsonNumber(out, "dcOffsetOutput", result.metrics.dcOffsetOutput);
        writeJsonNumber(out, "leftRightBalanceDeltaDb", result.metrics.leftRightBalanceDeltaDb);
        writeJsonNumber(out, "correlation", result.metrics.correlation);
        out << "      \"polarityMatch\": " << (result.metrics.polarityMatch ? "true" : "false") << ",\n";
        out << "      \"sampleDelay\": " << result.metrics.sampleDelay << ",\n";
        writeJsonNumber(out, "thdNDb", result.metrics.thdNDb);
        writeJsonNumber(out, "sinadDb", result.metrics.sinadDb);
        writeJsonNumber(out, "frequencyResponseDeltaDb", result.metrics.frequencyResponseDeltaDb);
        writeJsonNumber(out, "phaseDeltaDegrees", result.metrics.phaseDeltaDegrees);
        writeJsonNumber(out, "crestFactorInputDb", result.metrics.crestFactorInputDb);
        writeJsonNumber(out, "crestFactorOutputDb", result.metrics.crestFactorOutputDb);
        out << "      \"hasNaN\": " << (result.metrics.hasNaN ? "true" : "false") << ",\n";
        out << "      \"hasInf\": " << (result.metrics.hasInf ? "true" : "false") << ",\n";
        out << "      \"lengthMismatch\": " << (result.metrics.lengthMismatch ? "true" : "false") << ",\n";
        out << "      \"clippedSamples\": " << result.metrics.clippedSamples << "\n";
        out << "    }" << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    out.flush();
    return static_cast<bool>(out);
}

std::filesystem::path defaultReportPath(const char* name) {
    return std::filesystem::current_path() / name;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path jsonReport = defaultReportPath("audio-purity-report.json");
    std::filesystem::path textReport = defaultReportPath("audio-purity-report.txt");
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--report" && i + 1 < argc) {
            jsonReport = argv[++i];
        } else if (arg == "--text-report" && i + 1 < argc) {
            textReport = argv[++i];
        }
    }

    std::vector<AudioPurityResult> results;
    results.push_back(makeInfoResult("AuditScope", "audit",
                                     "real TrackManager->AudioEngine route for transparency/summing/buffer tests; "
                                     "direct SRC route for focused resampler invariants"));
    results.push_back(makeInfoResult("PanLaw", "pan-law",
                                     "center pan uses equal-power gain sqrt(1/2) per output channel"));
    results.push_back(runSilencePurity());
    const auto identityResults = runIdentityAndSumming();
    results.insert(results.end(), identityResults.begin(), identityResults.end());
    const auto bufferResults = runBufferSizeMatrix();
    results.insert(results.end(), bufferResults.begin(), bufferResults.end());
    results.push_back(runNoHiddenLimiter());
    const auto sameRateResults = runSameRateMatrix();
    results.insert(results.end(), sameRateResults.begin(), sameRateResults.end());
    results.push_back(runSrcPassthrough());
    results.push_back(runSrcSineQuality(44100, 48000, 1000.0));
    results.push_back(runSrcSineQuality(48000, 44100, 1000.0));
    results.push_back(runSrcSineQuality(44100, 96000, 10000.0));
    results.push_back(runSrcPhaseZeroImpulse());

    const bool wroteJsonReport = writeJsonReport(jsonReport, results);
    const bool wroteTextReport = writeTextReport(textReport, results);
    if (!wroteJsonReport || !wroteTextReport) {
        std::cerr << "Failed to write audio purity audit report(s): " << jsonReport << " / " << textReport << "\n";
        return 1;
    }

    int passCount = 0;
    int warnCount = 0;
    int failCount = 0;
    int infoCount = 0;
    std::cout << "============================================================\n";
    std::cout << "Aestra Audio Purity Audit\n";
    std::cout << "============================================================\n\n";
    for (const auto& result : results) {
        if (result.status == "PASS") {
            ++passCount;
        } else if (result.status == "WARN") {
            ++warnCount;
        } else if (result.status == "FAIL") {
            ++failCount;
        } else {
            ++infoCount;
        }
        std::cout << result.name << ":\n";
        std::cout << "  " << result.status << "  maxAbsError=" << formatDouble(result.metrics.maxAbsError)
                  << "  rmsErrorDbFS=" << formatDouble(result.metrics.rmsErrorDbFS, 4)
                  << "  gainDeltaDb=" << formatDouble(result.metrics.gainDeltaDb, 4)
                  << "  peakDeltaDb=" << formatDouble(result.metrics.peakDeltaDb, 4)
                  << "  sampleDelay=" << result.metrics.sampleDelay << "\n";
        if (!result.notes.empty()) {
            std::cout << "  notes: " << result.notes << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Summary:\n";
    std::cout << "  PASS: " << passCount << "\n";
    std::cout << "  WARN: " << warnCount << "\n";
    std::cout << "  FAIL: " << failCount << "\n";
    std::cout << "  INFO: " << infoCount << "\n";
    std::cout << "Reports:\n";
    std::cout << "  " << jsonReport << "\n";
    std::cout << "  " << textReport << "\n";

    return failCount == 0 ? 0 : 1;
}
