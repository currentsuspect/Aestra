// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/VST3Host.h"

#include "AestraLog.h"
#include "Plugin/EditorLoopPump.h"
#include "Plugin/Vst3ParamBridge.h"

#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#include <objbase.h>
#endif

// VST3 SDK includes
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <atomic>
#include <codecvt>
#include <cstring>
#include <locale>
#include <mutex>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Aestra {
namespace Audio {

struct VST3PluginInstance::ModuleHolder {
    explicit ModuleHolder(VST3::Hosting::Module::Ptr moduleIn) : module(std::move(moduleIn)) {}

    VST3::Hosting::Module::Ptr module;
};

#if defined(__linux__)
namespace {

// The host's side of a plugin editor on Linux (V82-H3). One object, owned by the instance, answers both questions the
// editor asks: IPlugFrame ("make my window this big") and Linux::IRunLoop ("call me when this descriptor is readable /
// every so many milliseconds"). The run loop only records the requests in the EditorLoopPump; the GUI thread that owns
// the window does the polling. Handlers are held by IPtr inside the pump's callbacks, so one a plugin unregisters from
// inside another handler is never called after it is gone.
class X11PlugFrame final : public IPlugFrame, public Linux::IRunLoop {
public:
    X11PlugFrame(Aestra::Audio::EditorLoopPump& pump, std::function<void(int, int)> requestResize)
        : m_pump(pump), m_requestResize(std::move(requestResize)) {}
    ~X11PlugFrame() { clearRegistrations(); }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) || FUnknownPrivate::iidEqual(iid, IPlugFrame::iid)) {
            *obj = static_cast<IPlugFrame*>(this);
            addRef();
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = static_cast<Linux::IRunLoop*>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_refs; }
    uint32 PLUGIN_API release() override {
        const uint32 left = --m_refs;
        if (left == 0) delete this;
        return left;
    }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize || m_resizing) return kInvalidArgument;
        m_resizing = true; // onSize() can make some plugins ask again: answer once
        if (m_requestResize) m_requestResize(newSize->getWidth(), newSize->getHeight());
        view->onSize(newSize); // the host window now has this size
        m_resizing = false;
        return kResultOk;
    }

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override {
        if (!handler) return kInvalidArgument;
        IPtr<Linux::IEventHandler> keep(handler); // addRefs
        const int token = m_pump.addFd(fd, [keep, fd]() { keep->onFDIsSet(fd); });
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fdTokens.push_back({handler, token});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_fdTokens.begin(); it != m_fdTokens.end();) {
            if (it->handler == handler) {
                m_pump.removeFd(it->token);
                it = m_fdTokens.erase(it);
            } else {
                ++it;
            }
        }
        return kResultOk;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) override {
        if (!handler) return kInvalidArgument;
        IPtr<Linux::ITimerHandler> keep(handler);
        const int token = m_pump.addTimer(std::chrono::milliseconds(static_cast<long long>(milliseconds)),
                                          [keep]() { keep->onTimer(); });
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timerTokens.push_back({handler, token});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_timerTokens.begin(); it != m_timerTokens.end();) {
            if (it->handler == handler) {
                m_pump.removeTimer(it->token);
                it = m_timerTokens.erase(it);
            } else {
                ++it;
            }
        }
        return kResultOk;
    }

    /// Drop everything the plugin registered (it should have, in removed(); this makes a careless one harmless).
    void clearRegistrations() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_fdTokens) m_pump.removeFd(entry.token);
        for (const auto& entry : m_timerTokens) m_pump.removeTimer(entry.token);
        m_fdTokens.clear();
        m_timerTokens.clear();
    }

private:
    struct FdToken {
        Linux::IEventHandler* handler;
        int token;
    };
    struct TimerToken {
        Linux::ITimerHandler* handler;
        int token;
    };
    Aestra::Audio::EditorLoopPump& m_pump;
    std::function<void(int, int)> m_requestResize;
    std::mutex m_mutex;
    std::vector<FdToken> m_fdTokens;
    std::vector<TimerToken> m_timerTokens;
    std::atomic<uint32> m_refs{1};
    bool m_resizing = false;
};

} // namespace
#endif // __linux__

// Helper for UTF-16 (VST3) to UTF-8 (Aestra) conversion
static std::string utf16_to_utf8(const Steinberg::Vst::TChar* str) {
    if (!str)
        return "";
    try {
        // VST3 strings are always UTF-16, regardless of platform wchar_t size
        std::u16string u16str(reinterpret_cast<const char16_t*>(str));
// Suppress C++17 deprecation warning for codecvt
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
        std::string result = convert.to_bytes(u16str);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        return result;
    } catch (const std::range_error&) {
        return "";
    }
}

// Helper function to isolate SEH from C++ object unwinding (C2712 fix)
// This function must NOT contain any objects with destructors.
static bool SafeProcessCall(IAudioProcessor* processor, ProcessData& data) {
#ifdef _WIN32
    __try {
        processor->process(data);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false; // Crashed
    }
#else
    // POSIX signal recovery is process-global and not realtime-safe. Linux/macOS
    // crash containment is provided by OutOfProcessPluginInstance instead.
    processor->process(data);
    return true;
#endif
}

