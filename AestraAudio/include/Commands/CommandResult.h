#pragma once

#include <cstdint>
#include <string>

namespace Aestra {
namespace Audio {

enum class CommandStatus {
    Success,
    ParseError,
    ValidationError,
    ExecutionError
};

struct CommandResult {
    CommandStatus status;
    std::string verb;
    std::string message;
    uint64_t commandId = 0;
    double executionMs = 0.0;
    bool undoable = false;
    /**
     * Id of an object the command created, valid when hasCreatedId is true.
     * Structured so agents never have to parse it out of the human-readable
     * message (clone_pattern: new pattern id; add_effect: slot index, where
     * 0 is a legitimate value — hence the explicit flag).
     */
    uint64_t createdId = 0;
    bool hasCreatedId = false;
    /**
     * The same, for objects addressed by a UUID (clips, lanes): the exact
     * string list_clips prints, so it can be passed straight back. When set,
     * it is what the response carries as createdId.
     */
    std::string createdIdText;
    /** What kind of object createdId names: "unit", "track", "lane", "clip", "pattern", "slot". */
    std::string createdKind;
    /**
     * What a value-setting verb left behind, as the plugin displays it — e.g.
     * set_effect_param's "3.5kHz". Lets a caller confirm the value that landed
     * (after unit conversion, rounding or a clamp) without a follow-up query.
     */
    std::string displayText;
};

} // namespace Audio
} // namespace Aestra
