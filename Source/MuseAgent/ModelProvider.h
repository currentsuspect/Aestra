// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The deliberately boring provider interface: everything provider-specific
// (HTTP, auth, streaming formats, tool-call syntax, token accounting) stays
// behind adapters. Muse owns the musical system; the model only proposes
// intent through these shapes.
#pragma once

#include "AestraJSON.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace MuseAgent {

struct ToolDefinition {
    std::string name;
    std::string description;
    JSON parametersSchema; // JSON Schema object for the tool arguments
};

struct ToolCall {
    std::string id;   // provider-assigned call id, echoed in the tool result
    std::string name;
    JSON arguments;   // parsed arguments object
};

struct Usage {
    long long inputTokens = 0;
    long long outputTokens = 0;
};

enum class StopReason {
    ToolUse,  // the model wants tool results before continuing
    EndTurn,  // the model finished its turn
    MaxTokens,
    Refusal,
    Error,
};

/**
 * @brief One conversation turn in provider-neutral form.
 *
 * Roles: "user" (text), "assistant" (text and/or toolCalls), "tool" (one
 * tool result; toolCallId links it to the assistant call). Adapters map
 * these onto each provider's wire format — e.g. the Anthropic adapter
 * groups consecutive tool messages into a single user message of
 * tool_result blocks, as that API requires.
 */
struct Message {
    std::string role;
    std::string text;
    std::vector<ToolCall> toolCalls; // assistant only
    std::string toolCallId;          // tool role only
    std::string toolName;            // tool role only
    bool isError = false;            // tool role only
};

struct ModelRequest {
    std::string model;
    std::string systemPrompt;
    std::vector<Message> messages;
    std::vector<ToolDefinition> tools;
    int maxTokens = 16000;
};

struct ModelResponse {
    std::vector<ToolCall> toolCalls;
    std::string text;
    Usage usage;
    StopReason stopReason = StopReason::Error;
    std::string error; // set when stopReason == Error
};

class ModelProvider {
public:
    virtual ~ModelProvider() = default;
    virtual ModelResponse complete(const ModelRequest& request) = 0;
    virtual std::string name() const = 0;
};

} // namespace MuseAgent
} // namespace Aestra
