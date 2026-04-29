// AestraVerb Quality Measurement Lab
// Generates impulse responses, noise bursts, and diagnostic metrics for
// Room, Hall, and Plate modes.
//
// Outputs:
//   - labs/reverb/quality/reverb_quality_baseline.json
//   - labs/reverb/quality/reverb_quality_baseline.md
//   - labs/reverb/quality/impulse_*.wav
//   - labs/reverb/quality/noiseburst_*.wav

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

// ============================================================================
// Simple power-of-2 FFT for spectral analysis
// ============================================================================
static void fft(std::vector<std::complex<float>>& a) {
    size_t n = a.size();
    if (n <= 1) return;

    // Bit-reverse permutation
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<float> u = a[i + j];
                std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ============================================================================
// WAV file writer (32-bit float stereo)
// ============================================================================
static bool writeWavFloat(const std::string& path, const std::vector<float>& left,
                          const std::vector<float>& right, float sampleRate) {
    if (left.size() != right.size() || left.empty()) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t numSamples = static_cast<uint32_t>(left.size());
    uint32_t byteRate = static_cast<uint32_t>(sampleRate) * 8; // 2 ch * 4 bytes
    uint32_t dataSize = numSamples * 8;
    uint32_t fileSize = 36 + dataSize;

    auto writeU32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4);
    writeU32(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32(16);       // subchunk1Size
    writeU16(3);        // audioFormat = IEEE float
    writeU16(2);        // numChannels
    writeU32(static_cast<uint32_t>(sampleRate));
    writeU32(byteRate);
    writeU16(8);        // blockAlign
    writeU16(32);       // bitsPerSample
    f.write("data", 4);
    writeU32(dataSize);

    for (uint32_t i = 0; i < numSamples; ++i) {
        f.write(reinterpret_cast<const char*>(&left[i]), 4);
        f.write(reinterpret_cast<const char*>(&right[i]), 4);
    }
    return f.good();
}

// ============================================================================
// Metric structures
// ============================================================================
struct DecayMetrics {
    float t20 = 0.0f;
    float t40 = 0.0f;
    float t60 = 0.0f;
    float maxReboundDb = 0.0f;
    bool monotonic = true;
};

struct SpectralMetrics {
    float lowEnergy = 0.0f;     // 20-500 Hz
    float midEnergy = 0.0f;     // 500-4000 Hz
    float highEnergy = 0.0f;    // 4000-20000 Hz
    float crestFactor = 0.0f;   // max bin / avg bin in late tail
    float metallicPeakHz = 0.0f;
    float metallicPeakDb = 0.0f;       // dB above broadband average
    float metallicPeakLocalDb = 0.0f;  // dB above local average (+/- 5 bins)
    std::string metallicPeakBand;      // low, low-mid, mid, presence, air
    std::string metallicPeakSeverity;  // low, medium, high
    bool metallicPeakPersistent = false;
};

struct StereoMetrics {
    float corrEarly = 0.0f;
    float corrLate = 0.0f;
    float monoPeak = 0.0f;
    float widthProxy = 0.0f;
};

struct TransientMetrics {
    float bloomTimeMs = 0.0f;        // time from impulse to -3dB below peak
    float earlyOnsetMs = 0.0f;       // first sample > -60dB relative to early-window peak
    float earlyPeakMs = 0.0f;        // time of maximum in early window
    float earlyPeakCount = 0.0f;     // local maxima above -30dB rel to early-window peak
    float earlyActiveBins1ms = 0.0f; // 1ms bins with energy above -40dB rel to early-window peak
    float earlyActiveBinRatio = 0.0f;// active bins / total bins in early window
    float earlyCrestFactor = 0.0f;   // peak / RMS in early window
    float earlyEnergyMs25 = 0.0f;    // ms to reach 25% of early-window cumulative energy
    float earlyEnergyMs50 = 0.0f;    // ms to reach 50% of early-window cumulative energy
    float earlyEnergyMs75 = 0.0f;    // ms to reach 75% of early-window cumulative energy
    float earlyDensityScore = 0.0f;  // composite: peakCount * activeRatio / crestFactor
};

struct ModeQuality {
    std::string name;
    DecayMetrics decay;
    SpectralMetrics spectral;
    StereoMetrics stereo;
    TransientMetrics transient;
    float rms = 0.0f;
    float peak = 0.0f;
    float hfRatio = 0.0f;
    float earlyCentroidHz = 0.0f;
    float lateCentroidHz = 0.0f;
    float earlyHighEnergy = 0.0f;
    float lateHighEnergy = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
};

// ============================================================================
// Metric computation
// ============================================================================
static DecayMetrics computeDecay(const std::vector<float>& energy, float sampleRate) {
    DecayMetrics m;
    if (energy.empty()) return m;

    // Schroeder backward integration for smoother decay curve
    size_t n = energy.size();
    std::vector<float> schroeder(n);
    double acc = 0.0;
    for (size_t i = n; i > 0; --i) {
        acc += energy[i - 1];
        schroeder[i - 1] = static_cast<float>(acc);
    }

    float peak = schroeder[0];
    float peakDb = 10.0f * std::log10(std::max(peak, 1e-20f));
    float prevDb = peakDb;
    float windowMs = 256.0f / sampleRate * 1000.0f;

    for (size_t i = 1; i < n; ++i) {
        float db = 10.0f * std::log10(std::max(schroeder[i], 1e-20f));
        if (db > prevDb + 1.0f) {
            m.monotonic = false;
            m.maxReboundDb = std::max(m.maxReboundDb, db - prevDb);
        }
        prevDb = db;

        float t = static_cast<float>(i) * windowMs;
        if (m.t20 == 0.0f && db <= peakDb - 20.0f) m.t20 = t;
        if (m.t40 == 0.0f && db <= peakDb - 40.0f) m.t40 = t;
        if (m.t60 == 0.0f && db <= peakDb - 60.0f) m.t60 = t;
    }
    if (m.t20 == 0.0f) m.t20 = static_cast<float>(n) * windowMs;
    if (m.t40 == 0.0f) m.t40 = m.t20;
    if (m.t60 == 0.0f) m.t60 = m.t40;
    return m;
}

static SpectralMetrics computeSpectral(const std::vector<float>& signal, float sampleRate) {
    SpectralMetrics m;
    size_t n = signal.size();
    if (n < 512) return m;

    // Use middle section (25-75% of signal) for spectral analysis
    // This avoids the direct sound (start) and near-silence (end)
    size_t start = n / 4;
    size_t fftSize = 2048;
    if (start + fftSize > n) start = (n > fftSize) ? (n - fftSize) : 0;
    std::vector<std::complex<float>> fftBuf(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        size_t idx = start + i;
        if (idx < n) {
            // Hann window
            float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
            fftBuf[i] = std::complex<float>(signal[idx] * w, 0.0f);
        } else {
            fftBuf[i] = std::complex<float>(0.0f, 0.0f);
        }
    }
    fft(fftBuf);

    float binHz = sampleRate / static_cast<float>(fftSize);
    float totalMag = 0.0f;
    float maxMag = 0.0f;
    float avgMag = 0.0f;
    size_t count = 0;

    for (size_t i = 0; i <= fftSize / 2; ++i) {
        float mag = std::abs(fftBuf[i]);
        float hz = static_cast<float>(i) * binHz;

        if (hz >= 20.0f && hz < 500.0f) m.lowEnergy += mag * mag;
        else if (hz >= 500.0f && hz < 4000.0f) m.midEnergy += mag * mag;
        else if (hz >= 4000.0f) m.highEnergy += mag * mag;

        if (i > 10) { // skip DC and very low bins
            totalMag += mag;
            avgMag += mag;
            count++;
            if (mag > maxMag) {
                maxMag = mag;
                m.metallicPeakHz = hz;
            }
        }
    }

    if (count > 0) {
        avgMag /= static_cast<float>(count);
        m.crestFactor = maxMag / std::max(avgMag, 1e-20f);
        m.metallicPeakDb = 20.0f * std::log10(maxMag / std::max(avgMag, 1e-20f));

        // Local average (+/- 5 bins around peak)
        size_t peakBin = static_cast<size_t>(m.metallicPeakHz / binHz);
        float localSum = 0.0f;
        size_t localCount = 0;
        for (size_t j = (peakBin > 5 ? peakBin - 5 : 0); j <= peakBin + 5 && j <= fftSize / 2; ++j) {
            if (j != peakBin && j > 10) {
                localSum += std::abs(fftBuf[j]);
                localCount++;
            }
        }
        float localAvg = localCount > 0 ? localSum / static_cast<float>(localCount) : avgMag;
        m.metallicPeakLocalDb = 20.0f * std::log10(maxMag / std::max(localAvg, 1e-20f));

        // Band label
        if (m.metallicPeakHz < 200.0f) m.metallicPeakBand = "low";
        else if (m.metallicPeakHz < 900.0f) m.metallicPeakBand = "low-mid";
        else if (m.metallicPeakHz < 3000.0f) m.metallicPeakBand = "mid";
        else if (m.metallicPeakHz < 6000.0f) m.metallicPeakBand = "presence";
        else m.metallicPeakBand = "air";

        // Perceptual severity based on frequency band and local crest
        float localCrest = maxMag / std::max(localAvg, 1e-20f);
        if (m.metallicPeakBand == "low-mid" && localCrest > 8.0f) m.metallicPeakSeverity = "high";
        else if (m.metallicPeakBand == "mid" && localCrest > 10.0f) m.metallicPeakSeverity = "high";
        else if (m.metallicPeakBand == "presence" && localCrest > 12.0f) m.metallicPeakSeverity = "high";
        else if (m.metallicPeakBand == "air" && localCrest > 15.0f) m.metallicPeakSeverity = "high";
        else if (localCrest > 6.0f) m.metallicPeakSeverity = "medium";
        else if (localCrest > 3.0f) m.metallicPeakSeverity = "low";
        else m.metallicPeakSeverity = "none";

        // Persistence check: is the same frequency dominant in late tail (last 25%)?
        size_t lateStart = n * 3 / 4;
        if (lateStart + fftSize <= n) {
            std::vector<std::complex<float>> lateFft(fftSize);
            for (size_t i = 0; i < fftSize; ++i) {
                float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
                lateFft[i] = std::complex<float>(signal[lateStart + i] * w, 0.0f);
            }
            fft(lateFft);
            float lateMaxMag = 0.0f;
            size_t lateMaxBin = 0;
            for (size_t i = 11; i <= fftSize / 2; ++i) {
                float mag = std::abs(lateFft[i]);
                if (mag > lateMaxMag) {
                    lateMaxMag = mag;
                    lateMaxBin = i;
                }
            }
            float latePeakHz = static_cast<float>(lateMaxBin) * binHz;
            // Consider persistent if within +/- 1.5 bins
            m.metallicPeakPersistent = std::abs(latePeakHz - m.metallicPeakHz) < (1.5f * binHz);
        }
    }

    float sum = m.lowEnergy + m.midEnergy + m.highEnergy;
    if (sum > 0.0f) {
        m.lowEnergy /= sum;
        m.midEnergy /= sum;
        m.highEnergy /= sum;
    }
    return m;
}

static void computeTailToneOverTime(const std::vector<float>& signal, float sampleRate,
                                    float& earlyCentroidHz, float& lateCentroidHz,
                                    float& earlyHighEnergy, float& lateHighEnergy) {
    if (signal.size() < 4096) return;

    auto analyzeWindow = [&](size_t start, float& centroid, float& highEnergy) {
        constexpr size_t fftSize = 2048;
        if (start + fftSize > signal.size()) return;
        std::vector<std::complex<float>> fftBuf(fftSize);
        for (size_t i = 0; i < fftSize; ++i) {
            const float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) *
                                                    static_cast<float>(i) / static_cast<float>(fftSize - 1)));
            fftBuf[i] = std::complex<float>(signal[start + i] * w, 0.0f);
        }
        fft(fftBuf);

        const float binHz = sampleRate / static_cast<float>(fftSize);
        double weightedHz = 0.0;
        double energy = 0.0;
        double high = 0.0;
        for (size_t bin = 1; bin <= fftSize / 2; ++bin) {
            const float hz = static_cast<float>(bin) * binHz;
            const double mag = std::abs(fftBuf[bin]);
            const double binEnergy = mag * mag;
            weightedHz += static_cast<double>(hz) * binEnergy;
            energy += binEnergy;
            if (hz >= 4000.0f) {
                high += binEnergy;
            }
        }
        if (energy > 1.0e-20) {
            centroid = static_cast<float>(weightedHz / energy);
            highEnergy = static_cast<float>(high / energy);
        }
    };

    analyzeWindow(signal.size() / 4, earlyCentroidHz, earlyHighEnergy);
    analyzeWindow(signal.size() * 3 / 4, lateCentroidHz, lateHighEnergy);
}

