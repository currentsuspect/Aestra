// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "OpenAICompatProvider.h"

#include "HttpClient.h"

namespace Aestra {
namespace MuseAgent {

ModelResponse OpenAICompatProvider::complete(const ModelRequest& request) {
    ModelResponse response;

    JSON body = JSON::object();
    body.set("model", JSON(request.model.empty() ? m_model : request.model));
    body.set("max_tokens", JSON(static_cast<double>(request.maxTokens)));

    if (!request.tools.empty()) {
        JSON tools = JSON::array();
        for (const auto& tool : request.tools) {
            JSON function = JSON::object();
            function.set("name", JSON(tool.name));
            function.set("description", JSON(tool.description));
            function.set("parameters", tool.parametersSchema);
            JSON entry = JSON::object();
            entry.set("type", JSON("function"));
            entry.set("function", function);
            tools.push(entry);
        }
        body.set("tools", tools);
    }

    JSON messages = JSON::array();
    if (!request.systemPrompt.empty()) {
        JSON systemEntry = JSON::object();
        systemEntry.set("role", JSON("system"));
        systemEntry.set("content", JSON(request.systemPrompt));
        messages.push(systemEntry);
    }
    for (const auto& message : request.messages) {
        JSON entry = JSON::object();
        if (message.role == "tool") {
            entry.set("role", JSON("tool"));
            entry.set("tool_call_id", JSON(message.toolCallId));
            entry.set("content", JSON(message.text));
        } else if (message.role == "assistant" && !message.toolCalls.empty()) {
            entry.set("role", JSON("assistant"));
            entry.set("content", JSON(message.text));
            JSON calls = JSON::array();
            for (const auto& call : message.toolCalls) {
                JSON function = JSON::object();
                function.set("name", JSON(call.name));
                // OpenAI wire format carries arguments as a JSON *string*.
                JSON arguments = call.arguments;
                function.set("arguments", JSON(arguments.toString()));
                JSON callEntry = JSON::object();
                callEntry.set("id", JSON(call.id));
                callEntry.set("type", JSON("function"));
                callEntry.set("function", function);
                calls.push(callEntry);
            }
            entry.set("tool_calls", calls);
        } else {
            entry.set("role", JSON(message.role));
            entry.set("content", JSON(message.text));
        }
        messages.push(entry);
    }
    body.set("messages", messages);

    std::vector<std::pair<std::string, std::string>> headers;
    if (!m_apiKey.empty()) {
        headers.emplace_back("Authorization", "Bearer " + m_apiKey);
    }

    const HttpResponse http = httpPostJson(m_baseUrl + "/v1/chat/completions", headers,
                                           body.toString(), request.timeoutSeconds);

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

    if (!parsed.has("choices") || parsed["choices"].size() == 0) {
        response.error = "response carried no choices";
        return response;
    }
    JSON& choice = parsed["choices"][0];
    if (!choice.has("message")) {
        response.error = "malformed choice (missing \"message\")";
        return response;
    }
    JSON& message = choice["message"];
    if (message.has("content") && message["content"].isString()) {
        response.text = message["content"].asString();
    }
    if (message.has("tool_calls")) {
        JSON& calls = message["tool_calls"];
        for (size_t i = 0; i < calls.size(); ++i) {
            JSON& callEntry = calls[i];
            ToolCall call;
            call.id = callEntry.has("id") && callEntry["id"].isString()
                          ? callEntry["id"].asString()
                          : "call_" + std::to_string(i);
            if (!callEntry.has("function") || !callEntry["function"].has("name") ||
                !callEntry["function"]["name"].isString()) {
                response.error = "malformed tool call (missing function name)";
                response.stopReason = StopReason::Error;
                return response;
            }
            JSON& function = callEntry["function"];
            call.name = function["name"].asString();
            // Arguments arrive as a JSON *string*. Anything else — missing,
            // non-string, or unparseable — must not silently become an
            // empty-argument call; reject the response so the loop reports
            // the provider, not a phantom Muse edit.
            if (!function.has("arguments") || !function["arguments"].isString()) {
                response.error = "tool call \"" + call.name +
                                 "\" carried missing or non-string arguments";
                response.stopReason = StopReason::Error;
                return response;
            }
            try {
                call.arguments = JSON::parse(function["arguments"].asString());
            } catch (const std::exception& e) {
                response.error = "tool call \"" + call.name +
                                 "\" carried unparseable arguments: " + e.what();
                response.stopReason = StopReason::Error;
                return response;
            }
            response.toolCalls.push_back(call);
        }
    }

    if (parsed.has("usage")) {
        JSON& usage = parsed["usage"];
        if (usage.has("prompt_tokens") && usage["prompt_tokens"].isNumber())
            response.usage.inputTokens =
                static_cast<long long>(usage["prompt_tokens"].asNumber());
        if (usage.has("completion_tokens") && usage["completion_tokens"].isNumber())
            response.usage.outputTokens =
                static_cast<long long>(usage["completion_tokens"].asNumber());
    }

    const std::string finish = choice.has("finish_reason") && choice["finish_reason"].isString()
                                   ? choice["finish_reason"].asString()
                                   : "stop";
    if (finish == "tool_calls" || !response.toolCalls.empty()) {
        response.stopReason = StopReason::ToolUse;
    } else if (finish == "length") {
        response.stopReason = StopReason::MaxTokens;
    } else {
        response.stopReason = StopReason::EndTurn;
    }
    return response;
}

} // namespace MuseAgent
} // namespace Aestra
