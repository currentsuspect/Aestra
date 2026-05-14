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
};

} // namespace Audio
} // namespace Aestra
