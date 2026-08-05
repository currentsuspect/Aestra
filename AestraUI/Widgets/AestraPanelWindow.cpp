// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraPanelWindow.h"
#include "../Platform/NUIPlatformBridge.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

AestraPanelWindow::AestraPanelWindow()
{
    cacheThemeColors();
}

AestraPanelWindow::~AestraPanelWindow()
{
    // Torn down mid-drag: cancel the capture so the bridge never routes to a
    // dangling owner and the cursor is never stranded hidden.
    if (m_platformBridge && m_platformBridge->isCursorCaptureOwner(this)) {
        m_platformBridge->cancelCursorCapture();
    }
}

void AestraPanelWindow::beginKnobCapture(const NUIPoint& knobCenter, const NUIPoint& grabPos)
{
    if (!m_platformBridge) return;
    m_captureKnobCenter = knobCenter;
    m_platformBridge->beginCursorCapture(
        this, AestraUI::NUICursorRestorePolicy::KnobCenter,
        static_cast<int>(grabPos.x), static_cast<int>(grabPos.y));
}

void AestraPanelWindow::endKnobCapture()
{
    if (!m_platformBridge) return;
    m_platformBridge->endCursorCapture(
        static_cast<int>(m_captureKnobCenter.x), static_cast<int>(m_captureKnobCenter.y));
}

float AestraPanelWindow::knobDragStep(const NUIMouseEvent& event, float rangePx) const
{
    // Shift = surgical precision (quarter speed). Held state is read per frame
    // so it can be toggled mid-drag. Default (no modifier) speed is unchanged.
    constexpr float kFineDragScale = 0.25f;
    const float fine = (event.modifiers & NUIModifiers::Shift) ? kFineDragScale : 1.0f;
    return (-event.delta.y / rangePx) * fine;
}

void AestraPanelWindow::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("backgroundPrimary");
    m_border = theme.getColor("borderSubtle");
    m_textSecondary = theme.getColor("textSecondary");
    m_textTertiary = theme.getColor("textDisabled");
    m_closeHover = theme.getColor("error").withAlpha(0.78f);
    m_closeActive = theme.getColor("error");
}

void AestraPanelWindow::setPanelTitle(const std::string& title)
{
    m_title = title;
    setDirty(true);
}

void AestraPanelWindow::setBadgeText(const std::string& text)
{
    m_badgeText = text;
    setDirty(true);
}

void AestraPanelWindow::setPlatformBridge(NUIPlatformBridge* bridge)
{
    m_platformBridge = bridge;
}

NUIRect AestraPanelWindow::getContentRect() const
{
    auto b = getBounds();
    return NUIRect(b.x, b.y + TITLE_BAR_H, b.width, std::max(0.0f, b.height - TITLE_BAR_H));
}

bool AestraPanelWindow::hitTestCloseButton(float x, float y) const
{
    auto b = getBounds();
    NUIRect closeBtn{b.right() - 28.0f, b.y + 4.0f, 24.0f, 24.0f};
    return closeBtn.contains({x, y});
}

bool AestraPanelWindow::hitTestTitleBar(float x, float y) const
{
    auto b = getBounds();
    return y >= b.y && y < b.y + TITLE_BAR_H && x >= b.x && x < b.right();
}

void AestraPanelWindow::drawTitleBar(NUIRenderer& renderer)
{
    auto b = getBounds();
    if (b.isEmpty()) return;

    // Bottom border only — body fillRoundedRect already covered the top area
    renderer.drawLine({b.x, b.y + TITLE_BAR_H - 0.5f},
                      {b.right(), b.y + TITLE_BAR_H - 0.5f},
                      0.5f, m_border);

    // Title text — 12px, left-aligned at x=12, vertically centered via font metrics
    if (!m_title.empty()) {
        NUIRect textRect{b.x, b.y, b.width, TITLE_BAR_H};
        float titleBaseline = renderer.calculateTextY(textRect, 12.0f);
        renderer.drawText(m_title,
                          {b.x + 12.0f, titleBaseline},
                          12.0f, m_textSecondary);
    }

    // Badge pill (right of title)
    if (!m_badgeText.empty()) {
        auto& theme = NUIThemeManager::getInstance();
        float badgeW = renderer.measureText(m_badgeText, 11.0f).width + 20.0f;
        float titleW = m_title.empty() ? 0.0f : renderer.measureText(m_title, 12.0f).width;
        NUIRect badgeRect{b.x + 12.0f + titleW + 8.0f,
                          b.y + (TITLE_BAR_H - 18.0f) * 0.5f,
                          badgeW, 18.0f};
        renderer.fillRoundedRect(badgeRect, 9.0f, theme.getColor("success").withAlpha(0.18f));
        renderer.strokeRoundedRect(badgeRect, 9.0f, 1.0f, theme.getColor("success").withAlpha(0.42f));
        renderer.drawTextCentered(m_badgeText, badgeRect, 11.0f, theme.getColor("success"));
    }

    // Close button — right-aligned at x=width-28
    NUIRect closeBtn{b.right() - 28.0f, b.y + 4.0f, 24.0f, 24.0f};
    if (m_closeHovered || m_closePressed) {
        renderer.fillRoundedRect(closeBtn, 6.0f, m_closeHover.withAlpha(m_closePressed ? 0.35f : 0.22f));
    }
    float cx = closeBtn.x + closeBtn.width * 0.5f;
    float cy = closeBtn.y + closeBtn.height * 0.5f;
    float d = 5.0f;
    NUIColor xColor = m_closeHovered ? m_closeActive : m_textTertiary;
    renderer.drawLine({cx - d, cy - d}, {cx + d, cy + d}, 1.5f, xColor);
    renderer.drawLine({cx + d, cy - d}, {cx - d, cy + d}, 1.5f, xColor);
}

