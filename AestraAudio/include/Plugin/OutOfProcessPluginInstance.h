// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

class PluginHostProcess;

class OutOfProcessPluginInstance : public IPluginInstance {
public:
    OutOfProcessPluginInstance(PluginInfo info, std::string hostExecutablePath);
    ~OutOfProcessPluginInstance() override;

    bool load();

    bool initialize(double sampleRate, uint32_t maxBlockSize) override;
    void shutdown() override;
    void activate() override;
    void deactivate() override;
    bool isActive() const override { return m_active.load(std::memory_order_acquire); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override;

    std::vector<PluginParameter> getParameters() const override { return {}; }
    uint32_t getParameterCount() const override { return 0; }
    float getParameter(uint32_t id) const override;
    void setParameter(uint32_t id, float value) override;
    std::string getParameterDisplay(uint32_t id) const override;

    std::vector<uint8_t> saveState() const override;
    bool loadState(const std::vector<uint8_t>& state) override;

    bool hasEditor() const override { return false; }
    bool openEditor(void* parentWindow) override;
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int width, int height) override;

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return m_maxBlockSize; }
    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override;
    void resetWatchdog() override;
    bool isBypassedByWatchdog() const override;
    bool isCrashed() const override { return m_crashed.load(std::memory_order_acquire); }

private:
    bool sendCommand(const std::string& command, std::string* response = nullptr,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    void startWorker();
    void stopWorker();
    void workerLoop();
    bool processBlockInHelper(const std::vector<float>& input, uint32_t channels, uint32_t frames,
                              const std::vector<uint8_t>& midiData, size_t midiBytes, std::vector<float>& output);
    void markCrashed();
    void passThrough(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                     uint32_t numFrames) const;

    PluginInfo m_info;
    std::string m_hostExecutablePath;
    std::unique_ptr<PluginHostProcess> m_process;
    mutable std::mutex m_ipcMutex;
    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_crashed{false};
    std::atomic<uint64_t> m_watchdogMaxExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogAvgExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogViolationCount{0};
    std::atomic<bool> m_watchdogBypassed{false};

    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 0;
    uint32_t m_transportChannels = 2;

    std::thread m_workerThread;
    std::atomic<bool> m_workerStop{false};
    std::atomic<bool> m_workerRunning{false};

    std::vector<float> m_pendingInput;
    std::vector<float> m_workerInput;
    std::vector<float> m_workerOutput;
    std::vector<float> m_readyOutput;
    std::vector<uint8_t> m_pendingMidiData;
    std::vector<uint8_t> m_workerMidiData;
    std::atomic<size_t> m_pendingMidiBytes{0};
    std::atomic<uint32_t> m_pendingFrames{0};
    std::atomic<uint32_t> m_readyFrames{0};
    std::atomic<uint8_t> m_pendingState{0};
    std::atomic<uint8_t> m_readyState{0};
};

} // namespace Audio
} // namespace Aestra