static StereoMetrics computeStereo(const std::vector<float>& left, const std::vector<float>& right,
                                   float sampleRate) {
    StereoMetrics m;
    size_t n = left.size();
    if (n == 0 || right.size() != n) return m;

    // Early correlation (first 10ms)
    size_t earlySamples = static_cast<size_t>(sampleRate * 0.01f);
    earlySamples = std::min(earlySamples, n);
    double sumL = 0.0, sumR = 0.0, sumLR = 0.0, sumL2 = 0.0, sumR2 = 0.0;
    for (size_t i = 0; i < earlySamples; ++i) {
        sumL += left[i];
        sumR += right[i];
        sumLR += left[i] * right[i];
        sumL2 += left[i] * left[i];
        sumR2 += right[i] * right[i];
    }
    double meanL = sumL / earlySamples;
    double meanR = sumR / earlySamples;
    double cov = (sumLR / earlySamples) - meanL * meanR;
    double varL = (sumL2 / earlySamples) - meanL * meanL;
    double varR = (sumR2 / earlySamples) - meanR * meanR;
    m.corrEarly = static_cast<float>(cov / std::max(std::sqrt(varL * varR), 1e-20));

    // Late correlation (last 20% of tail)
    size_t lateStart = n * 4 / 5;
    sumL = sumR = sumLR = sumL2 = sumR2 = 0.0;
    for (size_t i = lateStart; i < n; ++i) {
        sumL += left[i];
        sumR += right[i];
        sumLR += left[i] * right[i];
        sumL2 += left[i] * left[i];
        sumR2 += right[i] * right[i];
    }
    size_t lateCount = n - lateStart;
    meanL = sumL / lateCount;
    meanR = sumR / lateCount;
    cov = (sumLR / lateCount) - meanL * meanR;
    varL = (sumL2 / lateCount) - meanL * meanL;
    varR = (sumR2 / lateCount) - meanR * meanR;
    m.corrLate = static_cast<float>(cov / std::max(std::sqrt(varL * varR), 1e-20));

    // Mono fold-down peak
    float monoPeak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        monoPeak = std::max(monoPeak, std::abs(left[i] + right[i]) * 0.5f);
    }
    m.monoPeak = monoPeak;

    // Width proxy: L-R RMS / L+R RMS
    double sumDiffSq = 0.0, sumSumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float d = left[i] - right[i];
        float s = left[i] + right[i];
        sumDiffSq += d * d;
        sumSumSq += s * s;
    }
    m.widthProxy = static_cast<float>(std::sqrt(sumDiffSq / std::max(sumSumSq, 1e-20)));

    return m;
}

