// © 2026 Aestra Studios. All Rights Reserved.
// ResamplerWarTest is an audio-quality gate for Aestra's polyphase SRC.
// It keeps deterministic internal correctness checks in normal CTest runs and
// uses libsamplerate/soxr only as optional diagnostics when available.

#include "SampleRateConverter.h"

#ifdef AESTRA_HAS_LIBSAMPLERATE
#include <samplerate.h>
#endif

#ifdef AESTRA_HAS_SOXR
#include <soxr.h>
#endif

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr double kPI = 3.14159265358979323846;

struct Gate {
    int passed{0};
    int failed{0};

    void expect(const char* name, bool ok, const std::string& detail = {}) {
        if (ok) {
            ++passed;
            std::printf("[PASS] %s", name);
        } else {
            ++failed;
            std::printf("[FAIL] %s", name);
        }
        if (!detail.empty()) {
            std::printf(" - %s", detail.c_str());
        }
        std::printf("\n");
    }

    int result() const { return failed == 0 ? 0 : 1; }
};

struct Signal {
    std::vector<float> interleaved;
    uint32_t frames{0};
    uint32_t channels{2};
};

struct ResampleResult {
    std::vector<float> output;
    uint32_t outputFrames{0};
    bool available{true};
};

Signal makeSine(uint32_t sampleRate, uint32_t frames, double freqHz, double amp = 0.5) {
    Signal s;
    s.frames = frames;
    s.interleaved.resize(static_cast<size_t>(frames) * s.channels);
    for (uint32_t i = 0; i < frames; ++i) {
        const double v = amp * std::sin(2.0 * kPI * freqHz * static_cast<double>(i) / sampleRate);
        s.interleaved[i * 2] = static_cast<float>(v);
        s.interleaved[i * 2 + 1] = static_cast<float>(v);
    }
    return s;
}

Signal makeDC(uint32_t frames, double amp = 1.0) {
    Signal s;
    s.frames = frames;
    s.interleaved.resize(static_cast<size_t>(frames) * s.channels, static_cast<float>(amp));
    return s;
}

Signal makeImpulse(uint32_t frames, uint32_t pos) {
    Signal s;
    s.frames = frames;
    s.interleaved.resize(static_cast<size_t>(frames) * s.channels, 0.0f);
    if (pos < frames) {
        s.interleaved[pos * 2] = 1.0f;
        s.interleaved[pos * 2 + 1] = 1.0f;
    }
    return s;
}

ResampleResult resampleAestra(const float* input, uint32_t inputFrames, uint32_t srcRate, uint32_t dstRate,
                              uint32_t channels, SRCQuality quality) {
    SampleRateConverter src;
    src.configure(srcRate, dstRate, channels, quality);
    const uint32_t maxOut =
        static_cast<uint32_t>(std::ceil(static_cast<double>(inputFrames) * dstRate / srcRate)) + 128;
    std::vector<float> output(static_cast<size_t>(maxOut) * channels);
    const uint32_t written = src.process(input, inputFrames, output.data(), maxOut);
    output.resize(static_cast<size_t>(written) * channels);
    return {std::move(output), written, true};
}

#ifdef AESTRA_HAS_LIBSAMPLERATE
ResampleResult resampleLibsamplerate(const float* input, uint32_t inputFrames, uint32_t srcRate, uint32_t dstRate,
                                     uint32_t channels) {
    const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    const uint32_t maxOut = static_cast<uint32_t>(std::ceil(inputFrames * ratio)) + 128;
    std::vector<float> output(static_cast<size_t>(maxOut) * channels);

    SRC_DATA data{};
    data.data_in = input;
    data.data_out = output.data();
    data.input_frames = static_cast<long>(inputFrames);
    data.output_frames = static_cast<long>(maxOut);
    data.src_ratio = ratio;
    data.end_of_input = 1;

    const int error = src_simple(&data, SRC_SINC_BEST_QUALITY, static_cast<int>(channels));
    if (error != 0) {
        std::printf("  [REF] libsamplerate error: %s\n", src_strerror(error));
        return {{}, 0, true};
    }
    output.resize(static_cast<size_t>(data.output_frames_gen) * channels);
    return {std::move(output), static_cast<uint32_t>(data.output_frames_gen), true};
}
#endif

