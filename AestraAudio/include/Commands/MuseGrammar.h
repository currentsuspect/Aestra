#pragma once

#include <limits>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

enum class FlagType { String, Int, Float, Bool };
enum class CommandCategory { Transport, Track, Clip, Unit, Pattern };

struct FlagSchema {
    std::string name;
    FlagType type;
    bool required;
    double minValue = std::numeric_limits<double>::quiet_NaN();
    double maxValue = std::numeric_limits<double>::quiet_NaN();
};

struct CommandSchema {
    std::string verb;
    CommandCategory category;
    std::vector<FlagSchema> flags;
};

namespace MuseGrammar {
    const std::vector<CommandSchema>& allCommands();
    /** @brief The full command schema as a JSON string — the agent tool manifest. */
    std::string schemaToJsonString();
    void exportSchemaToJson(const std::string& outputPath);
}

} // namespace Audio
} // namespace Aestra
