// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/CLAPHost.h"

#include "AestraLog.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

// CLAP SDK includes
#include <clap/clap.h>
#include <cstring>

namespace Aestra {
namespace Audio {

// ============================================================================
// CLAP Host Callbacks
// ============================================================================

namespace {

// Host callbacks for CLAP plugins
void hostRequestRestart(const clap_host* /*host*/) {
    // TODO: Handle plugin restart request
}

void hostRequestProcess(const clap_host* /*host*/) {
    // TODO: Handle process request
}

void hostRequestCallback(const clap_host* /*host*/) {
    // TODO: Handle callback request on main thread
}

// Helper to create host log extension callback
const void* g_hostLogPtr = nullptr;
static clap_host_log createHostLog() {
    clap_host_log hostLog;
    hostLog.log = [](const clap_host* /*host*/, clap_log_severity severity, const char* msg) {
        switch (severity) {
        case CLAP_LOG_ERROR:
        case CLAP_LOG_HOST_MISBEHAVING:
        case CLAP_LOG_PLUGIN_MISBEHAVING:
            Log::error(std::string("CLAP Plugin: ") + msg);
            break;
        case CLAP_LOG_WARNING:
            Log::warning(std::string("CLAP Plugin: ") + msg);
            break;
        default:
            Log::info(std::string("CLAP Plugin: ") + msg);
            break;
        }
    };
    return hostLog;
}
static clap_host_log g_hostLog = createHostLog();

// Helper to create host params extension
static clap_host_params createHostParams() {
    clap_host_params params;
    params.rescan = [](const clap_host* /*host*/, clap_param_rescan_flags /*flags*/) {
        // TODO: Rescan parameters
    };
    params.clear = [](const clap_host* /*host*/, clap_id /*param_id*/, clap_param_clear_flags /*flags*/) {
        // TODO: Clear parameter modulation
    };
    params.request_flush = [](const clap_host* /*host*/) {
        // TODO: Request parameter flush
    };
    return params;
}
static clap_host_params g_hostParams = createHostParams();

const void* hostGetExtension(const clap_host* /*host*/, const char* extension_id) {
    if (strcmp(extension_id, CLAP_EXT_LOG) == 0) {
        return &g_hostLog;
    }
    if (strcmp(extension_id, CLAP_EXT_PARAMS) == 0) {
        return &g_hostParams;
    }
    return nullptr;
}

} // anonymous namespace

clap_host* CLAPPluginInstance::createHost() {
    static clap_host host;
    host.clap_version = CLAP_VERSION;
    host.host_data = nullptr;
    host.name = "Aestra";
    host.vendor = "Aestra Studios";
    host.url = "https://Aestra.audio";
    host.version = "1.0.0";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequestRestart;
    host.request_process = hostRequestProcess;
    host.request_callback = hostRequestCallback;
    return &host;
}

// ============================================================================
// CLAPPluginInstance Implementation
// ============================================================================

CLAPPluginInstance::CLAPPluginInstance() {
    m_host = createHost();
}

CLAPPluginInstance::~CLAPPluginInstance() {
    unload();
}

bool CLAPPluginInstance::load(const std::filesystem::path& path, int pluginIndex) {
    if (m_loaded) {
        unload();
    }

    // Load the library
#ifdef _WIN32
    m_library = LoadLibraryW(path.wstring().c_str());
    if (!m_library) {
        Log::error("CLAP: Failed to load library: " + path.string());
        return false;
    }

    // Get entry point
    auto entryFunc =
        reinterpret_cast<const clap_plugin_entry* (*)()>(GetProcAddress(static_cast<HMODULE>(m_library), "clap_entry"));
    if (!entryFunc) {
        // Try direct symbol
        m_entry =
            reinterpret_cast<const clap_plugin_entry*>(GetProcAddress(static_cast<HMODULE>(m_library), "clap_entry"));
    }
#else
    m_library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_library) {
        Log::error("CLAP: Failed to load library: " + path.string() + " - " + dlerror());
        return false;
    }

    m_entry = static_cast<const clap_plugin_entry*>(dlsym(m_library, "clap_entry"));
#endif

    if (!m_entry) {
        Log::error("CLAP: No clap_entry symbol in: " + path.string());
        unload();
        return false;
    }

