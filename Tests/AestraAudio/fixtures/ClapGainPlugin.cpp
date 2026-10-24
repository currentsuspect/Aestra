// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// A CLAP effect with one parameter, Gain, that the helper can load, play and move: the fixture for
// parameters through the sandbox (V82-H7). Output = input x gain.
#include <clap/clap.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr clap_id kGainId = 7;
const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT, "test.aestra.gain", "Gain Fixture", "Aestra Tests", nullptr, nullptr, nullptr, "1.0.0", "",
    kFeatures};

struct Gain {
    clap_plugin_t plugin;
    double gain = 0.5;
};
Gain* self(const clap_plugin_t* p) {
    return static_cast<Gain*>(p->plugin_data);
}

void applyEvents(Gain* g, const clap_input_events_t* in) {
    if (!in)
        return;
    for (uint32_t i = 0, n = in->size(in); i < n; ++i) {
        const clap_event_header_t* e = in->get(in, i);
        if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* v = reinterpret_cast<const clap_event_param_value_t*>(e);
            if (v->param_id == kGainId)
                g->gain = v->value;
        }
    }
}

// --- params ---
uint32_t paramCount(const clap_plugin_t*) {
    return 1;
}
bool paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = kGainId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "Gain");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    return true;
}
bool paramValue(const clap_plugin_t* p, clap_id id, double* out) {
    if (id != kGainId)
        return false;
    *out = self(p)->gain;
    return true;
}
bool valueToText(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t size) {
    if (id != kGainId)
        return false;
    std::snprintf(out, size, "%.2f", value);
    return true;
}
bool textToValue(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    if (id != kGainId)
        return false;
    *out = std::atof(text);
    return true;
}
void flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
    applyEvents(self(p), in);
}
const clap_plugin_params_t kParams = {paramCount, paramInfo, paramValue, valueToText, textToValue, flush};

// --- audio ports: one stereo in, one stereo out ---
uint32_t portCount(const clap_plugin_t*, bool) {
    return 1;
}
bool portInfo(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (index != 0)
        return false;
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t kPorts = {portCount, portInfo};

// --- the plugin ---
bool init(const clap_plugin_t*) {
    return true;
}
void destroy(const clap_plugin_t* p) {
    delete self(p);
}
bool activate(const clap_plugin_t*, double, uint32_t, uint32_t) {
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) {
    return true;
}
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t*) {}
clap_process_status process(const clap_plugin_t* p, const clap_process_t* proc) {
    Gain* g = self(p);
    applyEvents(g, proc->in_events);
    if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
        return CLAP_PROCESS_CONTINUE;
    const auto& in = proc->audio_inputs[0];
    const auto& out = proc->audio_outputs[0];
    for (uint32_t c = 0; c < out.channel_count; ++c) {
        const float* src = c < in.channel_count ? in.data32[c] : nullptr;
        for (uint32_t i = 0; i < proc->frames_count; ++i) {
            out.data32[c][i] = src ? static_cast<float>(src[i] * g->gain) : 0.0f;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}
const void* extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &kParams;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &kPorts;
    return nullptr;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t count(const clap_plugin_factory_t*) {
    return 1;
}
const clap_plugin_descriptor_t* descriptor(const clap_plugin_factory_t*, uint32_t i) {
    return i == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0)
        return nullptr;
    auto* g = new Gain();
    g->plugin = {&kDescriptor,   g,     init,    destroy,   activate,    deactivate, startProcessing,
                 stopProcessing, reset, process, extension, onMainThread};
    return &g->plugin;
}

const clap_plugin_factory_t kFactory = {count, descriptor, create};
bool entryInit(const char*) {
    return true;
}
void entryDeinit() {}
const void* getFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entryInit, entryDeinit, getFactory};
