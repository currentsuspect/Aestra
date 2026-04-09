// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPanel.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "MixerViewModel.h"
#include "MeterSnapshot.h"
#include "TrackManager.h"
#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kMinimapRadius = 7.0f;

NUIColor colorFromArgb(uint32_t argb, float alphaScale = 1.0f)
{
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >> 8) & 0xFF) / 255.0f;
    const float b = (argb & 0xFF) / 255.0f;
    return NUIColor(r, g, b, std::clamp(a * alphaScale, 0.0f, 1.0f));
}
}

UIMixerPanel::UIMixerPanel(std::shared_ptr<Aestra::MixerViewModel> viewModel,
                           std::shared_ptr<Aestra::Audio::TrackManager> trackManager)
    : m_viewModel(std::move(viewModel))
    , m_trackManager(std::move(trackManager))
{
    cacheThemeColors();

    // Inspector (pinned on right, before master).
    m_inspector = std::make_shared<UIMixerInspector>(m_viewModel.get());
    addChild(m_inspector);

    // Create master strip (pinned on right, does not scroll with channels).
    // Create master strip (pinned on right, does not scroll with channels).
    m_masterStrip = std::make_shared<UIMixerStrip>(0, 0, m_viewModel.get(), 
                                                   m_trackManager->getMeterSnapshots(), 
                                                   m_trackManager->getContinuousParams());
    m_masterStrip->onFXClicked = [this](uint32_t channelId) {
        if (m_viewModel) {
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));
        }
        if (m_inspector) {
            m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
        }
    };
    addChild(m_masterStrip);

    // Initial channel refresh
    refreshChannels();
}

void UIMixerPanel::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    // Deep Void Background from Theme
    m_backgroundColor = theme.getColor("backgroundPrimary");
    // Subtle Glass Separator
    m_separatorColor = theme.getColor("divider");
}

bool UIMixerPanel::channelLayoutMatchesViewModel() const
{
    if (!m_viewModel) {
        return m_strips.empty();
    }

    const size_t channelCount = m_viewModel->getChannelCount();
    if (m_strips.size() != channelCount) {
        return false;
    }

    for (size_t i = 0; i < channelCount; ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel || !m_strips[i] || m_strips[i]->getChannelId() != channel->id) {
            return false;
        }
    }

    return true;
}

void UIMixerPanel::refreshChannels()
{
    if (!m_viewModel) return;

    size_t channelCount = m_viewModel->getChannelCount();

    if (channelLayoutMatchesViewModel()) {
        layoutMeters();
        return;
    }

    for (auto& strip : m_strips) {
        removeChild(strip);
    }
    m_strips.clear();

    m_strips.reserve(channelCount);
    for (size_t i = 0; i < channelCount; ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel) continue;
        auto strip = std::make_shared<UIMixerStrip>(channel->id, static_cast<int>(i + 1), 
                                                    m_viewModel.get(), 
                                                    m_trackManager->getMeterSnapshots(), 
                                                    m_trackManager->getContinuousParams());
        strip->onFXClicked = [this](uint32_t channelId) {
            if (m_viewModel) {
                m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));
            }
            if (m_inspector) {
                m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
            }
        };
        m_strips.push_back(strip);
        addChild(strip);
    }

    // Ensure fixed panels stay on top for hit-testing/rendering.
    if (m_inspector) {
        removeChild(m_inspector);
        addChild(m_inspector);
    }
    if (m_masterStrip) {
        removeChild(m_masterStrip);
        addChild(m_masterStrip);
    }

    layoutMeters();
}

