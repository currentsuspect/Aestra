// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The sandbox helper links no CLAP SDK: it carries a hand-copied mirror of the parts of the ABI it speaks
// (ClapAbiMirror.h). A mirror that drifts from the real headers does not fail to compile, it calls the wrong function or
// reads the wrong field. This pins every mirrored struct against the SDK's own, size and member offsets, so a drift is a
// build failure instead of a plugin that silently ignores parameters (the clap.params copy once had a field too many).

#include "ClapAbiMirror.h"

#include <clap/clap.h>

#include <cstddef>
#include <iostream>

namespace M = AestraHelper::ClapAbi;

#define SAME_SIZE(Mirror, Real) static_assert(sizeof(M::Mirror) == sizeof(Real), #Mirror " size differs from " #Real)
#define SAME_OFFSET(Mirror, mf, Real, rf) \
    static_assert(offsetof(M::Mirror, mf) == offsetof(Real, rf), #Mirror "::" #mf " offset differs from " #Real "::" #rf)

SAME_SIZE(ClapVersion, clap_version_t);
SAME_SIZE(ClapPluginDescriptor, clap_plugin_descriptor_t);
SAME_SIZE(ClapHost, clap_host_t);
SAME_SIZE(ClapPlugin, clap_plugin_t);
SAME_SIZE(ClapPluginFactory, clap_plugin_factory_t);
SAME_SIZE(ClapPluginEntry, clap_plugin_entry_t);
SAME_SIZE(ClapProcess, clap_process_t);
SAME_SIZE(ClapAudioBuffer, clap_audio_buffer_t);
SAME_SIZE(ClapEventHeader, clap_event_header_t);
SAME_SIZE(ClapEventParamValue, clap_event_param_value_t);
SAME_SIZE(ClapEventNote, clap_event_note_t);
SAME_SIZE(ClapEventMidi, clap_event_midi_t);
SAME_SIZE(ClapInputEvents, clap_input_events_t);
SAME_SIZE(ClapOutputEvents, clap_output_events_t);
SAME_SIZE(ClapNotePortInfo, clap_note_port_info_t);
SAME_SIZE(ClapPluginNotePorts, clap_plugin_note_ports_t);
SAME_SIZE(ClapPluginLatency, clap_plugin_latency_t);
SAME_SIZE(ClapPluginState, clap_plugin_state_t);
SAME_SIZE(ClapParamInfo, clap_param_info_t);
SAME_SIZE(ClapPluginParams, clap_plugin_params_t);
SAME_SIZE(ClapHostParams, clap_host_params_t);
SAME_SIZE(ClapHostThreadCheck, clap_host_thread_check_t);
SAME_SIZE(ClapHostLatency, clap_host_latency_t);
SAME_SIZE(ClapHostState, clap_host_state_t);
SAME_SIZE(ClapWindow, clap_window_t);
SAME_SIZE(ClapGuiResizeHints, clap_gui_resize_hints_t);
SAME_SIZE(ClapPluginGui, clap_plugin_gui_t);
SAME_SIZE(ClapHostGui, clap_host_gui_t);
SAME_SIZE(ClapPluginTimerSupport, clap_plugin_timer_support_t);
SAME_SIZE(ClapHostTimerSupport, clap_host_timer_support_t);
SAME_SIZE(ClapPluginPosixFdSupport, clap_plugin_posix_fd_support_t);
SAME_SIZE(ClapHostPosixFdSupport, clap_host_posix_fd_support_t);

// Function-pointer tables: where each entry the helper calls actually sits.
SAME_OFFSET(ClapPluginParams, count, clap_plugin_params_t, count);
SAME_OFFSET(ClapPluginParams, getInfo, clap_plugin_params_t, get_info);
SAME_OFFSET(ClapPluginParams, getValue, clap_plugin_params_t, get_value);
SAME_OFFSET(ClapPluginParams, valueToText, clap_plugin_params_t, value_to_text);
SAME_OFFSET(ClapPluginParams, textToValue, clap_plugin_params_t, text_to_value);
SAME_OFFSET(ClapPluginParams, flush, clap_plugin_params_t, flush);
SAME_OFFSET(ClapParamInfo, cookie, clap_param_info_t, cookie);
SAME_OFFSET(ClapParamInfo, name, clap_param_info_t, name);
SAME_OFFSET(ClapParamInfo, module, clap_param_info_t, module);
SAME_OFFSET(ClapParamInfo, minValue, clap_param_info_t, min_value);
SAME_OFFSET(ClapParamInfo, maxValue, clap_param_info_t, max_value);
SAME_OFFSET(ClapParamInfo, defaultValue, clap_param_info_t, default_value);
SAME_OFFSET(ClapEventParamValue, paramId, clap_event_param_value_t, param_id);
SAME_OFFSET(ClapEventParamValue, value, clap_event_param_value_t, value);
SAME_OFFSET(ClapPluginGui, create, clap_plugin_gui_t, create);
SAME_OFFSET(ClapPluginGui, getSize, clap_plugin_gui_t, get_size);
SAME_OFFSET(ClapPluginGui, canResize, clap_plugin_gui_t, can_resize);
SAME_OFFSET(ClapPluginGui, setSize, clap_plugin_gui_t, set_size);
SAME_OFFSET(ClapPluginGui, setParent, clap_plugin_gui_t, set_parent);
SAME_OFFSET(ClapPluginGui, show, clap_plugin_gui_t, show);
SAME_OFFSET(ClapPluginGui, hide, clap_plugin_gui_t, hide);
SAME_OFFSET(ClapHostGui, requestResize, clap_host_gui_t, request_resize);
SAME_OFFSET(ClapHostTimerSupport, unregisterTimer, clap_host_timer_support_t, unregister_timer);
SAME_OFFSET(ClapHostPosixFdSupport, unregisterFd, clap_host_posix_fd_support_t, unregister_fd);
SAME_OFFSET(ClapHostParams, requestFlush, clap_host_params_t, request_flush);
SAME_OFFSET(ClapHost, requestCallback, clap_host_t, request_callback);
SAME_OFFSET(ClapPlugin, onMainThread, clap_plugin_t, on_main_thread);
SAME_OFFSET(ClapProcess, outEvents, clap_process_t, out_events);

static_assert(M::kClapParamIsStepped == CLAP_PARAM_IS_STEPPED && M::kClapParamIsHidden == CLAP_PARAM_IS_HIDDEN &&
                  M::kClapParamIsReadOnly == CLAP_PARAM_IS_READONLY && M::kClapParamIsBypass == CLAP_PARAM_IS_BYPASS &&
                  M::kClapParamIsAutomatable == CLAP_PARAM_IS_AUTOMATABLE,
              "parameter flag values differ from the SDK");
static_assert(M::kClapEventParamValue == CLAP_EVENT_PARAM_VALUE && M::kClapEventNoteOn == CLAP_EVENT_NOTE_ON &&
                  M::kClapEventNoteOff == CLAP_EVENT_NOTE_OFF && M::kClapEventMidi == CLAP_EVENT_MIDI,
              "event type ids differ from the SDK");

int main() {
    std::cout << "ClapAbiMirrorLayoutTest passed (checked at compile time)\n";
    return 0;
}
