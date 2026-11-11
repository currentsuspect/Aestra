// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// --max-seconds must bound blocking calls inside a turn: the line reader's
// timeout, the budget handed to the Muse transport, and the deadline re-check
// after each tool call. Real pipes and a real (short) clock; no network.

#include "AgentLoop.h"
#include "LineReader.h"
#include "ModelProvider.h"

#include <unistd.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using Aestra::JSON;
using namespace Aestra::MuseAgent;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) ++g_failures;
}

long msSince(std::chrono::steady_clock::time_point t) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t).count());
}

ToolCall call(const std::string& name, const std::string& args, const std::string& id) {
    ToolCall c;
    c.id = id;
    c.name = name;
    c.arguments = JSON::parse(args);
    return c;
}

// Turn 1: two Muse calls. Afterwards: keeps asking for more.
class TwoCallProvider : public ModelProvider {
public:
    ModelResponse complete(const ModelRequest&) override {
        ModelResponse r;
        r.stopReason = StopReason::ToolUse;
        r.toolCalls.push_back(call("get_transport", "{}", "a" + std::to_string(n)));
        r.toolCalls.push_back(call("get_transport", "{}", "b" + std::to_string(n)));
        ++n;
        return r;
    }
    std::string name() const override { return "two"; }
    int n = 0;
};

class AskProvider : public ModelProvider {
public:
    ModelResponse complete(const ModelRequest& request) override {
        for (const auto& m : request.messages) {
            if (m.role == "tool") lastToolText = m.text;
        }
        ModelResponse r;
        r.stopReason = StopReason::ToolUse;
        r.toolCalls.push_back(call("ask_user", "{\"question\": \"which?\"}", "q" + std::to_string(n++)));
        return r;
    }
    std::string name() const override { return "ask"; }
    int n = 0;
    std::string lastToolText;
};

} // namespace

int main() {
    // --- LineReader ---
    {
        int fds[2];
        if (::pipe(fds) != 0) return 2;
        LineReader reader(fds[0]);
        std::string line;
        check(::write(fds[1], "hello\nwor", 9) == 9, "wrote to pipe");
        check(reader.readLine(line, 1000) == LineReader::Result::Line && line == "hello",
              "a ready line is returned");
        const auto t0 = std::chrono::steady_clock::now();
        check(reader.readLine(line, 100) == LineReader::Result::TimedOut, "a partial line times out");
        check(msSince(t0) < 1500, "the timeout is honoured promptly");
        check(::write(fds[1], "ld\r\n", 4) == 4, "finished the line");
        check(reader.readLine(line, 1000) == LineReader::Result::Line && line == "world",
              "the partial line is continued, CR stripped");
        ::close(fds[1]);
        check(reader.readLine(line, 1000) == LineReader::Result::Eof, "closed writer reports EOF");
        ::close(fds[0]);
    }

    const AgentLoop::Transport transport = [](const std::string&) {
        return std::string("{\"id\": 1, \"status\": \"ok\", \"result\": {}}");
    };

    // --- budget hook gets a positive, shrinking allowance before each Muse call ---
    {
        TwoCallProvider provider;
        std::vector<long> seen;
        AgentLoop::Options options;
        options.maxSeconds = 3;
        options.maxIterations = 1;
        AgentLoop loop(provider, transport, options);
        loop.setBudgetHook([&seen](long ms) { seen.push_back(ms); });
        loop.run("x");
        check(seen.size() == 3, "hook called for get_schema and both tool calls");
        bool sane = !seen.empty();
        for (long ms : seen) sane = sane && ms >= 1 && ms <= 3000;
        check(sane, "every allowance is within 1 ms .. the budget");
    }

    // --- the deadline is re-checked after each tool call, not only per turn ---
    {
        TwoCallProvider provider;
        int museCalls = 0;
        AgentLoop::Options options;
        options.maxSeconds = 2;
        AgentLoop loop(provider,
                       [&museCalls](const std::string& line) {
                           if (line.find("get_schema") == std::string::npos) {
                               ++museCalls;
                               std::this_thread::sleep_for(std::chrono::milliseconds(2100));
                           }
                           return std::string("{\"id\": 1, \"status\": \"ok\", \"result\": {}}");
                       },
                       options);
        const auto outcome = loop.run("x");
        check(outcome.stopReason == "max_seconds", "stops with max_seconds mid-turn");
        check(museCalls == 1, "the second call of the turn is not sent after the deadline");
    }

    // --- ask_user: closed stdin yields an explicit answer, not an empty one ---
    {
        int fds[2];
        if (::pipe(fds) != 0) return 2;
        ::close(fds[1]); // writer gone: immediate EOF
        AskProvider provider;
        AgentLoop::Options options;
        options.mode = AgentLoop::Mode::Collaborative;
        options.maxIterations = 2;
        AgentLoop loop(provider, transport, options);
        loop.setInputFd(fds[0]);
        loop.run("x");
        check(provider.lastToolText.find("No interactive user") != std::string::npos,
              "EOF answers an explicit No interactive user message");
        ::close(fds[0]);
    }

    // --- ask_user: unanswered question times out at the budget ---
    {
        int fds[2];
        if (::pipe(fds) != 0) return 2;
        AskProvider provider;
        AgentLoop::Options options;
        options.mode = AgentLoop::Mode::Collaborative;
        options.maxSeconds = 2;
        AgentLoop loop(provider, transport, options);
        loop.setInputFd(fds[0]);
        const auto t0 = std::chrono::steady_clock::now();
        const auto outcome = loop.run("x");
        check(msSince(t0) < 5000, "an unanswered question does not hold the run past the budget");
        check(outcome.stopReason == "max_seconds", "run ends with max_seconds");
        ::close(fds[0]);
        ::close(fds[1]);
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
