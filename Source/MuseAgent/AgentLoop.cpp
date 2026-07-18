// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AgentLoop.h"

#include <chrono>
#include <iostream>

namespace Aestra {
namespace MuseAgent {

namespace {

const char* kSystemPromptHeader =
    "You are Muse, the production agent inside the Aestra DAW. You control a real "
    "audio session through structured verbs; Muse validates and executes every "
    "command through the same undo history the user's UI edits use.\n\n"
    "Working method:\n"
    "- Look before you touch: use the query verbs (get_session_state, list_*, "
    "get_pattern, get_meters) to ground every decision in actual session state.\n"
    "- Verify with your ears: after building or revising, render and check peakDb "
    "— silence or clipping means something is wrong.\n"
    "- Errors carry reasons; read them and adjust rather than repeating a failed "
    "call.\n"
    "- Use batch for multi-step gestures so they undo as one step.\n"
    "- When the brief is satisfied (or you cannot proceed), call finish with an "
    "honest summary of what you did and what you verified. Never claim work you "
    "did not verify.\n";

std::string modeInstructions(AgentLoop::Mode mode) {
    switch (mode) {
    case AgentLoop::Mode::Direct:
        return "Mode: direct. Execute exactly what the instruction asks — no "
               "embellishment, no extra creative choices — then finish.\n";
    case AgentLoop::Mode::Collaborative:
        return "Mode: collaborative. Before major creative choices (style, key, "
               "arrangement direction), call ask_user with one concise question. "
               "Do not ask about trivia you can decide yourself.\n";
    case AgentLoop::Mode::Autonomous:
        return "Mode: autonomous. The user is not watching; never ask questions. "
               "Make reasonable creative decisions yourself and continue until "
               "the brief is met or you must stop.\n";
    }
    return "";
}

const char* schemaTypeName(const std::string& museType) {
    if (museType == "int") return "integer";
    if (museType == "float") return "number";
    if (museType == "bool") return "boolean";
    return "string";
}

} // namespace

AgentLoop::AgentLoop(ModelProvider& provider, Transport transport, Options options)
    : m_provider(provider), m_transport(std::move(transport)), m_options(options) {
    m_askUser = [](const std::string& question) {
        std::cerr << "\n[muse-agent asks] " << question << "\n> " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        return answer;
    };
}

std::string AgentLoop::buildSystemPrompt(const std::string& manifestJson) const {
    std::string prompt = kSystemPromptHeader;
    prompt += modeInstructions(m_options.mode);
    prompt += "\nBudget: at most " + std::to_string(m_options.maxIterations) +
              " turns and " + std::to_string(m_options.maxToolCalls) +
              " tool calls. Work efficiently; prefer batch and set_steps over "
              "note-by-note edits.\n";

    // The manifest's notes are the semantics an agent cannot discover through
    // the protocol (sampler root = pitch 60, routing rules, step characters).
    try {
        JSON manifest = JSON::parse(manifestJson);
        if (manifest.has("notes")) {
            prompt += "\nEngine semantics you must respect:\n";
            JSON& notes = manifest["notes"];
            for (auto& [key, value] : notes.asObject()) {
                prompt += "- " + key + ": " + value.asString() + "\n";
            }
        }
    } catch (const std::exception&) {
        // Manifest without notes still yields a usable prompt.
    }
    return prompt;
}

std::vector<ToolDefinition> AgentLoop::buildTools(const std::string& manifestJson) const {
    std::vector<ToolDefinition> tools;
    JSON manifest = JSON::parse(manifestJson);

    if (manifest.has("commands")) {
        JSON& commands = manifest["commands"];
        for (size_t i = 0; i < commands.size(); ++i) {
            JSON& command = commands[i];
            ToolDefinition tool;
            tool.name = command["verb"].asString();
            tool.description =
                command.has("description") ? command["description"].asString() : tool.name;

            JSON properties = JSON::object();
            JSON required = JSON::array();
            if (command.has("flags")) {
                JSON& flags = command["flags"];
                for (size_t flagIndex = 0; flagIndex < flags.size(); ++flagIndex) {
                    JSON& flag = flags[flagIndex];
                    JSON property = JSON::object();
                    property.set("type",
                                 JSON(schemaTypeName(flag["type"].asString())));
                    if (flag.has("min")) property.set("minimum", flag["min"]);
                    if (flag.has("max")) property.set("maximum", flag["max"]);
                    properties.set(flag["name"].asString(), property);
                    if (flag.has("required") && flag["required"].asBool()) {
                        required.push(JSON(flag["name"].asString()));
                    }
                }
            }
            JSON schema = JSON::object();
            schema.set("type", JSON("object"));
            schema.set("properties", properties);
            if (required.size() > 0) schema.set("required", required);
            tool.parametersSchema = schema;
            tools.push_back(tool);
        }
    }

    // Queries and actions publish their arg shapes as prose; Muse validates,
    // so a permissive schema plus the description is honest and sufficient.
    const auto addLooseSection = [&tools](JSON& section) {
        for (size_t i = 0; i < section.size(); ++i) {
            JSON& entry = section[i];
            ToolDefinition tool;
            tool.name = entry["verb"].asString();
            tool.description = entry.has("description") ? entry["description"].asString()
                                                        : tool.name;
            if (entry.has("args") && entry["args"].isString() &&
                entry["args"].asString() != "none") {
                tool.description += " Args: " + entry["args"].asString();
            }
            JSON schema = JSON::object();
            schema.set("type", JSON("object"));
            schema.set("properties", JSON::object());
            tool.parametersSchema = schema;
            tools.push_back(tool);
        }
    };
    if (manifest.has("queries")) addLooseSection(manifest["queries"]);
    if (manifest.has("actions")) addLooseSection(manifest["actions"]);

    // The explicit finish action — success is declared, not vibed.
    {
        ToolDefinition finish;
        finish.name = "finish";
        finish.description =
            "Declare the brief complete (or explain why you must stop). Summarize what "
            "was built, what was verified (renders, peak levels), and anything left "
            "undone.";
        JSON properties = JSON::object();
        JSON summary = JSON::object();
        summary.set("type", JSON("string"));
        properties.set("summary", summary);
        JSON schema = JSON::object();
        schema.set("type", JSON("object"));
        schema.set("properties", properties);
        JSON required = JSON::array();
        required.push(JSON("summary"));
        schema.set("required", required);
        finish.parametersSchema = schema;
        tools.push_back(finish);
    }

    if (m_options.mode == Mode::Collaborative) {
        ToolDefinition ask;
        ask.name = "ask_user";
        ask.description = "Ask the user one concise question at a major creative "
                          "decision point and wait for their answer.";
        JSON properties = JSON::object();
        JSON question = JSON::object();
        question.set("type", JSON("string"));
        properties.set("question", question);
        JSON schema = JSON::object();
        schema.set("type", JSON("object"));
        schema.set("properties", properties);
        JSON required = JSON::array();
        required.push(JSON("question"));
        schema.set("required", required);
        ask.parametersSchema = schema;
        tools.push_back(ask);
    }
    return tools;
}

AgentLoop::Outcome AgentLoop::run(const std::string& brief) {
    Outcome outcome;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(m_options.maxSeconds);

    // Bootstrap the tool manifest over the wire.
    const std::string schemaResponse =
        m_transport("{\"id\": 0, \"verb\": \"get_schema\"}");
    std::string manifestJson;
    try {
        JSON parsed = JSON::parse(schemaResponse);
        if (!parsed.has("result")) {
            outcome.stopReason = "provider_error";
            outcome.summary = "get_schema failed: " + schemaResponse;
            return outcome;
        }
        manifestJson = parsed["result"].toString();
    } catch (const std::exception& e) {
        outcome.stopReason = "provider_error";
        outcome.summary = std::string("get_schema unparseable: ") + e.what();
        return outcome;
    }

    ModelRequest request;
    request.systemPrompt = buildSystemPrompt(manifestJson);
    request.tools = buildTools(manifestJson);
    request.maxTokens = m_options.maxTokensPerTurn;

    Message userMessage;
    userMessage.role = "user";
    userMessage.text = brief;
    request.messages.push_back(userMessage);

    long long museRequestId = 1;

    while (outcome.iterations < m_options.maxIterations) {
        if (std::chrono::steady_clock::now() > deadline) {
            outcome.stopReason = "max_seconds";
            outcome.summary = "time budget exhausted after " +
                              std::to_string(outcome.iterations) + " turns";
            return outcome;
        }
        ++outcome.iterations;

        const ModelResponse response = m_provider.complete(request);
        outcome.usage.inputTokens += response.usage.inputTokens;
        outcome.usage.outputTokens += response.usage.outputTokens;

        if (response.stopReason == StopReason::Error) {
            outcome.stopReason = "provider_error";
            outcome.summary = response.error;
            return outcome;
        }
        if (response.stopReason == StopReason::Refusal) {
            outcome.stopReason = "refusal";
            outcome.summary = "the model declined the request";
            return outcome;
        }

        if (m_options.verbose && !response.text.empty()) {
            std::cerr << "[model] " << response.text << "\n";
        }

        Message assistantMessage;
        assistantMessage.role = "assistant";
        assistantMessage.text = response.text;
        assistantMessage.toolCalls = response.toolCalls;
        request.messages.push_back(assistantMessage);

        if (response.toolCalls.empty()) {
            // Ending a turn without finish is a stop, not a success.
            outcome.stopReason = "end_without_finish";
            outcome.summary = response.text.empty() ? "the model ended without a summary"
                                                    : response.text;
            return outcome;
        }

        for (const ToolCall& call : response.toolCalls) {
            Message result;
            result.role = "tool";
            result.toolCallId = call.id;
            result.toolName = call.name;

            if (call.name == "finish") {
                outcome.finished = true;
                outcome.stopReason = "finished";
                outcome.summary = call.arguments.has("summary")
                                      ? JSON(call.arguments)["summary"].asString()
                                      : "";
                return outcome;
            }
            if (call.name == "ask_user") {
                const std::string question =
                    call.arguments.has("question")
                        ? JSON(call.arguments)["question"].asString()
                        : "";
                result.text = m_askUser(question);
                request.messages.push_back(result);
                continue;
            }

            if (outcome.toolCalls >= m_options.maxToolCalls) {
                outcome.stopReason = "max_tool_calls";
                outcome.summary = "tool-call budget exhausted";
                return outcome;
            }
            ++outcome.toolCalls;

            JSON museRequest = JSON::object();
            museRequest.set("id", JSON(static_cast<double>(museRequestId++)));
            museRequest.set("verb", JSON(call.name));
            JSON arguments = call.arguments;
            if (arguments.isObject() && arguments.size() > 0) {
                museRequest.set("args", arguments);
            }
            const std::string museLine = museRequest.toString();
            const std::string museResponse = m_transport(museLine);
            if (m_options.verbose) {
                std::cerr << "[muse] " << museLine << "\n       " << museResponse << "\n";
            }

            result.text = museResponse;
            try {
                JSON parsed = JSON::parse(museResponse);
                result.isError =
                    parsed.has("status") && parsed["status"].asString() != "ok";
            } catch (const std::exception&) {
                result.isError = true;
            }
            request.messages.push_back(result);
        }
    }

    outcome.stopReason = "max_iterations";
    outcome.summary = "iteration budget exhausted";
    return outcome;
}

} // namespace MuseAgent
} // namespace Aestra
