#include "Commands/MuseGrammar.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace Aestra {
namespace Audio {
namespace MuseGrammar {

static const std::vector<CommandSchema> s_schemas = {

    // === Transport (3) ===
    {"set_bpm", CommandCategory::Transport, {
        {"value", FlagType::Float, true, 1.0, 999.0}
    },
     "Set the session tempo in BPM."},
    {"play", CommandCategory::Transport, {
    },
     "Start transport playback."},
    {"stop", CommandCategory::Transport, {
    },
     "Stop transport playback."},

    // === Track (8) ===
    // NOTE: no "type" flag until the factory consumes one — advertising a
    // flag the registry ignores would make the schema lie to agents.
    {"add_track", CommandCategory::Track, {
        {"name", FlagType::String, false}
    },
     "Add a mixer track (channel). Tracks are addressed by index in later commands."},
    {"delete_track", CommandCategory::Track, {
        {"track", FlagType::Int, true}
    },
     "Delete the track at this index. Later indexes shift down; ids stay stable."},
    {"rename_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"name", FlagType::String, true}
    },
     "Rename the track at this index."},
    {"mute_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"state", FlagType::Bool, true}
    },
     "Set the mute state of a track."},
    {"solo_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"state", FlagType::Bool, true}
    },
     "Set the solo state of a track."},
    {"set_volume", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"value", FlagType::Float, true, 0.0, 1.0}
    },
     "Set track volume (0..1 linear)."},
    {"set_pan", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"value", FlagType::Float, true, -1.0, 1.0}
    },
     "Set track pan (-1 left .. 1 right)."},

    // === Clip (8) ===
    // Every "id" here is FlagType::Id: the same 32-hex-char string list_clips
    // prints. These used to be Int, which no listed id could satisfy.
    {"add_lane", CommandCategory::Clip, {
        {"name", FlagType::String, false}
    },
     "Create a playlist lane. Clips live on lanes, so a blank project needs one before add_clip."},
    {"add_clip", CommandCategory::Clip, {
        {"track", FlagType::Int, true},
        {"file", FlagType::String, true},
        {"bar", FlagType::Int, true, 1.0}
    },
     "Import an audio file and place it as a playable clip on a lane at a bar position. Decodes the file, registers it as a source and creates its audio pattern; on any failure nothing is added."},
    {"delete_clip", CommandCategory::Clip, {
        {"id", FlagType::Id, true}
    },
     "Delete a clip by id."},
    {"move_clip", CommandCategory::Clip, {
        {"id", FlagType::Id, true},
        {"track", FlagType::Int, true},
        {"start", FlagType::Float, true}
    },
     "Move a clip to a track and start beat."},
    {"duplicate_clip", CommandCategory::Clip, {
        {"id", FlagType::Id, true},
        {"bar", FlagType::Int, true, 1.0}
    },
     "Duplicate a clip so the copy starts at a bar (1-based, 4/4: bar 1 is beat 0), on the source clip's lane."},
    {"trim_clip", CommandCategory::Clip, {
        {"id", FlagType::Id, true},
        {"start", FlagType::Float, true},
        {"end", FlagType::Float, true}
    },
     "Trim a clip to a start/end beat range."},
    {"reverse_clip", CommandCategory::Clip, {
        {"id", FlagType::Id, true}
    },
     "Replace an audio clip's source with a reversed render of itself. Writes a new file; undo restores the original."},
    {"commit_clip_edits", CommandCategory::Clip, {
        {"id", FlagType::Id, true}
    },
     "Bake an audio clip's own gain and fades into a new source file and reset those edits to unity. Clip-local only: this does not render the track's plugins, automation, sends or routing."},
    // === Unit (4) ===
    // "type" accepts: sampler (default), 808. The schema format cannot
    // express enums yet; the factory rejects anything else.
    {"add_unit", CommandCategory::Unit, {
        {"name", FlagType::String, false},
        {"type", FlagType::String, false}
    },
     "Add an Arsenal unit: sampler (polyphonic, default) or 808 (mono with glide). Also creates the unit default MIDI pattern (see list_units.defaultPatternId)."},
    {"load_sample", CommandCategory::Unit, {
        {"unit", FlagType::Int, true, 1.0},
        {"file", FlagType::String, true}
    },
     "Load an audio file into a unit sampler. MIDI pitch 60 plays it unshifted."},
    {"set_unit_gain", CommandCategory::Unit, {
        {"unit", FlagType::Int, true, 1.0},
        {"value", FlagType::Float, true, 0.0, 2.0}
    },
     "Set a unit's linear output gain (1 = unity, applied at its mix point). The per-drum balance knob for multi-unit tracks."},
    {"set_sampler", CommandCategory::Unit, {
        {"unit", FlagType::Int, true, 1.0},
        {"attack", FlagType::Float, false, 0.0, 2.0},
        {"decay", FlagType::Float, false, 0.0, 2.0},
        {"sustain", FlagType::Float, false, 0.0, 1.0},
        {"release", FlagType::Float, false, 0.0, 5.0},
        {"voices", FlagType::Int, false, 1.0, 32.0},
        {"mono", FlagType::Bool, false},
        {"cut_self", FlagType::Bool, false}
    },
     "Set a unit sampler's envelope (seconds; sustain 0..1) and play modes, as one undo step. Only the fields given change. A short release stops a held note when it ends instead of letting it ring into the next hit. list_units shows the current values."},

    // === Pattern (4) ===
    // Notes are identified by (pattern, unit, pitch, start) — the same key
    // the piano-roll note commands match on.
    {"add_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0},
        {"duration", FlagType::Float, true, 0.001},
        {"velocity", FlagType::Float, false, 0.0, 1.0},
        {"pan", FlagType::Float, false, -1.0, 1.0}
    },
     "Add one note. Notes carry the unit they play through; one pattern may hold notes from many units."},
    {"delete_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0}
    },
     "Delete the note identified by (pattern, unit, pitch, start)."},
    {"move_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0},
        {"to_start", FlagType::Float, true, 0.0},
        {"to_pitch", FlagType::Int, false, 0.0, 127.0}
    },
     "Move a note to a new start and optionally a new pitch."},
    {"arrange_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"track", FlagType::Int, true, 0.0},
        {"start", FlagType::Float, true, 0.0}
    },
     "Place a pattern on the timeline as a clip. Arrangement does not change the pattern units' mixer destinations."},
    // steps: one char per step — 'x' hit, 'X' accented hit, '-' or '.' rest.
    // The string defines the ENTIRE row for that (unit, pitch): re-issuing
    // the verb rewrites the groove, an all-rest string clears it.
    {"set_steps", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"steps", FlagType::String, true},
        {"step", FlagType::Float, false, 0.015625, 4.0},
        {"velocity", FlagType::Float, false, 0.0, 1.0},
        {"gate", FlagType::Float, false, 0.05, 1.0},
        {"swing", FlagType::Float, false, 0.0, 0.9}
    },
     "Write one drum row from a step string. The string defines the ENTIRE row for (unit, pitch): re-issuing rewrites it, an all-rest string clears it. Digits 1-9 are hits at velocity n/9; swing delays every second step."},
    {"quantize_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"grid", FlagType::Float, true, 0.015625, 16.0},
        {"strength", FlagType::Float, false, 0.0, 1.0},
        {"unit", FlagType::Int, false, 1.0}
    },
     "Move note starts toward the grid (in beats). strength 1 = snap; omit unit to affect all units."},
    {"transpose_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"semitones", FlagType::Int, true, -48.0, 48.0},
        {"unit", FlagType::Int, false, 1.0}
    },
     "Shift note pitches by semitones. Rejected if any note would leave MIDI range 0..127."},
    {"clone_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0}
    },
     "Duplicate a pattern (notes, name, length). The new pattern id is returned in result.createdId; list_patterns shows it."},
    {"set_pattern_length", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"beats", FlagType::Float, true, 1.0, 512.0}
    },
     "Set the length of a pattern in beats. Playback and arranged clip scheduling use this length."},

    // === Effects (4) ===
    {"add_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, -1.0},
        {"effect", FlagType::String, true},
        {"slot", FlagType::Int, false, 0.0, 9.0}
    },
     "Insert an effect on a track (first empty slot unless given); track -1 is the master channel, for mastering. effect = id or name from list_plugins. result.createdId carries the slot."},
    {"remove_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, -1.0},
        {"slot", FlagType::Int, true, 0.0, 9.0}
    },
     "Remove the effect in a slot of a track's chain."},
    {"bypass_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, -1.0},
        {"slot", FlagType::Int, true, 0.0, 9.0},
        {"state", FlagType::Bool, true}
    },
     "Bypass (true) or re-enable (false) an effect slot."},
    {"set_effect_param", CommandCategory::Track, {
        {"track", FlagType::Int, true, -1.0},
        {"slot", FlagType::Int, true, 0.0, 9.0},
        {"param", FlagType::String, true},
        {"value", FlagType::Float, true, 0.0, 1.0}
    },
     "Set an effect parameter, normalized 0..1. param = name (case-insensitive) or numeric id from get_effects; get_effects shows the resulting display value."},

    // === Mixer routing (V82-M5) === tracks by index as list_tracks shows them; -1 = master
    {"route_unit", CommandCategory::Unit, {
        {"unit", FlagType::Int, true, 1.0},
        {"track", FlagType::Int, true, -1.0}
    },
     "Send an Arsenal unit's audio to a mixer track (by index), or -1 for straight to the master. Its patterns play there wherever they are arranged, so the track's fader, inserts and sends apply."},
    {"route_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"track", FlagType::Int, true, -1.0}
    },
     "Send an audio pattern (an audio clip's source, see list_patterns / list_clips) to a mixer track by index, or -1 for the master. Every clip of that pattern plays through the track's fader, inserts and sends."},
    {"make_clip_unique", CommandCategory::Clip, {
        {"id", FlagType::Id, true}
    },
     "Give a clip its own copy of its pattern, so edits and routing (route_pattern) apply to this clip only; other clips keep the original. list_clips shows the new pattern id. Same as the UI's Make Unique."},
    {"set_output", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"to", FlagType::Int, true, -1.0}
    },
     "Route a track's main output into another track (a bus), or -1 for the master. Refused if it would make a loop."},
    {"add_send", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"to", FlagType::Int, true, 0.0},
        {"level", FlagType::Float, false, 0.0, 2.0},
        {"pan", FlagType::Float, false, -1.0, 1.0},
        {"pre_fader", FlagType::Bool, false},
        {"sidechain", FlagType::Bool, false}
    },
     "Add a send from a track to another track (an FX return or bus). level is linear (1 = unity, default), post-fader unless pre_fader. sidechain=true feeds the target's sidechain input only, not its audible mix. get_routing_graph shows the new sendId. Sends to the master are refused (use set_output); so are loops."},
    {"set_send", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"send", FlagType::Int, true, 1.0},
        {"level", FlagType::Float, false, 0.0, 2.0},
        {"pan", FlagType::Float, false, -1.0, 1.0},
        {"pre_fader", FlagType::Bool, false},
        {"mute", FlagType::Bool, false},
        {"sidechain", FlagType::Bool, false}
    },
     "Change a send by its sendId (from get_routing_graph): level, pan, pre/post fader, mute, sidechain-only. Flags left out keep their value."},
    {"remove_send", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"send", FlagType::Int, true, 1.0}
    },
     "Remove a send by its sendId. Undo puts it back with the same id."},
    {"move_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"from", FlagType::Int, true, 0.0, 9.0},
        {"to", FlagType::Int, true, 0.0, 9.0}
    },
     "Move an insert effect to another slot in the same track's chain (order matters: EQ before or after compression)."},
    {"set_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0},
        {"velocity", FlagType::Float, false, 0.0, 1.0},
        {"pan", FlagType::Float, false, -1.0, 1.0}
    },
     "Update velocity and/or pan of the note identified by (pattern, unit, pitch, start)."}
};

