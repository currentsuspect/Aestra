// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// muse-agent — interchangeable intelligence for the Muse protocol.
//
// Connects to a Muse socket (the live app with AESTRA_MUSE_PORT, or
// `MuseRepl --port N` headless), hands the tool manifest to a model
// provider, and runs the session loop under hard budgets. Muse owns the
// musical system; the model only proposes intent.
//
//   muse-agent --connect 41952 --provider anthropic --brief "make a 140 BPM trap loop"
//   muse-agent --connect 41952 --provider openai --base-url http://127.0.0.1:8080 \
//              --model llama-3.1-8b --brief "add reverb to track 0"
//
// API keys come from the environment, never flags: ANTHROPIC_API_KEY or
// OPENAI_API_KEY / MUSE_AGENT_API_KEY. Local OpenAI-compatible servers
// (llama.cpp, Ollama, LM Studio) usually need no key.

#include "AgentLoop.h"
#include "AnthropicProvider.h"
#include "CliArgs.h"
#include "MuseSocketClient.h"
#include "MuseSocketToken.h"
#include "OpenAICompatProvider.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::MuseAgent;

namespace {

// Generous ceilings: they exist to reject typos and overflow, not to tune policy.
constexpr int kMaxCountLimit = 1000000;
constexpr int kMaxSecondsLimit = 7 * 24 * 3600;

void printUsage() {
    std::cout <<
        "muse-agent: drive a Muse session with a model.\n"
        "  --connect <port|host:port|[v6]:port>  Muse socket (default host 127.0.0.1)\n"
        "  --token-file <path>         session token file (default: the per-user file the app\n"
        "                              wrote for the port; the socket refuses clients without it)\n"
        "  --brief \"...\"               the production brief (required)\n"
        "  --provider anthropic|openai (default openai — local-first)\n"
        "  --model <id>                model id (defaults: claude-opus-4-8 / server default)\n"
        "  --base-url <url>            OpenAI-compatible endpoint\n"
        "                              (default http://127.0.0.1:8080 — llama.cpp server)\n"
        "  --mode direct|collab|auto   interruption policy (default auto)\n"
        "  --max-iterations <n>        model-turn budget (default 25)\n"
        "  --max-tool-calls <n>        Muse-request budget (default 200)\n"
        "  --max-seconds <n>           wall-clock budget (default 900)\n"
        "  --verbose                   log every verb and response to stderr\n";
}

std::string envOr(const char* primary, const char* fallback) {
    if (const char* value = std::getenv(primary); value && *value) return value;
    if (fallback) {
        if (const char* value = std::getenv(fallback); value && *value) return value;
    }
    return "";
}

} // namespace

int main(int argc, char** argv) {
    std::string connect = "41952";
    std::string tokenFile;
    std::string brief;
    std::string providerName = "openai";
    std::string model;
    std::string baseUrl = "http://127.0.0.1:8080";
    AgentLoop::Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--connect") connect = next("--connect");
        else if (arg == "--token-file") tokenFile = next("--token-file");
        else if (arg == "--brief") brief = next("--brief");
        else if (arg == "--provider") providerName = next("--provider");
        else if (arg == "--model") model = next("--model");
        else if (arg == "--base-url") baseUrl = next("--base-url");
        else if (arg == "--mode") {
            const std::string mode = next("--mode");
            if (mode == "direct") options.mode = AgentLoop::Mode::Direct;
            else if (mode == "collab") options.mode = AgentLoop::Mode::Collaborative;
            else if (mode == "auto") options.mode = AgentLoop::Mode::Autonomous;
            else { std::cerr << "unknown mode: " << mode << "\n"; return 2; }
        }
        else if (arg == "--max-iterations" || arg == "--max-tool-calls" || arg == "--max-seconds") {
            const std::string text = next(arg.c_str());
            const int upper = (arg == "--max-seconds") ? kMaxSecondsLimit : kMaxCountLimit;
            const auto value = parseBoundedInt(text, 1, upper);
            if (!value) {
                std::cerr << "invalid " << arg << ": " << text << " (expected a whole number from 1 to "
                          << upper << ")\n";
                return 2;
            }
            if (arg == "--max-iterations") options.maxIterations = *value;
            else if (arg == "--max-tool-calls") options.maxToolCalls = *value;
            else options.maxSeconds = *value;
        }
        else if (arg == "--verbose") options.verbose = true;
        else if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        else { std::cerr << "unknown argument: " << arg << "\n"; return 2; }
    }

    if (brief.empty()) {
        std::cerr << "a --brief is required\n";
        printUsage();
        return 2;
    }

    // Resolve the Muse endpoint.
    const HostPort endpoint = splitHostPort(connect, "127.0.0.1");
    if (!endpoint.ok) {
        std::cerr << "invalid --connect: " << connect
                  << " (expected <port>, <host>:<port> or [<ipv6>]:<port>)\n";
        return 2;
    }
    const std::string host = endpoint.host;
    const std::string portText = endpoint.portText;
    const auto parsedPort = parseBoundedInt(portText, 1, 65535);
    if (!parsedPort) {
        std::cerr << "invalid port in --connect: " << connect << "\n";
        return 2;
    }

    const int port = *parsedPort;

    std::string error;
    std::string token;
    if (!Aestra::MuseToken::loadToken(static_cast<uint16_t>(port), tokenFile, token, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    MuseSocketClient client;
    if (!client.connect(host, static_cast<uint16_t>(port), error)) {
        std::cerr << error << "\n"
                  << "Is Muse listening? Start the app with AESTRA_MUSE_PORT=" << port
                  << " or run: MuseRepl --port " << port << "\n";
        return 1;
    }
    client.setReadTimeoutMs(10 * 1000);
    if (!client.authenticate(token, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    client.setReadTimeoutMs(0);

    // Resolve the brain. Local-first: the OpenAI-compatible adapter covers
    // llama.cpp server, Ollama, and LM Studio out of the box; hosted keys
    // plug in the same way (BYOK).
    std::unique_ptr<ModelProvider> provider;
    if (providerName == "anthropic") {
        const std::string apiKey = envOr("ANTHROPIC_API_KEY", "MUSE_AGENT_API_KEY");
        if (apiKey.empty()) {
            std::cerr << "ANTHROPIC_API_KEY is not set\n";
            return 1;
        }
        provider = std::make_unique<AnthropicProvider>(
            apiKey, model.empty() ? "claude-opus-4-8" : model);
    } else if (providerName == "openai") {
        const std::string apiKey = envOr("OPENAI_API_KEY", "MUSE_AGENT_API_KEY");
        provider = std::make_unique<OpenAICompatProvider>(
            apiKey, model.empty() ? "default" : model, baseUrl);
    } else {
        std::cerr << "unknown provider: " << providerName << "\n";
        return 2;
    }

    AgentLoop loop(*provider,
                   [&client](const std::string& line) { return client.request(line); },
                   options);

    std::cerr << "[muse-agent] " << provider->name() << " -> " << host << ":" << port
              << "\n";
    const AgentLoop::Outcome outcome = loop.run(brief);

    std::cout << "\n=== muse-agent " << outcome.stopReason << " ===\n"
              << outcome.summary << "\n"
              << "turns: " << outcome.iterations << ", muse calls: " << outcome.toolCalls
              << ", tokens in/out: " << outcome.usage.inputTokens << "/"
              << outcome.usage.outputTokens << "\n";
    return outcome.finished ? 0 : 1;
}
