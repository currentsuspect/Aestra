// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AgentLoop Tests — the muse-agent session loop, with a scripted FakeProvider
// standing in for the model and a real MuseService as the session. No network,
// no sockets: the transport is a direct lambda into the service, proving the
// loop is transport- and provider-agnostic by construction.

#include "AgentLoop.h"
#include "ModelProvider.h"

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using namespace Aestra::MuseAgent;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

/**
 * A model that plays back a script of turns. Each turn is a list of tool
 * calls (empty list = end turn with text only).
 */
class FakeProvider : public ModelProvider {
public:
    struct Turn {
        std::vector<ToolCall> calls;
        std::string text;
        bool truncated = false; // report StopReason::MaxTokens for this turn
    };

    explicit FakeProvider(std::vector<Turn> turns) : m_turns(std::move(turns)) {}

    ModelResponse complete(const ModelRequest& request) override {
        m_lastRequest = request;
        ModelResponse response;
        if (m_next >= m_turns.size()) {
            // Script exhausted: keep making a harmless call so budget tests
            // can prove the loop, not the script, is what stops the run.
            ToolCall call;
            call.id = "loop_" + std::to_string(m_next);
            call.name = "get_transport";
            call.arguments = JSON::object();
            response.toolCalls.push_back(call);
            response.stopReason = StopReason::ToolUse;
            ++m_next;
            return response;
        }
        const Turn& turn = m_turns[m_next++];
        response.text = turn.text;
        response.toolCalls = turn.calls;
        response.stopReason = turn.truncated
                                  ? StopReason::MaxTokens
                                  : (turn.calls.empty() ? StopReason::EndTurn
                                                        : StopReason::ToolUse);
        return response;
    }

    std::string name() const override { return "fake"; }

    ModelRequest m_lastRequest;

private:
    std::vector<Turn> m_turns;
    size_t m_next = 0;
};

ToolCall makeCall(const std::string& name, const std::string& argsJson) {
    static int counter = 0;
    ToolCall call;
    call.id = "call_" + std::to_string(++counter);
    call.name = name;
    call.arguments = JSON::parse(argsJson);
    return call;
}

} // namespace