static uint32_t countAudioBusChannels(IComponent* component, BusDirection direction) {
    if (!component) {
        return 0;
    }

    uint32_t totalChannels = 0;
    const int32_t busCount = component->getBusCount(kAudio, direction);
    for (int32_t busIndex = 0; busIndex < busCount; ++busIndex) {
        BusInfo busInfo{};
        if (component->getBusInfo(kAudio, direction, busIndex, busInfo) == kResultOk && busInfo.channelCount > 0) {
            totalChannels += static_cast<uint32_t>(busInfo.channelCount);
        }
    }
    return totalChannels;
}

// ============================================================================
// VST3PluginInstance Implementation
// ============================================================================

/// What one VST3 instance needs to move parameter values between host, controller and processor.
struct Vst3ParamState {
    ParamBridge toProcessor;     ///< values the processor must hear (host sets, window edits)
    ParamBridge toController;    ///< values the processor reported, for the controller to show
    HostParameterChanges input;  ///< this block's IParameterChanges, rebuilt every block
    OutputParameterChanges output;
    std::mutex listenerMutex;
    std::function<void(uint32_t, double)> listener;
    std::unique_ptr<HostComponentHandler> handler;
    Vst3ParamState()
        : handler(std::make_unique<HostComponentHandler>(toProcessor, [this](uint32_t id, double v) {
              std::function<void(uint32_t, double)> call;
              {
                  std::lock_guard<std::mutex> lock(listenerMutex);
                  call = listener;
              }
              if (call) call(id, v);
          })) {}
};

VST3PluginInstance::VST3PluginInstance() : m_params(std::make_unique<Vst3ParamState>()) {}

VST3PluginInstance::~VST3PluginInstance() {
    unload();
}

bool VST3PluginInstance::load(const std::filesystem::path& path, int classIndex) {
    if (m_loaded) {
        unload();
    }

    std::string errorMsg;

    try {
        // Load the VST3 module
        auto module = VST3::Hosting::Module::create(path.string(), errorMsg);
        if (!module) {
            Log::error("VST3: Failed to load module: " + path.string() + " - " + errorMsg);
            return false;
        }

        m_module = std::make_unique<ModuleHolder>(std::move(module));
        auto& mod = m_module->module;
        m_hostApplication = new HostApplication();
        auto hostApplication = static_cast<HostApplication*>(m_hostApplication);

        // Get the factory
        auto factory = mod->getFactory();
        if (!factory.get()) {
            Log::error("VST3: No factory in module: " + path.string());
            unload();
            return false;
        }

        // Get class info
        auto classInfos = factory.classInfos();
        if (classInfos.empty()) {
            Log::error("VST3: No classes in module: " + path.string());
            unload();
            return false;
        }

        if (classIndex < 0 || classIndex >= static_cast<int>(classInfos.size())) {
            classIndex = 0;
        }

        const auto& classInfo = classInfos[classIndex];

        // Store plugin info
        m_info.id = classInfo.ID().toString();
        m_info.name = classInfo.name();
        m_info.vendor = classInfo.vendor();
        m_info.version = classInfo.version();
        m_info.category = classInfo.subCategoriesString();
        m_info.format = PluginFormat::VST3;
        m_info.path = path;

        // Determine plugin type from category
        std::string cat = classInfo.subCategoriesString();
        if (cat.find("Instrument") != std::string::npos || cat.find("Synth") != std::string::npos) {
            m_info.type = PluginType::Instrument;
            m_info.hasMidiInput = true;
        } else if (cat.find("Analyzer") != std::string::npos) {
            m_info.type = PluginType::Analyzer;
        } else {
            m_info.type = PluginType::Effect;
        }

        // Create the component
        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            Log::error("VST3: Failed to create component: " + m_info.name);
            unload();
            return false;
        }

        m_component = component.take();

        // Initialize component
        auto comp = static_cast<IComponent*>(m_component);
        if (comp->initialize(hostApplication) != kResultOk) {
            Log::error("VST3: Failed to initialize component: " + m_info.name);
            unload();
            return false;
        }

        // Query for audio processor
        auto processor = FUnknownPtr<IAudioProcessor>(comp);
        if (processor) {
            m_processor = processor.take();
        }

        // Query or create edit controller
        auto controller = FUnknownPtr<IEditController>(comp);
        if (controller) {
            m_controller = controller.take();
        } else {
            // Check for separate controller
            TUID controllerCID;
            if (comp->getControllerClassId(controllerCID) == kResultOk) {
                auto editController = factory.createInstance<IEditController>(controllerCID);
                if (editController) {
                    m_controller = editController.take();
                    auto ctrl = static_cast<IEditController*>(m_controller);
                    ctrl->initialize(hostApplication);
                    // A split plugin's two halves find each other through IConnectionPoint; many (clap-wrapper ones,
                    // like Surge XT) cannot build an editor until they have been introduced.
                    auto compPoint = FUnknownPtr<Vst::IConnectionPoint>(comp);
                    auto ctrlPoint = FUnknownPtr<Vst::IConnectionPoint>(ctrl);
                    if (compPoint && ctrlPoint) {
                        compPoint->connect(ctrlPoint);
                        ctrlPoint->connect(compPoint);
                    }
                }
            }
        }

        // The controller's host: edits made in the plugin's window come back through this and reach the processor.
        if (m_controller) static_cast<IEditController*>(m_controller)->setComponentHandler(m_params->handler.get());

        // Get I/O configuration from the real component buses.
        const uint32_t discoveredInputs = countAudioBusChannels(comp, kInput);
        const uint32_t discoveredOutputs = countAudioBusChannels(comp, kOutput);
        m_info.numAudioInputs = discoveredInputs > 0 ? discoveredInputs : 2;
        m_info.numAudioOutputs = discoveredOutputs > 0 ? discoveredOutputs : 2;

        // Do not instantiate the editor during plugin load. Some Linux VST3s
        // allocate UI/toolkit state or crash when createView() is called from a
        // headless scan/load path. openEditor()/getEditorSize() create the view
        // only when the UI explicitly requests it.
        m_info.hasEditor = (m_controller != nullptr);

        m_loaded = true;
        Log::info("VST3: Loaded plugin: " + m_info.name + " (" + m_info.vendor + ")");

        return true;

    } catch (const std::exception& e) {
        Log::error("VST3: Exception loading " + path.string() + ": " + e.what());
        unload();
        return false;
    }
}

