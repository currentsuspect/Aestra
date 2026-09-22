// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A do-nothing NUIRenderer for tests that need a paint pass but not its output.
//
// Some interaction state is only established while painting — TrackUIComponent
// fills m_allClipBounds inside renderStatic(), and nothing can hit-test a clip
// until it has. Those tests need a renderer purely so the paint path can run.
//
// measureText() returns a plausible non-zero size on purpose: a zero-width
// measurement makes layout arithmetic degenerate, which would be a property of
// the stub rather than of the code under test. renderCachedOrUpdate() returns
// false so callers take the uncached path and actually execute their draw code.
//
// Use RecordingRenderer (see BPMEditorPaintTest) instead when the test needs to
// assert on what was painted.

#pragma once

#include "../../AestraUI/Graphics/NUIRenderer.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Aestra::Testing {

class NullRenderer : public AestraUI::NUIRenderer {
public:
    bool initialize(int, int) override { return true; }
    void shutdown() override {}
    void resize(int, int) override {}
    void beginFrame() override {}
    void endFrame() override {}
    void clear(const AestraUI::NUIColor&) override {}
    void pushTransform(float, float, float = 1.0f) override {}
    void popTransform() override {}
    void setClipRect(const AestraUI::NUIRect&) override {}
    void clearClipRect() override {}
    void setOpacity(float) override {}
    void fillRect(const AestraUI::NUIRect&, const AestraUI::NUIColor&) override {}
    void fillRoundedRect(const AestraUI::NUIRect&, float, const AestraUI::NUIColor&) override {}
    void strokeRect(const AestraUI::NUIRect&, float, const AestraUI::NUIColor&) override {}
    void strokeRoundedRect(const AestraUI::NUIRect&, float, float, const AestraUI::NUIColor&) override {}
    void fillCircle(const AestraUI::NUIPoint&, float, const AestraUI::NUIColor&) override {}
    void strokeCircle(const AestraUI::NUIPoint&, float, float, const AestraUI::NUIColor&) override {}
    void drawLine(const AestraUI::NUIPoint&, const AestraUI::NUIPoint&, float, const AestraUI::NUIColor&) override {}
    void drawPolyline(const AestraUI::NUIPoint*, int, float, const AestraUI::NUIColor&) override {}
    void fillWaveform(const AestraUI::NUIPoint*, const AestraUI::NUIPoint*, int, const AestraUI::NUIColor&) override {}
    void fillWaveformGradient(const AestraUI::NUIPoint*, const AestraUI::NUIPoint*, int, const AestraUI::NUIColor&,
                              const AestraUI::NUIColor&) override {}
    void fillRectGradient(const AestraUI::NUIRect&, const AestraUI::NUIColor&, const AestraUI::NUIColor&,
                          bool = true) override {}
    void fillCircleGradient(const AestraUI::NUIPoint&, float, const AestraUI::NUIColor&,
                            const AestraUI::NUIColor&) override {}
    void drawGlow(const AestraUI::NUIRect&, float, float, const AestraUI::NUIColor&) override {}
    void drawShadow(const AestraUI::NUIRect&, float, float, float, const AestraUI::NUIColor&) override {}
    void drawText(const std::string&, const AestraUI::NUIPoint&, float, const AestraUI::NUIColor&) override {}
    void drawTextCentered(const std::string&, const AestraUI::NUIRect&, float, const AestraUI::NUIColor&) override {}
    AestraUI::NUISize measureText(const std::string& text, float fontSize) override {
        return {static_cast<float>(text.size()) * fontSize * 0.6f, fontSize};
    }
    void drawTexture(uint32_t, const AestraUI::NUIRect&, const AestraUI::NUIRect&) override {}
    void drawTexture(const AestraUI::NUIRect&, const unsigned char*, int, int) override {}
    uint32_t loadTexture(const std::string&) override { return 0; }
    uint32_t createTexture(const uint8_t*, int, int) override { return 0; }
    void deleteTexture(uint32_t) override {}
    void beginBatch() override {}
    void endBatch() override {}
    void flush() override {}
    void setCachingEnabled(bool) override {}
    AestraUI::NUIRenderCache* getRenderCache() override { return nullptr; }
    void invalidateCache(uint64_t) override {}
    bool renderCachedOrUpdate(uint64_t, const AestraUI::NUIRect&, const std::function<void()>&) override {
        return false;
    }
    int getWidth() const override { return 1366; }
    int getHeight() const override { return 768; }
    const char* getBackendName() const override { return "null"; }
};

} // namespace Aestra::Testing