const std::vector<CommandSchema>& allCommands() {
    return s_schemas;
}

std::string schemaToJsonString() {
    std::ostringstream out;
    out << "{\n\"commands\": [\n";
    for (size_t i = 0; i < s_schemas.size(); ++i) {
        const auto& cmd = s_schemas[i];
        out << "  {\n";
        out << "    \"verb\": \"" << cmd.verb << "\",\n";
        if (!cmd.description.empty())
            out << "    \"description\": \"" << cmd.description << "\",\n";

        const char* catStr = "unknown";
        switch (cmd.category) {
        case CommandCategory::Transport: catStr = "transport"; break;
        case CommandCategory::Track: catStr = "track"; break;
        case CommandCategory::Clip: catStr = "clip"; break;
        case CommandCategory::Unit: catStr = "unit"; break;
        case CommandCategory::Pattern: catStr = "pattern"; break;
        }
        out << "    \"category\": \"" << catStr << "\",\n";
        out << "    \"flags\": [\n";

        for (size_t j = 0; j < cmd.flags.size(); ++j) {
            const auto& flag = cmd.flags[j];
            const char* typeStr = "string";
            switch (flag.type) {
            case FlagType::String: typeStr = "string"; break;
            case FlagType::Int: typeStr = "int"; break;
            case FlagType::Float: typeStr = "float"; break;
            case FlagType::Bool: typeStr = "bool"; break;
            case FlagType::Id: typeStr = "id"; break;
            }
            out << "      {\n";
            out << "        \"name\": \"" << flag.name << "\",\n";
            out << "        \"type\": \"" << typeStr << "\",\n";
            out << "        \"required\": " << (flag.required ? "true" : "false");
            if (!std::isnan(flag.minValue))
                out << ",\n        \"min\": " << flag.minValue;
            if (!std::isnan(flag.maxValue))
                out << ",\n        \"max\": " << flag.maxValue;
            out << "\n      }";
            if (j + 1 < cmd.flags.size())
                out << ",";
            out << "\n";
        }

        out << "    ]\n";
        out << "  }";
        if (i + 1 < s_schemas.size())
            out << ",";
        out << "\n";
    }
    out << "],\n";

    // Queries and actions are implemented by MuseService; keep this list in
    // sync with its isQueryVerb()/isActionVerb().
    out << R"("queries": [
  {"verb": "get_transport", "args": "none", "description": "bpm, playing, positionSeconds."},
  {"verb": "list_tracks", "args": "none", "description": "mixer inserts: index, stable id, name, volume, pan, muted, soloed. These are independent from Playlist lanes."},
  {"verb": "list_units", "args": "none", "description": "id, name, type, defaultPatternId, mixerChannelId (0 = Master), legacy timelineLane, samplePath per unit; units with a sampler also carry sampler {attack, decay, sustain, release, voices, mono, cutSelf}."},
  {"verb": "list_clips", "args": "none", "description": "playlist lanes with clips (id, name, startBeat, durationBeats, pattern; pattern 0 = not a pattern clip)."},
  {"verb": "list_patterns", "args": "none", "description": "id, name, lengthBeats, noteCount and type; audio sources also include mixerChannelId."},
  {"verb": "list_plugins", "args": "none", "description": "available effect plugins: id, name, category. Use id or name with add_effect."},
  {"verb": "get_effects", "args": "{\"track\": <index, or -1 for the master>}", "description": "a track's (or the master's) effect chain: slot, id, name, bypassed, and every parameter (id, name, value 0..1, display, unit)."},
  {"verb": "get_meters", "args": "none", "description": "master + per-track meters from the most recently processed audio block: peakDb, rmsDb, lufs, clip flags. Headless this reflects the last render; in-app it is live."},
  {"verb": "get_audio_health", "args": "none", "description": "A read-only snapshot of published engine telemetry: callback timing and budget, xruns/underruns, RT violations, recovery, command-queue drops, scheduling status, signal sanitization, resampling, and patternScheduling (lateRefills, missedHits, maxLateMs: pattern hits skipped because a main-loop refill came after the playhead, a stutter that counts no xrun). Counters have engine-lifetime scope."},
  {"verb": "get_project_load_report", "args": "none", "description": "The currently loaded project's or latest load attempt's format, migration/recovery state, missing plugins/assets, unrestored state, and structured warnings/errors. A blank project or no observed load returns status unobserved."},
  {"verb": "get_routing_graph", "args": "none", "description": "Read-only audio topology: stable Arsenal-unit and audio-pattern sources, Master/mixer destinations, main paths, sends/sidechains, unresolved destinations, and explicit positional identity for insert slots and sends."},
  {"verb": "get_latency_report", "args": "none", "description": "The latest published PDC solve: per-node intrinsic/downstream/path latency, node and edge compensation, graph maximum, uncompensated sidechain paths, solver warnings, pending recalculation, and solved-vs-applied mismatches."},
  {"verb": "list_samples", "args": "{\"dir\": <path>}", "description": "audio files under a directory (recursive, depth 3, max 500): path, name, sizeBytes. Feed paths to load_sample."},
  {"verb": "get_pattern", "args": "{\"pattern\": <id>}", "description": "one pattern with its notes (pitch, start, duration, velocity, pan, unit)."},
  {"verb": "get_session_state", "args": "none", "description": "transport + tracks + laneCount + unitCount + canUndo in one call."},
  {"verb": "get_schema", "args": "none", "description": "this manifest, over the wire — socket clients bootstrap without the --schema flag."}
],
"actions": [
  {"verb": "render_pattern", "args": "{\"pattern\": <id>, \"file\": <path>, \"tail\": <seconds 0..30>}", "description": "Bounce one pattern (Arsenal preview routing) to a float32 WAV. Result carries durationSeconds, frames, sampleRate, peakDb."},
  {"verb": "render_song", "args": "{\"file\": <path>, \"tail\": <seconds 0..30>, \"start\": <beat>, \"end\": <beat>, \"channel\": <mixer channel id>, \"post_fader\": <bool>}", "description": "Bounce the arranged timeline to a float32 WAV through the project's real mix. start/end render only that beat range (start alone runs to the end). channel renders one mixer channel's own output instead of the master: post-insert, post-fader by default (a stem); post_fader false reads before fader and pan. Use these to A/B a section or hear one part in context without touching mute or solo. Errors on an empty timeline. Result carries durationSeconds, frames, sampleRate, peakDb, and startBeat/endBeat/channel when given."},
  {"verb": "verify_render", "args": "{\"file\": <path>, \"tail\": <seconds 0..30>, \"against\": <path>, \"expect_peak_min_db\": <dB>, \"expect_peak_max_db\": <dB>, \"expect_not_silent\": <bool>, \"expect_no_clipping\": <bool>, \"start\": <beat>, \"end\": <beat>, \"channel\": <mixer channel id>, \"post_fader\": <bool>}", "description": "Render the timeline (or the range/channel render_song would) and JUDGE it, instead of leaving you to read peakDb and decide. Renders through the same path as render_song, then applies checks and returns a verdict of \"pass\" or \"fail\" plus every check with its expectation and actual value. By default it fails a silent render (peak at or below -89 dBFS) and a clipping one. 'against' compares the new file byte-for-byte with an earlier render to prove the path is deterministic. The status stays \"ok\" even when the verdict is \"fail\": the verb worked, the audio did not — read the verdict, and do not claim a render is good until it says pass."},
  {"verb": "batch", "args": "{\"commands\": [{\"verb\": ..., \"args\": ...}, ...]}", "description": "Run 1..64 mutation verbs all-or-nothing as a single undo step. Members execute against the state their predecessors produced. A member takes only \"verb\" and \"args\"; any other key is refused."},
  {"verb": "undo", "args": "none", "description": "Step back one entry in the shared undo history (a batch is one entry). Refuses with 'nothing to undo' at the end of the stack rather than reporting a no-op as success. Result carries canUndo, canRedo."},
  {"verb": "redo", "args": "none", "description": "Step forward one entry. Refuses with 'nothing to redo' at the end of the stack. Result carries canUndo, canRedo."}
],
"notes": {
  "samplerPitch": "The sampler root is MIDI pitch 60: notes at 60 play the loaded sample unshifted, other pitches resample relative to 60. Put drum hits at pitch 60; write melodies around 60.",
  "unitTypes": "sampler = polyphonic sampler; 808 = mono pitched sampler with glide.",
  "patterns": "Every non-audio unit gets a default MIDI pattern at creation (list_units.defaultPatternId). A pattern is just a container: notes carry their unit, so one pattern can hold a whole multi-unit groove.",
  "routing": "Each unit has one stable mixer destination (mixerChannelId; 0 = Master); route_unit changes it. Timeline arrangement is independent, so the same pattern may be placed on multiple lanes without changing its audio destination. Mixer verbs address tracks by index (list_tracks); -1 means the master. Sends are addressed by the sendId get_routing_graph shows.",
  "steps": "Step strings: 'x' hit at the row velocity, 'X' accented hit (+0.2), digits '1'-'9' hit at velocity n/9, '-', '.', ' ' rest. Default step is 0.25 beats (16ths). swing (0..0.9) delays every second step by swing * step/2 for shuffle feels.",
  "ids": "Track, unit, pattern and clip ids are stable across edits; indexes shift when items are deleted.",
  "units_of_measure": "velocity 0..1, pan -1..1, volume 0..1 linear, positions and durations in beats.",
  "effects": "Effect parameters are normalized 0..1; get_effects shows the human display value after a set. A track chain has 10 slots.",
  "undo": "Every mutation and every batch is one step in the same undo history the UI uses."
}
}
)";
    return out.str();
}

void exportSchemaToJson(const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out.is_open())
        return;
    out << schemaToJsonString();
}

} // namespace MuseGrammar
} // namespace Audio
} // namespace Aestra