static TransientMetrics computeTransient(const std::vector<float>& left, const std::vector<float>& right,
                                         float sampleRate) {
    TransientMetrics m;
    size_t n = left.size();
    if (n == 0) return m;

    // Bloom time: time for running RMS (10ms window) to drop 3dB from initial peak
    size_t window = static_cast<size_t>(sampleRate * 0.01f);
    window = std::max(window, size_t(1));
    float peakRms = 0.0f;
    size_t peakIdx = 0;
    std::vector<float> rms(n);
    for (size_t i = 0; i < n; ++i) {
        size_t end = std::min(i + window, n);
        size_t start = (i >= window) ? (i - window) : 0;
        double sumSq = 0.0;
        for (size_t j = start; j < end; ++j) {
            sumSq += left[j] * left[j] + right[j] * right[j];
        }
        rms[i] = static_cast<float>(std::sqrt(sumSq / static_cast<double>(end - start) / 2.0));
        if (rms[i] > peakRms) { peakRms = rms[i]; peakIdx = i; }
    }
    float threshold = peakRms * 0.707f;
    for (size_t i = peakIdx; i < n; ++i) {
        if (rms[i] < threshold) {
            m.bloomTimeMs = static_cast<float>(i - peakIdx) / sampleRate * 1000.0f;
            break;
        }
    }
    if (m.bloomTimeMs == 0.0f) m.bloomTimeMs = static_cast<float>(n - peakIdx) / sampleRate * 1000.0f;

    // =========================================================================
    // Session 014: Comprehensive early reflection metrics
    // =========================================================================
    // The old metric reported 0 density because:
    // 1. First 50ms is silent (predelay + processing latency)
    // 2. Threshold was relative to direct impulse peak (1.0), which is far above
    //    the actual early reflection energy (~0.05-0.4)
    // 3. AestraVerb produces a concentrated early energy burst, not multiple
    //    discrete peaks in the first 50ms
    //
    // New approach: use a wider window (0-150ms), threshold relative to the
    // EARLY WINDOW peak, and report multiple complementary metrics.
    // =========================================================================

    const size_t kEarlyWindowEnd = static_cast<size_t>(sampleRate * 0.15f); // 150ms
    const size_t kEarlyEnd = std::min(kEarlyWindowEnd, n);
    if (kEarlyEnd < 3) return m;

    // Mono sum for analysis
    std::vector<float> mono(kEarlyEnd);
    for (size_t i = 0; i < kEarlyEnd; ++i) {
        mono[i] = std::abs(left[i]) + std::abs(right[i]);
    }

    // Find early-window peak (ignoring sample 0 which is the input impulse)
    float earlyPeak = 0.0f;
    size_t earlyPeakIdx = 0;
    for (size_t i = 1; i < kEarlyEnd; ++i) {
        if (mono[i] > earlyPeak) {
            earlyPeak = mono[i];
            earlyPeakIdx = i;
        }
    }
    if (earlyPeak < 1e-10f) return m; // no early energy at all

    m.earlyPeakMs = static_cast<float>(earlyPeakIdx) / sampleRate * 1000.0f;

    // Onset: first sample > -60 dB relative to early-window peak
    const float onsetThresh = earlyPeak * 0.001f; // -60 dB
    for (size_t i = 1; i < kEarlyEnd; ++i) {
        if (mono[i] > onsetThresh) {
            m.earlyOnsetMs = static_cast<float>(i) / sampleRate * 1000.0f;
            break;
        }
    }

    // Peak count: local maxima above -30 dB relative to early-window peak
    const float peakThresh = earlyPeak * 0.0316f; // -30 dB
    int peakCount = 0;
    for (size_t i = 2; i + 2 < kEarlyEnd; ++i) {
        if (mono[i] > mono[i-1] && mono[i] > mono[i+1] && mono[i] > peakThresh) {
            peakCount++;
        }
    }
    m.earlyPeakCount = static_cast<float>(peakCount);

    // 1ms bin analysis: count active bins
    const size_t bins1ms = static_cast<size_t>(sampleRate * 0.001f); // samples per 1ms bin
    if (bins1ms > 0) {
        size_t totalBins = (kEarlyEnd + bins1ms - 1) / bins1ms;
        const float binThresh = earlyPeak * 0.01f; // -40 dB
        size_t activeBins = 0;
        for (size_t b = 0; b < totalBins; ++b) {
            size_t start = b * bins1ms;
            size_t end = std::min(start + bins1ms, kEarlyEnd);
            float binMax = 0.0f;
            for (size_t i = start; i < end; ++i) {
                binMax = std::max(binMax, mono[i]);
            }
            if (binMax > binThresh) activeBins++;
        }
        m.earlyActiveBins1ms = static_cast<float>(activeBins);
        m.earlyActiveBinRatio = totalBins > 0 ? static_cast<float>(activeBins) / static_cast<float>(totalBins) : 0.0f;
    }

    // Crest factor in early window
    double earlySumSq = 0.0;
    for (size_t i = 0; i < kEarlyEnd; ++i) {
        earlySumSq += mono[i] * mono[i];
    }
    float earlyRms = static_cast<float>(std::sqrt(earlySumSq / static_cast<double>(kEarlyEnd)));
    m.earlyCrestFactor = earlyPeak / std::max(earlyRms, 1e-20f);

    // Energy buildup: time to reach 25%, 50%, 75% of cumulative energy
    std::vector<double> cumEnergy(kEarlyEnd);
    double totalEnergy = 0.0;
    for (size_t i = 0; i < kEarlyEnd; ++i) {
        totalEnergy += mono[i] * mono[i];
        cumEnergy[i] = totalEnergy;
    }
    if (totalEnergy > 1e-20) {
        for (size_t i = 0; i < kEarlyEnd; ++i) {
            float ratio = static_cast<float>(cumEnergy[i] / totalEnergy);
            if (m.earlyEnergyMs25 == 0.0f && ratio >= 0.25f) {
                m.earlyEnergyMs25 = static_cast<float>(i) / sampleRate * 1000.0f;
            }
            if (m.earlyEnergyMs50 == 0.0f && ratio >= 0.50f) {
                m.earlyEnergyMs50 = static_cast<float>(i) / sampleRate * 1000.0f;
            }
            if (m.earlyEnergyMs75 == 0.0f && ratio >= 0.75f) {
                m.earlyEnergyMs75 = static_cast<float>(i) / sampleRate * 1000.0f;
                break;
            }
        }
    }

    // Composite density score: more peaks + more active bins = higher density
    // Divided by crest factor to penalize sparse single-spike responses
    m.earlyDensityScore = (m.earlyPeakCount * m.earlyActiveBinRatio) / std::max(m.earlyCrestFactor, 1.0f);

    return m;
}