void VST3PluginInstance::unload() {
    if (m_editorOpen) {
        closeEditor();
    }

    if (m_active) {
        deactivate();
    }

    // Release VST3 objects in reverse order
    if (m_plugView) {
        // View should already be removed
        m_plugView = nullptr;
    }

    if (m_controller && m_controller != m_component) {
        auto ctrl = static_cast<IEditController*>(m_controller);
        ctrl->terminate();
        ctrl->release();
    }
    m_controller = nullptr;

    if (m_processor && m_processor != m_component) {
        static_cast<IAudioProcessor*>(m_processor)->release();
    }
    m_processor = nullptr;

    if (m_component) {
        auto comp = static_cast<IComponent*>(m_component);
        comp->terminate();
        comp->release();
        m_component = nullptr;
    }

    m_module.reset();

    if (m_hostApplication) {
        delete static_cast<HostApplication*>(m_hostApplication);
        m_hostApplication = nullptr;
    }

    m_loaded = false;
    m_parameterCacheValid = false;
}

bool VST3PluginInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    if (!m_loaded || !m_processor) {
        return false;
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    auto processor = static_cast<IAudioProcessor*>(m_processor);

    // Setup processing
    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;

    if (processor->setupProcessing(setup) != kResultOk) {
        Log::error("VST3: setupProcessing failed for " + m_info.name);
        return false;
    }

    // The host turns on the buses it feeds (VST3: activateBus before setActive).
    // Without the event bus a synth never hears a note: Surge XT and Dexed loaded
    // and played silence (V82-H7). The main audio buses are usually on by default,
    // which is why effects worked.
    if (auto* comp = static_cast<IComponent*>(m_component)) {
        m_audioInputBuses = comp->getBusCount(kAudio, kInput) > 0 ? 1 : 0;
        m_audioOutputBuses = comp->getBusCount(kAudio, kOutput) > 0 ? 1 : 0;
        if (m_audioInputBuses) comp->activateBus(kAudio, kInput, 0, true);
        if (m_audioOutputBuses) comp->activateBus(kAudio, kOutput, 0, true);
        if (comp->getBusCount(kEvent, kInput) > 0) comp->activateBus(kEvent, kInput, 0, true);
    }

    // Allocate temp buffers
    m_tempBuffer.resize(maxBlockSize * 4); // Extra space for stereo I/O

    return true;
}

void VST3PluginInstance::shutdown() {
    if (m_active) {
        deactivate();
    }
    m_tempBuffer.clear();
    m_inputBuffers.clear();
    m_outputBuffers.clear();
}

void VST3PluginInstance::activate() {
    if (!m_loaded || m_active)
        return;

    auto comp = static_cast<IComponent*>(m_component);
    if (comp->setActive(true) == kResultOk) {
        m_active = true;
        // VST3 asks for setProcessing(true) once active, before the first process call.
        if (auto* processor = static_cast<IAudioProcessor*>(m_processor)) processor->setProcessing(true);
    }
}

void VST3PluginInstance::deactivate() {
    if (!m_loaded || !m_active)
        return;

    if (auto* processor = static_cast<IAudioProcessor*>(m_processor)) processor->setProcessing(false);
    auto comp = static_cast<IComponent*>(m_component);
    comp->setActive(false);
    m_active = false;
}

