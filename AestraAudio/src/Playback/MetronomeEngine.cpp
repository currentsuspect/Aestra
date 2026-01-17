// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MetronomeEngine.h"
#include "../../AestraCore/include/AestraLog.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Aestra {
namespace Audio {

MetronomeEngine::MetronomeEngine() {
    generateDefaultSounds();
}

void MetronomeEngine::setBeatsPerBar(int beats) {
    m_beatsPerBar.store(beats, std::memory_order_relaxed);
    // Reset beat counter on signature change
    m_currentBeat = 0;
}

void MetronomeEngine::generateDefaultSounds() {
    const size_t sampleRate = 48000;
    const size_t durationSamples = static_cast<size_t>(0.10 * sampleRate); // 100ms

    m_synthClickLow.resize(durationSamples);
    m_synthClickHigh.resize(durationSamples);

    const double freqLow = 800.0;
    const double freqHigh = 1600.0;
    const double kPi = 3.14159265358979323846;

    for (size_t i = 0; i < durationSamples; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double env = std::pow(1.0 - static_cast<double>(i)/durationSamples, 4.0);
        m_synthClickLow[i] = static_cast<float>(std::sin(2.0 * kPi * freqLow * t) * env);
        m_synthClickHigh[i] = static_cast<float>(std::sin(2.0 * kPi * freqHigh * t) * env);
    }
}

void MetronomeEngine::loadClickSounds(const std::string& downbeatPath, const std::string& upbeatPath) {
    auto loadWav = [](const std::string& wavPath, std::vector<float>& samples) -> bool {
        FILE* file = fopen(wavPath.c_str(), "rb");
        if (!file) return false;

        char riff[4];
        if (fread(riff, 1, 4, file) != 4 || memcmp(riff, "RIFF", 4) != 0) {
            fclose(file); return false;
        }

        fseek(file, 4, SEEK_CUR); // Skip size

        char wave[4];
        if (fread(wave, 1, 4, file) != 4 || memcmp(wave, "WAVE", 4) != 0) {
            fclose(file); return false;
        }

        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;

        while (true) {
            char chunkId[4];
            uint32_t chunkLen;
            if (fread(chunkId, 1, 4, file) != 4) break;
            if (fread(&chunkLen, 4, 1, file) != 1) break;

            if (memcmp(chunkId, "fmt ", 4) == 0) {
                fread(&audioFormat, 2, 1, file);
                fread(&numChannels, 2, 1, file);
                fread(&sampleRate, 4, 1, file);
                fseek(file, 6, SEEK_CUR);
                fread(&bitsPerSample, 2, 1, file);
                if (chunkLen > 16) fseek(file, chunkLen - 16, SEEK_CUR);
            } else if (memcmp(chunkId, "data", 4) == 0) {
                if (audioFormat != 1 || (bitsPerSample != 16 && bitsPerSample != 24)) {
                    fclose(file); return false;
                }

                const uint32_t bytesPerSample = bitsPerSample / 8;
                const uint32_t numSamples = chunkLen / (numChannels * bytesPerSample);
                samples.resize(numSamples);

                if (bitsPerSample == 16) {
                    std::vector<int16_t> rawData(numSamples * numChannels);
                    fread(rawData.data(), 2, numSamples * numChannels, file);
                    for (uint32_t i = 0; i < numSamples; ++i) {
                        float sample = 0.0f;
                        for (uint16_t ch = 0; ch < numChannels; ++ch) {
                            sample += static_cast<float>(rawData[i * numChannels + ch]) / 32768.0f;
                        }
                        samples[i] = sample / static_cast<float>(numChannels);
                    }
                } else if (bitsPerSample == 24) {
                    std::vector<uint8_t> rawData(numSamples * numChannels * 3);
                    fread(rawData.data(), 1, numSamples * numChannels * 3, file);
                    for (uint32_t i = 0; i < numSamples; ++i) {
                        float sample = 0.0f;
                        for (uint16_t ch = 0; ch < numChannels; ++ch) {
                            size_t byteIdx = (i * numChannels + ch) * 3;
                            int32_t val = rawData[byteIdx] | (rawData[byteIdx + 1] << 8) | (rawData[byteIdx + 2] << 16);
                            if (val & 0x800000) val |= 0xFF000000;
                            sample += static_cast<float>(val) / 8388608.0f;
                        }
                        samples[i] = sample / static_cast<float>(numChannels);
                    }
                }
                fclose(file);
                return true;
            } else {
                fseek(file, chunkLen, SEEK_CUR);
            }
        }
        fclose(file);
        return false;
    };

    loadWav(downbeatPath, m_clickSamplesDown);
    loadWav(upbeatPath, m_clickSamplesUp);

    if (m_clickSamplesUp.empty() && !m_clickSamplesDown.empty()) m_clickSamplesUp = m_clickSamplesDown;
    if (m_clickSamplesDown.empty() && !m_clickSamplesUp.empty()) m_clickSamplesDown = m_clickSamplesUp;

    m_activeClickSamples = &m_clickSamplesDown;
}

void MetronomeEngine::reset(uint64_t globalSamplePos, uint32_t sampleRate) {
    if (sampleRate == 0) return;

    float bpm = m_bpm.load(std::memory_order_relaxed);
    int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);