void UIMixerPanel::layoutMeters()
{
    auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    const float stripY = minimapRect.bottom() + MINIMAP_GAP;
    const float stripHeight = std::max(1.0f, bounds.bottom() - stripY - PADDING);

    // Layout master strip on the right.
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH; // No padding
    if (m_masterStrip) {
        m_masterStrip->setBounds(masterX, stripY, MASTER_STRIP_WIDTH, stripHeight);
        m_masterStrip->setVisible(true);
    }

    // Layout inspector just to the left of master.
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    if (m_inspector) {
        m_inspector->setBounds(inspectorX, stripY, INSPECTOR_WIDTH, stripHeight);
        m_inspector->setVisible(true);
        m_inspector->onResize(static_cast<int>(INSPECTOR_WIDTH), static_cast<int>(stripHeight));
    }

    // Layout channel strips to the left, keeping them out of the inspector/master area.
    const float left = bounds.x; // Start at 0, no padding
    const float right = inspectorX - STRIP_SPACING;
    const float visibleW = getChannelViewportWidth();
    const float contentW = getChannelContentWidth();
    const float maxScroll = getChannelMaxScroll();
    m_scrollX = std::clamp(m_scrollX, 0.0f, maxScroll);
    m_targetScrollX = std::clamp(m_targetScrollX, 0.0f, maxScroll);

    float x = left - m_scrollX;
    for (size_t i = 0; i < m_strips.size(); ++i) {
        float stripX = x + i * (STRIP_WIDTH + STRIP_SPACING);
        const bool visible = (stripX + STRIP_WIDTH) >= left && stripX <= right;
        m_strips[i]->setVisible(visible);
        m_strips[i]->setBounds(stripX, stripY, STRIP_WIDTH, stripHeight);
    }
}

void UIMixerPanel::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutMeters();
}

void UIMixerPanel::onUpdate(double deltaTime)
{
    const float maxScroll = getChannelMaxScroll();
    m_targetScrollX = std::clamp(m_targetScrollX, 0.0f, maxScroll);

    const float delta = m_targetScrollX - m_scrollX;
    if (std::abs(delta) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);
        m_scrollX += delta * ease;
        layoutMeters();
    } else if (std::abs(delta) > 0.0f) {
        m_scrollX = m_targetScrollX;
        layoutMeters();
    }

    if (m_viewModel && m_trackManager) {
        auto snapshots = m_trackManager->getMeterSnapshots();
        if (snapshots) {
            m_viewModel->updateMeters(*snapshots, deltaTime);
        }
        m_viewModel->updateInputDiagnostics(*m_trackManager, deltaTime);
    }

    // Update children
    updateChildren(deltaTime);
}

void UIMixerPanel::renderSeparators(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    float y1 = minimapRect.bottom() + MINIMAP_GAP;
    float y2 = bounds.y + bounds.height;

    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    const float left = bounds.x;
    const float right = inspectorX - STRIP_SPACING;

    // Draw separators between visible channel strips.
    for (size_t i = 1; i < m_strips.size(); ++i) {
        if (!m_strips[i] || !m_strips[i]->isVisible()) continue;
        const float x = m_strips[i]->getBounds().x - STRIP_SPACING / 2.0f;
        if (x < left || x > right) continue;
        renderer.drawLine({x, y1}, {x, y2}, 1.0f, m_separatorColor);
    }

    // Draw separator before inspector
    if (m_inspector && m_inspector->isVisible()) {
        renderer.drawLine({inspectorX - STRIP_SPACING, y1}, {inspectorX - STRIP_SPACING, y2}, 1.0f, m_separatorColor);
    }

    // Draw separator before master strip
    if (m_masterStrip && m_masterStrip->isVisible()) {
        renderer.drawLine({masterX - STRIP_SPACING, y1}, {masterX - STRIP_SPACING, y2}, 1.0f, m_separatorColor);
    }
}