void AestraPanelWindow::onRender(NUIRenderer& renderer)
{
    auto b = getBounds();
    if (b.isEmpty()) return;

    // Window body
    renderer.fillRoundedRect(b, kRadius, m_bg);

    // Content area (clipped, global coordinates)
    NUIRect content = getContentRect();
    if (!content.isEmpty()) {
        renderer.setClipRect(content);
        drawContent(renderer, content);
        renderer.clearClipRect();
    }

    // Title bar on top of content
    drawTitleBar(renderer);

    // Border stroke on top of everything
    renderer.strokeRoundedRect(b, kRadius, 1.0f, m_border);
}

bool AestraPanelWindow::onMouseEvent(const NUIMouseEvent& event)
{
    auto b = getBounds();

    // Close on outside click
    if (m_closeOnOutsideClick && event.pressed && event.button == NUIMouseButton::Left
        && !b.contains(event.position) && !m_isDraggingWindow) {
        if (m_onClose) m_onClose();
        return false;
    }

    // Close button interaction
    bool overClose = hitTestCloseButton(event.position.x, event.position.y);
    if (event.pressed && event.button == NUIMouseButton::Left && overClose) {
        m_closePressed = true;
        setDirty(true);
        return true;
    }
    if (event.released && event.button == NUIMouseButton::Left && m_closePressed) {
        m_closePressed = false;
        setDirty(true);
        if (overClose && m_onClose) {
            m_onClose();
        }
        return true;
    }
    if (overClose != m_closeHovered) {
        m_closeHovered = overClose;
        setDirty(true);
    }

    // Window drag from title bar
    if (event.pressed && event.button == NUIMouseButton::Left && hitTestTitleBar(event.position.x, event.position.y) && !overClose) {
        m_isDraggingWindow = true;
        m_dragStartPos = event.position;
        m_windowStartPos = {b.x, b.y};
        return true;
    }
    if (m_isDraggingWindow) {
        if (event.released && event.button == NUIMouseButton::Left) {
            if (m_enforceParentBounds) {
                m_userPositioned = true;
            }
            onDragEnd();
            m_isDraggingWindow = false;
            return true;
        }
        float dx = event.position.x - m_dragStartPos.x;
        float dy = event.position.y - m_dragStartPos.y;
        float newX = m_windowStartPos.x + dx;
        float newY = m_windowStartPos.y + dy;
        if (m_enforceParentBounds) {
            if (auto* parent = getParent()) {
                const auto pb = parent->getBounds();
                const float margin = 14.0f;
                newX = std::clamp(newX, pb.x + margin, std::max(pb.x + margin, pb.right() - margin - b.width));
                newY = std::clamp(newY, pb.y + margin, std::max(pb.y + margin, pb.bottom() - margin - b.height));
            }
        }
        setBounds(NUIRect(newX, newY, b.width, b.height));
        return true;
    }

    return false;
}

void AestraPanelWindow::onUpdate(double deltaTime)
{
    NUIComponent::onUpdate(deltaTime);

    if (!m_enforceParentBounds || isDraggingWindow()) return;

    if (auto* parent = getParent()) {
        const auto pb = parent->getBounds();
        const bool parentChanged = !m_haveParentSnapshot ||
            std::abs(pb.x - m_lastParentBounds.x) > 0.5f ||
            std::abs(pb.y - m_lastParentBounds.y) > 0.5f ||
            std::abs(pb.width - m_lastParentBounds.width) > 0.5f ||
            std::abs(pb.height - m_lastParentBounds.height) > 0.5f;
        if (parentChanged) {
            m_lastParentBounds = pb;
            m_haveParentSnapshot = true;
            enforceBoundsInParent();
        } else if (!m_haveParentSnapshot) {
            m_lastParentBounds = pb;
            m_haveParentSnapshot = true;
        }
    }
}

void AestraPanelWindow::enforceBoundsInParent(float safeMargin)
{
    auto* parent = getParent();
    if (!parent) return;

    const NUIRect pb = parent->getBounds();
    if (pb.isEmpty()) return;

    auto b = getBounds();
    float minX = pb.x + safeMargin;
    float minY = pb.y + safeMargin;
    float maxX = std::max(minX, pb.right() - safeMargin - b.width);
    float maxY = std::max(minY, pb.bottom() - safeMargin - b.height);
    float targetX = std::clamp(b.x, minX, maxX);
    float targetY = std::clamp(b.y, minY, maxY);

    if (std::abs(b.x - targetX) > 0.5f || std::abs(b.y - targetY) > 0.5f) {
        setPosition(targetX, targetY);
    }
}

} // namespace AestraUI