#ifdef AESTRA_HAS_SOXR
ResampleResult resampleSoxr(const float* input, uint32_t inputFrames, uint32_t srcRate, uint32_t dstRate,
                            uint32_t channels) {
    const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    const uint32_t maxOut = static_cast<uint32_t>(std::ceil(inputFrames * ratio)) + 128;
    std::vector<float> output(static_cast<size_t>(maxOut) * channels);

    soxr_quality_spec_t qspec = soxr_quality_spec(SOXR_VHQ, 0);
    soxr_t soxr = soxr_create(srcRate, dstRate, channels, nullptr, nullptr, &qspec, nullptr);
    if (!soxr) {
        std::printf("  [REF] soxr_create failed\n");
        return {{}, 0, true};
    }

    size_t written = 0;
    const soxr_error_t err = soxr_process(soxr, input, inputFrames, nullptr, output.data(), maxOut, &written);
    soxr_delete(soxr);
    if (err) {
        std::printf("  [REF] soxr error: %s\n", soxr_strerror(err));
        return {{}, 0, true};
    }

    output.resize(written * channels);
    return {std::move(output), static_cast<uint32_t>(written), true};
}
#endif

double meanAfterSettling(const ResampleResult& res, uint32_t channels, uint32_t skip) {
    if (res.outputFrames <= skip) {
        return 0.0;
    }
    double sum = 0.0;
    for (uint32_t i = skip; i < res.outputFrames; ++i) {
        sum += res.output[i * channels];
    }
    return sum / static_cast<double>(res.outputFrames - skip);
}