void UIMixerPanel::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Background
    renderer.fillRect(bounds, m_backgroundColor);

    const NUIRect minimapRect = getMinimapRect();
    const NUIColor minimapFill = m_backgroundColor.withAlpha(0.72f);
    const NUIColor minimapBorder = m_separatorColor.withAlpha(0.70f);
    renderer.fillRoundedRect(minimapRect, kMinimapRadius, minimapFill);
    renderer.strokeRoundedRect(minimapRect, kMinimapRadius, 1.0f, minimapBorder);

    const float contentW = getChannelContentWidth();
    const float visibleW = getChannelViewportWidth();
    if (contentW > 0.0f && minimapRect.width > 8.0f) {
        const float gap = 2.0f;
        const float laneW = std::max(1.0f, minimapRect.width - gap * 2.0f);
        const float laneH = std::max(1.0f, minimapRect.height - gap * 2.0f);
        const float perStripW = static_cast<float>(STRIP_WIDTH + STRIP_SPACING) / contentW * laneW;
        const float barW = std::max(2.0f, perStripW - 1.0f);
        float x = minimapRect.x + gap;
        for (size_t i = 0; i < m_strips.size(); ++i) {
            const auto* channel = m_viewModel ? m_viewModel->getChannelById(m_strips[i]->getChannelId()) : nullptr;
            const bool selected = m_viewModel && m_viewModel->getSelectedChannelId() == static_cast<int32_t>(m_strips[i]->getChannelId());
            const NUIColor stripColor = channel ? colorFromArgb(channel->trackColor, selected ? 0.95f : 0.82f)
                                                : m_separatorColor.withAlpha(0.52f);

            const float activityDb = channel ? std::max(channel->smoothedPeakL, channel->smoothedPeakR) : -72.0f;
            const float activityNorm = std::clamp((activityDb + 60.0f) / 60.0f, 0.0f, 1.0f);
            const float capH = std::min(4.0f, laneH * 0.28f);
            const float bodyY = minimapRect.y + gap + capH + 1.0f;
            const float bodyH = std::max(1.0f, laneH - capH - 1.0f);
            const float activeH = std::max(1.0f, bodyH * activityNorm);

            const NUIRect fullRect(x, minimapRect.y + gap, barW, laneH);
            renderer.fillRoundedRect(fullRect, 2.5f, m_separatorColor.withAlpha(selected ? 0.24f : 0.14f));

            renderer.fillRoundedRect(
                NUIRect(x, minimapRect.y + gap, barW, capH),
                2.0f,
                stripColor);

            renderer.fillRoundedRect(
                NUIRect(x, bodyY + (bodyH - activeH), barW, activeH),
                2.0f,
                stripColor.withAlpha(selected ? 0.42f : 0.28f));

            if (selected) {
                renderer.strokeRoundedRect(fullRect, 3.0f, 1.0f, stripColor.withAlpha(0.55f));
            }
            x += perStripW;
        }

        const float maxScroll = getChannelMaxScroll();
        const float viewportRatio = std::clamp(contentW > 0.0f ? visibleW / contentW : 1.0f, 0.05f, 1.0f);
        const float viewportW = std::max(16.0f, laneW * viewportRatio);
        const float viewportX = minimapRect.x + gap +
            (maxScroll > 0.0f ? (m_scrollX / maxScroll) * std::max(0.0f, laneW - viewportW) : 0.0f);
        const NUIRect viewportRect(viewportX, minimapRect.y + 1.0f, viewportW, minimapRect.height - 2.0f);
        renderer.fillRoundedRect(viewportRect, 6.0f, NUIColor(0.72f, 0.76f, 1.0f, 0.10f));
        renderer.strokeRoundedRect(viewportRect, 6.0f, 1.25f, NUIColor(0.72f, 0.76f, 1.0f, 0.68f));
    }

    // Separators
    renderSeparators(renderer);

    // Render channel strips with a clip so they never draw into the inspector/master area.
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    const float channelW = std::max(0.0f, (inspectorX - STRIP_SPACING) - bounds.x);
    const float channelY = minimapRect.bottom() + MINIMAP_GAP;
    const NUIRect channelClip(bounds.x, channelY, channelW, std::max(0.0f, bounds.bottom() - channelY));

    bool clipEnabled = false;
    if (!channelClip.isEmpty()) {
        renderer.setClipRect(channelClip);
        clipEnabled = true;
    }

    for (const auto& strip : m_strips) {
        if (strip && strip->isVisible()) {
            strip->onRender(renderer);
        }
    }

    if (clipEnabled) {
        renderer.clearClipRect();
    }

    if (m_inspector && m_inspector->isVisible()) {
        m_inspector->onRender(renderer);
    }

    // Master strip renders on top / outside the clip.
    if (m_masterStrip && m_masterStrip->isVisible()) {
        m_masterStrip->onRender(renderer);
    }
}

