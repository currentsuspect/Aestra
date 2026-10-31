// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Retry policy and the loop's handling of transient provider failures. A
// scripted provider and a recording sleeper stand in for the network and clock.

#include "AgentLoop.h"
#include "ModelProvider.h"
#include "RetryPolicy.h"

#include <iostream>
#include <string>
#include <vector>

using Aestra::JSON;
using namespace Aestra::MuseAgent;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) ++g_failures;
}

struct Failure {
    int status = 0;
    bool transport = false;
    int retryAfter = -1;
};

// Fails per script, then finishes.
class FlakyProvider : public ModelProvider {
public:
    explicit FlakyProvider(std::vector<Failure> failures) : m_failures(std::move(failures)) {}
    ModelResponse complete(const ModelRequest&) override {
        ++calls;
        ModelResponse response;
        if (m_next < m_failures.size()) {
            const Failure& f = m_failures[m_next++];
            response.stopReason = StopReason::Error;
            response.httpStatus = f.status;
            response.transportFailure = f.transport;
            response.retryAfterSeconds = f.retryAfter;
            response.error = f.transport ? "transport: connection refused"
                                         : "HTTP " + std::to_string(f.status) + ": nope";
            return response;
        }
        ToolCall call;
        call.id = "f1";
        call.name = "finish";
        call.arguments = JSON::parse("{\"summary\": \"done after retries\"}");
        response.toolCalls.push_back(call);
        response.stopReason = StopReason::ToolUse;
        return response;
    }
    std::string name() const override { return "flaky"; }
    int calls = 0;

private:
    std::vector<Failure> m_failures;
    size_t m_next = 0;
};

AgentLoop::Outcome runWith(FlakyProvider& provider, std::vector<long>& sleeps,
                           AgentLoop::Options options = {}) {
    AgentLoop loop(provider,
                   [](const std::string&) { return std::string("{\"id\": 0, \"result\": {}}"); },
                   options);
    loop.setSleeper([&sleeps](long ms) { sleeps.push_back(ms); });
    return loop.run("brief");
}

} // namespace

int main() {
    // --- classification ---
    check(isTransientFailure(true, 0), "transport failure is transient");
    check(isTransientFailure(false, 429), "429 is transient");
    check(isTransientFailure(false, 408), "408 is transient");
    check(isTransientFailure(false, 500) && isTransientFailure(false, 503) &&
              isTransientFailure(false, 599),
          "5xx is transient");
    check(!isTransientFailure(false, 400) && !isTransientFailure(false, 401) &&
              !isTransientFailure(false, 403) && !isTransientFailure(false, 404) &&
              !isTransientFailure(false, 422),
          "other 4xx is not transient");
    check(!isTransientFailure(false, 0), "no status and no transport flag is not transient");

    // --- schedule ---
    {
        RetryPolicy p;
        check(retryDelayMs(p, 1, -1, 0.0) == 1000, "first retry waits the base delay");
        check(retryDelayMs(p, 2, -1, 0.0) == 2000, "delay doubles");
        check(retryDelayMs(p, 3, -1, 0.0) == 4000, "delay doubles again");
        check(retryDelayMs(p, 20, -1, 0.0) == 30000, "delay is capped");
        const long jittered = retryDelayMs(p, 1, -1, 0.5);
        check(jittered > 1000 && jittered < 1250, "jitter adds up to 25%");
        check(retryDelayMs(p, 1, 7, 0.5) == 7000, "Retry-After is honoured exactly");
        check(retryDelayMs(p, 1, 100000, 0.0) == 120000, "Retry-After is capped");
        check(retryDelayMs(p, 1, 0, 0.0) == 0, "Retry-After: 0 means retry now");
    }

    // --- loop: recovers from transient failures ---
    {
        FlakyProvider provider({{503, false, -1}, {0, true, -1}, {429, false, 3}});
        std::vector<long> sleeps;
        const auto outcome = runWith(provider, sleeps);
        check(outcome.finished && outcome.summary == "done after retries",
              "run completes after three transient failures");
        check(provider.calls == 4, "four provider calls");
        check(sleeps.size() == 3, "slept before each retry");
        if (sleeps.size() == 3) {
            check(sleeps[0] >= 1000 && sleeps[0] < 1250, "first backoff is about the base delay");
            check(sleeps[1] >= 2000 && sleeps[1] < 2500, "second backoff doubled");
            check(sleeps[2] == 3000, "third wait follows Retry-After");
        }
        check(outcome.iterations == 1, "retries do not consume model turns");
    }

    // --- loop: non-transient is not retried ---
    {
        FlakyProvider provider({{401, false, -1}});
        std::vector<long> sleeps;
        const auto outcome = runWith(provider, sleeps);
        check(!outcome.finished && outcome.stopReason == "provider_error", "401 ends the run");
        check(provider.calls == 1 && sleeps.empty(), "401 tried once, no sleep");
    }

    // --- loop: bounded ---
    {
        FlakyProvider provider(std::vector<Failure>(50, Failure{503, false, -1}));
        std::vector<long> sleeps;
        const auto outcome = runWith(provider, sleeps);
        check(!outcome.finished && outcome.stopReason == "provider_error", "gives up eventually");
        check(provider.calls == 5, "exactly maxAttempts calls");
        check(sleeps.size() == 4, "four waits between five attempts");
        check(outcome.summary.find("HTTP 503") != std::string::npos &&
                  outcome.summary.find("5 attempts") != std::string::npos,
              "summary carries the last error and the attempt count");
    }

    // --- loop: the wall-clock budget wins over a long wait ---
    {
        FlakyProvider provider({{429, false, 100}});
        std::vector<long> sleeps;
        AgentLoop::Options options;
        options.maxSeconds = 10;
        const auto outcome = runWith(provider, sleeps, options);
        check(!outcome.finished && provider.calls == 1 && sleeps.empty(),
              "a wait longer than the remaining budget is not taken");
        check(outcome.summary.find("budget") != std::string::npos,
              "summary says the time budget prevented the retry");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
