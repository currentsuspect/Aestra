// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "LineReader.h"
#include "ModelProvider.h"
#include "RetryPolicy.h"

#include <chrono>
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
        RetryPolicy retry;        // transient provider failures: bounded exponential backoff
    };

    struct Outcome {
        bool finished = false;      // the model called finish
        std::string summary;        // finish summary, or the stop explanation
        std::string stopReason;     // "finished" | "max_iterations" | "max_tool_calls"
                                    // | "max_seconds" | "max_tokens" | "provider_error"
                                    // | "refusal" | "end_without_finish"
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

    /** @brief Replace the backoff sleep (default: this thread sleeps). For tests. */
    using Sleeper = std::function<void(long milliseconds)>;
    void setSleeper(Sleeper sleeper) { m_sleeper = std::move(sleeper); }
    /**
     * @brief Called before every Muse request with the milliseconds left in the
     *        time budget, so the transport can bound its own blocking read.
     */
    using BudgetHook = std::function<void(long remainingMs)>;
    void setBudgetHook(BudgetHook hook) { m_budgetHook = std::move(hook); }

    /** @brief File descriptor the default ask_user reads answers from (default 0, stdin). */
    void setInputFd(int fd) { m_lineReader = LineReader(fd); }

    /** @brief Run one brief through the loop. */
    Outcome run(const std::string& brief);

private:
    std::string buildSystemPrompt(const std::string& manifestJson) const;
    std::vector<ToolDefinition> buildTools(const std::string& manifestJson) const;

    ModelProvider& m_provider;
    Transport m_transport;
    Options m_options;
    AskUser m_askUser;
    Sleeper m_sleeper;
    BudgetHook m_budgetHook;
    LineReader m_lineReader{0};
    std::chrono::steady_clock::time_point m_deadline{};
};

} // namespace MuseAgent
} // namespace Aestra