bool UIMixerPanel::onMouseEvent(const NUIMouseEvent& event)
{
    const NUIRect minimapRect = getMinimapRect();
    const float contentW = getChannelContentWidth();
    const float visibleW = getChannelViewportWidth();
    const float maxScroll = getChannelMaxScroll();

    if (m_isDraggingMinimap) {
        if (event.released && event.button == NUIMouseButton::Left) {
            m_isDraggingMinimap = false;
            return true;
        }
        updateScrollFromMinimapX(event.position.x - m_minimapDragOffsetX);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && minimapRect.contains(event.position) && maxScroll > 0.0f) {
        const float gap = 2.0f;
        const float laneW = std::max(1.0f, minimapRect.width - gap * 2.0f);
        const float viewportRatio = std::clamp(contentW > 0.0f ? visibleW / contentW : 1.0f, 0.05f, 1.0f);
        const float viewportW = std::max(16.0f, laneW * viewportRatio);
        const float viewportX = minimapRect.x + gap +
            (maxScroll > 0.0f ? (m_scrollX / maxScroll) * std::max(0.0f, laneW - viewportW) : 0.0f);
        const NUIRect viewportRect(viewportX, minimapRect.y + 1.0f, viewportW, minimapRect.height - 2.0f);
        if (viewportRect.contains(event.position)) {
            m_isDraggingMinimap = true;
            m_minimapDragOffsetX = event.position.x - viewportRect.x;
        } else {
            m_minimapDragOffsetX = viewportW * 0.5f;
            updateScrollFromMinimapX(event.position.x - m_minimapDragOffsetX);
            m_isDraggingMinimap = true;
        }
        return true;
    }

    if (event.wheelDelta != 0.0f) {
        auto bounds = getBounds();
        const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
        const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
        const float visibleW = std::max(0.0f, (inspectorX - STRIP_SPACING) - bounds.x);
        const float contentW = m_strips.empty() ? 0.0f : (m_strips.size() * (STRIP_WIDTH + STRIP_SPACING) - STRIP_SPACING);
        const float maxScroll = std::max(0.0f, contentW - visibleW);
        const float channelY = minimapRect.bottom() + MINIMAP_GAP;

        const NUIRect channelClip(bounds.x, channelY, visibleW, std::max(0.0f, bounds.bottom() - channelY));
        if (maxScroll > 0.0f && channelClip.contains(event.position)) {
            constexpr float SCROLL_PX = 60.0f;
            m_targetScrollX = std::clamp(m_targetScrollX - static_cast<float>(event.wheelDelta) * SCROLL_PX, 0.0f, maxScroll);
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}

NUIRect UIMixerPanel::getMinimapRect() const
{
    auto bounds = getBounds();
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    const float width = std::max(0.0f, (inspectorX - STRIP_SPACING) - bounds.x);
    return NUIRect(bounds.x, bounds.y + 2.0f, width, MINIMAP_HEIGHT);
}

float UIMixerPanel::getChannelViewportWidth() const
{
    auto bounds = getBounds();
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    return std::max(0.0f, (inspectorX - STRIP_SPACING) - bounds.x);
}

float UIMixerPanel::getChannelContentWidth() const
{
    return m_strips.empty() ? 0.0f : (m_strips.size() * (STRIP_WIDTH + STRIP_SPACING) - STRIP_SPACING);
}

float UIMixerPanel::getChannelMaxScroll() const
{
    return std::max(0.0f, getChannelContentWidth() - getChannelViewportWidth());
}

void UIMixerPanel::updateScrollFromMinimapX(float x)
{
    const NUIRect minimapRect = getMinimapRect();
    const float contentW = getChannelContentWidth();
    const float visibleW = getChannelViewportWidth();
    const float maxScroll = getChannelMaxScroll();
    if (maxScroll <= 0.0f) {
        m_targetScrollX = 0.0f;
        return;
    }

    const float gap = 2.0f;
    const float laneW = std::max(1.0f, minimapRect.width - gap * 2.0f);
    const float viewportRatio = std::clamp(contentW > 0.0f ? visibleW / contentW : 1.0f, 0.05f, 1.0f);
    const float viewportW = std::max(16.0f, laneW * viewportRatio);
    const float clampedX = std::clamp(x, minimapRect.x + gap, minimapRect.x + gap + std::max(0.0f, laneW - viewportW));
    const float t = (laneW - viewportW) > 0.0f ? (clampedX - (minimapRect.x + gap)) / (laneW - viewportW) : 0.0f;
    m_targetScrollX = std::clamp(t * maxScroll, 0.0f, maxScroll);
}

} // namespace AestraUI