// ============================================================================
// Generate test signal responses
// ============================================================================
static ModeQuality measureMode(const std::string& name, float modeParam,
                               float decayParam, float sizeParam, float diffusionParam,
                               float sampleRate, uint32_t numFrames,
                               const std::string& qualityDir) {
    ModeQuality q;
    q.name = name;

    AestraVerb verb;
    verb.initialize(sampleRate, 256);
    verb.setParameter(AestraVerb::kMode, modeParam);
    verb.setParameter(AestraVerb::kDecay, decayParam);
    verb.setParameter(AestraVerb::kSize, sizeParam);
    verb.setParameter(AestraVerb::kDiffusion, diffusionParam);
    verb.setParameter(AestraVerb::kModRate, 0.5f);
    verb.setParameter(AestraVerb::kModDepth, 0.3f);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.setParameter(AestraVerb::kWidth, 0.7f);
    verb.activate();

    // 1. Impulse response
    std::vector<float> impulseL(numFrames, 0.0f), impulseR(numFrames, 0.0f);
    impulseL[0] = 1.0f;
    impulseR[0] = 1.0f;
    std::vector<float> tailL(numFrames), tailR(numFrames);

    const float* inPtrs[2] = { impulseL.data(), impulseR.data() };
    float* outPtrs[2] = { tailL.data(), tailR.data() };
    verb.process(inPtrs, outPtrs, 2, 2, numFrames);

    // Check for NaN/Inf
    for (uint32_t i = 0; i < numFrames; ++i) {
        if (!std::isfinite(tailL[i]) || !std::isfinite(tailR[i])) {
            q.hasNaN = q.hasNaN || std::isnan(tailL[i]) || std::isnan(tailR[i]);
            q.hasInf = q.hasInf || std::isinf(tailL[i]) || std::isinf(tailR[i]);
        }
    }

    // Write impulse WAV
    writeWavFloat(qualityDir + "/impulse_" + name + ".wav", tailL, tailR, sampleRate);

    // 2. Noise burst response (500 samples white noise)
    std::vector<float> noiseL(numFrames, 0.0f), noiseR(numFrames, 0.0f);
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < 500 && i < numFrames; ++i) {
        seed = seed * 1103515245u + 12345u;
        float v = static_cast<float>((seed >> 16) & 0x7FFFu) / 16384.0f - 1.0f;
        noiseL[i] = v;
        noiseR[i] = v;
    }
    std::vector<float> noiseOutL(numFrames), noiseOutR(numFrames);
    inPtrs[0] = noiseL.data(); inPtrs[1] = noiseR.data();
    outPtrs[0] = noiseOutL.data(); outPtrs[1] = noiseOutR.data();
    verb.process(inPtrs, outPtrs, 2, 2, numFrames);
    writeWavFloat(qualityDir + "/noiseburst_" + name + ".wav", noiseOutL, noiseOutR, sampleRate);

    // 3. RMS and peak
    double sumSq = 0.0;
    for (uint32_t i = 0; i < numFrames; ++i) {
        q.peak = std::max(q.peak, std::max(std::abs(tailL[i]), std::abs(tailR[i])));
        sumSq += tailL[i] * tailL[i] + tailR[i] * tailR[i];
    }
    q.rms = static_cast<float>(std::sqrt(sumSq / (2.0 * numFrames)));

    // 4. HF energy ratio
    double hfEnergy = 0.0, totalEnergy = 0.0;
    for (uint32_t i = 0; i < numFrames; ++i) {
        totalEnergy += tailL[i] * tailL[i];
        if (i > 0) {
            float diff = tailL[i] - tailL[i-1];
            hfEnergy += diff * diff;
        }
    }
    q.hfRatio = totalEnergy > 1e-20f ? static_cast<float>(hfEnergy / totalEnergy) : 0.0f;

    // 5. Energy decay curve (256-sample windows)
    size_t numWindows = numFrames / 256;
    std::vector<float> energy(numWindows);
    for (size_t w = 0; w < numWindows; ++w) {
        double e = 0.0;
        for (size_t i = 0; i < 256; ++i) {
            size_t idx = w * 256 + i;
            e += tailL[idx] * tailL[idx] + tailR[idx] * tailR[idx];
        }
        energy[w] = static_cast<float>(e / 512.0);
    }
    q.decay = computeDecay(energy, sampleRate);

    // 6. Spectral metrics (use L channel)
    q.spectral = computeSpectral(tailL, sampleRate);
    computeTailToneOverTime(tailL, sampleRate,
                            q.earlyCentroidHz, q.lateCentroidHz,
                            q.earlyHighEnergy, q.lateHighEnergy);

    // 7. Stereo metrics
    q.stereo = computeStereo(tailL, tailR, sampleRate);

    // 8. Transient metrics
    q.transient = computeTransient(tailL, tailR, sampleRate);

    return q;
}

