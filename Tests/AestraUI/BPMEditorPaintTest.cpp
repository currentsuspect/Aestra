// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// BPM editor paint probe: double-clicking the BPM value must produce an editor
// that actually emits visible paint — text with ink that contrasts its field,
// positioned inside its own bounds. Logic-only tests proved the editor opens
// with "120"; this pins the render contract after the invisible-field defect.

#include "../../Source/Components/TransportInfoContainer.h"
#include "../../AestraUI/Base/NUITextInput.h"
#include "../../AestraUI/Graphics/NUIRenderer.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

struct PaintCall {
    std::string kind;
    std::string text;
    NUIRect rect{};
    NUIPoint point{};
    NUIColor color{};
};

class RecordingRenderer : public NUIRenderer {
public:
    std::vector<PaintCall> calls;

    bool initialize(int, int) override { return true; }
    void shutdown() override {}
    void resize(int, int) override {}
    void beginFrame() override {}
    void endFrame() override {}
    void clear(const NUIColor&) override {}
    void pushTransform(float, float, float = 1.0f) override {}
    void popTransform() override {}
    void setClipRect(const NUIRect&) override {}
    void clearClipRect() override {}
    void setOpacity(float) override {}
    void fillRect(const NUIRect& rect, const NUIColor& color) override {
        calls.push_back({"fillRect", "", rect, {}, color});
    }
    void fillRoundedRect(const NUIRect& rect, float, const NUIColor& color) override {
        calls.push_back({"fillRoundedRect", "", rect, {}, color});
    }
    void strokeRect(const NUIRect&, float, const NUIColor&) override {}
    void strokeRoundedRect(const NUIRect&, float, float, const NUIColor&) override {}
    void fillCircle(const NUIPoint&, float, const NUIColor&) override {}
    void strokeCircle(const NUIPoint&, float, float, const NUIColor&) override {}
    void drawLine(const NUIPoint&, const NUIPoint&, float, const NUIColor&) override {}
    void drawPolyline(const NUIPoint*, int, float, const NUIColor&) override {}
    void fillWaveform(const NUIPoint*, const NUIPoint*, int, const NUIColor&) override {}
    void fillWaveformGradient(const NUIPoint*, const NUIPoint*, int, const NUIColor&,
                              const NUIColor&) override {}
    void fillRectGradient(const NUIRect&, const NUIColor&, const NUIColor&, bool = true) override {}
    void fillCircleGradient(const NUIPoint&, float, const NUIColor&, const NUIColor&) override {}
    void drawGlow(const NUIRect&, float, float, const NUIColor&) override {}
    void drawShadow(const NUIRect&, float, float, float, const NUIColor&) override {}
    void drawText(const std::string& text, const NUIPoint& position, float fontSize,
                  const NUIColor& color) override {
        calls.push_back({"drawText", text, {position.x, position.y, fontSize, 0.0f}, position, color});
        (void)fontSize;
    }
    void drawTextCentered(const std::string& text, const NUIRect& rect, float,
                          const NUIColor& color) override {
        calls.push_back({"drawTextCentered", text, rect, {}, color});
    }
    NUISize measureText(const std::string& text, float fontSize) override {
        return {static_cast<float>(text.size()) * fontSize * 0.6f, fontSize};
    }
    void drawTexture(uint32_t, const NUIRect&, const NUIRect&) override {}
    void drawTexture(const NUIRect&, const unsigned char*, int, int) override {}
    uint32_t loadTexture(const std::string&) override { return 0; }
    uint32_t createTexture(const uint8_t*, int, int) override { return 0; }
    void deleteTexture(uint32_t) override {}
    void beginBatch() override {}
    void endBatch() override {}
    void flush() override {}
    void setDirtyRegionTrackingEnabled(bool) override {}
    void setCachingEnabled(bool) override {}
    NUIDirtyRegionManager* getDirtyRegionManager() override { return nullptr; }
    NUIRenderCache* getRenderCache() override { return nullptr; }
    void invalidateCache(uint64_t) override {}
    void getOptimizationStats(size_t&, size_t&, size_t&, size_t&) override {}
    bool renderCachedOrUpdate(uint64_t, const NUIRect&, const std::function<void()>&) override {
        return false;
    }
    int getWidth() const override { return 1366; }
    int getHeight() const override { return 768; }
    const char* getBackendName() const override { return "record"; }
};

