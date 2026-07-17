#include "Commands/CommandParser.h"
#include "Commands/CommandHistory.h"
#include "Commands/CommandRegistry.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace Aestra {
namespace Audio {

static std::vector<std::string> splitWhitespace(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream stream(input);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

CommandResult CommandParser::parse(const std::string& input, CommandHistory& history) {
    CommandResult result;
    result.commandId = 0;

    auto startTime = std::chrono::steady_clock::now();

    // 1. Split input on whitespace, first token = verb
    std::vector<std::string> tokens = splitWhitespace(input);
    if (tokens.empty()) {
        result.status = CommandStatus::ParseError;
        result.message = "empty input";
        result.executionMs = 0.0;
        return result;
    }

    result.verb = tokens[0];
    tokens.erase(tokens.begin());

    // 2. Tokenise remaining tokens into {"flag": "value"} map
    std::string tokeniseError;
    auto parsedFlags = tokeniseFlags(tokens, tokeniseError);
    if (!tokeniseError.empty()) {
        result.status = CommandStatus::ParseError;
        result.message = std::move(tokeniseError);
        auto endTime = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    // 3. Shared back half: schema lookup, validation, build, execute
    CommandResult executed = execute(result.verb, parsedFlags, history);
    auto endTime = std::chrono::steady_clock::now();
    executed.executionMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    return executed;
}

std::unique_ptr<ICommand> CommandParser::buildValidated(
    const std::string& verb,
    const std::unordered_map<std::string, std::string>& flags,
    CommandStatus& outStatus,
    std::string& outMessage) {
    // Look up verb in MuseGrammar::allCommands()
    const auto& allCmds = MuseGrammar::allCommands();
    const CommandSchema* schema = nullptr;
    for (const auto& cmd : allCmds) {
        if (cmd.verb == verb) {
            schema = &cmd;
            break;
        }
    }

    if (!schema) {
        outStatus = CommandStatus::ParseError;
        outMessage = "unknown command: " + verb;
        return nullptr;
    }

    // Reject flags the schema does not define — accepting them would let a
    // caller believe intent was honoured when it was silently dropped.
    for (const auto& entry : flags) {
        bool known = false;
        for (const auto& flagSchema : schema->flags) {
            if (flagSchema.name == entry.first) {
                known = true;
                break;
            }
        }
        if (!known) {
            outStatus = CommandStatus::ValidationError;
            outMessage = "unknown flag for " + verb + ": --" + entry.first;
            return nullptr;
        }
    }

    // Validate required flags present and values within type + range
    std::string validationError;
    if (!validateFlags(*schema, flags, validationError)) {
        outStatus = CommandStatus::ValidationError;
        outMessage = std::move(validationError);
        return nullptr;
    }

    // Build ICommand via registry
    auto cmd = CommandRegistry::instance().build(verb, flags);
    if (!cmd) {
        outStatus = CommandStatus::ExecutionError;
        outMessage = "failed to build command for: " + verb;
        return nullptr;
    }

    outStatus = CommandStatus::Success;
    outMessage.clear();
    return cmd;
}

CommandResult CommandParser::execute(const std::string& verb,
                                     const std::unordered_map<std::string, std::string>& flags,
                                     CommandHistory& history) {
    CommandResult result;
    result.commandId = 0;
    result.verb = verb;

    auto startTime = std::chrono::steady_clock::now();
    const auto finish = [&](CommandResult& r) -> CommandResult& {
        auto endTime = std::chrono::steady_clock::now();
        r.executionMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return r;
    };

    CommandStatus buildStatus = CommandStatus::Success;
    std::string buildMessage;
    auto cmd = buildValidated(verb, flags, buildStatus, buildMessage);
    if (!cmd) {
        result.status = buildStatus;
        result.message = std::move(buildMessage);
        return finish(result);
    }

    const bool undoable = cmd->changesProjectState();

    // Execute through CommandHistory; only a successful execution is undoable.
    try {
        auto shared = std::shared_ptr<ICommand>(std::move(cmd));
        history.pushAndExecute(shared);
        result.undoable = undoable;
    } catch (const std::exception& e) {
        result.status = CommandStatus::ExecutionError;
        result.message = e.what();
        return finish(result);
    }

    result.status = CommandStatus::Success;
    result.message = "ok";
    return finish(result);
}

std::unordered_map<std::string, std::string> CommandParser::tokeniseFlags(
    const std::vector<std::string>& tokens,
    std::string& outError)
{
    std::unordered_map<std::string, std::string> result;
    size_t i = 0;
    while (i < tokens.size()) {
        const std::string& token = tokens[i];

        if (token.size() < 3 || token[0] != '-' || token[1] != '-') {
            outError = "expected flag (e.g. --name), got: " + token;
            return {};
        }

        std::string key = token.substr(2);
        ++i;

        if (i >= tokens.size()) {
            outError = "flag --" + key + " requires a value";
            return {};
        }

        result[key] = tokens[i];
        ++i;
    }
    return result;
}

bool CommandParser::validateFlags(
    const CommandSchema& schema,
    const std::unordered_map<std::string, std::string>& parsed,
    std::string& outError)
{
    for (const auto& flag : schema.flags) {
        auto it = parsed.find(flag.name);

        if (flag.required && it == parsed.end()) {
            outError = "missing required flag: --" + flag.name;
            return false;
        }

        if (it == parsed.end())
            continue;

        if (!convertAndValidateValue(flag, it->second, outError))
            return false;
    }

    // Warn about unknown flags (not an error, but they'll be ignored)
    for (const auto& kv : parsed) {
        bool found = false;
        for (const auto& flag : schema.flags) {
            if (flag.name == kv.first) {
                found = true;
                break;
            }
        }
        if (!found) {
            // Unknown flags are silently ignored in v1
        }
    }

    return true;
}

bool CommandParser::convertAndValidateValue(
    const FlagSchema& flag,
    const std::string& rawValue,
    std::string& outError)
{
    switch (flag.type) {
    case FlagType::String:
        return true;

    case FlagType::Int: {
        char* end = nullptr;
        long val = std::strtol(rawValue.c_str(), &end, 10);
        if (end == rawValue.c_str() || *end != '\0') {
            outError = "flag --" + flag.name + " requires an integer, got: " + rawValue;
            return false;
        }
        if (!std::isnan(flag.minValue) && val < static_cast<long>(flag.minValue)) {
            outError = "flag --" + flag.name + " minimum is " + std::to_string(flag.minValue) + ", got: " + rawValue;
            return false;
        }
        if (!std::isnan(flag.maxValue) && val > static_cast<long>(flag.maxValue)) {
            outError = "flag --" + flag.name + " maximum is " + std::to_string(flag.maxValue) + ", got: " + rawValue;
            return false;
        }
        return true;
    }

    case FlagType::Float: {
        char* end = nullptr;
        double val = std::strtod(rawValue.c_str(), &end);
        if (end == rawValue.c_str() || *end != '\0') {
            outError = "flag --" + flag.name + " requires a number, got: " + rawValue;
            return false;
        }
        if (!std::isnan(flag.minValue) && val < flag.minValue) {
            outError = "flag --" + flag.name + " minimum is " + std::to_string(flag.minValue) + ", got: " + rawValue;
            return false;
        }
        if (!std::isnan(flag.maxValue) && val > flag.maxValue) {
            outError = "flag --" + flag.name + " maximum is " + std::to_string(flag.maxValue) + ", got: " + rawValue;
            return false;
        }
        return true;
    }

    case FlagType::Bool: {
        std::string lower;
        lower.reserve(rawValue.size());
        for (char c : rawValue) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lower != "true" && lower != "false" && lower != "1" && lower != "0" && lower != "yes" && lower != "no") {
            outError = "flag --" + flag.name + " requires a boolean (true/false/1/0), got: " + rawValue;
            return false;
        }
        return true;
    }
    }

    return true;
}

} // namespace Audio
} // namespace Aestra
