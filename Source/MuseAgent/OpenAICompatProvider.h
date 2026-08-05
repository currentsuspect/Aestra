// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ModelProvider.h"

namespace Aestra {
namespace MuseAgent {

/**
 * @brief Adapter for OpenAI-compatible chat-completions endpoints.
 *
 * This is the local-first workhorse: llama.cpp server, Ollama, and LM Studio
 * all speak this protocol, as do OpenAI, OpenRouter, and Gemini's
 * compatibility endpoint. POST {baseUrl}/v1/chat/completions with optional
 * Bearer auth (local servers typically need none).
 */
class OpenAICompatProvider : public ModelProvider {
public:
    OpenAICompatProvider(std::string apiKey, std::string model, std::string baseUrl)
        : m_apiKey(std::move(apiKey)), m_model(std::move(model)), m_baseUrl(std::move(baseUrl)) {}

    ModelResponse complete(const ModelRequest& request) override;
    std::string name() const override { return "openai-compat:" + m_model; }

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;
};

} // namespace MuseAgent
} // namespace Aestra
