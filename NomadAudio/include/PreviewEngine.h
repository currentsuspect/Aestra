// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "SamplePool.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace Nomad {
namespace Audio {

enum class PreviewResult {
    Success,
    Failed
};

class PreviewEngine {
public:
    PreviewEngine();
    ~PreviewEngine();

    PreviewResult play(const std::string& path, float gainDb = -6.0f, double maxSeconds = 5.0);
    void stop();
    void setOutputSampleRate(double sr);
    void process(float* interleavedOutput, uint32_t numFrames);
    bool isPlaying() const;
    void setOnComplete(std::function<void(const std::string& path)> callback);
    void setGlobalPreviewVolume(float gainDb);
    float getGlobalPreviewVolume() const;

private:
    struct PreviewVoice {
        std::shared_ptr<AudioBuffer> buffer;
        std::string path;
        double phaseFrames{0.0};
        double sampleRate{48000.0};
        uint32_t channels{2};
        float gain{0.5f};
        double durationSeconds{0.0};
        double maxPlaySeconds{0.0};
        double elapsedSeconds{0.0};
        double fadeInPos{0.0};
        double fadeOutPos{0.0};
        std::atomic<bool> stopRequested{false};
        bool fadeOutActive{false};
        std::atomic<bool> playing{false};
    };

    std::shared_ptr<AudioBuffer> loadBuffer(const std::string& path, uint32_t& sampleRate, uint32_t& channels);
    void downmixToStereo(std::vector<float>& data, uint32_t inChannels);
    float dbToLinear(float db) const;

    // Use shared_ptr with a mutex guard instead of atomic (std::atomic does not support non-trivial types)
    mutable std::mutex m_voiceMutex;
    std::shared_ptr<PreviewVoice> m_activeVoice;
    std::atomic<double> m_outputSampleRate;
    std::atomic<float> m_globalGainDb;
    std::function<void(const std::string&)> m_onComplete;
};

} // namespace Audio
} // namespace Nomad
