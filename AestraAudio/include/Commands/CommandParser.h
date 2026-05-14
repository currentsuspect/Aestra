#pragma once

#include "Commands/CommandResult.h"
#include "Commands/MuseGrammar.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

class CommandHistory;

class CommandParser {
public:
    CommandResult parse(const std::string& input, CommandHistory& history);

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
