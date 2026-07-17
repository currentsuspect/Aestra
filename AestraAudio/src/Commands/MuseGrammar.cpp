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
    }},
    {"play", CommandCategory::Transport, {
    }},
    {"stop", CommandCategory::Transport, {
    }},

    // === Track (8) ===
    // NOTE: no "type" flag until the factory consumes one — advertising a
    // flag the registry ignores would make the schema lie to agents.
    {"add_track", CommandCategory::Track, {
        {"name", FlagType::String, false}
    }},
    {"delete_track", CommandCategory::Track, {
        {"track", FlagType::Int, true}
    }},
    {"rename_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"name", FlagType::String, true}
    }},
    {"mute_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"state", FlagType::Bool, true}
    }},
    {"solo_track", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"state", FlagType::Bool, true}
    }},
    {"set_volume", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"value", FlagType::Float, true, 0.0, 1.0}
    }},
    {"set_pan", CommandCategory::Track, {
        {"track", FlagType::Int, true},
        {"value", FlagType::Float, true, -1.0, 1.0}
    }},

    // === Clip (5) ===
    {"add_clip", CommandCategory::Clip, {
        {"track", FlagType::Int, true},
        {"file", FlagType::String, true},
        {"bar", FlagType::Int, true}
    }},
    {"delete_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true}
    }},
    {"move_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"track", FlagType::Int, true},
        {"start", FlagType::Float, true}
    }},
    {"duplicate_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"bar", FlagType::Int, true}
    }},
    {"trim_clip", CommandCategory::Clip, {
        {"id", FlagType::Int, true},
        {"start", FlagType::Float, true},
        {"end", FlagType::Float, true}
    }},

    // === Unit (2) ===
    // "type" accepts: sampler (default), 808. The schema format cannot
    // express enums yet; the factory rejects anything else.
    {"add_unit", CommandCategory::Unit, {
        {"name", FlagType::String, false},
        {"type", FlagType::String, false}
    }},
    {"load_sample", CommandCategory::Unit, {
        {"unit", FlagType::Int, true, 1.0},
        {"file", FlagType::String, true}
    }},

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
    }},
    {"delete_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0}
    }},
    {"move_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0},
        {"to_start", FlagType::Float, true, 0.0},
        {"to_pitch", FlagType::Int, false, 0.0, 127.0}
    }},
    {"arrange_pattern", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"track", FlagType::Int, true, 0.0},
        {"start", FlagType::Float, true, 0.0}
    }},
    {"set_note", CommandCategory::Pattern, {
        {"pattern", FlagType::Int, true, 1.0},
        {"unit", FlagType::Int, true, 1.0},
        {"pitch", FlagType::Int, true, 0.0, 127.0},
        {"start", FlagType::Float, true, 0.0},
        {"velocity", FlagType::Float, false, 0.0, 1.0},
        {"pan", FlagType::Float, false, -1.0, 1.0}
    }}
};

const std::vector<CommandSchema>& allCommands() {
    return s_schemas;
}

std::string schemaToJsonString() {
    std::ostringstream out;
    out << "[\n";
    for (size_t i = 0; i < s_schemas.size(); ++i) {
        const auto& cmd = s_schemas[i];
        out << "  {\n";
        out << "    \"verb\": \"" << cmd.verb << "\",\n";

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
    out << "]\n";
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
