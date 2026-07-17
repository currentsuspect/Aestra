#include "Commands/MuseGrammar.h"

#include <cmath>
#include <fstream>

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
    }}
};

const std::vector<CommandSchema>& allCommands() {
    return s_schemas;
}

void exportSchemaToJson(const std::string& outputPath) {
    std::ofstream out(outputPath);
    if (!out.is_open())
        return;

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
}

} // namespace MuseGrammar
} // namespace Audio
} // namespace Aestra
