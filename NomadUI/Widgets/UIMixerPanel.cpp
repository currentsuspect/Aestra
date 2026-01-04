// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPanel.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include "../../Source/MixerViewModel.h"
#include "../../NomadAudio/include/MeterSnapshot.h"
#include <algorithm>

namespace NomadUI {

UIMixerPanel::UIMixerPanel(std::shared_ptr<Nomad::MixerViewModel> viewModel,
                           std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> meterSnapshots,
                           std::shared_ptr<Nomad::Audio::ContinuousParamBuffer> continuousParams)
    : m_viewModel(std::move(viewModel))
    , m_meterSnapshots(std::move(meterSnapshots))
    , m_continuousParams(std::move(continuousParams))
{
    cacheThemeColors();

    // Inspector (pinned on right, before master).
    m_inspector = std::make_shared<UIMixerInspector>(m_viewModel.get());
    addChild(m_inspector);

    // Create master strip (pinned on right, does not scroll with channels).
    m_masterStrip = std::make_shared<UIMixerStrip>(0, 0, m_viewModel.get(), m_meterSnapshots, m_continuousParams);
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
    // User requested "the inside of the mixer should be the grey"
    // Since we unified primary/secondary to black, we hardcode the desired grey here
    m_backgroundColor = NomadUI::NUIColor(0.12f, 0.12f, 0.14f, 1.0f);  // "The Grey"
    m_separatorColor = theme.getColor("borderSubtle");        // #2c2c2f
}

void UIMixerPanel::refreshChannels()
{
    if (!m_viewModel) return;

    size_t channelCount = m_viewModel->getChannelCount();

    for (auto& strip : m_strips) {
        removeChild(strip);
    }
    m_strips.clear();

    m_strips.reserve(channelCount);
    for (size_t i = 0; i < channelCount; ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel) continue;
        auto strip = std::make_shared<UIMixerStrip>(channel->id, static_cast<int>(i + 1), m_viewModel.get(), m_meterSnapshots, m_continuousParams);
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
    const float stripY = bounds.y + PADDING;
    const float stripHeight = std::max(1.0f, bounds.height - PADDING * 2);

    // Layout master strip on the right.
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
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
    const float left = bounds.x + PADDING;
    const float right = inspectorX - STRIP_SPACING;
    const float visibleW = std::max(0.0f, right - left);
    const float contentW = m_strips.empty() ? 0.0f : (m_strips.size() * (STRIP_WIDTH + STRIP_SPACING) - STRIP_SPACING);
    const float maxScroll = std::max(0.0f, contentW - visibleW);
    m_scrollX = std::clamp(m_scrollX, 0.0f, maxScroll);

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
    // Update meters from snapshot buffer via view model
    if (m_viewModel && m_meterSnapshots) {
        m_viewModel->updateMeters(*m_meterSnapshots, deltaTime);
    }

    // Update children
    updateChildren(deltaTime);
}

void UIMixerPanel::renderSeparators(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    float y1 = bounds.y + PADDING;
    float y2 = bounds.y + bounds.height - PADDING;

    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    const float left = bounds.x + PADDING;
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

    // Separators
    renderSeparators(renderer);

    // Render channel strips with a clip so they never draw into the inspector/master area.
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
    const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
    const float channelW = std::max(0.0f, (inspectorX - STRIP_SPACING) - (bounds.x + PADDING));
    const NUIRect channelClip(bounds.x + PADDING, bounds.y, channelW, bounds.height);

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
    if (event.wheelDelta != 0.0f) {
        auto bounds = getBounds();
        const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
        const float inspectorX = masterX - STRIP_SPACING - INSPECTOR_WIDTH;
        const float visibleW = std::max(0.0f, (inspectorX - STRIP_SPACING) - (bounds.x + PADDING));
        const float contentW = m_strips.empty() ? 0.0f : (m_strips.size() * (STRIP_WIDTH + STRIP_SPACING) - STRIP_SPACING);
        const float maxScroll = std::max(0.0f, contentW - visibleW);

        const NUIRect channelClip(bounds.x + PADDING, bounds.y, visibleW, bounds.height);
        if (maxScroll > 0.0f && channelClip.contains(event.position)) {
            constexpr float SCROLL_PX = 60.0f;
            m_scrollX = std::clamp(m_scrollX - static_cast<float>(event.wheelDelta) * SCROLL_PX, 0.0f, maxScroll);
            layoutMeters();
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}

} // namespace NomadUI
