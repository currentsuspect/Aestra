// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TextDiagnosticOverlay.h
 * @brief F11-toggleable text pipeline diagnostic (V8-C9, governed by FD-21)
 *
 * WHAT THIS IS FOR
 *
 * Aestra's text has looked wrong on light themes for a long time, and the
 * reason tuning never converged is that the pipeline has four independent
 * causes and one shared knob. Screenshots could show that text looked wrong;
 * they could not say which mechanism made it wrong, because every variable
 * moves at once — theme luminance sets textContrast_, which resolves into two
 * shader uniforms, while the atlas tier is chosen by a threshold table and the
 * blend colour space depends on whether the widget happened to be cached.
 *
 * So this overlay is not a screenshot of the result. It does two things a
 * screenshot cannot:
 *
 *   1. Reports the pipeline's own state — the actual variable values and the
 *      uniforms they resolve to, read from the renderer rather than restated
 *      here. If the panel disagrees with the rendering, the panel is right
 *      about what the code intends and the rendering is the bug.
 *
 *   2. Varies exactly one thing per row. A difference between two samples in
 *      the same section is attributable to that section's variable and nothing
 *      else, which is what makes a visual judgement evidence instead of an
 *      impression.
 *
 * Sections A-D correspond to the audit's findings: A is the atlas tiers behind
 * F5, B is the alpha lift behind F7, C is the polarity compensation behind F3,
 * and D is the cached/uncached blend-space split that is F3 itself.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO
 *
 * It changes no rendering behaviour and fixes nothing. Per FD-21 the ordering
 * is binding: establish the measured behaviour against the contract first. A
 * defect becoming visible here is not permission to tune it.
 *
 * LCD versus grayscale is absent by design rather than oversight. fontUseLCD_
 * is read inside buildAtlas, so switching it rebuilds all four atlases — a
 * restart-scoped toggle (AESTRA_DISABLE_LCD=1), not a column. The panel reports
 * which mode is live so a two-run comparison stays attributable.
 */

#pragma once

#include "../../AestraUI/Core/NUIComponent.h"
#include "../../AestraUI/Graphics/NUIRenderer.h"

#include <string>

/**
 * @brief Read-only text pipeline diagnostic overlay.
 */
class TextDiagnosticOverlay : public AestraUI::NUIComponent {
public:
    static constexpr float PANEL_WIDTH = 470.0f;
    // Floor, not the height. The panel sizes itself to its content — see
    // m_contentHeight. A fixed height is what let the last section render
    // outside the background: NUIComponent::renderChildren does not clip, so
    // overflow is silent, and every row added to a section would re-introduce
    // it. Sized from the content, adding a row cannot break the panel.
    static constexpr float PANEL_MIN_HEIGHT = 560.0f;
    static constexpr float PADDING = 12.0f;
    static constexpr float LINE = 13.0f;

    TextDiagnosticOverlay();
    ~TextDiagnosticOverlay() override = default;

    void toggle() { m_visible = !m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    void onUpdate(double deltaTime) override;
    void onRender(AestraUI::NUIRenderer& renderer) override;

private:
    // The string every sample draws. Mixed ascender/descender/round/thin-stem
    // so stem weight, baseline placement and the 'e' crossbar are all visible;
    // digits included because tabular figures are where weight changes read
    // worst in a dense session.
    static constexpr const char* SAMPLE = "Handgloves 0369";

    void renderBackground(AestraUI::NUIRenderer& renderer);
    void sectionRule(AestraUI::NUIRenderer& renderer, const char* label, float& y);
    void keyValue(AestraUI::NUIRenderer& renderer, const char* key, const std::string& value,
                  float& y, bool flag = false, bool flagIsBad = false);

    void renderState(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRenderer::TextDiagnostics& d, float& y);
    void renderTierTable(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRenderer::TextDiagnostics& d, float& y);
    void renderAtlasTierSamples(AestraUI::NUIRenderer& renderer, float& y);
    void renderAlphaRamp(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRenderer::TextDiagnostics& d, float& y);
    void renderPolarity(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRenderer::TextDiagnostics& d, float& y);
    void renderCacheSplit(AestraUI::NUIRenderer& renderer, float& y);

    bool m_visible{false};
    // Measured at the end of onRender and applied by the next onUpdate. The
    // first frame after a toggle can therefore be one layout behind; that is
    // acceptable for a panel nobody screenshots on frame one, and it is the
    // price of not maintaining a second, drifting copy of the layout.
    float m_contentHeight{PANEL_MIN_HEIGHT};
};
