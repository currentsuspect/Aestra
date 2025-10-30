// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>

namespace NomadUI {
namespace Tests {

// Forward declarations for test utilities
class MockRenderer : public NUIRenderer {
public:
    void initialize(int width, int height) override {}
    void shutdown() override {}
    void resize(int width, int height) override {}
    void beginFrame() override {}
    void endFrame() override {}

    // Basic drawing operations for testing
    void fillRect(const NUIRect& rect, const NUIColor& color) override {}
    void strokeRect(const NUIRect& rect, float width, const NUIColor& color) override {}
    void fillRoundedRect(const NUIRect& rect, float radius, const NUIColor& color) override {}
    void strokeRoundedRect(const NUIRect& rect, float radius, float width, const NUIColor& color) override {}
    void drawLine(const NUIPoint& start, const NUIPoint& end, float width, const NUIColor& color) override {}
    void drawText(const std::string& text, const NUIPoint& position, float size, const NUIColor& color) override {}
    void drawTextCentered(const std::string& text, const NUIRect& bounds, float size, const NUIColor& color) override {}

    std::string getBackendName() const override { return "Mock"; }
};

} // namespace Tests
} // namespace NomadUI
