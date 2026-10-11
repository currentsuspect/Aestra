// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TextDiagnosticOverlay.h"

#include "../../AestraUI/Core/NUIThemeSystem.h"
#include "../../AestraUI/Graphics/OpenGL/NUIRenderCache.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using AestraUI::NUIColor;
using AestraUI::NUIPoint;
using AestraUI::NUIRect;
using AestraUI::NUIRenderer;

namespace {

constexpr float kLabelSize = 9.0f;
constexpr float kValueSize = 10.0f;

// Fixed, theme-independent panel colours. The panel reports on the theme, so it
// must not be repainted by it — a diagnostic whose own contrast moves with the
// thing it measures cannot be used to judge that thing.
const NUIColor kPanelBg(0.03f, 0.035f, 0.05f, 0.96f);
const NUIColor kRule(0.30f, 0.34f, 0.40f, 0.55f);
const NUIColor kHeading(0.40f, 0.80f, 0.95f, 1.0f);
const NUIColor kLabel(0.58f, 0.60f, 0.66f, 1.0f);
const NUIColor kValue(0.90f, 0.92f, 0.95f, 1.0f);
const NUIColor kGood(0.40f, 0.85f, 0.55f, 1.0f);
const NUIColor kBad(0.95f, 0.45f, 0.40f, 1.0f);
const NUIColor kSampleInk(0.88f, 0.90f, 0.93f, 1.0f);
const NUIColor kDarkGround(0.06f, 0.07f, 0.09f, 1.0f);
const NUIColor kLightGround(0.93f, 0.93f, 0.92f, 1.0f);
const NUIColor kDarkInk(0.10f, 0.11f, 0.13f, 1.0f);

std::string f(float v, int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

} // namespace

TextDiagnosticOverlay::TextDiagnosticOverlay() {
    setBounds(NUIRect(0, 0, PANEL_WIDTH, PANEL_MIN_HEIGHT));
}

void TextDiagnosticOverlay::onUpdate(double /*deltaTime*/) {
    if (!m_visible) return;
    if (auto* parent = getParent()) {
        const auto pb = parent->getBounds();
        const float h = std::max(PANEL_MIN_HEIGHT, m_contentHeight);
        setBounds(NUIRect(pb.width - PANEL_WIDTH - 10.0f, 35.0f, PANEL_WIDTH, h));
    }
}

void TextDiagnosticOverlay::renderBackground(NUIRenderer& renderer) {
    renderer.fillRoundedRect(getBounds(), 8.0f, kPanelBg);
    renderer.strokeRoundedRect(getBounds(), 8.0f, 1.5f, kHeading.withAlpha(0.5f));
}

void TextDiagnosticOverlay::sectionRule(NUIRenderer& renderer, const char* label, float& y) {
    const float x = getBounds().x + PADDING;
    y += 6.0f;
    renderer.drawText(label, NUIPoint(x, y), kLabelSize, kHeading);
    const float ruleY = y + 4.0f;
    // Rule starts past the label rather than under it; measureText is the only
    // honest way to know where that is, and it exercises the same metrics path
    // the samples below use.
    const float labelWidth = renderer.measureText(label, kLabelSize).width;
    renderer.drawLine(NUIPoint(x + labelWidth + 8.0f, ruleY),
                      NUIPoint(getBounds().x + PANEL_WIDTH - PADDING, ruleY), 1.0f, kRule);
    y += LINE + 2.0f;
}

void TextDiagnosticOverlay::keyValue(NUIRenderer& renderer, const char* key, const std::string& value,
                                     float& y, bool flag, bool flagIsBad) {
    const float x = getBounds().x + PADDING;
    renderer.drawText(key, NUIPoint(x, y), kLabelSize, kLabel);
    const NUIColor c = flag ? (flagIsBad ? kBad : kGood) : kValue;
    renderer.drawText(value, NUIPoint(x + 118.0f, y), kValueSize, c);
    y += LINE;
}

void TextDiagnosticOverlay::renderState(NUIRenderer& renderer, const NUIRenderer::TextDiagnostics& d, float& y) {
    sectionRule(renderer, "PIPELINE STATE", y);

    keyValue(renderer, "rasteriser",
             d.lcdSubpixel ? "FreeType · LCD subpixel (filtered)" : "FreeType · grayscale",
             y);
    // The shader collapses the LCD triplet to one scalar, so the filter's blur
    // is paid for and its resolution discarded (F1, measured 2026-09-12).
    if (d.lcdSubpixel) {
        keyValue(renderer, "", "subpixel data is averaged away in the shader (F1)", y, true, true);
    }
    keyValue(renderer, "textContrast_", f(d.textContrast), y);
    keyValue(renderer, "caller alpha",
             d.alphaPreserved ? "preserved — multiplied, never reshaped"
                              : "RESHAPED — see section B",
             y, true, !d.alphaPreserved);
    keyValue(renderer, "blend space",
             d.framebufferSRGB ? "LINEAR — split from the cache, see F3"
                               : "gamma (sRGB) — cached and direct agree",
             y, true, d.framebufferSRGB);
}

void TextDiagnosticOverlay::renderTierTable(NUIRenderer& renderer, const NUIRenderer::TextDiagnostics& d, float& y) {
    sectionRule(renderer, "RESOLVED UNIFORMS — what the shader will use", y);
    const float x = getBounds().x + PADDING;
    const float cols[] = {0.0f, 70.0f, 150.0f, 240.0f, 320.0f};

    renderer.drawText("tier", NUIPoint(x + cols[0], y), kLabelSize, kLabel);
    renderer.drawText("atlas", NUIPoint(x + cols[1], y), kLabelSize, kLabel);
    renderer.drawText("serves", NUIPoint(x + cols[2], y), kLabelSize, kLabel);
    renderer.drawText("gamma", NUIPoint(x + cols[3], y), kLabelSize, kLabel);
    renderer.drawText("sharpen", NUIPoint(x + cols[4], y), kLabelSize, kLabel);
    y += LINE;

    for (int i = 0; i < d.tierCount; ++i) {
        const auto& t = d.tiers[i];
        std::ostringstream serves;
        if (t.maxFontSize > 0.0f) serves << "<= " << f(t.maxFontSize, 2) << " px";
        else serves << "above";

        // The small tier is baked at 16 px and serves up to 17 px — it is
        // magnified, not supersampled, yet still gets the aggressive tiny-atlas
        // treatment (F5). Flagged where it is visible rather than in a comment.
        const bool magnifies = (t.maxFontSize > 0.0f && t.atlasSize > 0 &&
                                t.maxFontSize > static_cast<float>(t.atlasSize));

        renderer.drawText(t.name, NUIPoint(x + cols[0], y), kValueSize, magnifies ? kBad : kValue);
        renderer.drawText(std::to_string(t.atlasSize) + " px", NUIPoint(x + cols[1], y), kValueSize, kValue);
        renderer.drawText(serves.str(), NUIPoint(x + cols[2], y), kValueSize, magnifies ? kBad : kValue);
        renderer.drawText(f(t.gamma), NUIPoint(x + cols[3], y), kValueSize, kValue);
        renderer.drawText(f(t.sharpen, 2), NUIPoint(x + cols[4], y), kValueSize, kValue);
        y += LINE;
    }
}

void TextDiagnosticOverlay::renderAtlasTierSamples(NUIRenderer& renderer, float& y) {
    sectionRule(renderer, "A · ATLAS TIER — one string, five sizes", y);
    const float x = getBounds().x + PADDING;
    // Sizes straddle every threshold in selectAtlas(), so consecutive rows that
    // look different in kind rather than degree are crossing a tier boundary.
    const float sizes[] = {10.0f, 11.0f, 12.0f, 17.0f, 18.0f, 24.0f};
    for (float s : sizes) {
        renderer.drawText(f(s, 0) + " px", NUIPoint(x, y), kLabelSize, kLabel);
        renderer.drawText(SAMPLE, NUIPoint(x + 48.0f, y), s, kSampleInk);
        y += std::max(LINE, s + 3.0f);
    }
}

void TextDiagnosticOverlay::renderAlphaRamp(NUIRenderer& renderer, const NUIRenderer::TextDiagnostics& d, float& y) {
    sectionRule(renderer, "B · ALPHA RAMP — caller alpha must survive the pipeline", y);
    const float x = getBounds().x + PADDING;

    // This section used to predict a distortion. It now asserts its absence,
    // which is the shape a regression check wants: the renderer reports whether
    // it reshapes caller alpha, and the ramp shows what that looks like.
    //
    // The old 0.35 light-theme lift is still computed, as the counter-example.
    // Keeping it visible is the point — it is what a reintroduced compensation
    // would do, and the "was" column is the only thing that makes "is" legible.
    constexpr float kRemovedLift = 0.35f;
    constexpr float kLightThemeContrast = 0.88f;

    // Two rendered columns, not one. A single column proves nothing on a dark
    // theme: textContrast_ is 1.0 there, the removed lift resolved to 1.0 too,
    // and the old pipeline produced identity as well. The discriminating case
    // needs alpha < 1 AND the light-theme contrast at the same time, which no
    // other section produces — C varies contrast at full alpha, and this used to
    // vary alpha at whatever contrast happened to be live.
    //
    // So the right column is drawn under textContrast_ = 0.88. Before the fix it
    // rendered visibly heavier than the left at every step below 1.0; the two
    // columns matching is the proof, and it holds on either theme.
    // Offsets must match the sample draws below exactly — 48 / 190 / 410. A
    // column heading over the wrong column is worse in a diagnostic than no
    // heading at all: it produces a confident misattribution, which is the one
    // failure this panel exists to prevent.
    renderer.drawText("at live contrast", NUIPoint(x + 48.0f, y), kLabelSize, kLabel);
    renderer.drawText("at 0.88 (light)", NUIPoint(x + 190.0f, y), kLabelSize, kLabel);
    renderer.drawText("was", NUIPoint(x + 410.0f, y), kLabelSize, kLabel);
    y += LINE;

    const float alphas[] = {1.0f, 0.75f, 0.5f, 0.25f};
    for (float a : alphas) {
        renderer.drawText("a=" + f(a, 2), NUIPoint(x, y), kLabelSize, kLabel);

        renderer.drawText(SAMPLE, NUIPoint(x + 48.0f, y), 11.0f, kSampleInk.withAlpha(a));
        renderer.flush();
        renderer.setTextContrast(kLightThemeContrast);
        renderer.drawText(SAMPLE, NUIPoint(x + 190.0f, y), 11.0f, kSampleInk.withAlpha(a));
        renderer.flush();
        renderer.setTextContrast(d.textContrast);
        renderer.flush();

        renderer.drawText("(" + f(std::pow(a, kRemovedLift)) + ")",
                          NUIPoint(x + 410.0f, y), kLabelSize, kLabel);
        y += LINE + 3.0f;
    }

    // The assertion, stated so a reader does not have to infer it from four rows
    // of numbers agreeing.
    // State the check rather than claim a verdict: the panel cannot read back
    // its own pixels, so it says what a reader must look for. Both columns
    // fading in step with a is the pass; either one staying flat is the F7
    // regression. The columns are not expected to be identical — 0.88 contrast
    // legitimately thickens strokes through uTextGamma — but their ramps must
    // have the same shape.
    renderer.drawText(d.alphaPreserved
                          ? "both columns must fade in step with a; a flat column = renderer overriding alpha"
                          : "FAIL — the renderer reports it is reshaping caller alpha (F7 regression)",
                      NUIPoint(x + 48.0f, y), kLabelSize, d.alphaPreserved ? kGood : kBad);
    y += LINE;
}

void TextDiagnosticOverlay::renderPolarity(NUIRenderer& renderer, const NUIRenderer::TextDiagnostics& d, float& y) {
    sectionRule(renderer, "C · POLARITY x COMPENSATION — 2x2, one variable per axis", y);
    const float x = getBounds().x + PADDING;

    // A 2x2 and not two rows, deliberately. Ground polarity and textContrast_
    // are different mechanisms — one is how the glyph composites against what is
    // behind it, the other is the compensation the frame loop applies for it —
    // and varying both together produces a difference that cannot be assigned to
    // either. The off-diagonal cells are the whole point: light ground at 1.00
    // is the uncompensated case, dark ground at 0.88 is the compensation with
    // nothing to compensate for.
    const float colW = (PANEL_WIDTH - PADDING * 2.0f - 62.0f) * 0.5f;
    const float rowH = 19.0f;

    renderer.drawText("contrast 1.00", NUIPoint(x + 62.0f, y), kLabelSize, kLabel);
    renderer.drawText("contrast 0.88", NUIPoint(x + 62.0f + colW, y), kLabelSize, kLabel);
    y += LINE;

    struct GroundRow { const char* label; NUIColor ground; NUIColor ink; };
    const GroundRow grounds[] = {
        {"on dark",  kDarkGround,  kSampleInk},
        {"on light", kLightGround, kDarkInk},
    };
    const float contrasts[] = {1.0f, 0.88f};

    for (const auto& g : grounds) {
        renderer.drawText(g.label, NUIPoint(x, y + 4.0f), kLabelSize, kLabel);
        for (int c = 0; c < 2; ++c) {
            const float cx = x + 62.0f + colW * static_cast<float>(c);
            renderer.fillRect(NUIRect(cx, y, colW - 4.0f, rowH), g.ground);
            // textContrast_ resolves into uniforms uploaded with the batch, so
            // the batch must be closed between cells or all four render with
            // whichever value was set last. That is the only reason flush()
            // appears in this file.
            renderer.flush();
            renderer.setTextContrast(contrasts[c]);
            renderer.drawText(SAMPLE, NUIPoint(cx + 4.0f, y + 4.0f), 11.0f, g.ink);
            renderer.flush();
        }
        y += rowH + 3.0f;
    }

    // Leave the renderer exactly as found: this panel reports, it does not
    // configure. The frame loop sets this every frame anyway, but relying on
    // that would make the overlay's correctness depend on a caller it does not
    // own — and an overlay that leaves global state altered is a diagnostic that
    // changes the thing it measures.
    renderer.setTextContrast(d.textContrast);
    renderer.flush();
}

void TextDiagnosticOverlay::renderCacheSplit(NUIRenderer& renderer, float& y) {
    sectionRule(renderer, "D · RENDER CACHE — same blend space as direct since F3", y);
    const float x = getBounds().x + PADDING;

    renderer.drawText("direct", NUIPoint(x, y), kLabelSize, kLabel);
    renderer.drawText(SAMPLE, NUIPoint(x + 60.0f, y), 12.0f, kSampleInk);
    y += LINE + 2.0f;

    auto* cache = renderer.getRenderCache();
    if (cache == nullptr || !cache->isEnabled()) {
        // Not a failure to report around: on Linux the cache is disabled
        // outright (V8-C2), which is itself the answer for this row.
        renderer.drawText("cached", NUIPoint(x, y), kLabelSize, kLabel);
        renderer.drawText(cache == nullptr ? "no cache on this backend"
                                           : "render cache disabled on this platform (V8-C2)",
                          NUIPoint(x + 60.0f, y), kValueSize, kBad);
        y += LINE + 2.0f;
        return;
    }

    renderer.drawText("cached", NUIPoint(x, y), kLabelSize, kLabel);
    renderer.drawText("enabled — compare against 'direct' above", NUIPoint(x + 60.0f, y), kValueSize, kGood);
    y += LINE + 2.0f;
}

void TextDiagnosticOverlay::onRender(NUIRenderer& renderer) {
    if (!m_visible) return;

    renderBackground(renderer);

    float y = getBounds().y + PADDING;
    const float x = getBounds().x + PADDING;

    renderer.drawText("TEXT PIPELINE DIAGNOSTICS", NUIPoint(x, y), 11.0f, kHeading);
    renderer.drawText("FD-21 · V8-C9 · reporting only", NUIPoint(x + 200.0f, y), kLabelSize, kLabel);
    y += LINE + 4.0f;

    NUIRenderer::TextDiagnostics d;
    if (!renderer.getTextDiagnostics(d)) {
        renderer.drawText("Text diagnostics unavailable: no font atlas is built.",
                          NUIPoint(x, y), kValueSize, kBad);
        return;
    }

    renderState(renderer, d, y);
    renderTierTable(renderer, d, y);
    renderAtlasTierSamples(renderer, y);
    renderAlphaRamp(renderer, d, y);
    renderPolarity(renderer, d, y);
    renderCacheSplit(renderer, y);

    // What the next frame sizes the panel to. Taken from where the layout
    // actually finished rather than from a constant restating it.
    m_contentHeight = (y - getBounds().y) + PADDING;
}