void VST3PluginInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* midiInput,
                                 MidiBuffer* midiOutput) {
    if (!m_active || !m_processor) {
        // Pass-through or silence
        return;
    }

    // Watchdog Check
    if (m_watchdogBypassed.load(std::memory_order_acquire) || m_crashed.load(std::memory_order_acquire)) {
        // Clear outputs to silence
        for (uint32_t i = 0; i < numOutputChannels; ++i) {
            if (outputs[i]) {
                std::memset(outputs[i], 0, numFrames * sizeof(float));
            }
        }
        return;
    }

    auto processor = static_cast<IAudioProcessor*>(m_processor);

    // Setup audio buses
    AudioBusBuffers inputBus;
    inputBus.numChannels = numInputChannels;
    inputBus.silenceFlags = 0;
    inputBus.channelBuffers32 = const_cast<float**>(inputs);

    AudioBusBuffers outputBus;
    outputBus.numChannels = numOutputChannels;
    outputBus.silenceFlags = 0;
    outputBus.channelBuffers32 = outputs;

    // Process data
    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = numFrames;
    data.numInputs = m_audioInputBuses;
    data.numOutputs = m_audioOutputBuses;
    data.inputs = &inputBus;
    data.outputs = &outputBus;
    // Parameter values for the processor (host sets and edits from the plugin's window), and its answers back.
    ParamChange pending[kMaxParamChangesPerBlock];
    m_params->input.fill(pending, m_params->toProcessor.drain(pending, kMaxParamChangesPerBlock));
    m_params->output.clear();
    data.inputParameterChanges = &m_params->input;
    data.outputParameterChanges = &m_params->output;
    data.processContext = nullptr;

    // MIDI input events
    struct EventList : public IEventList {
        Event events[256];
        int32 count = 0;

        tresult addEvent(Event& e) override {
            if (count < 256) {
                events[count++] = e;
                return kResultOk;
            }
            return kOutOfMemory;
        }
        int32 getEventCount() override { return count; }
        tresult getEvent(int32 index, Event& e) override {
            if (index >= 0 && index < count) {
                e = events[index];
                return kResultOk;
            }
            return kInvalidArgument;
        }
        uint32 addRef() override { return 1; }
        uint32 release() override { return 0; }
        tresult queryInterface(const TUID, void**) override { return kNoInterface; }
    } inputEvents;

    if (midiInput && !midiInput->isEmpty()) {
        size_t eventCount = midiInput->getEventCount();
        for (size_t i = 0; i < eventCount && inputEvents.count < 256; ++i) {
            const auto& ev = midiInput->getEvent(i);
            if (ev.size < 3) {
                continue;
            }
            const uint8_t status = ev.data[0] & 0xF0;
            if (status != 0x80 && status != 0x90) {
                continue;
            }
            const uint8_t channel = ev.data[0] & 0x0F;
            const uint8_t pitch = ev.data[1];
            const uint8_t velocity = ev.data[2];
            Event vstEvent;
            vstEvent.busIndex = 0;
            vstEvent.sampleOffset = ev.sampleOffset;
            vstEvent.ppqPosition = 0;
            vstEvent.flags = Event::kIsLive;
            vstEvent.type = Event::kNoteOnEvent;
            vstEvent.noteOn.channel = channel;
            vstEvent.noteOn.pitch = pitch;
            vstEvent.noteOn.velocity = velocity / 127.0f;
            vstEvent.noteOn.tuning = 0.0f;
            vstEvent.noteOn.noteId = -1;
            if (status == 0x80 || (status == 0x90 && velocity == 0)) {
                vstEvent.type = Event::kNoteOffEvent;
                vstEvent.noteOff.channel = channel;
                vstEvent.noteOff.pitch = pitch;
                vstEvent.noteOff.velocity = velocity / 127.0f;
                vstEvent.noteOff.tuning = 0.0f;
                vstEvent.noteOff.noteId = -1;
            }
            inputEvents.addEvent(vstEvent);
        }
    }
    data.inputEvents = &inputEvents;

    // MIDI output events (simple pass-through buffer)
    struct OutputEventList : public IEventList {
        Event events[256];
        int32 count = 0;

        tresult addEvent(Event& e) override {
            if (count < 256) {
                events[count++] = e;
                return kResultOk;
            }
            return kOutOfMemory;
        }
        int32 getEventCount() override { return count; }
        tresult getEvent(int32 index, Event& e) override {
            if (index >= 0 && index < count) {
                e = events[index];
                return kResultOk;
            }
            return kInvalidArgument;
        }
        uint32 addRef() override { return 1; }
        uint32 release() override { return 0; }
        tresult queryInterface(const TUID, void**) override { return kNoInterface; }
    } outputEvents;
    data.outputEvents = &outputEvents;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Call safe wrapper
    bool success = SafeProcessCall(processor, data);

    if (!success) {
        m_crashed.store(true, std::memory_order_release);
        m_crashReportPending.store(true, std::memory_order_release);

        // Silence output buffers immediately
        for (uint32_t i = 0; i < numOutputChannels; ++i) {
            if (outputs[i])
                std::memset(outputs[i], 0, numFrames * sizeof(float));
        }
        return;
    }

    if (m_params->output.getParameterCount() > 0) {
        ParamChange reported[kMaxParamChangesPerBlock];
        const size_t n = m_params->output.collect(reported, kMaxParamChangesPerBlock);
        for (size_t i = 0; i < n; ++i) m_params->toController.push(reported[i].id, reported[i].value);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double durationNs = std::chrono::duration<double, std::nano>(endTime - startTime).count();
    const auto durationNsU64 = durationNs > 0.0 ? static_cast<uint64_t>(durationNs) : 0;

    // Update stats
    if (durationNsU64 > m_watchdogMaxExecutionTimeNs.load(std::memory_order_relaxed)) {
        m_watchdogMaxExecutionTimeNs.store(durationNsU64, std::memory_order_relaxed);
    }

    const auto previousAvg = m_watchdogAvgExecutionTimeNs.load(std::memory_order_relaxed);
    uint64_t nextAvg = durationNsU64;
    if (previousAvg != 0) {
        if (durationNsU64 > previousAvg) {
            nextAvg = previousAvg + ((durationNsU64 - previousAvg) / 100);
        } else {
            nextAvg = previousAvg - ((previousAvg - durationNsU64) / 100);
        }
    }
    // Exponential moving average (alpha ~ 0.01)
    m_watchdogAvgExecutionTimeNs.store(nextAvg, std::memory_order_relaxed);

    // Check budget
    // Budget is the duration of the audio block itself
    double budgetNs = (static_cast<double>(numFrames) / m_sampleRate) * 1.0e9;

    // We allow a small margin (e.g. 100% CPU usage) but if it exceeds strict budget it's a violation
    // A stricter budget might be 80% to leave room for other tracks.
    // For now, let's use 100% of the block time.
    if (durationNs > budgetNs) {
        const auto violationCount = m_watchdogViolationCount.fetch_add(1, std::memory_order_acq_rel) + 1;

        if (violationCount > WATCHDOG_VIOLATION_LIMIT) {
            const bool wasBypassed = m_watchdogBypassed.exchange(true, std::memory_order_acq_rel);
            if (!wasBypassed) {
                m_watchdogReportPending.store(true, std::memory_order_release);
            }
        }
    } else {
        // Slowly decay violation count if behaving well (optional recovery)
        const auto violationCount = m_watchdogViolationCount.load(std::memory_order_relaxed);
        if (violationCount > 0) {
            m_watchdogViolationCount.store(violationCount - 1, std::memory_order_relaxed);
        }
    }

    // Wire MIDI output events back to caller's buffer
    if (midiOutput && outputEvents.count > 0) {
        for (int32 i = 0; i < outputEvents.count; ++i) {
            const Event& ev = outputEvents.events[i];
            if (ev.type == Event::kNoteOnEvent) {
                uint8_t data[3] = {static_cast<uint8_t>(0x90 | (ev.noteOn.channel & 0x0F)),
                                   static_cast<uint8_t>(ev.noteOn.pitch),
                                   static_cast<uint8_t>(ev.noteOn.velocity * 127.0f)};
                midiOutput->addEvent(ev.sampleOffset, data, 3);
            } else if (ev.type == Event::kNoteOffEvent) {
                uint8_t data[3] = {static_cast<uint8_t>(0x80 | (ev.noteOff.channel & 0x0F)),
                                   static_cast<uint8_t>(ev.noteOff.pitch),
                                   static_cast<uint8_t>(ev.noteOff.velocity * 127.0f)};
                midiOutput->addEvent(ev.sampleOffset, data, 3);
            }
        }
    }
}

std::vector<PluginParameter> VST3PluginInstance::getParameters() const {
    if (!m_parameterCacheValid) {
        buildParameterCache();
    }
    return m_parameterCache;
}

uint32_t VST3PluginInstance::getParameterCount() const {
    if (!m_controller)
        return 0;
    auto ctrl = static_cast<IEditController*>(m_controller);
    return ctrl->getParameterCount();
}

float VST3PluginInstance::getParameter(uint32_t id) const {
    if (!m_controller)
        return 0.0f;
    auto ctrl = static_cast<IEditController*>(m_controller);
    return static_cast<float>(ctrl->getParamNormalized(id));
}

void VST3PluginInstance::setParameter(uint32_t id, float value) {
    if (!m_controller)
        return;
    auto ctrl = static_cast<IEditController*>(m_controller);
    ctrl->setParamNormalized(id, value);
    // The controller alone changes nothing audible: the processor only hears what process() is handed.
    m_params->toProcessor.push(id, value);
}

void VST3PluginInstance::setEditListener(std::function<void(uint32_t, double)> listener) {
    std::lock_guard<std::mutex> lock(m_params->listenerMutex);
    m_params->listener = std::move(listener);
}

void VST3PluginInstance::pumpControllerSync() {
    if (!m_controller) return;
    auto ctrl = static_cast<IEditController*>(m_controller);
    ParamChange reported[kMaxParamChangesPerBlock];
    size_t n;
    while ((n = m_params->toController.drain(reported, kMaxParamChangesPerBlock)) > 0)
        for (size_t i = 0; i < n; ++i) ctrl->setParamNormalized(reported[i].id, reported[i].value);
    if (m_params->handler->takeRestartFlags() & kParamValuesChanged) m_parameterCacheValid = false;
}

std::string VST3PluginInstance::getParameterDisplay(uint32_t id) const {
    if (!m_controller)
        return "";
    auto ctrl = static_cast<IEditController*>(m_controller);

    String128 display;
    if (ctrl->getParamStringByValue(id, ctrl->getParamNormalized(id), display) == kResultOk) {
        return utf16_to_utf8(display);
    }
    return "";
}

std::vector<uint8_t> VST3PluginInstance::saveState() const {
    if (!m_component)
        return {};

    auto component = static_cast<IComponent*>(m_component);

    // VST3 writes state directly into the provided stream.
    struct MemStream : public IBStream {
        std::vector<uint8_t> data;
        int64_t pos = 0;
        FUnknown* i = nullptr;

        MemStream() { addRef(); }
        ~MemStream() {
            if (i)
                i->release();
        }

        tresult write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
            if (!buffer || numBytes < 0 || pos < 0) {
                if (numBytesWritten)
                    *numBytesWritten = 0;
                return kInvalidArgument;
            }
            // [SEC-FIX] Cap stream size to prevent malicious plugins from exhausting memory.
            constexpr int64_t MAX_MEMSTREAM_SIZE = 256LL * 1024 * 1024; // 256 MiB
            if (pos > MAX_MEMSTREAM_SIZE || numBytes > MAX_MEMSTREAM_SIZE || pos + numBytes > MAX_MEMSTREAM_SIZE) {
                if (numBytesWritten)
                    *numBytesWritten = 0;
                return kInvalidArgument;
            }
            if (pos + numBytes > static_cast<int64_t>(data.size()))
                data.resize(pos + numBytes);
            std::memcpy(data.data() + pos, buffer, numBytes);
            if (numBytesWritten)
                *numBytesWritten = numBytes;
            pos += numBytes;
            return kResultOk;
        }
        tresult read(void* buffer, int32 numBytes, int32* numBytesRead) override {
            if (numBytesRead)
                *numBytesRead = 0;
            if (!buffer || numBytes < 0 || pos < 0) {
                return kInvalidArgument;
            }
            int32 toRead = 0;
            if (static_cast<size_t>(pos) < data.size()) {
                toRead = std::min(numBytes, static_cast<int32_t>(data.size() - static_cast<size_t>(pos)));
            }
            if (toRead > 0) {
                std::memcpy(buffer, data.data() + pos, toRead);
            }
            if (numBytesRead)
                *numBytesRead = toRead;
            pos += toRead;
            return kResultOk;
        }
        tresult seek(int64 pos, int32 type, int64* result) override {
            if (type == kIBSeekSet)
                this->pos = pos;
            else if (type == kIBSeekCur)
                this->pos += pos;
            else if (type == kIBSeekEnd)
                this->pos = static_cast<int64_t>(data.size()) + pos;
            else
                return kInvalidArgument;
            this->pos = std::max<int64_t>(0, std::min<int64_t>(this->pos, static_cast<int64_t>(data.size())));
            if (result)
                *result = this->pos;
            return kResultOk;
        }
        tresult tell(int64* pos) override {
            if (pos)
                *pos = this->pos;
            return kResultOk;
        }
        uint32 addRef() override { return ++refCount; }
        uint32 release() override {
            uint32 r = --refCount;
            if (r == 0) { /* don't delete - stack allocated */
                return 0;
            }
            return r;
        }
        tresult queryInterface(const TUID iid, void** obj) override {
            if (iid == IBStream::iid || iid == FUnknown::iid) {
                *obj = static_cast<IBStream*>(this);
                addRef();
                return kResultOk;
            }
            return kNoInterface;
        }

    private:
        uint32 refCount = 0;
    } stream;

    // Save component state (audio processing state)
    if (component->getState(&stream) != kResultOk)
        return {};

    // Also save controller state (UI/parameter state) if different
    // For most plugins, component state is sufficient
    std::vector<uint8_t> result = std::move(stream.data);
    return result;
}