int main() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    CommandRegistry::initialize(trackManager.get());
    CommandRegistry::setAudioEngine(&engine);
    MuseService service(trackManager.get(), &engine);

    const AgentLoop::Transport transport = [&service](const std::string& line) {
        return service.handleRequest(line);
    };

    // --- A scripted production run: build, verify, finish ---
    {
        FakeProvider provider({
            {{makeCall("set_bpm", "{\"value\": 142}"),
              makeCall("add_track", "{\"name\": \"Drums\"}")},
             "setting up"},
            {{makeCall("list_tracks", "{}")}, "checking"},
            {{makeCall("finish",
                       "{\"summary\": \"bpm set, track added, verified via list_tracks\"}")},
             ""},
        });
        AgentLoop::Options options;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("set the session up at 142 BPM");

        check(outcome.finished, "scripted run reaches finish");
        check(outcome.stopReason == "finished", "stop reason is finished");
        check(outcome.summary.find("verified") != std::string::npos,
              "finish summary carried through");
        check(outcome.toolCalls == 3, "three Muse calls executed (finish not counted)");
        check(outcome.iterations == 3, "three model turns");

        // The edits really landed in the session.
        JSON state = JSON::parse(
            service.handleRequest("{\"id\": 900, \"verb\": \"get_session_state\"}"));
        check(state["result"]["transport"]["bpm"].asNumber() == 142.0,
              "bpm actually set in the session");
        check(state["result"]["tracks"].size() == 1, "track actually added");

        // The provider received real tools built from the wire manifest.
        bool sawSetSteps = false, sawFinish = false, sawAskUser = false;
        for (const auto& tool : provider.m_lastRequest.tools) {
            if (tool.name == "set_steps") sawSetSteps = true;
            if (tool.name == "finish") sawFinish = true;
            if (tool.name == "ask_user") sawAskUser = true;
        }
        check(sawSetSteps, "manifest mutation verbs became tools");
        check(sawFinish, "finish tool offered");
        check(!sawAskUser, "ask_user absent in autonomous mode");
        check(provider.m_lastRequest.systemPrompt.find("pitch 60") != std::string::npos,
              "system prompt carries the sampler-root semantics");
    }

    // --- Errors flow back as tool results, loop keeps going ---
    {
        FakeProvider provider({
            {{makeCall("set_volume", "{\"track\": 99, \"value\": 0.5}")}, ""},
            {{makeCall("finish", "{\"summary\": \"could not: no such track\"}")}, ""},
        });
        AgentLoop::Options options;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("set volume on track 99");
        check(outcome.finished, "loop survives a reasoned Muse error");

        // The error text was delivered to the model as a tool result.
        bool sawErrorResult = false;
        for (const auto& message : provider.m_lastRequest.messages) {
            if (message.role == "tool" && message.isError &&
                message.text.find("no such track: 99") != std::string::npos) {
                sawErrorResult = true;
            }
        }
        check(sawErrorResult, "refusal reason reached the model verbatim");
    }

    // --- Budgets stop a model that never finishes ---
    {
        FakeProvider provider({}); // immediately falls into endless get_transport
        AgentLoop::Options options;
        options.maxIterations = 4;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("loop forever");
        check(!outcome.finished && outcome.stopReason == "max_iterations",
              "iteration budget stops a non-finishing model");
        check(outcome.iterations == 4, "stopped exactly at the budget");
    }
    {
        FakeProvider provider({});
        AgentLoop::Options options;
        options.maxToolCalls = 2;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("loop forever");
        check(!outcome.finished && outcome.stopReason == "max_tool_calls",
              "tool-call budget stops a non-finishing model");
    }

    // --- Ending without finish is a stop, not a success ---
    {
        FakeProvider provider({{{}, "I think we're done here."}});
        AgentLoop::Options options;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("do nothing");
        check(!outcome.finished && outcome.stopReason == "end_without_finish",
              "ending without finish is not success");
    }

    // --- finish without a real summary bounces back as an error ---
    {
        FakeProvider provider({
            {{makeCall("finish", "{}")}, ""},
            {{makeCall("finish", "{\"summary\": \"\"}")}, ""},
            {{makeCall("finish", "{\"summary\": \"done properly this time\"}")}, ""},
        });
        AgentLoop::Options options;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("finish sloppily");
        check(outcome.finished && outcome.summary == "done properly this time",
              "empty finish rejected until a real summary arrives");

        bool sawFinishError = false;
        for (const auto& message : provider.m_lastRequest.messages) {
            if (message.role == "tool" && message.isError &&
                message.text.find("non-empty string") != std::string::npos) {
                sawFinishError = true;
            }
        }
        check(sawFinishError, "sloppy finish returned as an error tool result");
    }

    // --- Truncation surfaces as max_tokens, not end_without_finish ---
    {
        FakeProvider provider({{{}, "half a thou", true}});
        AgentLoop::Options options;
        AgentLoop loop(provider, transport, options);
        const AgentLoop::Outcome outcome = loop.run("say something long");
        check(!outcome.finished && outcome.stopReason == "max_tokens",
              "provider truncation reported as max_tokens");
    }

    // --- Collaborative mode: ask_user offered and routed ---
    {
        FakeProvider provider({
            {{makeCall("ask_user", "{\"question\": \"eerie or triumphant?\"}")}, ""},
            {{makeCall("finish", "{\"summary\": \"went eerie per the answer\"}")}, ""},
        });
        AgentLoop::Options options;
        options.mode = AgentLoop::Mode::Collaborative;
        AgentLoop loop(provider, transport, options);
        std::string asked;
        loop.setAskUser([&asked](const std::string& question) {
            asked = question;
            return std::string("eerie");
        });
        const AgentLoop::Outcome outcome = loop.run("make a melody");
        check(outcome.finished, "collaborative run finishes");
        check(asked == "eerie or triumphant?", "question reached the user hook");

        bool answerDelivered = false;
        for (const auto& message : provider.m_lastRequest.messages) {
            if (message.role == "tool" && message.text == "eerie") answerDelivered = true;
        }
        check(answerDelivered, "answer returned to the model as the tool result");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