    // Verify CLAP version compatibility
    if (!clap_version_is_compatible(m_entry->clap_version)) {
        Log::error("CLAP: Incompatible version in: " + path.string());
        unload();
        return false;
    }

    // Initialize the entry
    if (!m_entry->init(path.string().c_str())) {
        Log::error("CLAP: Entry init failed for: " + path.string());
        unload();
        return false;
    }

    // Get plugin factory
    auto factory = static_cast<const clap_plugin_factory*>(m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) {
        Log::error("CLAP: No plugin factory in: " + path.string());
        unload();
        return false;
    }

    // Get plugin count
    uint32_t pluginCount = factory->get_plugin_count(factory);
    if (pluginCount == 0) {
        Log::error("CLAP: No plugins in: " + path.string());
        unload();
        return false;
    }

    if (pluginIndex < 0 || static_cast<uint32_t>(pluginIndex) >= pluginCount) {
        pluginIndex = 0;
    }

    // Get plugin descriptor
    auto desc = factory->get_plugin_descriptor(factory, pluginIndex);
    if (!desc) {
        Log::error("CLAP: Failed to get descriptor for: " + path.string());
        unload();
        return false;
    }

    // Store plugin info
    m_info.id = desc->id ? desc->id : "";
    m_info.name = desc->name ? desc->name : "";
    m_info.vendor = desc->vendor ? desc->vendor : "";
    m_info.version = desc->version ? desc->version : "";
    m_info.category = "";
    m_info.format = PluginFormat::CLAP;
    m_info.path = path;

    // Parse features to determine type
    if (desc->features) {
        for (const char* const* f = desc->features; *f; ++f) {
            if (strcmp(*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0 || strcmp(*f, CLAP_PLUGIN_FEATURE_SYNTHESIZER) == 0) {
                m_info.type = PluginType::Instrument;
                m_info.hasMidiInput = true;
            } else if (strcmp(*f, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) == 0) {
                m_info.type = PluginType::Effect;
            } else if (strcmp(*f, CLAP_PLUGIN_FEATURE_ANALYZER) == 0) {
                m_info.type = PluginType::Analyzer;
            }
            // Build category string
            if (!m_info.category.empty()) {
                m_info.category += "|";
            }
            m_info.category += *f;
        }
    }

    // Create plugin instance
    m_plugin = factory->create_plugin(factory, m_host, desc->id);
    if (!m_plugin) {
        Log::error("CLAP: Failed to create plugin: " + m_info.name);
        unload();
        return false;
    }

    // Initialize the plugin
    if (!m_plugin->init(m_plugin)) {
        Log::error("CLAP: Plugin init failed: " + m_info.name);
        unload();
        return false;
    }

    // Check for GUI
    auto gui = static_cast<const clap_plugin_gui*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    m_info.hasEditor = (gui != nullptr);

    m_loaded = true;
    Log::info("CLAP: Loaded plugin: " + m_info.name + " (" + m_info.vendor + ")");

    return true;
}

void CLAPPluginInstance::unload() {
    if (m_editorOpen) {
        closeEditor();
    }

    if (m_active) {
        deactivate();
    }

    if (m_plugin) {
        m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
    }

    if (m_entry) {
        m_entry->deinit();
        m_entry = nullptr;
    }

#ifdef _WIN32
    if (m_library) {
        FreeLibrary(static_cast<HMODULE>(m_library));
        m_library = nullptr;
    }
#else
    if (m_library) {
        dlclose(m_library);
        m_library = nullptr;
    }
#endif

    m_loaded = false;
    m_parameterCacheValid = false;
}

bool CLAPPluginInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    if (!m_loaded || !m_plugin) {
        return false;
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    return true;
}

void CLAPPluginInstance::shutdown() {
    if (m_active) {
        deactivate();
    }
}

void CLAPPluginInstance::activate() {
    if (!m_loaded || m_active || !m_plugin)
        return;

    if (m_plugin->activate(m_plugin, m_sampleRate, 1, m_maxBlockSize)) {
        m_plugin->start_processing(m_plugin);
        m_active = true;
    }
}

void CLAPPluginInstance::deactivate() {
    if (!m_loaded || !m_active || !m_plugin)
        return;

    m_plugin->stop_processing(m_plugin);
    m_plugin->deactivate(m_plugin);
    m_active = false;
}

void CLAPPluginInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                                 uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* midiInput,
                                 MidiBuffer* midiOutput) {
    if (!m_active || !m_plugin) {
        return;
    }

    (void)midiInput;
    (void)midiOutput;

    // Setup audio buffers
    clap_audio_buffer inputBuffer = {};
    inputBuffer.data32 = const_cast<float**>(inputs);
    inputBuffer.channel_count = numInputChannels;
    inputBuffer.latency = 0;
    inputBuffer.constant_mask = 0;

    clap_audio_buffer outputBuffer = {};
    outputBuffer.data32 = outputs;
    outputBuffer.channel_count = numOutputChannels;
    outputBuffer.latency = 0;
    outputBuffer.constant_mask = 0;

    // Setup process data
    clap_process process = {};
    process.steady_time = -1;
    process.frames_count = numFrames;
    process.transport = nullptr;
    process.audio_inputs = &inputBuffer;
    process.audio_outputs = &outputBuffer;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.in_events = nullptr; // TODO: MIDI events
    process.out_events = nullptr;

    m_plugin->process(m_plugin, &process);
}

std::vector<PluginParameter> CLAPPluginInstance::getParameters() const {
    if (!m_parameterCacheValid) {
        buildParameterCache();
    }
    return m_parameterCache;
}

uint32_t CLAPPluginInstance::getParameterCount() const {
    if (!m_plugin)
        return 0;

    auto params = static_cast<const clap_plugin_params*>(m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));
    if (!params)
        return 0;

