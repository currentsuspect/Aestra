// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include "AestraThreading.h"

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

    // Number of parameter changes dropped because the queue was full. Non-zero
    // means setParameter outran the worker's drain — surfaced for diagnostics and
    // tests since setParameter itself is void (#238).
    uint64_t parameterDropCount() const { return m_paramDrops.load(std::memory_order_relaxed); }

#ifdef AESTRA_ENABLE_TEST_HOOKS
    // Send a raw command to the helper and return its reply. Test-only: used by the
    // #244 CLAP-note e2e test to read the fake plugin's recorded events (TESTNOTES).
    std::string sendRawCommandForTest(const std::string& command) {
        std::string response;
        sendCommand(command, &response, std::chrono::seconds(2));
        return response;
    }

    /**
     * @brief Blocks the worker has finished, monotonically increasing.
     *
     * The audio path here is a SINGLE-SLOT double buffer, not a queue: every
     * process() overwrites the pending slot, so a block that has not been picked
     * up yet is simply lost. That makes "has the worker caught up?" unobservable
     * from outside, and tests were left sleeping a fixed 20ms and hoping — a race
     * that failed on a loaded macOS runner (#622) and could not be fixed by
     * polling, because polling means calling process(), which destroys the very
     * block being waited for.
     *
     * This counter makes the completion an event a test can wait on. It is
     * incremented after the worker publishes a result, so observing it advance
     * means that result is readable on the next process() call.
     */
    uint64_t processedBlockCountForTest() const {
        return m_processedBlocks.load(std::memory_order_acquire);
    }
#endif

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
    // Counts blocks the worker has published. Always maintained, not just under
    // test hooks: a counter that only exists in test builds would be measuring a
    // different binary from the one that ships.
    std::atomic<uint64_t> m_processedBlocks{0};
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

    // Host->child parameter changes. setParameter() only pushes onto this
    // lock-free queue (no IPC, no lock, no allocation, never blocks); the worker
    // thread (single consumer) drains it and forwards each change to the child as
    // an ordered SETPARAM command, keeping the realtime callback free of parameter
    // IPC (#238). The ring is SPSC: setParameter must be called from one producer
    // thread. Today that is the UI/message thread — automation gates plugin-
    // parameter curves to Internal-format plugins in AudioEngine, so the render
    // thread never reaches OOP setParameter. Because the push itself never blocks,
    // a future single RT producer (e.g. third-party automation, #467) would remain
    // realtime-safe; adding a *second* concurrent producer would require an MPSC
    // queue instead.
    struct ParamChange {
        uint32_t id{0};
        float value{0.0f};
    };
    // Sized to comfortably absorb a full-preset burst (hundreds of distinct
    // parameters applied in a tight loop) before the worker drains it.
    static constexpr size_t kParamQueueCapacity = 4096;
    // Cap per worker pass so a large burst against a slow child cannot monopolize
    // the worker and starve PROCESS (which would make the RT callback fall back to
    // audible passThrough). The remainder stays queued for the next pass.
    static constexpr size_t kMaxParamDrainPerPass = 64;
    LockFreeRingBuffer<ParamChange, kParamQueueCapacity> m_paramQueue;
    std::atomic<uint64_t> m_paramDrops{0};

    void drainParamQueueToChild();
};

} // namespace Audio
} // namespace Aestra