bool VST3PluginInstance::loadState(const std::vector<uint8_t>& state) {
    if (!m_component || state.empty())
        return false;

    struct MemStream : public IBStream {
        const std::vector<uint8_t>& data;
        int64_t pos = 0;

        MemStream(const std::vector<uint8_t>& d) : data(d) { addRef(); }

        tresult write(void*, int32, int32*) override { return kNotImplemented; }
        tresult read(void* buffer, int32 numBytes, int32* numBytesRead) override {
            if (numBytesRead)
                *numBytesRead = 0;
            if (!buffer || numBytes < 0 || pos < 0) {
                return kInvalidArgument;
            }
            int32 toRead = 0;
            if (static_cast<size_t>(pos) < data.size()) {
                toRead = std::min(numBytes, static_cast<int32_t>(data.size() - static_cast<size_t>(pos)));
            }
            if (toRead > 0) {
                std::memcpy(buffer, data.data() + pos, toRead);
            }
            if (numBytesRead)
                *numBytesRead = toRead;
            pos += toRead;
            return kResultOk;
        }
        tresult seek(int64 p, int32 type, int64* result) override {
            if (type == kIBSeekSet)
                pos = p;
            else if (type == kIBSeekCur)
                pos += p;
            else if (type == kIBSeekEnd)
                pos = static_cast<int64_t>(data.size()) + p;
            else
                return kInvalidArgument;
            pos = std::max<int64_t>(0, std::min<int64_t>(pos, static_cast<int64_t>(data.size())));
            if (result)
                *result = pos;
            return kResultOk;
        }
        tresult tell(int64* p) override {
            if (p)
                *p = pos;
            return kResultOk;
        }
        uint32 addRef() override { return ++refCount; }
        uint32 release() override {
            uint32 r = --refCount;
            if (r == 0)
                return 0;
            return r;
        }
        tresult queryInterface(const TUID iid, void** obj) override {
            if (iid == IBStream::iid || iid == FUnknown::iid) {
                *obj = static_cast<IBStream*>(this);
                addRef();
                return kResultOk;
            }
            return kNoInterface;
        }

    private:
        uint32 refCount = 0;
    } stream(state);

    auto component = static_cast<IComponent*>(m_component);
    const bool ok = component->setState(&stream) == kResultOk;
    // A split plugin's controller (and so its window) learns the processor's state from the same bytes.
    if (ok && m_controller && m_controller != m_component) {
        stream.pos = 0;
        static_cast<IEditController*>(m_controller)->setComponentState(&stream);
    }
    return ok;
}

