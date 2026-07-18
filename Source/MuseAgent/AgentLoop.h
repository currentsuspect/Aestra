// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ModelProvider.h"

#include <functional>
#include <string>

namespace Aestra {
namespace MuseAgent {

/**
 * @brief The Muse session loop: model proposes verbs, Muse executes them.
 *
 * The loop is provider-agnostic and transport-agnostic. It bootstraps the
 * tool manifest over the wire (get_schema), converts it into provider tool
 * definitions, and iterates model → tool calls → Muse responses until the
 * model calls finish, a budget is exhausted, or the user is needed.
 *
 * Hard boundaries (the model must not decide success by vibes alone):
 * max iterations, max tool calls, max elapsed time, and an explicit finish
 * action carrying a summary.
 */
class AgentLoop {
public:
    enum class Mode {
        Direct,        // execute one explicit instruction, minimal iteration
        Collaborative, // may ask the user before major creative choices
        Autonomous,    // continue until the brief or a budget is met
    };

    struct Options {
        Mode mode = Mode::Autonomous;
        int maxIterations = 25;   // model turns
        int maxToolCalls = 200;   // Muse requests
        int maxSeconds = 900;     // wall clock
        int maxTokensPerTurn = 16000;
        bool verbose = false;     // log every verb + response to stderr
    };

    struct Outcome {
        bool finished = false;      // the model called finish
        std::string summary;        // finish summary, or the stop explanation
        std::string stopReason;     // "finished" | "max_iterations" | "max_tool_calls"
                                    // | "max_seconds" | "provider_error" | "refusal"
                                    // | "end_without_finish"
        int iterations = 0;
        int toolCalls = 0;
        Usage usage;
    };

    /** @brief Transport: send one JSONL request line, get the response line. */
    using Transport = std::function<std::string(const std::string&)>;

    /** @brief Collaborative-mode question relay (default: stderr + stdin). */
    using AskUser = std::function<std::string(const std::string& question)>;

    AgentLoop(ModelProvider& provider, Transport transport, Options options);

    /** @brief Route ask_user somewhere other than the terminal (e.g. tests). */
    void setAskUser(AskUser askUser) { m_askUser = std::move(askUser); }

    /** @brief Run one brief through the loop. */
    Outcome run(const std::string& brief);

private:
    std::string buildSystemPrompt(const std::string& manifestJson) const;
    std::vector<ToolDefinition> buildTools(const std::string& manifestJson) const;

    ModelProvider& m_provider;
    Transport m_transport;
    Options m_options;
    AskUser m_askUser;
};

} // namespace MuseAgent
} // namespace Aestra