// ============================================================================
// Report generation
// ============================================================================
static std::string generateJsonReport(const std::vector<ModeQuality>& modes, float sampleRate) {
    std::ostringstream j;
    j << "{\n";
    j << "  \"sampleRate\": " << static_cast<int>(sampleRate) << ",\n";
    j << "  \"frames\": 8192,\n";
    j << "  \"modes\": [\n";
    for (size_t m = 0; m < modes.size(); ++m) {
        const auto& q = modes[m];
        j << "    {\n";
        j << "      \"name\": \"" << q.name << "\",\n";
        j << "      \"rms\": " << q.rms << ",\n";
        j << "      \"peak\": " << q.peak << ",\n";
        j << "      \"hfRatio\": " << q.hfRatio << ",\n";
        j << "      \"earlyCentroidHz\": " << q.earlyCentroidHz << ",\n";
        j << "      \"lateCentroidHz\": " << q.lateCentroidHz << ",\n";
        j << "      \"earlyHighEnergy\": " << q.earlyHighEnergy << ",\n";
        j << "      \"lateHighEnergy\": " << q.lateHighEnergy << ",\n";
        j << "      \"hasNaN\": " << (q.hasNaN ? "true" : "false") << ",\n";
        j << "      \"hasInf\": " << (q.hasInf ? "true" : "false") << ",\n";
        j << "      \"decay\": {\n";
        j << "        \"t20Ms\": " << q.decay.t20 << ",\n";
        j << "        \"t40Ms\": " << q.decay.t40 << ",\n";
        j << "        \"t60Ms\": " << q.decay.t60 << ",\n";
        j << "        \"monotonic\": " << (q.decay.monotonic ? "true" : "false") << ",\n";
        j << "        \"maxReboundDb\": " << q.decay.maxReboundDb << "\n";
        j << "      },\n";
        j << "      \"spectral\": {\n";
        j << "        \"lowRatio\": " << q.spectral.lowEnergy << ",\n";
        j << "        \"midRatio\": " << q.spectral.midEnergy << ",\n";
        j << "        \"highRatio\": " << q.spectral.highEnergy << ",\n";
        j << "        \"crestFactor\": " << q.spectral.crestFactor << ",\n";
        j << "        \"metallicPeakHz\": " << q.spectral.metallicPeakHz << ",\n";
        j << "        \"metallicPeakDb\": " << q.spectral.metallicPeakDb << ",\n";
        j << "        \"metallicPeakLocalDb\": " << q.spectral.metallicPeakLocalDb << ",\n";
        j << "        \"metallicPeakBand\": \"" << q.spectral.metallicPeakBand << "\",\n";
        j << "        \"metallicPeakSeverity\": \"" << q.spectral.metallicPeakSeverity << "\",\n";
        j << "        \"metallicPeakPersistent\": " << (q.spectral.metallicPeakPersistent ? "true" : "false") << "\n";
        j << "      },\n";
        j << "      \"stereo\": {\n";
        j << "        \"corrEarly\": " << q.stereo.corrEarly << ",\n";
        j << "        \"corrLate\": " << q.stereo.corrLate << ",\n";
        j << "        \"monoPeak\": " << q.stereo.monoPeak << ",\n";
        j << "        \"widthProxy\": " << q.stereo.widthProxy << "\n";
        j << "      },\n";
        j << "      \"transient\": {\n";
        j << "        \"bloomTimeMs\": " << q.transient.bloomTimeMs << ",\n";
        j << "        \"earlyOnsetMs\": " << q.transient.earlyOnsetMs << ",\n";
        j << "        \"earlyPeakMs\": " << q.transient.earlyPeakMs << ",\n";
        j << "        \"earlyPeakCount\": " << q.transient.earlyPeakCount << ",\n";
        j << "        \"earlyActiveBins1ms\": " << q.transient.earlyActiveBins1ms << ",\n";
        j << "        \"earlyActiveBinRatio\": " << q.transient.earlyActiveBinRatio << ",\n";
        j << "        \"earlyCrestFactor\": " << q.transient.earlyCrestFactor << ",\n";
        j << "        \"earlyEnergyMs25\": " << q.transient.earlyEnergyMs25 << ",\n";
        j << "        \"earlyEnergyMs50\": " << q.transient.earlyEnergyMs50 << ",\n";
        j << "        \"earlyEnergyMs75\": " << q.transient.earlyEnergyMs75 << ",\n";
        j << "        \"earlyDensityScore\": " << q.transient.earlyDensityScore << "\n";
        j << "      }\n";
        j << "    }" << (m + 1 < modes.size() ? "," : "") << "\n";
    }
    j << "  ]\n";
    j << "}\n";
    return j.str();
}