double rms(const std::vector<float>& samples, uint32_t channels, uint32_t startFrame, uint32_t endFrame) {
    double sum = 0.0;
    uint32_t count = 0;
    for (uint32_t frame = startFrame; frame < endFrame; ++frame) {
        const double v = samples[frame * channels];
        sum += v * v;
        ++count;
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

struct SineFit {
    double amplitude{0.0};
    double snrDb{0.0};
};

SineFit fitSine(const std::vector<float>& samples, uint32_t frames, uint32_t channels, double freqHz,
                double sampleRate, uint32_t skip) {
    double sumS = 0.0;
    double sumC = 0.0;
    double sumSig = 0.0;
    uint32_t count = 0;

    for (uint32_t i = skip; i < frames; ++i) {
        const double t = static_cast<double>(i - skip) / sampleRate;
        const double x = samples[i * channels];
        sumS += x * std::sin(2.0 * kPI * freqHz * t);
        sumC += x * std::cos(2.0 * kPI * freqHz * t);
        sumSig += x * x;
        ++count;
    }

    if (count == 0 || sumSig <= 0.0) {
        return {};
    }

    const double a = 2.0 * sumS / static_cast<double>(count);
    const double b = 2.0 * sumC / static_cast<double>(count);
    const double amp = std::sqrt(a * a + b * b);
    const double phase = std::atan2(b, a);

    double err = 0.0;
    for (uint32_t i = skip; i < frames; ++i) {
        const double t = static_cast<double>(i - skip) / sampleRate;
        const double fitted = amp * std::sin(2.0 * kPI * freqHz * t + phase);
        const double diff = static_cast<double>(samples[i * channels]) - fitted;
        err += diff * diff;
    }

    return {amp, err > 0.0 ? 10.0 * std::log10(sumSig / err) : 999.0};
}

double responseMagnitude(const PolyphaseFilterBank& bank, uint32_t phase, double normalizedFrequency) {
    std::complex<double> h{0.0, 0.0};
    for (uint32_t tap = 0; tap < bank.numTaps; ++tap) {
        const double angle = -2.0 * kPI * normalizedFrequency * static_cast<double>(tap);
        h += static_cast<double>(bank.coeffs[phase][tap]) *
             std::complex<double>{std::cos(angle), std::sin(angle)};
    }
    return std::abs(h);
}

double phaseSum(const PolyphaseFilterBank& bank, uint32_t phase) {
    double sum = 0.0;
    for (uint32_t tap = 0; tap < bank.numTaps; ++tap) {
        sum += bank.coeffs[phase][tap];
    }
    return sum;
}

double phasePeak(const PolyphaseFilterBank& bank, uint32_t phase) {
    double peak = 0.0;
    for (uint32_t tap = 0; tap < bank.numTaps; ++tap) {
        peak = std::max(peak, std::abs(static_cast<double>(bank.coeffs[phase][tap])));
    }
    return peak;
}

void testDCGain(Gate& gate) {
    std::printf("\n[Gate] DC gain\n");
    constexpr uint32_t srcRate = 48000;
    constexpr uint32_t dstRate = 44100;
    constexpr uint32_t frames = srcRate;
    constexpr uint32_t channels = 2;
    const Signal dc = makeDC(frames, 1.0);

    for (SRCQuality quality : {SRCQuality::Sinc16, SRCQuality::Sinc64}) {
        const ResampleResult res = resampleAestra(dc.interleaved.data(), frames, srcRate, dstRate, channels, quality);
        const double mean = meanAfterSettling(res, channels, 500);
        gate.expect(quality == SRCQuality::Sinc16 ? "Aestra Sinc16 DC gain ~= 1" : "Aestra Sinc64 DC gain ~= 1",
                    res.outputFrames > 1000 && std::abs(mean - 1.0) <= 5e-5,
                    "mean=" + std::to_string(mean) + ", frames=" + std::to_string(res.outputFrames));
    }
}

void testImpulseSanity(Gate& gate) {
    std::printf("\n[Gate] impulse response sanity\n");
    constexpr uint32_t srcRate = 48000;
    constexpr uint32_t dstRate = 44100;
    constexpr uint32_t frames = srcRate;
    constexpr uint32_t impPos = 256;
    constexpr uint32_t channels = 2;
    const Signal imp = makeImpulse(frames, impPos);

    for (SRCQuality quality : {SRCQuality::Sinc16, SRCQuality::Sinc64}) {
        const ResampleResult res = resampleAestra(imp.interleaved.data(), frames, srcRate, dstRate, channels, quality);
        float peak = 0.0f;
        uint32_t peakIndex = 0;
        for (uint32_t i = 0; i < res.outputFrames; ++i) {
            const float v = std::abs(res.output[i * channels]);
            if (!std::isfinite(v)) {
                gate.expect("Impulse response finite", false, "non-finite sample at frame " + std::to_string(i));
                return;
            }
            if (v > peak) {
                peak = v;
                peakIndex = i;
            }
        }

        const double expectedPeak = static_cast<double>(impPos) * dstRate / static_cast<double>(srcRate);
        const double peakError = std::abs(static_cast<double>(peakIndex) - expectedPeak);
        const double tailRms = rms(res.output, channels, std::min(peakIndex + 128, res.outputFrames), res.outputFrames);

        gate.expect(quality == SRCQuality::Sinc16 ? "Aestra Sinc16 impulse peak is sane"
                                                  : "Aestra Sinc64 impulse peak is sane",
                    res.outputFrames > 1000 && peak > 0.60f && peak < 1.05f && peakError <= 2.0 && tailRms < 1e-4,
                    "peak=" + std::to_string(peak) + ", peakError=" + std::to_string(peakError) +
                        ", tailRms=" + std::to_string(tailRms));
    }
}

void testPhaseContinuity(Gate& gate) {
    std::printf("\n[Gate] phase-bank continuity\n");
    SampleRateConverter src;
    src.configure(48000, 44100, 1, SRCQuality::Sinc64);

    const PolyphaseFilterBank* bank = src.getFilterBankForTests();
    if (!bank || bank->numTaps == 0) {
        gate.expect("Phase bank exists", false);
        return;
    }

    double worstDcError = 0.0;
    double worstAdjacentPeakDelta = 0.0;
    double worstAdjacentResponseDelta = 0.0;
    for (uint32_t phase = 0; phase < SRCConstants::POLYPHASE_PHASES; ++phase) {
        worstDcError = std::max(worstDcError, std::abs(phaseSum(*bank, phase) - 1.0));
        const uint32_t nextPhase = (phase + 1) & (SRCConstants::POLYPHASE_PHASES - 1);
        const double peakDelta = std::abs(phasePeak(*bank, phase) - phasePeak(*bank, nextPhase));
        worstAdjacentPeakDelta = std::max(worstAdjacentPeakDelta, peakDelta);
        for (double f : {0.0, 0.05, 0.10, 0.20, 0.35, 0.43}) {
            const double delta = std::abs(responseMagnitude(*bank, phase, f) - responseMagnitude(*bank, nextPhase, f));
            worstAdjacentResponseDelta = std::max(worstAdjacentResponseDelta, delta);
        }
    }

    const double phase0NeighborPeak =
        0.5 * (phasePeak(*bank, 1) + phasePeak(*bank, SRCConstants::POLYPHASE_PHASES - 1));
    double worstPhase0ResponseDelta = 0.0;
    for (double f : {0.0, 0.05, 0.10, 0.20, 0.35, 0.43}) {
        const double neighborMag = 0.5 * (responseMagnitude(*bank, 1, f) +
                                          responseMagnitude(*bank, SRCConstants::POLYPHASE_PHASES - 1, f));
        const double phase0ResponseDelta = std::abs(responseMagnitude(*bank, 0, f) - neighborMag);
        worstPhase0ResponseDelta = std::max(worstPhase0ResponseDelta, phase0ResponseDelta);
    }

    gate.expect("All phase kernels have unity DC sum", worstDcError <= 1e-6,
                "worstDcError=" + std::to_string(worstDcError));
    gate.expect("Adjacent phase impulse peaks are continuous", worstAdjacentPeakDelta <= 0.01,
                "worstAdjacentPeakDelta=" + std::to_string(worstAdjacentPeakDelta));
    gate.expect("Adjacent phase magnitude responses are continuous", worstAdjacentResponseDelta <= 0.02,
                "worstAdjacentResponseDelta=" + std::to_string(worstAdjacentResponseDelta));
    gate.expect("Phase 0 has no impulse peak anomaly", std::abs(phasePeak(*bank, 0) - phase0NeighborPeak) <= 0.03,
                "phase0=" + std::to_string(phasePeak(*bank, 0)) +
                    ", neighborMean=" + std::to_string(phase0NeighborPeak));
    gate.expect("Phase 0 has no magnitude-response anomaly", worstPhase0ResponseDelta <= 0.02,
                "worstPhase0ResponseDelta=" + std::to_string(worstPhase0ResponseDelta));
}

void testRepresentativeFrequencyResponse(Gate& gate) {
    std::printf("\n[Gate] representative frequency response\n");
    constexpr uint32_t srcRate = 48000;
    constexpr uint32_t dstRate = 44100;
    constexpr uint32_t frames = srcRate;
    constexpr uint32_t channels = 2;

    for (double freq : {100.0, 997.0, 5000.0, 12000.0}) {
        const Signal sig = makeSine(srcRate, frames, freq, 0.5);
        const ResampleResult res =
            resampleAestra(sig.interleaved.data(), frames, srcRate, dstRate, channels, SRCQuality::Sinc64);
        const SineFit fit = fitSine(res.output, res.outputFrames, channels, freq, dstRate, 1000);
        const bool gainOk = fit.amplitude >= 0.40 && fit.amplitude <= 0.55;
        const bool coherenceOk = fit.snrDb >= 18.0;
        gate.expect("Aestra Sinc64 representative sine response is bounded",
                    res.outputFrames > 1000 && gainOk && coherenceOk,
                    "freq=" + std::to_string(freq) + ", amplitude=" + std::to_string(fit.amplitude) +
                        ", snrDb=" + std::to_string(fit.snrDb));
    }
}

void runOptionalReferences() {
    std::printf("\n[Optional references]\n");
    constexpr uint32_t srcRate = 48000;
    constexpr uint32_t dstRate = 44100;
    constexpr uint32_t frames = srcRate;
    constexpr uint32_t channels = 2;
    const Signal dc = makeDC(frames, 1.0);

#ifdef AESTRA_HAS_LIBSAMPLERATE
    const ResampleResult libsr = resampleLibsamplerate(dc.interleaved.data(), frames, srcRate, dstRate, channels);
    std::printf("  libsamplerate DC mean: %.6f (%u frames)\n", meanAfterSettling(libsr, channels, 500),
                libsr.outputFrames);
#else
    std::printf("  libsamplerate unavailable; reference comparison skipped\n");
#endif

#ifdef AESTRA_HAS_SOXR
    const ResampleResult soxr = resampleSoxr(dc.interleaved.data(), frames, srcRate, dstRate, channels);
    std::printf("  soxr VHQ DC mean: %.6f (%u frames)\n", meanAfterSettling(soxr, channels, 500), soxr.outputFrames);
#else
    std::printf("  soxr unavailable; reference comparison skipped\n");
#endif
}

} // namespace

int main() {
    std::printf("============================================================\n");
    std::printf("  Resampler War Test - Audio Quality Gate\n");
    std::printf("============================================================\n");

    Gate gate;
    testDCGain(gate);
    testImpulseSanity(gate);
    testPhaseContinuity(gate);
    testRepresentativeFrequencyResponse(gate);
    runOptionalReferences();

    std::printf("\n============================================================\n");
    std::printf("  Passed: %d\n", gate.passed);
    std::printf("  Failed: %d\n", gate.failed);
    std::printf("============================================================\n");
    return gate.result();
}
