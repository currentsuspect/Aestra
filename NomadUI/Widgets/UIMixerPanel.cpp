// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPanel.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include "../../Source/MixerViewModel.h"
#include "MeterSnapshot.h"
#include <algorithm>

namespace NomadUI {

UIMixerPanel::UIMixerPanel(std::shared_ptr<Nomad::MixerViewModel> viewModel,
                           std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> meterSnapshots)
    : m_viewModel(std::move(viewModel))
    , m_meterSnapshots(std::move(meterSnapshots))
{
    cacheThemeColors();

    // Create master meter
    m_masterMeter = std::make_shared<UIMixerMeter>();
    addChild(m_masterMeter);

    // Set up clip clear callback for master
    m_masterMeter->onClipCleared = [this]() {
        if (m_viewModel) {
            m_viewModel->clearMasterClipLatch();
        }
        if (m_meterSnapshots) {
            m_meterSnapshots->clearClip(Nomad::Audio::ChannelSlotMap::MASTER_SLOT_INDEX);
        }
    };

    // Initial channel refresh
    refreshChannels();
}

void UIMixerPanel::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_backgroundColor = theme.getColor("backgroundPrimary");  // #181819
    m_separatorColor = theme.getColor("borderSubtle");        // #2c2c2f
}

void UIMixerPanel::refreshChannels()
{
    if (!m_viewModel) return;

    size_t channelCount = m_viewModel->getChannelCount();

    // Remove excess meters
    while (m_meters.size() > channelCount) {
        removeChild(m_meters.back());
        m_meters.pop_back();
    }

    // Add new meters
    while (m_meters.size() < channelCount) {
        auto meter = std::make_shared<UIMixerMeter>();
        size_t index = m_meters.size();

        // Set up clip clear callback
        meter->onClipCleared = [this, index]() {
            if (index < m_viewModel->getChannelCount()) {
                auto* channel = m_viewModel->getChannelByIndex(index);
                if (channel) {
                    m_viewModel->clearClipLatch(channel->id);
                    if (m_meterSnapshots) {
                        m_meterSnapshots->clearClip(channel->slotIndex);
                    }
                }
            }
        };

        m_meters.push_back(meter);
        addChild(meter);
    }

    layoutMeters();
}

void UIMixerPanel::layoutMeters()
{
    auto bounds = getBounds();
    float x = bounds.x + PADDING;
    float y = bounds.y + HEADER_HEIGHT + PADDING;
    float meterHeight = bounds.height - HEADER_HEIGHT - FOOTER_HEIGHT - PADDING * 2;

    // Layout channel meters
    for (size_t i = 0; i < m_meters.size(); ++i) {
        float stripX = x + i * (STRIP_WIDTH + STRIP_SPACING);
        // Center meter within strip width
        float meterX = stripX + (STRIP_WIDTH - METER_WIDTH) / 2.0f;

        m_meters[i]->setBounds(meterX, y, METER_WIDTH, meterHeight);
    }

    // Layout master meter on the right
    if (m_masterMeter) {
        float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
        float masterMeterX = masterX + (MASTER_STRIP_WIDTH - METER_WIDTH * 1.5f) / 2.0f;
        m_masterMeter->setBounds(masterMeterX, y, METER_WIDTH * 1.5f, meterHeight);
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

    // Pass smoothed values to meter widgets
    updateMeterWidgets();

    // Update children
    updateChildren(deltaTime);
}

void UIMixerPanel::updateMeterWidgets()
{
    if (!m_viewModel) return;

    // Update channel meters
    for (size_t i = 0; i < m_meters.size(); ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (channel) {
            m_meters[i]->setLevels(channel->smoothedPeakL, channel->smoothedPeakR);
            m_meters[i]->setPeakHold(channel->peakHoldL, channel->peakHoldR);
            m_meters[i]->setClipLatch(channel->clipLatchL, channel->clipLatchR);
        }
    }

    // Update master meter
    if (m_masterMeter) {
        auto* master = m_viewModel->getMaster();
        if (master) {
            m_masterMeter->setLevels(master->smoothedPeakL, master->smoothedPeakR);
            m_masterMeter->setPeakHold(master->peakHoldL, master->peakHoldR);
            m_masterMeter->setClipLatch(master->clipLatchL, master->clipLatchR);
        }
    }
}

void UIMixerPanel::renderSeparators(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    float y1 = bounds.y + HEADER_HEIGHT;
    float y2 = bounds.y + bounds.height - FOOTER_HEIGHT;

    // Draw separators between channel strips
    for (size_t i = 1; i < m_meters.size(); ++i) {
        float x = bounds.x + PADDING + i * (STRIP_WIDTH + STRIP_SPACING) - STRIP_SPACING / 2.0f;
        renderer.drawLine({x, y1}, {x, y2}, 1.0f, m_separatorColor);
    }

    // Draw separator before master strip
    if (!m_meters.empty() && m_masterMeter) {
        float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
        renderer.drawLine({masterX - STRIP_SPACING, y1}, {masterX - STRIP_SPACING, y2}, 1.0f, m_separatorColor);
    }
}

void UIMixerPanel::renderLabels(NUIRenderer& renderer)
{
    if (!m_viewModel) return;

    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    NUIColor textColor = theme.getColor("textPrimary");
    NUIColor textSecondary = theme.getColor("textSecondary");
    float fontSize = 11.0f;

    // Render channel labels
    for (size_t i = 0; i < m_meters.size(); ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel) continue;

        float stripX = bounds.x + PADDING + i * (STRIP_WIDTH + STRIP_SPACING);
        NUIRect labelRect = {stripX, bounds.y + 4.0f, STRIP_WIDTH, HEADER_HEIGHT - 8.0f};

        // Truncate name if too long
        std::string displayName = channel->name;
        NUISize textSize = renderer.measureText(displayName, fontSize);
        if (textSize.width > STRIP_WIDTH - 8.0f) {
            // Simple truncation with ellipsis
            while (displayName.length() > 3 && textSize.width > STRIP_WIDTH - 16.0f) {
                displayName = displayName.substr(0, displayName.length() - 1);
                textSize = renderer.measureText(displayName + "...", fontSize);
            }
            displayName += "...";
        }

        renderer.drawTextCentered(displayName, labelRect, fontSize, textColor);
    }

    // Render master label
    if (m_masterMeter) {
        float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH - PADDING;
        NUIRect masterLabelRect = {masterX, bounds.y + 4.0f, MASTER_STRIP_WIDTH, HEADER_HEIGHT - 8.0f};
        renderer.drawTextCentered("MASTER", masterLabelRect, fontSize, textColor);
    }
}

void UIMixerPanel::onRender(NUIRenderer& renderer)
{
    auto bounds = getBounds();

    // Background
    renderer.fillRect(bounds, m_backgroundColor);

    // Separators
    renderSeparators(renderer);

    // Labels
    renderLabels(renderer);

    // Render children (meters)
    renderChildren(renderer);
}

bool UIMixerPanel::onMouseEvent(const NUIMouseEvent& event)
{
    // Let children handle mouse events first
    for (auto& meter : m_meters) {
        if (meter->containsPoint(event.position)) {
            if (meter->onMouseEvent(event)) {
                return true;
            }
        }
    }

    if (m_masterMeter && m_masterMeter->containsPoint(event.position)) {
        if (m_masterMeter->onMouseEvent(event)) {
            return true;
        }
    }

    return false;
}

} // namespace NomadUI
