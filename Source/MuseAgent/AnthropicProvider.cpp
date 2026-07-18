// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AnthropicProvider.h"

#include "HttpClient.h"

namespace Aestra {
namespace MuseAgent {

ModelResponse AnthropicProvider::complete(const ModelRequest& request) {
    ModelResponse response;

    JSON body = JSON::object();
    body.set("model", JSON(request.model.empty() ? m_model : request.model));
    body.set("max_tokens", JSON(static_cast<double>(request.maxTokens)));
    if (!request.systemPrompt.empty()) {
        body.set("system", JSON(request.systemPrompt));
    }

    if (!request.tools.empty()) {
        JSON tools = JSON::array();
        for (const auto& tool : request.tools) {
            JSON entry = JSON::object();
            entry.set("name", JSON(tool.name));
            entry.set("description", JSON(tool.description));
            entry.set("input_schema", tool.parametersSchema);
            tools.push(entry);
        }
        body.set("tools", tools);
    }

    // Consecutive tool messages must land in ONE user message of tool_result
    // blocks — splitting them degrades the model's parallel tool use.
    JSON messages = JSON::array();
    size_t i = 0;
    while (i < request.messages.size()) {
        const Message& message = request.messages[i];
        if (message.role == "tool") {
            JSON content = JSON::array();
            while (i < request.messages.size() && request.messages[i].role == "tool") {
                const Message& result = request.messages[i];
                JSON block = JSON::object();
                block.set("type", JSON("tool_result"));
                block.set("tool_use_id", JSON(result.toolCallId));
                block.set("content", JSON(result.text));
                if (result.isError) block.set("is_error", JSON(true));
                content.push(block);
                ++i;
            }
            JSON entry = JSON::object();
            entry.set("role", JSON("user"));
            entry.set("content", content);
            messages.push(entry);
            continue;
        }

        JSON entry = JSON::object();
        entry.set("role", JSON(message.role));
        if (message.role == "assistant" && !message.toolCalls.empty()) {
            JSON content = JSON::array();
            if (!message.text.empty()) {
                JSON textBlock = JSON::object();
                textBlock.set("type", JSON("text"));
                textBlock.set("text", JSON(message.text));
                content.push(textBlock);
            }
            for (const auto& call : message.toolCalls) {
                JSON toolUse = JSON::object();
                toolUse.set("type", JSON("tool_use"));
                toolUse.set("id", JSON(call.id));
                toolUse.set("name", JSON(call.name));
                toolUse.set("input", call.arguments);
                content.push(toolUse);
            }
            entry.set("content", content);
        } else {
            entry.set("content", JSON(message.text));
        }
        messages.push(entry);
        ++i;
    }
    body.set("messages", messages);

    const HttpResponse http = httpPostJson(
        m_baseUrl + "/v1/messages",
        {{"x-api-key", m_apiKey}, {"anthropic-version", "2023-06-01"}}, body.toString(),
        request.timeoutSeconds);

    if (!http.error.empty()) {
        response.error = "transport: " + http.error;
        return response;
    }

    JSON parsed;
    try {
        parsed = JSON::parse(http.body);
    } catch (const std::exception& e) {
        response.error = "unparseable response: " + std::string(e.what());
        return response;
    }

    if (http.status != 200) {
        std::string detail = http.body;
        if (parsed.has("error") && parsed["error"].has("message") &&
            parsed["error"]["message"].isString()) {
            detail = parsed["error"]["message"].asString();
        }
        response.error = "HTTP " + std::to_string(http.status) + ": " + detail;
        return response;
    }

    // A 200 with missing or mistyped fields stays inside the error contract —
    // nothing below may throw past complete().
    if (parsed.has("content")) {
        JSON& content = parsed["content"];
        for (size_t blockIndex = 0; blockIndex < content.size(); ++blockIndex) {
            JSON& block = content[blockIndex];
            const std::string type = block.has("type") && block["type"].isString()
                                         ? block["type"].asString()
                                         : "";
            if (type == "text") {
                if (!block.has("text") || !block["text"].isString()) {
                    response.error = "malformed text block (missing string \"text\")";
                    response.stopReason = StopReason::Error;
                    return response;
                }
                response.text += block["text"].asString();
            } else if (type == "tool_use") {
                if (!block.has("id") || !block["id"].isString() || !block.has("name") ||
                    !block["name"].isString() || !block.has("input")) {
                    response.error =
                        "malformed tool_use block (needs string id, string name, input)";
                    response.stopReason = StopReason::Error;
                    return response;
                }
                ToolCall call;
                call.id = block["id"].asString();
                call.name = block["name"].asString();
                call.arguments = block["input"];
                response.toolCalls.push_back(call);
            }
        }
    }

    if (parsed.has("usage")) {
        JSON& usage = parsed["usage"];
        if (usage.has("input_tokens") && usage["input_tokens"].isNumber())
            response.usage.inputTokens = static_cast<long long>(usage["input_tokens"].asNumber());
        if (usage.has("output_tokens") && usage["output_tokens"].isNumber())
            response.usage.outputTokens =
                static_cast<long long>(usage["output_tokens"].asNumber());
    }

    const std::string stop = parsed.has("stop_reason") && parsed["stop_reason"].isString()
                                 ? parsed["stop_reason"].asString()
                                 : "end_turn";
    if (stop == "tool_use") {
        response.stopReason = StopReason::ToolUse;
    } else if (stop == "max_tokens") {
        response.stopReason = StopReason::MaxTokens;
    } else if (stop == "refusal") {
        response.stopReason = StopReason::Refusal;
    } else {
        response.stopReason = StopReason::EndTurn;
    }
    return response;
}

} // namespace MuseAgent
} // namespace Aestra
