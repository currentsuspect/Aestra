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

    // === Clip (5) ===
    {"add_clip", CommandCategory::Clip, {
        {"track", FlagType::Int, true},
        {"file", FlagType::String, true},
        {"bar", FlagType::Int, true}
    },
     "Add a file-based clip on a track at a bar position."},
    {"delete_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true}
    },
     "Delete a clip by id."},
    {"move_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"track", FlagType::Int, true},
        {"start", FlagType::Float, true}
    },
     "Move a clip to a track and start beat."},
    {"duplicate_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"bar", FlagType::Int, true}
    },
     "Duplicate a clip to a bar position."},
    {"trim_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"start", FlagType::Float, true},
        {"end", FlagType::Float, true}
    },
     "Trim a clip to a start/end beat range."},

    // === Unit (2) ===
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
     "Place a pattern on the timeline as a clip, routing its units to that track. A unit routes to at most one track; conflicts are rejected."},
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
        {"track", FlagType::Int, true, 0.0},
        {"effect", FlagType::String, true},
        {"slot", FlagType::Int, false, 0.0, 9.0}
    },
     "Insert an effect on a track (first empty slot unless given). effect = id or name from list_plugins. result.createdId carries the slot."},
    {"remove_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"slot", FlagType::Int, true, 0.0, 9.0}
    },
     "Remove the effect in a slot of a track's chain."},
    {"bypass_effect", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"slot", FlagType::Int, true, 0.0, 9.0},
        {"state", FlagType::Bool, true}
    },
     "Bypass (true) or re-enable (false) an effect slot."},
    {"set_effect_param", CommandCategory::Track, {
        {"track", FlagType::Int, true, 0.0},
        {"slot", FlagType::Int, true, 0.0, 9.0},
        {"param", FlagType::String, true},
        {"value", FlagType::Float, true, 0.0, 1.0}
    },
     "Set an effect parameter, normalized 0..1. param = name (case-insensitive) or numeric id from get_effects; get_effects shows the resulting display value."},
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
  {"verb": "list_tracks", "args": "none", "description": "index, stable id, name, volume, pan, muted, soloed per track."},
  {"verb": "list_units", "args": "none", "description": "id, name, type, defaultPatternId, timelineLane (-1 = preview), samplePath per unit."},
  {"verb": "list_clips", "args": "none", "description": "playlist lanes with clips (id, name, startBeat, durationBeats, pattern; pattern 0 = not a pattern clip)."},
  {"verb": "list_patterns", "args": "none", "description": "id, name, lengthBeats, noteCount, type per pattern."},
  {"verb": "list_plugins", "args": "none", "description": "available effect plugins: id, name, category. Use id or name with add_effect."},
  {"verb": "get_effects", "args": "{\"track\": <index>}", "description": "a track's effect chain: slot, id, name, bypassed, and every parameter (id, name, value 0..1, display, unit)."},
  {"verb": "get_meters", "args": "none", "description": "master + per-track meters from the most recently processed audio block: peakDb, rmsDb, lufs, clip flags. Headless this reflects the last render; in-app it is live."},
  {"verb": "list_samples", "args": "{\"dir\": <path>}", "description": "audio files under a directory (recursive, depth 3, max 500): path, name, sizeBytes. Feed paths to load_sample."},
  {"verb": "get_pattern", "args": "{\"pattern\": <id>}", "description": "one pattern with its notes (pitch, start, duration, velocity, pan, unit)."},
  {"verb": "get_session_state", "args": "none", "description": "transport + tracks + laneCount + unitCount + canUndo in one call."}
],
"actions": [
  {"verb": "render_pattern", "args": "{\"pattern\": <id>, \"file\": <path>, \"tail\": <seconds 0..30>}", "description": "Bounce one pattern (Arsenal preview routing) to a float32 WAV. Result carries durationSeconds, frames, sampleRate, peakDb."},
  {"verb": "render_song", "args": "{\"file\": <path>, \"tail\": <seconds 0..30>}", "description": "Bounce the arranged timeline to a float32 WAV. Errors on an empty timeline. Result carries durationSeconds, frames, sampleRate, peakDb."},
  {"verb": "batch", "args": "{\"commands\": [{\"verb\": ..., \"args\": ...}, ...]}", "description": "Run 1..64 mutation verbs all-or-nothing as a single undo step. Members execute against the state their predecessors produced."}
],
"notes": {
  "samplerPitch": "The sampler root is MIDI pitch 60: notes at 60 play the loaded sample unshifted, other pitches resample relative to 60. Put drum hits at pitch 60; write melodies around 60.",
  "unitTypes": "sampler = polyphonic sampler; 808 = mono pitched sampler with glide.",
  "patterns": "Every non-audio unit gets a default MIDI pattern at creation (list_units.defaultPatternId). A pattern is just a container: notes carry their unit, so one pattern can hold a whole multi-unit groove.",
  "routing": "A unit routes to at most one timeline track. arrange_pattern routes the pattern's units to its track and rejects conflicting arrangements.",
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