bool VST3PluginInstance::hasEditor() const {
    return m_info.hasEditor;
}

bool VST3PluginInstance::openEditor(void* parentWindow) {
    if (!m_controller || m_editorOpen)
        return false;

    auto ctrl = static_cast<IEditController*>(m_controller);
    auto view = ctrl->createView("editor");
    if (!view)
        return false;

    m_plugView = view;

#ifdef _WIN32
    if (view->isPlatformTypeSupported("HWND") == kResultOk) {
        view->attached(parentWindow, "HWND");
        m_editorOpen = true;
        return true;
    }
#endif

    view->release();
    m_plugView = nullptr;
    return false;
}

void VST3PluginInstance::closeEditor() {
    if (!m_plugView)
        return;

    auto view = static_cast<IPlugView*>(m_plugView);
    view->removed();
#if defined(__linux__)
    if (m_plugFrame) {
        view->setFrame(nullptr);
        static_cast<X11PlugFrame*>(m_plugFrame)->clearRegistrations();
        static_cast<X11PlugFrame*>(m_plugFrame)->release();
        m_plugFrame = nullptr;
    }
#endif
    view->release();
    m_plugView = nullptr;
    m_editorOpen = false;
}

std::pair<int, int> VST3PluginInstance::getEditorSize() const {
    if (!m_plugView)
        return {0, 0};

    auto view = static_cast<IPlugView*>(m_plugView);
    ViewRect rect;
    if (view->getSize(&rect) == kResultOk) {
        return {rect.getWidth(), rect.getHeight()};
    }
    return {800, 600}; // Default size
}

