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
     * Id of an object the command created (0 = none). Structured so agents
     * never have to parse it out of the human-readable message
     * (clone_pattern sets this to the new pattern id).
     */
    uint64_t createdId = 0;
};

} // namespace Audio
} // namespace Aestra
