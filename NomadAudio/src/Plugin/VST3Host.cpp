// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/VST3Host.h"
#include "NomadLog.h"

#ifdef _WIN32
#include <Windows.h>
#include <objbase.h>
#endif

// VST3 SDK includes
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Nomad {
namespace Audio {

// ============================================================================
// VST3PluginInstance Implementation
// ============================================================================

VST3PluginInstance::VST3PluginInstance() = default;

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
        
        m_module = new VST3::Hosting::Module::Ptr(std::move(module));
        auto& mod = *static_cast<VST3::Hosting::Module::Ptr*>(m_module);
        
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
        if (cat.find("Instrument") != std::string::npos || 
            cat.find("Synth") != std::string::npos) {
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
        if (comp->initialize(nullptr) != kResultOk) {
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
                    ctrl->initialize(nullptr);
                }
            }
        }
        
        // Get I/O configuration
        m_info.numAudioInputs = 2;  // Default stereo
        m_info.numAudioOutputs = 2;
        
        // Check for editor
        if (m_controller) {
            auto ctrl = static_cast<IEditController*>(m_controller);
            auto view = ctrl->createView("editor");
            m_info.hasEditor = (view != nullptr);
            if (view) {
                view->release();
            }
        }
        
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
    
    if (m_module) {
        delete static_cast<VST3::Hosting::Module::Ptr*>(m_module);
        m_module = nullptr;
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
    if (!m_loaded || m_active) return;
    
    auto comp = static_cast<IComponent*>(m_component);
    if (comp->setActive(true) == kResultOk) {
        m_active = true;
    }
}

void VST3PluginInstance::deactivate() {
    if (!m_loaded || !m_active) return;
    
    auto comp = static_cast<IComponent*>(m_component);
    comp->setActive(false);
    m_active = false;
}

void VST3PluginInstance::process(
    const float* const* inputs,
    float** outputs,
    uint32_t numInputChannels,
    uint32_t numOutputChannels,
    uint32_t numFrames,
    const MidiBuffer* midiInput,
    MidiBuffer* midiOutput
) {
    if (!m_active || !m_processor) {
        // Pass-through or silence
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
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    
    // TODO: Handle MIDI events via inputEvents/outputEvents
    (void)midiInput;
    (void)midiOutput;
    
    processor->process(data);
}

std::vector<PluginParameter> VST3PluginInstance::getParameters() const {
    if (!m_parameterCacheValid) {
        buildParameterCache();
    }
    return m_parameterCache;
}

uint32_t VST3PluginInstance::getParameterCount() const {
    if (!m_controller) return 0;
    auto ctrl = static_cast<IEditController*>(m_controller);
    return ctrl->getParameterCount();
}

float VST3PluginInstance::getParameter(uint32_t id) const {
    if (!m_controller) return 0.0f;
    auto ctrl = static_cast<IEditController*>(m_controller);
    return static_cast<float>(ctrl->getParamNormalized(id));
}

void VST3PluginInstance::setParameter(uint32_t id, float value) {
    if (!m_controller) return;
    auto ctrl = static_cast<IEditController*>(m_controller);
    ctrl->setParamNormalized(id, value);
}

std::string VST3PluginInstance::getParameterDisplay(uint32_t id) const {
    if (!m_controller) return "";
    auto ctrl = static_cast<IEditController*>(m_controller);
    
    String128 display;
    if (ctrl->getParamStringByValue(id, ctrl->getParamNormalized(id), display) == kResultOk) {
        // Convert from UTF-16 to UTF-8
        char utf8[256];
        WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(display), -1, 
                           utf8, sizeof(utf8), nullptr, nullptr);
        return utf8;
    }
    return "";
}

std::vector<uint8_t> VST3PluginInstance::saveState() const {
    // TODO: Implement state saving via IComponent::getState and IEditController::getState
    return {};
}

bool VST3PluginInstance::loadState(const std::vector<uint8_t>& state) {
    // TODO: Implement state loading via IComponent::setState and IEditController::setState
    (void)state;
    return false;
}

bool VST3PluginInstance::hasEditor() const {
    return m_info.hasEditor;
}

bool VST3PluginInstance::openEditor(void* parentWindow) {
    if (!m_controller || m_editorOpen) return false;
    
    auto ctrl = static_cast<IEditController*>(m_controller);
    auto view = ctrl->createView("editor");
    if (!view) return false;
    
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
    if (!m_plugView) return;
    
    auto view = static_cast<IPlugView*>(m_plugView);
    view->removed();
    view->release();
    m_plugView = nullptr;
    m_editorOpen = false;
}

std::pair<int, int> VST3PluginInstance::getEditorSize() const {
    if (!m_plugView) return {0, 0};
    
    auto view = static_cast<IPlugView*>(m_plugView);
    ViewRect rect;
    if (view->getSize(&rect) == kResultOk) {
        return {rect.getWidth(), rect.getHeight()};
    }
    return {800, 600}; // Default size
}

bool VST3PluginInstance::resizeEditor(int width, int height) {
    if (!m_plugView) return false;
    
    auto view = static_cast<IPlugView*>(m_plugView);
    ViewRect rect(0, 0, width, height);
    return view->onSize(&rect) == kResultOk;
}

uint32_t VST3PluginInstance::getLatencySamples() const {
    if (!m_processor) return 0;
    auto processor = static_cast<IAudioProcessor*>(m_processor);
    return processor->getLatencySamples();
}

uint32_t VST3PluginInstance::getTailSamples() const {
    if (!m_processor) return 0;
    auto processor = static_cast<IAudioProcessor*>(m_processor);
    return processor->getTailSamples();
}

void VST3PluginInstance::buildParameterCache() const {
    m_parameterCache.clear();
    
    if (!m_controller) return;
    
    auto ctrl = static_cast<IEditController*>(m_controller);
    int32 count = ctrl->getParameterCount();
    
    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info;
        if (ctrl->getParameterInfo(i, info) == kResultOk) {
            PluginParameter param;
            param.id = info.id;
            
            // Convert UTF-16 to UTF-8
            char utf8[256];
            WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(info.title), -1,
                               utf8, sizeof(utf8), nullptr, nullptr);
            param.name = utf8;
            
            WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(info.shortTitle), -1,
                               utf8, sizeof(utf8), nullptr, nullptr);
            param.shortName = utf8;
            
            WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(info.units), -1,
                               utf8, sizeof(utf8), nullptr, nullptr);
            param.unit = utf8;
            
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
        if (cat.find("Instrument") != std::string::npos ||
            cat.find("Synth") != std::string::npos) {
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
    
    auto instance = std::make_shared<VST3PluginInstance>();
    if (!instance->load(info.path)) {
        return nullptr;
    }
    
    return instance;
}

} // namespace Audio
} // namespace Nomad