    double samplesPerBeat = (static_cast<double>(sampleRate) * 60.0) / static_cast<double>(bpm);
    uint64_t samplesPerBeatInt = static_cast<uint64_t>(samplesPerBeat);

    if (samplesPerBeatInt > 0) {
        m_nextBeatSample = (globalSamplePos / samplesPerBeatInt) * samplesPerBeatInt;
        m_currentBeat = static_cast<int>((globalSamplePos / samplesPerBeatInt) % beatsPerBar);
        m_clickPlaying = false;
        m_clickPlayhead = 0;
    }
}

void MetronomeEngine::process(float* outputBuffer,
                              uint32_t numFrames,
                              uint32_t numChannels,
                              uint64_t globalSamplePos,
                              uint32_t sampleRate,
                              bool transportPlaying) {

    if (!m_enabled.load(std::memory_order_relaxed) || !transportPlaying) return;
    if (numFrames == 0 || sampleRate == 0) return;

    const float bpm = m_bpm.load(std::memory_order_relaxed);
    const float clickVol = m_volume.load(std::memory_order_relaxed);
    const int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);

    const uint64_t samplesPerBeat = static_cast<uint64_t>(
        (static_cast<double>(sampleRate) * 60.0) / static_cast<double>(bpm));

    // Reset tracking on jumps
    uint64_t prevBeatSample = (m_nextBeatSample >= samplesPerBeat) ? m_nextBeatSample - samplesPerBeat : 0;

    if (globalSamplePos < prevBeatSample) {
        reset(globalSamplePos, sampleRate);
    }

    // Init first beat
    if (m_nextBeatSample == 0 && globalSamplePos == 0) {
        m_nextBeatSample = 0;
        m_currentBeat = 0;
    }

    const uint64_t blockStart = globalSamplePos;
    const uint64_t blockEnd = blockStart + numFrames;

    bool newBeatTriggers = false;
    uint32_t triggerOffset = numFrames;

    while (m_nextBeatSample < blockEnd && samplesPerBeat > 0) {
        if (m_nextBeatSample >= blockStart) {
            newBeatTriggers = true;
            triggerOffset = static_cast<uint32_t>(m_nextBeatSample - blockStart);
            break;
        }
        m_nextBeatSample += samplesPerBeat;
    }

    // Mix TAIL
    if (m_clickPlaying && m_activeClickSamples && !m_activeClickSamples->empty()) {
         size_t clickLen = m_activeClickSamples->size();
         uint32_t tailFrames = std::min(numFrames, triggerOffset);

         for (uint32_t i = 0; i < tailFrames && m_clickPlayhead < clickLen; ++i) {
             float sample = (*m_activeClickSamples)[m_clickPlayhead] * clickVol * m_currentClickGain;
             // Mix into all channels
             for (uint32_t ch=0; ch < numChannels; ++ch) {
                 outputBuffer[i * numChannels + ch] += sample;
             }
             ++m_clickPlayhead;
         }

         if (m_clickPlayhead >= clickLen) {
             m_clickPlaying = false;
         }
    }

    // Start NEW Click
    if (newBeatTriggers) {
         m_clickPlaying = true;
         m_clickPlayhead = 0;

         // Select sample based on beat (Downbeat vs Upbeat)
         // Note: m_currentBeat will be incremented AFTER we select the sample?
         // AudioEngine.cpp: "m_activeClickSamples = (m_currentBeat == 0) ? &m_synthClickLow : &m_synthClickHigh;"
         // Then "m_currentBeat = (m_currentBeat + 1) % beatsPerBar;"

         // If custom samples loaded
         if (!m_clickSamplesDown.empty()) {
             m_activeClickSamples = (m_currentBeat == 0) ? &m_clickSamplesDown : &m_clickSamplesUp;
         } else {
             m_activeClickSamples = (m_currentBeat == 0) ? &m_synthClickLow : &m_synthClickHigh;
         }

         m_currentClickGain = 1.0f;

         // Advance Beat Counter
         m_currentBeat = (m_currentBeat + 1) % beatsPerBar;
         m_nextBeatSample += samplesPerBeat;

         // Mix New Click
         if (m_activeClickSamples && !m_activeClickSamples->empty()) {
             size_t clickLen = m_activeClickSamples->size();
             uint32_t remainingFrames = numFrames - triggerOffset;
             uint32_t framesToMix = std::min(remainingFrames, (uint32_t)clickLen);

             for (uint32_t i = 0; i < framesToMix; ++i) {
                 float sample = (*m_activeClickSamples)[m_clickPlayhead] * clickVol * m_currentClickGain;
                 uint32_t dstIdx = triggerOffset + i;
                 for (uint32_t ch=0; ch < numChannels; ++ch) {
                     outputBuffer[dstIdx * numChannels + ch] += sample;
                 }
                 ++m_clickPlayhead;
             }
         }
    }
}

} // namespace Audio
} // namespace Aestra