    return params->count(m_plugin);
}

float CLAPPluginInstance::getParameter(uint32_t id) const {
    if (!m_plugin)
        return 0.0f;

    auto params = static_cast<const clap_plugin_params*>(m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));
    if (!params)
        return 0.0f;

    double value = 0.0;
    params->get_value(m_plugin, id, &value);
    return static_cast<float>(value);
}

void CLAPPluginInstance::setParameter(uint32_t id, float value) {
    // CLAP parameters are set via events during process
    // For now, store and apply in next process call
    (void)id;
    (void)value;
    // TODO: Queue parameter change event
}

std::string CLAPPluginInstance::getParameterDisplay(uint32_t id) const {
    if (!m_plugin)
        return "";

    auto params = static_cast<const clap_plugin_params*>(m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));
    if (!params)
        return "";

    double value = 0.0;
    params->get_value(m_plugin, id, &value);

    char display[256];
    if (params->value_to_text(m_plugin, id, value, display, sizeof(display))) {
        return display;
    }
    return std::to_string(value);
}

std::vector<uint8_t> CLAPPluginInstance::saveState() const {
    // TODO: Implement via clap_plugin_state extension
    return {};
}

bool CLAPPluginInstance::loadState(const std::vector<uint8_t>& state) {
    (void)state;
    // TODO: Implement via clap_plugin_state extension
    return false;
}

bool CLAPPluginInstance::hasEditor() const {
    return m_info.hasEditor;
}

bool CLAPPluginInstance::openEditor(void* parentWindow) {
    if (!m_plugin || m_editorOpen)
        return false;

    auto gui = static_cast<const clap_plugin_gui*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    if (!gui)
        return false;

#ifdef _WIN32
    if (!gui->is_api_supported(m_plugin, CLAP_WINDOW_API_WIN32, false)) {
        return false;
    }

    if (!gui->create(m_plugin, CLAP_WINDOW_API_WIN32, false)) {
        return false;
    }

    clap_window window = {};
    window.api = CLAP_WINDOW_API_WIN32;
    window.win32 = parentWindow;

    if (!gui->set_parent(m_plugin, &window)) {
        gui->destroy(m_plugin);
        return false;
    }

    gui->show(m_plugin);
    m_editorOpen = true;
    return true;
#else
    (void)parentWindow;
    return false;
#endif
}

void CLAPPluginInstance::closeEditor() {
    if (!m_plugin || !m_editorOpen)
        return;

    auto gui = static_cast<const clap_plugin_gui*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    if (gui) {
        gui->hide(m_plugin);
        gui->destroy(m_plugin);
    }

    m_editorOpen = false;
}

std::pair<int, int> CLAPPluginInstance::getEditorSize() const {
    if (!m_plugin)
        return {0, 0};

    auto gui = static_cast<const clap_plugin_gui*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    if (!gui)
        return {0, 0};

    uint32_t width = 0, height = 0;
    if (gui->get_size(m_plugin, &width, &height)) {
        return {static_cast<int>(width), static_cast<int>(height)};
    }
    return {800, 600};
}

