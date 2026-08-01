#pragma once

#include <limits>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * Id is the canonical 32-hex-char object identifier that queries emit
 * (AestraUUID::toString). It exists so an id an agent reads back from
 * list_clips can be passed to a verb unchanged — the one representation, in
 * both directions. Int is for indexes and counts, never for object identity.
 */
enum class FlagType { String, Int, Float, Bool, Id };
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
    /** One-line semantics for agents; rendered into the schema manifest. */
    std::string description;
};

namespace MuseGrammar {
    const std::vector<CommandSchema>& allCommands();
    /**
     * @brief The full agent tool manifest as a JSON string.
     *
     * An object with "commands" (mutations from allCommands()), "queries" and
     * "actions" (documented here, implemented by MuseService — keep in sync
     * with its isQueryVerb/isActionVerb), and "notes" (engine semantics an
     * agent cannot discover through the protocol: sampler root, unit routing,
     * step characters, id stability).
     */
    std::string schemaToJsonString();
    void exportSchemaToJson(const std::string& outputPath);
}

} // namespace Audio
} // namespace Aestra