bool VST3PluginInstance::resizeEditor(int width, int height) {
    if (!m_plugView)
        return false;

    auto view = static_cast<IPlugView*>(m_plugView);
    ViewRect rect(0, 0, width, height);
    return view->onSize(&rect) == kResultOk;
}

bool VST3PluginInstance::canResizeEditor() const {
    if (!m_plugView)
        return false;
    return static_cast<IPlugView*>(m_plugView)->canResize() == kResultTrue;
}

bool VST3PluginInstance::openEditorX11(const std::function<unsigned long(int, int, bool)>& makeWindow,
                                       Aestra::Audio::EditorLoopPump& pump,
                                       std::function<void(int, int)> requestResize) {
#if defined(__linux__)
    if (!m_controller || m_editorOpen || !makeWindow)
        return false;

    auto* controller = static_cast<IEditController*>(m_controller);
    IPlugView* view = controller->createView(ViewType::kEditor);
    if (!view)
        return false;
    if (view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) {
        view->release();
        return false;
    }

    ViewRect rect;
    int width = 800, height = 600; // only if the plugin will not say
    if (view->getSize(&rect) == kResultOk && rect.getWidth() > 0 && rect.getHeight() > 0) {
        width = rect.getWidth();
        height = rect.getHeight();
    }
    const unsigned long window = makeWindow(width, height, view->canResize() == kResultTrue);
    if (window == 0) {
        view->release();
        return false;
    }

    auto* frame = new X11PlugFrame(pump, std::move(requestResize)); // one reference: ours
    view->setFrame(frame);
    if (view->attached(reinterpret_cast<void*>(window), kPlatformTypeX11EmbedWindowID) != kResultOk) {
        view->setFrame(nullptr);
        frame->clearRegistrations();
        frame->release();
        view->release();
        return false; // the caller made the window, so the caller takes it down
    }
    m_plugView = view;
    m_plugFrame = frame;
    m_editorOpen = true;
    return true;
#else
    (void)makeWindow;
    (void)pump;
    (void)requestResize;
    return false;
#endif
}

uint32_t VST3PluginInstance::getLatencySamples() const {
    if (!m_processor)
        return 0;
    auto processor = static_cast<IAudioProcessor*>(m_processor);
    return processor->getLatencySamples();
}

uint32_t VST3PluginInstance::getTailSamples() const {
    if (!m_processor)
        return 0;
    auto processor = static_cast<IAudioProcessor*>(m_processor);
    return processor->getTailSamples();
}

void VST3PluginInstance::buildParameterCache() const {
    m_parameterCache.clear();

    if (!m_controller)
        return;

    auto ctrl = static_cast<IEditController*>(m_controller);
    int32 count = ctrl->getParameterCount();

    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info;
        if (ctrl->getParameterInfo(i, info) == kResultOk) {
            PluginParameter param;
            param.id = info.id;

            param.name = utf16_to_utf8(info.title);
            param.shortName = utf16_to_utf8(info.shortTitle);
            param.unit = utf16_to_utf8(info.units);

            param.defaultValue = static_cast<float>(info.defaultNormalizedValue);
            param.minValue = 0.0f;
            param.maxValue = 1.0f;
            param.stepCount = info.stepCount;
            param.isAutomatable = (info.flags & ParameterInfo::kCanAutomate) != 0;
            param.isBypass = (info.flags & ParameterInfo::kIsBypass) != 0;
            param.isReadOnly = (info.flags & ParameterInfo::kIsReadOnly) != 0;

            m_parameterCache.push_back(param);
        }
    }

    m_parameterCacheValid = true;
}