bool CLAPPluginInstance::resizeEditor(int width, int height) {
    if (!m_plugin)
        return false;

    auto gui = static_cast<const clap_plugin_gui*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    if (!gui)
        return false;

    return gui->set_size(m_plugin, width, height);
}

uint32_t CLAPPluginInstance::getLatencySamples() const {
    if (!m_plugin)
        return 0;

    auto latency = static_cast<const clap_plugin_latency*>(m_plugin->get_extension(m_plugin, CLAP_EXT_LATENCY));
    if (!latency)
        return 0;

    return latency->get(m_plugin);
}

uint32_t CLAPPluginInstance::getTailSamples() const {
    if (!m_plugin)
        return 0;

    auto tail = static_cast<const clap_plugin_tail*>(m_plugin->get_extension(m_plugin, CLAP_EXT_TAIL));
    if (!tail)
        return 0;

    return tail->get(m_plugin);
}

void CLAPPluginInstance::buildParameterCache() const {
    m_parameterCache.clear();

    if (!m_plugin)
        return;

    auto params = static_cast<const clap_plugin_params*>(m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));
    if (!params)
        return;

    uint32_t count = params->count(m_plugin);
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info info;
        if (params->get_info(m_plugin, i, &info)) {
            PluginParameter param;
            param.id = static_cast<uint32_t>(info.id);
            param.name = info.name;
            param.shortName = info.name; // CLAP doesn't have short name
            param.unit = "";
            param.defaultValue = static_cast<float>(info.default_value);
            param.minValue = static_cast<float>(info.min_value);
            param.maxValue = static_cast<float>(info.max_value);
            param.isAutomatable = (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0;
            param.isBypass = (info.flags & CLAP_PARAM_IS_BYPASS) != 0;
            param.isReadOnly = (info.flags & CLAP_PARAM_IS_READONLY) != 0;

            m_parameterCache.push_back(param);
        }
    }

    m_parameterCacheValid = true;
}

// ============================================================================
// CLAPPluginFactory Implementation
// ============================================================================

std::vector<PluginInfo> CLAPPluginFactory::scanPlugin(const std::filesystem::path& path) {
    std::vector<PluginInfo> result;

#ifdef _WIN32
    HMODULE library = LoadLibraryW(path.wstring().c_str());
    if (!library)
        return result;

    auto entry = reinterpret_cast<const clap_plugin_entry*>(GetProcAddress(library, "clap_entry"));
#else
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return result;

    auto entry = static_cast<const clap_plugin_entry*>(dlsym(library, "clap_entry"));
#endif

    if (!entry || !clap_version_is_compatible(entry->clap_version)) {
#ifdef _WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return result;
    }

    if (!entry->init(path.string().c_str())) {
#ifdef _WIN32
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return result;
    }

    auto factory = static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));

    if (factory) {
        uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < count; ++i) {
            auto desc = factory->get_plugin_descriptor(factory, i);
            if (desc) {
                PluginInfo info;
                info.id = desc->id ? desc->id : "";
                info.name = desc->name ? desc->name : "";
                info.vendor = desc->vendor ? desc->vendor : "";
                info.version = desc->version ? desc->version : "";
                info.format = PluginFormat::CLAP;
                info.type = PluginType::Effect;
                info.path = path;
                info.hasEditor = true;

                // Parse features
                if (desc->features) {
                    for (const char* const* f = desc->features; *f; ++f) {
                        if (strcmp(*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0) {
                            info.type = PluginType::Instrument;
                            info.hasMidiInput = true;
                        }
                        if (!info.category.empty())
                            info.category += "|";
                        info.category += *f;
                    }
                }

                result.push_back(info);
            }
        }
    }

    entry->deinit();

#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif

    return result;
}

std::shared_ptr<CLAPPluginInstance> CLAPPluginFactory::createInstance(const PluginInfo& info) {
    if (info.format != PluginFormat::CLAP) {
        return nullptr;
    }

    auto instance = std::make_shared<CLAPPluginInstance>();
    if (!instance->load(info.path)) {
        return nullptr;
    }

    return instance;
}

} // namespace Audio
} // namespace Aestra
