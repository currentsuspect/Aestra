#pragma once

#include "Commands/CommandResult.h"
#include "Commands/MuseGrammar.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

class CommandHistory;
class ICommand;

class CommandParser {
public:
    CommandResult parse(const std::string& input, CommandHistory& history);

    /**
     * @brief Execute a verb from an already-tokenised flag map.
     *
     * The shared back half of parse(): schema lookup, flag validation,
     * registry build, and execution through CommandHistory. MuseService's
     * JSON surface enters here so text and structured requests can never
     * diverge in behaviour.
     */
    CommandResult execute(const std::string& verb,
                          const std::unordered_map<std::string, std::string>& flags,
                          CommandHistory& history);

    /**
     * @brief Validate and build a command without executing it.
     *
     * The front half of execute(): schema lookup, unknown-flag rejection,
     * flag validation, and registry build. Batch execution uses this to
     * validate each member against the current state immediately before
     * executing it. Returns nullptr with outStatus/outMessage set on failure.
     */
    std::unique_ptr<ICommand> buildValidated(
        const std::string& verb,
        const std::unordered_map<std::string, std::string>& flags,
        CommandStatus& outStatus,
        std::string& outMessage);

private:
    std::unordered_map<std::string, std::string> tokeniseFlags(
        const std::vector<std::string>& tokens,
        std::string& outError);
    bool validateFlags(
        const CommandSchema& schema,
        const std::unordered_map<std::string, std::string>& parsed,
        std::string& outError);
    bool convertAndValidateValue(
        const FlagSchema& flag,
        const std::string& rawValue,
        std::string& outError);
};

} // namespace Audio
} // namespace Aestra
