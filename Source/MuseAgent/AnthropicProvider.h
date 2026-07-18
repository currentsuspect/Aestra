// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ModelProvider.h"

namespace Aestra {
namespace MuseAgent {

/**
 * @brief Adapter for the Anthropic Messages API (BYOK performance tier).
 *
 * POST {baseUrl}/v1/messages with x-api-key auth. Requests stay minimal —
 * model, max_tokens, system, messages, tools — no sampling parameters
 * (removed on current models) and no thinking configuration.
 */
class AnthropicProvider : public ModelProvider {
public:
    AnthropicProvider(std::string apiKey, std::string model,
                      std::string baseUrl = "https://api.anthropic.com")
        : m_apiKey(std::move(apiKey)), m_model(std::move(model)), m_baseUrl(std::move(baseUrl)) {}

    ModelResponse complete(const ModelRequest& request) override;
    std::string name() const override { return "anthropic:" + m_model; }

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;
};

} // namespace MuseAgent
} // namespace Aestra