void VST3PluginInstance::setupProcessing() {
    // Called after initialize to prepare buffers
}

IPluginInstance::WatchdogStats VST3PluginInstance::getWatchdogStats() const {
    reportDeferredRealtimeEvents();

    WatchdogStats stats;
    stats.maxExecutionTimeNs = static_cast<double>(m_watchdogMaxExecutionTimeNs.load(std::memory_order_relaxed));
    stats.avgExecutionTimeNs = static_cast<double>(m_watchdogAvgExecutionTimeNs.load(std::memory_order_relaxed));
    stats.violationCount = m_watchdogViolationCount.load(std::memory_order_relaxed);
    stats.isBypassed = m_watchdogBypassed.load(std::memory_order_acquire);
    return stats;
}

bool VST3PluginInstance::isBypassedByWatchdog() const {
    reportDeferredRealtimeEvents();
    return m_watchdogBypassed.load(std::memory_order_acquire);
}

bool VST3PluginInstance::isCrashed() const {
    reportDeferredRealtimeEvents();
    return m_crashed.load(std::memory_order_acquire);
}

void VST3PluginInstance::resetWatchdog() {
    reportDeferredRealtimeEvents();
    m_watchdogMaxExecutionTimeNs.store(0, std::memory_order_release);
    m_watchdogAvgExecutionTimeNs.store(0, std::memory_order_release);
    m_watchdogViolationCount.store(0, std::memory_order_release);
    m_watchdogBypassed.store(false, std::memory_order_release);
    m_crashed.store(false, std::memory_order_release);
    m_watchdogReportPending.store(false, std::memory_order_release);
    m_crashReportPending.store(false, std::memory_order_release);
    Log::info("VST3: Watchdog reset for " + m_info.name);
}

void VST3PluginInstance::reportDeferredRealtimeEvents() const {
    if (m_crashReportPending.exchange(false, std::memory_order_acq_rel)) {
#ifdef _WIN32
        Log::error("VST3: CRASH DETECTED in " + m_info.name + "! (Access Violation trapped)");
#else
        Log::error("VST3: Crash detected in " + m_info.name);
#endif
    }

    if (m_watchdogReportPending.exchange(false, std::memory_order_acq_rel)) {
        Log::error("VST3: Plugin " + m_info.name + " exceeded time budget " + std::to_string(WATCHDOG_VIOLATION_LIMIT) +
                   " times. Auto-bypassing.");
    }
}

// ============================================================================
// VST3PluginFactory Implementation
// ============================================================================

std::vector<PluginInfo> VST3PluginFactory::scanPlugin(const std::filesystem::path& path) {
    std::vector<PluginInfo> result;

    std::string errorMsg;
    auto module = VST3::Hosting::Module::create(path.string(), errorMsg);
    if (!module) {
        return result;
    }

    auto factory = module->getFactory();
    if (!factory.get()) {
        return result;
    }

    auto classInfos = factory.classInfos();
    for (size_t i = 0; i < classInfos.size(); ++i) {
        const auto& classInfo = classInfos[i];

        // Only include audio plugins (skip controller, etc.)
        if (classInfo.category() != kVstAudioEffectClass) {
            continue;
        }

        PluginInfo info;
        info.id = classInfo.ID().toString();
        info.name = classInfo.name();
        info.vendor = classInfo.vendor();
        info.version = classInfo.version();
        info.category = classInfo.subCategoriesString();
        info.format = PluginFormat::VST3;
        info.path = path;

        // Determine type from category
        std::string cat = classInfo.subCategoriesString();
        if (cat.find("Instrument") != std::string::npos || cat.find("Synth") != std::string::npos) {
            info.type = PluginType::Instrument;
            info.hasMidiInput = true;
        } else if (cat.find("Analyzer") != std::string::npos) {
            info.type = PluginType::Analyzer;
        } else {
            info.type = PluginType::Effect;
        }

        info.numAudioInputs = 2;
        info.numAudioOutputs = 2;
        info.hasEditor = true; // Assume true, verified on load

        result.push_back(info);
    }

    return result;
}

std::shared_ptr<VST3PluginInstance> VST3PluginFactory::createInstance(const PluginInfo& info) {
    if (info.format != PluginFormat::VST3) {
        return nullptr;
    }

    int classIndex = 0;
    if (!info.id.empty()) {
        bool foundRequestedClass = false;
        std::string errorMsg;
        auto module = VST3::Hosting::Module::create(info.path.string(), errorMsg);
        if (module) {
            auto factory = module->getFactory();
            if (factory.get()) {
                auto classInfos = factory.classInfos();
                for (size_t i = 0; i < classInfos.size(); ++i) {
                    const auto& classInfo = classInfos[i];
                    if (classInfo.category() == kVstAudioEffectClass && classInfo.ID().toString() == info.id) {
                        classIndex = static_cast<int>(i);
                        foundRequestedClass = true;
                        break;
                    }
                }
            }
        }
        if (!foundRequestedClass) {
            return nullptr;
        }
    }

    auto instance = std::make_shared<VST3PluginInstance>();
    if (!instance->load(info.path, classIndex)) {
        return nullptr;
    }

    return instance;
}

} // namespace Audio
} // namespace Aestra