static std::string generateMarkdownReport(const std::vector<ModeQuality>& modes) {
    std::ostringstream md;
    md << "# AestraVerb Quality Measurement Baseline\n\n";
    md << "Generated by AestraReverbQualityLab.\n\n";

    md << "## Summary Table\n\n";
    md << "| Mode | T20 (ms) | T40 (ms) | T60 (ms) | Monotonic | Rebound (dB) |\n";
    md << "|------|----------|----------|----------|-----------|-------------|\n";
    for (const auto& q : modes) {
        md << "| " << q.name << " | " << static_cast<int>(q.decay.t20)
           << " | " << static_cast<int>(q.decay.t40)
           << " | " << static_cast<int>(q.decay.t60)
           << " | " << (q.decay.monotonic ? "Yes" : "No")
           << " | " << std::fixed << std::setprecision(1) << q.decay.maxReboundDb
           << " |\n";
    }

    md << "\n## Spectral Profile\n\n";
    md << "| Mode | Low (20-500Hz) | Mid (500-4kHz) | High (4k-20kHz) | Crest | Metallic Peak (Hz/dB/local/band/sev) |\n";
    md << "|------|----------------|----------------|-----------------|-------|---------------------------------------|\n";
    for (const auto& q : modes) {
        md << "| " << q.name
           << " | " << std::fixed << std::setprecision(2) << q.spectral.lowEnergy
           << " | " << q.spectral.midEnergy
           << " | " << q.spectral.highEnergy
           << " | " << std::setprecision(1) << q.spectral.crestFactor
           << " | " << static_cast<int>(q.spectral.metallicPeakHz) << " Hz / "
           << std::setprecision(1) << q.spectral.metallicPeakDb << " dB / "
           << std::setprecision(1) << q.spectral.metallicPeakLocalDb << " local / "
           << q.spectral.metallicPeakBand << " / "
           << q.spectral.metallicPeakSeverity
           << (q.spectral.metallicPeakPersistent ? " (persist)" : "")
           << " |\n";
    }

    md << "\n## Tail Tone Over Time\n\n";
    md << "| Mode | Early Centroid | Late Centroid | Early High Energy | Late High Energy | Darkens? |\n";
    md << "|------|----------------|---------------|-------------------|------------------|----------|\n";
    for (const auto& q : modes) {
        md << "| " << q.name
           << " | " << static_cast<int>(q.earlyCentroidHz) << " Hz"
           << " | " << static_cast<int>(q.lateCentroidHz) << " Hz"
           << " | " << std::fixed << std::setprecision(3) << q.earlyHighEnergy
           << " | " << q.lateHighEnergy
           << " | " << (q.lateCentroidHz <= q.earlyCentroidHz && q.lateHighEnergy <= q.earlyHighEnergy ? "Yes" : "No")
           << " |\n";
    }

    md << "\n## Stereo / Mono Safety\n\n";
    md << "| Mode | Early Corr | Late Corr | Mono Peak | Width Proxy |\n";
    md << "|------|------------|-----------|-----------|-------------|\n";
    for (const auto& q : modes) {
        md << "| " << q.name
           << " | " << std::fixed << std::setprecision(3) << q.stereo.corrEarly
           << " | " << q.stereo.corrLate
           << " | " << std::setprecision(3) << q.stereo.monoPeak
           << " | " << std::setprecision(3) << q.stereo.widthProxy
           << " |\n";
    }

    md << "\n## Transient Smear & Early Reflections\n\n";
    md << "| Mode | Bloom (ms) | Onset (ms) | Peak (ms) | PeakCount | ActiveBins | ActiveRatio | Crest | E25% | E50% | E75% | DensityScore |\n";
    md << "|------|------------|------------|-----------|-----------|------------|-------------|-------|------|------|------|--------------|\n";
    for (const auto& q : modes) {
        md << "| " << q.name
           << " | " << std::fixed << std::setprecision(1) << q.transient.bloomTimeMs
           << " | " << std::setprecision(1) << q.transient.earlyOnsetMs
           << " | " << std::setprecision(1) << q.transient.earlyPeakMs
           << " | " << static_cast<int>(q.transient.earlyPeakCount)
           << " | " << static_cast<int>(q.transient.earlyActiveBins1ms)
           << " | " << std::setprecision(2) << q.transient.earlyActiveBinRatio
           << " | " << std::setprecision(1) << q.transient.earlyCrestFactor
           << " | " << std::setprecision(1) << q.transient.earlyEnergyMs25
           << " | " << std::setprecision(1) << q.transient.earlyEnergyMs50
           << " | " << std::setprecision(1) << q.transient.earlyEnergyMs75
           << " | " << std::setprecision(3) << q.transient.earlyDensityScore
           << " |\n";
    }

    md << "\n## Red Flags\n\n";
    bool anyFlags = false;
    for (const auto& q : modes) {
        if (q.hasNaN) { md << "- **" << q.name << "**: NaN detected in output\n"; anyFlags = true; }
        if (q.hasInf) { md << "- **" << q.name << "**: Inf detected in output\n"; anyFlags = true; }
        if (!q.decay.monotonic) { md << "- **" << q.name << "**: Non-monotonic tail decay (max rebound "
            << std::fixed << std::setprecision(1) << q.decay.maxReboundDb << " dB)\n"; anyFlags = true; }
        if (q.spectral.metallicPeakSeverity == "high" && q.spectral.metallicPeakPersistent) {
            md << "- **" << q.name << "**: Persistent " << q.spectral.metallicPeakBand
               << " metallic peak at " << static_cast<int>(q.spectral.metallicPeakHz) << " Hz ("
               << std::setprecision(1) << q.spectral.metallicPeakLocalDb << " dB local) — severity: HIGH\n";
            anyFlags = true;
        }
        if (q.lateCentroidHz > q.earlyCentroidHz * 1.05f && q.lateHighEnergy > q.earlyHighEnergy * 1.05f) {
            md << "- **" << q.name << "**: Tail gets brighter over time; possible HF buildup\n";
            anyFlags = true;
        }
        if (q.stereo.corrLate > 0.95f) { md << "- **" << q.name << "**: Late tail highly correlated ("
            << std::setprecision(3) << q.stereo.corrLate << ") — risk of mono collapse\n"; anyFlags = true; }
        if (q.transient.bloomTimeMs < 0.5f) { md << "- **" << q.name << "**: Very fast bloom ("
            << std::setprecision(1) << q.transient.bloomTimeMs << " ms) — possible harsh attack\n"; anyFlags = true; }
    }
    if (!anyFlags) md << "No significant red flags detected.\n";

    md << "\n## Recommended Next Session Target\n\n";
    md << "Based on this baseline, the most promising quality improvement areas are:\n\n";
    md << "1. **Listening validation with synthetic material**: The reverb has been optimized for "
       << "metrics. Real listening tests with snare, vocal, and bright transient material would "
       << "confirm whether the current Plate brightness and Room density are musically appropriate.\n\n";
    md << "2. **Mode distinctness**: If Room/Hall/Plate show similar decay shapes or early response "
       << "profiles, the mode differentiation may need stronger parameter scaling.\n\n";
    md << "3. **Performance on lower-end hardware**: Current benchmarks are on a 2-core machine. "
       << "Verify real-time budget on target hardware.\n";

    return md.str();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    const float sampleRate = 48000.0f;
    const uint32_t numFrames = 65536; // ~1.36s tail
    const std::string qualityDir = "labs/reverb/quality";

    // Ensure directory exists (simple mkdir on Linux)
    std::system(("mkdir -p " + qualityDir).c_str());

    std::cout << "AestraVerb Quality Measurement Lab\n";
    std::cout << "==================================\n\n";

    std::vector<ModeQuality> modes;
    modes.push_back(measureMode("room", 0.0f, 0.6f, 0.5f, 0.7f, sampleRate, numFrames, qualityDir));
    modes.push_back(measureMode("hall", 0.5f, 0.9f, 0.9f, 0.85f, sampleRate, numFrames, qualityDir));
    modes.push_back(measureMode("plate", 1.0f, 0.7f, 0.6f, 0.8f, sampleRate, numFrames, qualityDir));

    // Write JSON report
    {
        std::ofstream f(qualityDir + "/reverb_quality_baseline.json");
        if (f) f << generateJsonReport(modes, sampleRate);
    }

    // Write Markdown report
    {
        std::ofstream f(qualityDir + "/reverb_quality_baseline.md");
        if (f) f << generateMarkdownReport(modes);
    }

    // Write README
    {
        std::ofstream f(qualityDir + "/README.md");
        if (f) {
            f << "# AestraVerb Quality Lab\n\n";
            f << "This directory contains quality measurement artifacts for AestraVerb.\n\n";
            f << "## Files\n\n";
            f << "- `reverb_quality_baseline.json` — Machine-readable quality metrics\n";
            f << "- `reverb_quality_baseline.md` — Human-readable quality report\n";
            f << "- `impulse_room.wav` — Room mode impulse response\n";
            f << "- `impulse_hall.wav` — Hall mode impulse response\n";
            f << "- `impulse_plate.wav` — Plate mode impulse response\n";
            f << "- `noiseburst_room.wav` — Room mode noise burst response\n";
            f << "- `noiseburst_hall.wav` — Hall mode noise burst response\n";
            f << "- `noiseburst_plate.wav` — Plate mode noise burst response\n";
        }
    }

    std::cout << "\nQuality measurement complete.\n";
    std::cout << "Artifacts written to: " << qualityDir << "/\n";

    // Print summary to console
    std::cout << "\n--- Summary ---\n";
    for (const auto& q : modes) {
        std::cout << q.name << ": T20=" << static_cast<int>(q.decay.t20)
                  << "ms T40=" << static_cast<int>(q.decay.t40)
                  << "ms T60=" << static_cast<int>(q.decay.t60)
                  << "ms Peak=" << std::fixed << std::setprecision(3) << q.peak
                  << " RMS=" << q.rms
                  << " HF=" << std::setprecision(1) << (q.hfRatio * 100.0f) << "%"
                  << " Centroid=" << static_cast<int>(q.earlyCentroidHz) << "->"
                  << static_cast<int>(q.lateCentroidHz) << "Hz"
                  << " Crest=" << q.spectral.crestFactor
                  << " Metallic=" << static_cast<int>(q.spectral.metallicPeakHz) << "Hz/"
                  << q.spectral.metallicPeakBand << "/" << q.spectral.metallicPeakSeverity
                  << " Early=" << std::setprecision(1) << q.transient.earlyOnsetMs
                  << "ms-onset " << std::setprecision(1) << q.transient.earlyPeakMs
                  << "ms-peak " << static_cast<int>(q.transient.earlyPeakCount)
                  << "-peaks " << std::setprecision(2) << q.transient.earlyActiveBinRatio
                  << "-active " << std::setprecision(3) << q.transient.earlyDensityScore
                  << "-score"
                  << "\n";
    }

    return 0;
}