float luminance(const NUIColor& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

NUIMouseEvent mousePress(float x, float y) {
    NUIMouseEvent e;
    e.position = NUIPoint(x, y);
    e.button = NUIMouseButton::Left;
    e.pressed = true;
    return e;
}

NUIMouseEvent mouseRelease(float x, float y) {
    NUIMouseEvent e;
    e.position = NUIPoint(x, y);
    e.button = NUIMouseButton::Left;
    e.released = true;
    return e;
}

NUITextInput* findEditor(Aestra::BPMDisplay& bpm) {
    for (const auto& child : bpm.getChildren()) {
        if (auto* input = dynamic_cast<NUITextInput*>(child.get())) {
            return input;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    Aestra::BPMDisplay bpm;
    bpm.setBounds(NUIRect(0.0f, 0.0f, 90.0f, 28.0f));
    bpm.setBPM(120.0f);

    bpm.onMouseEvent(mousePress(10.0f, 18.0f));
    bpm.onMouseEvent(mouseRelease(10.0f, 18.0f));
    bpm.onMouseEvent(mousePress(10.0f, 18.0f));
    bpm.onMouseEvent(mouseRelease(10.0f, 18.0f));

    NUITextInput* editor = findEditor(bpm);
    expect(editor != nullptr, "double-click opens the BPM editor");
    if (editor == nullptr) {
        std::cerr << g_failures << " BPMEditorPaint test(s) failed.\n";
        return 1;
    }
    expect(editor->getText() == "120", "editor prefilled with 120");
    expect(editor->isVisible(), "editor visible");
    expect(editor->isFocused(), "editor takes focus");

    RecordingRenderer rec;
    // Paint through the PARENT, like the live traversal does: calling the
    // editor directly would bypass a missing renderChildren and prove nothing.
    bpm.onRender(rec);

    const PaintCall* textCall = nullptr;
    const PaintCall* bgCall = nullptr;
    for (const auto& c : rec.calls) {
        if ((c.kind == "drawText" || c.kind == "drawTextCentered") && c.text == "120" && !textCall) {
            textCall = &c;
        }
        if ((c.kind == "fillRoundedRect" || c.kind == "fillRect") && !bgCall) {
            bgCall = &c;
        }
    }
    expect(textCall != nullptr, "editor paint emits the 120 glyphs");
    if (textCall != nullptr) {
        expect(textCall->color.a > 0.05f, "glyph ink is opaque enough to see");
        const NUIRect eb = editor->getBounds();
        const bool insideX = textCall->point.x >= eb.x - 1.0f && textCall->point.x <= eb.right() + 1.0f;
        const bool insideY = textCall->point.y >= eb.y - 1.0f && textCall->point.y <= eb.bottom() + 1.0f;
        expect(insideX && insideY, "glyph origin lands inside the editor bounds");
        std::cout << "[INFO] glyphs at (" << textCall->point.x << "," << textCall->point.y << ") rgba("
                  << textCall->color.r << "," << textCall->color.g << "," << textCall->color.b << ","
                  << textCall->color.a << ") editor rect (" << eb.x << "," << eb.y << "," << eb.width
                  << "," << eb.height << ")\n";
        if (bgCall != nullptr) {
            const float contrast =
                std::fabs(luminance(textCall->color) - luminance(bgCall->color));
            std::cout << "[INFO] text/bg contrast (lum delta): " << contrast << "\n";
            expect(contrast > 0.08f, "glyph ink contrasts the field background");
        }
    } else {
        std::cout << "[INFO] paint calls emitted: " << rec.calls.size() << "\n";
        for (const auto& c : rec.calls) {
            std::cout << "[INFO]   " << c.kind << " text='" << c.text << "' rgba(" << c.color.r << ","
                      << c.color.g << "," << c.color.b << "," << c.color.a << ")\n";
        }
    }

    if (g_failures == 0) {
        std::cout << "All BPMEditorPaint tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " BPMEditorPaint test(s) failed.\n";
    return 1;
}
