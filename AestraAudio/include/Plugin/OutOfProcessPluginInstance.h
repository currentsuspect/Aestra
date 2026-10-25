// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include "Plugin/HostTransport.h"

#include "AestraThreading.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <array>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

class PluginHostProcess;
class SharedAudioBlock;
class WorkerWake;

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

    // Parameters (V82-H5). The list and the live values are asked of the helper (IPC), so these belong to the UI side,
    // never the render thread; the render thread hands automation over through applyAutomation().
    std::vector<PluginParameter> getParameters() const override;
    uint32_t getParameterCount() const override { return static_cast<uint32_t>(getParameters().size()); }
    float getParameter(uint32_t id) const override;
    void setParameter(uint32_t id, float value) override;
    std::string getParameterDisplay(uint32_t id) const override;
    bool supportsRealtimeAutomation() const noexcept override;
    void applyAutomation(uint32_t id, float normalizedValue) noexcept override;

    struct ParameterEdit {
        uint32_t id{0};
        float value{0.0f};
    };
    /// Knobs moved in the plugin's own window since the last call (newest value per parameter). UI thread.
    std::vector<ParameterEdit> takeEditedParameters();

    std::vector<uint8_t> saveState() const override;
    bool loadState(const std::vector<uint8_t>& state) override;

    // The plugin's own window (V82-H3). The helper owns it (Linux, VST3): the app only asks for it to open or close and
    // never gets a window handle, so a plugin that hangs or crashes in its UI takes the helper with it, not the app.
    bool hasEditor() const override;
    bool openEditor(void* parentWindow) override;
    void closeEditor() override;
    bool isEditorOpen() const override { return m_editorOpen.load(std::memory_order_acquire); }
    std::pair<int, int> getEditorSize() const override;
    bool resizeEditor(int width, int height) override;

    const PluginInfo& getInfo() const override { return m_info; }
    // One block of transport delay (the sandbox hands audio over through a single-slot double buffer) plus the
    // plugin's own latency, which the helper reports (LATENCY) once the plugin is set up and active. PDC reads this.
    uint32_t getLatencySamples() const override {
        return m_maxBlockSize + m_pluginLatency.load(std::memory_order_acquire);
    }
    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override;
    void resetWatchdog() override;
    bool isBypassedByWatchdog() const override;
    bool isCrashed() const override { return m_crashed.load(std::memory_order_acquire); }

    // Number of parameter changes dropped because the queue was full. Non-zero
    // means setParameter outran the worker's drain — surfaced for diagnostics and
    // tests since setParameter itself is void (#238).
    uint64_t parameterDropCount() const { return m_paramDrops.load(std::memory_order_relaxed); }

    // Blocks the helper answered, and blocks played dry because its answer was not
    // back in time. The second is audible (the plugin drops out for a block), so
    // the compatibility run reports it per plugin (V82-H7).
    uint64_t helperBlockCount() const { return m_helperBlocks.load(std::memory_order_relaxed); }
    uint64_t dryBlockCount() const { return m_dryBlocks.load(std::memory_order_relaxed); }

    // Blocks whose input and MIDI never reached the helper because the previous
    // block was still with it. A note-on in such a block is lost (V82-H7).
    uint64_t unsentBlockCount() const { return m_unsentBlocks.load(std::memory_order_relaxed); }
    // True when the block's audio travels through shared memory rather than hex over the pipe (V82-H7 follow-up). Set up by
    // initialize(); false means the helper could not map the region and the slower path is in use.
    bool usesSharedAudio() const { return m_shm != nullptr; }

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
                              const std::vector<uint8_t>& midiData, size_t midiBytes, const HostTransport& transport,
                              std::vector<float>& output);
    void markCrashed();
    void setupSharedBlock();     ///< audio through shared memory when the helper can map it (V82-H7 follow-up)
    static void raiseWorkerPriority();
    void refreshPluginLatency(); ///< asks the helper for the plugin's own latency
    void passThrough(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                     uint32_t numFrames) const;

    PluginInfo m_info;
    std::string m_hostExecutablePath;
    std::unique_ptr<PluginHostProcess> m_process;
    std::unique_ptr<SharedAudioBlock> m_shm; ///< null: the block travels hex-encoded over the pipe
    std::unique_ptr<WorkerWake> m_wake;
    mutable std::mutex m_ipcMutex;
    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_crashed{false};
    std::atomic<uint64_t> m_watchdogMaxExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogAvgExecutionTimeNs{0};
    std::atomic<uint64_t> m_watchdogViolationCount{0};
    std::atomic<bool> m_watchdogBypassed{false};
    std::atomic<bool> m_editorOpen{false};
    std::atomic<int> m_editorWidth{0};
    std::atomic<int> m_editorHeight{0};
    std::atomic<uint32_t> m_pluginLatency{0}; ///< the plugin's own latency in samples, from the helper

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
    // The block's tempo and playhead, handed over with its input under the same pending-slot protocol.
    HostTransport m_pendingTransport;
    HostTransport m_workerTransport;
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
    // Automation (V82-H5) has its own single producer, the render thread, so it gets its own ring; the worker drains
    // both. A curve hands over its value every block, so only a value that changed is queued (recent[] remembers the
    // last one sent per parameter; a miss in it just sends once more).
    LockFreeRingBuffer<ParamChange, 1024> m_autoQueue;
    struct RecentAutomation {
        uint32_t id{0};
        float value{0.0f};
        bool valid{false};
    };
    std::array<RecentAutomation, 16> m_recentAutomation{};
    size_t m_recentAutomationNext = 0;
    mutable std::mutex m_paramListMutex;
    mutable std::vector<PluginParameter> m_paramList;
    mutable bool m_paramListValid = false;
    std::atomic<uint64_t> m_paramDrops{0};
    std::atomic<uint64_t> m_helperBlocks{0};
    std::atomic<uint64_t> m_dryBlocks{0};
    std::atomic<uint64_t> m_unsentBlocks{0};

    void drainParamQueueToChild();

    // How often the worker re-reads the plugin's own latency from the helper, in blocks: about 0.7 s at 512 frames,
    // 48 kHz. One tiny command; the answer lands in m_pluginLatency, which EffectChain::refreshLatency() watches.
    static constexpr uint64_t kLatencyCheckEveryBlocks = 64;
};

} // namespace Audio
} // namespace Aestra
