// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPanel.h"
#include "UIMixerStrip.h"
#include "TrackColorPalette.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "MixerViewModel.h"
#include "MeterSnapshot.h"
#include "Commands/SetVolumeCommand.h"
#include "../../AestraCore/include/AestraUnifiedProfiler.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/SetPanCommand.h"
#include "TrackManager.h"
#include "PluginManager.h"
#include "Commands/PluginCommands.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Plugin/EffectChain.h"
#include "Plugin/AestraDelay.h"
#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kMinimapRadius = 7.0f;

float safeClampMixerScroll(float value, float upper)
{
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

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
        showPluginDropdown(channelId);
    };
    addChild(m_masterStrip);

    // Plugin finder dropdown (created hidden, shown on Add Insert click)
    m_pluginDropdown = std::make_shared<UIMixerPluginDropdown>();
    m_pluginDropdown->setId("UIMixerPluginDropdown");
    m_pluginDropdown->onPluginSelected = [this](const std::string& pluginId, const std::string&) {
        loadPluginToSelectedChannel(pluginId);
    };
    m_pluginDropdown->onBrowseAllRequested = [this]() {
        if (m_inspector) {
            m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
        }
    };
    addChild(m_pluginDropdown);

    // Initial channel refresh
    refreshChannels();
}

UIMixerPanel::~UIMixerPanel() {
    NUIDragDropManager::getInstance().unregisterDropTarget(this);
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
            showPluginDropdown(channelId);
        };

        // Wire fader to CommandHistory for undo/redo
        uint32_t chId = channel->id;
        strip->onFaderChanged = [this, chId](float newDb) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            // Convert dB to linear gain
            float newGain = (newDb <= -90.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
            float oldGain = mixerChannel->getVolume();

            if (std::abs(newGain - oldGain) > 0.0001f) {
                m_trackManager->getCommandHistory().pushAndExecute(
                    std::make_shared<Aestra::Audio::SetVolumeCommand>(*mixerChannel, newGain));
                Aestra::Log::info("[UIMixerPanel] Fader cmd: " + std::to_string(newDb) + " dB");
            }
        };

        // Wire mute to CommandHistory for undo/redo
        strip->onMuteChanged = [this, chId](bool muted) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetMuteCommand>(*mixerChannel, muted));
        };

        // Wire solo to CommandHistory for undo/redo
        strip->onSoloChanged = [this, chId](bool soloed) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetSoloCommand>(*mixerChannel, soloed));
        };

        // Wire pan to CommandHistory for undo/redo
        strip->onPanChanged = [this, chId](float pan) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetPanCommand>(*mixerChannel, pan));
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

void UIMixerPanel::setPlatformBridge(NUIPlatformBridge* bridge)
{
    for (auto& strip : m_strips) {
        if (strip) strip->setPlatformBridge(bridge);
    }
    if (m_masterStrip) m_masterStrip->setPlatformBridge(bridge);
    if (m_inspector) m_inspector->setPlatformBridge(bridge);
}

void UIMixerPanel::layoutMeters()
{
    auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    const float stripY = minimapRect.bottom() + MINIMAP_GAP;
    const float stripHeight = std::max(MIXER_MIN_CHANNEL_HEIGHT, bounds.bottom() - stripY - PADDING);

    // Layout master strip on the right.
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH; // No padding
    if (m_masterStrip) {
        m_masterStrip->setBounds(masterX, stripY, MASTER_STRIP_WIDTH, stripHeight);
        m_masterStrip->setVisible(true);
    }

    // Layout inspector just to the left of master.
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
    if (m_inspector) {
        // Collapsed, the inspector yields its width to the channel strips and
        // leaves only the re-open rail drawn by the panel.
        m_inspector->setBounds(inspectorX, stripY, INSPECTOR_WIDTH, stripHeight);
        m_inspector->setVisible(!m_inspectorCollapsed);
        if (!m_inspectorCollapsed) {
            m_inspector->onResize(static_cast<int>(INSPECTOR_WIDTH), static_cast<int>(stripHeight));
        }
    }

    // Layout channel strips to the left, keeping them out of the inspector/master area.
    const float left = bounds.x; // Start at 0, no padding
    const float right = inspectorX - STRIP_SPACING;
    const float visibleW = getChannelViewportWidth();
    const float contentW = getChannelContentWidth();
    const float maxScroll = getChannelMaxScroll();
    (void)visibleW;
    (void)contentW;
    m_scrollX = safeClampMixerScroll(m_scrollX, maxScroll);
    m_targetScrollX = safeClampMixerScroll(m_targetScrollX, maxScroll);

    float x = left - m_scrollX;
    for (size_t i = 0; i < m_strips.size(); ++i) {
        float stripX = x + i * (STRIP_WIDTH + STRIP_SPACING);
        const bool visible = (stripX + STRIP_WIDTH) >= left && stripX <= right;
        m_strips[i]->setVisible(visible);
        m_strips[i]->setBounds(stripX, stripY, STRIP_WIDTH, stripHeight);
    }
}

NUIRect UIMixerPanel::getInspectorToggleRect() const
{
    const auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    const float stripY = minimapRect.bottom() + MINIMAP_GAP;
    const float stripHeight = std::max(MIXER_MIN_CHANNEL_HEIGHT, bounds.bottom() - stripY - PADDING);
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();

    // Collapsed: the whole thin rail is the target. Expanded: a grip strip down
    // the inspector's leading edge.
    const float railW = m_inspectorCollapsed ? INSPECTOR_COLLAPSED_WIDTH : 10.0f;
    return NUIRect{inspectorX, stripY, railW, stripHeight};
}

void UIMixerPanel::setInspectorCollapsed(bool collapsed)
{
    if (m_inspectorCollapsed == collapsed) {
        return;
    }
    m_inspectorCollapsed = collapsed;
    layoutMeters();
    repaint();
}

void UIMixerPanel::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutMeters();
}

void UIMixerPanel::onUpdate(double deltaTime)
{
    ensureDropTargetRegistration();

    const float maxScroll = getChannelMaxScroll();
    m_targetScrollX = safeClampMixerScroll(m_targetScrollX, maxScroll);

    const float delta = m_targetScrollX - m_scrollX;
    if (std::abs(delta) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);
        m_scrollX += delta * ease;
        m_scrollX = safeClampMixerScroll(m_scrollX, maxScroll);
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

void UIMixerPanel::ensureDropTargetRegistration() {
    auto& dnd = NUIDragDropManager::getInstance();

    // One-time registration (shared_from_this is unavailable in the constructor).
    if (!m_dropTargetRegistered) {
        try {
            if (auto sharedThis = std::dynamic_pointer_cast<IDropTarget>(shared_from_this())) {
                dnd.registerDropTarget(sharedThis);
                m_dropTargetRegistered = true;
            }
        } catch (const std::bad_weak_ptr&) {
            // Not yet owned by a shared_ptr; retry next frame.
            return;
        }
    }

    // The drop manager scans targets in reverse registration order and the
    // mixer overlays the timeline, so re-register once per drag to move this
    // panel to the back of the list — it must be checked before TrackManagerUI
    // or plugin drops fall through to the lane geometry behind it (#395).
    // onUpdate only runs while the mixer is shown; when hidden, the manager's
    // visibility eligibility check skips this target entirely.
    if (dnd.isDragging()) {
        if (!m_dropOrderClaimedForDrag) {
            try {
                if (auto sharedThis = std::dynamic_pointer_cast<IDropTarget>(shared_from_this())) {
                    dnd.unregisterDropTarget(this);
                    dnd.registerDropTarget(sharedThis);
                    m_dropOrderClaimedForDrag = true;
                }
            } catch (const std::bad_weak_ptr&) {}
        }
    } else {
        m_dropOrderClaimedForDrag = false;
        m_dropHoverChannelId = -1; // clear highlight if a drag ended without onDragLeave
    }
}

void UIMixerPanel::renderSeparators(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    float y1 = minimapRect.bottom() + MINIMAP_GAP;
    float y2 = bounds.y + bounds.height;

    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
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
    AESTRA_ZONE("Mixer_Render");
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
            const NUIColor stripColor = channel ? colorFromArgb(paletteIndexToARGB(channel->trackColorIndex), selected ? 0.95f : 0.82f)
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
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
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

    // Drop-target highlight for channel strips (#395) — inside the clip so it
    // never bleeds into the inspector/master area.
    if (m_dropHoverChannelId >= 0) {
        const NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
        for (const auto& strip : m_strips) {
            if (strip && strip->isVisible() && static_cast<int64_t>(strip->getChannelId()) == m_dropHoverChannelId) {
                renderer.strokeRoundedRect(strip->getBounds(), 6.0f, 2.0f, accent);
                break;
            }
        }
    }

    if (clipEnabled) {
        renderer.clearClipRect();
    }

    if (m_inspector && m_inspector->isVisible()) {
        m_inspector->onRender(renderer);
    }

    // Inspector collapse rail. Collapsed it is the only thing left of the
    // inspector; expanded it is a slim grip on the panel's leading edge.
    {
        auto& theme = NUIThemeManager::getInstance();
        const NUIRect rail = getInspectorToggleRect();
        const NUIColor railColor = m_inspectorToggleHovered
            ? theme.getColor("accentPrimary").withAlpha(0.34f)
            : theme.getColor("surfaceTertiary").withAlpha(m_inspectorCollapsed ? 0.72f : 0.34f);
        renderer.fillRoundedRect(rail, 3.0f, railColor);

        // Chevron points the way the panel will move.
        const NUIColor glyph = theme.getColor("textSecondary")
                                   .withAlpha(m_inspectorToggleHovered ? 0.95f : 0.6f);
        const float cx = rail.x + rail.width * 0.5f;
        const float cy = rail.y + rail.height * 0.5f;
        const float dir = m_inspectorCollapsed ? -1.0f : 1.0f;
        for (int i = 0; i < 5; ++i) {
            const float dy = static_cast<float>(i) - 2.0f;
            const float dx = dir * (2.0f - std::abs(dy));
            renderer.fillRect(NUIRect{cx + dx - 0.5f, cy + dy * 2.0f, 1.5f, 2.0f}, glyph);
        }
    }

    // Master strip renders on top / outside the clip.
    if (m_masterStrip && m_masterStrip->isVisible()) {
        m_masterStrip->onRender(renderer);
    }

    // Drop-target highlight for the master strip (rendered outside the clip).
    if (m_dropHoverChannelId >= 0 && m_masterStrip && m_masterStrip->isVisible() &&
        static_cast<int64_t>(m_masterStrip->getChannelId()) == m_dropHoverChannelId) {
        const NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
        renderer.strokeRoundedRect(m_masterStrip->getBounds(), 6.0f, 2.0f, accent);
    }

    // Plugin dropdown renders as a floating overlay on top of everything.
    if (m_pluginDropdown && m_pluginDropdown->isVisible()) {
        m_pluginDropdown->onRender(renderer);
    }
}

bool UIMixerPanel::onMouseEvent(const NUIMouseEvent& event)
{
    const NUIRect minimapRect = getMinimapRect();
    const float contentW = getChannelContentWidth();
    const float visibleW = getChannelViewportWidth();
    const float maxScroll = getChannelMaxScroll();

    // Inspector collapse rail claims the pointer before anything underneath it.
    {
        const NUIRect rail = getInspectorToggleRect();
        const bool overRail = rail.contains(event.position);
        if (m_inspectorToggleHovered != overRail) {
            m_inspectorToggleHovered = overRail;
            repaint();
        }
        if (overRail && event.pressed && event.button == NUIMouseButton::Left) {
            toggleInspectorCollapsed();
            return true;
        }
    }

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
        const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
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
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
    const float width = std::max(0.0f, (inspectorX - STRIP_SPACING) - bounds.x);
    return NUIRect(bounds.x, bounds.y + 2.0f, width, MINIMAP_HEIGHT);
}

float UIMixerPanel::getChannelViewportWidth() const
{
    auto bounds = getBounds();
    const float masterX = bounds.x + bounds.width - MASTER_STRIP_WIDTH;
    const float inspectorX = masterX - STRIP_SPACING - inspectorWidth();
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

void UIMixerPanel::showPluginDropdown(uint32_t channelId)
{
    if (!m_pluginDropdown || !m_viewModel) return;

    // Select the channel first
    m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));

    // Find the strip that triggered this so we can anchor to its FX summary button
    UIMixerStrip* triggerStrip = nullptr;
    for (const auto& strip : m_strips) {
        if (strip && strip->getChannelId() == channelId) {
            triggerStrip = strip.get();
            break;
        }
    }
    if (!triggerStrip) {
        triggerStrip = m_masterStrip.get();
    }
    if (!triggerStrip) return;

    // Anchor dropdown to the left edge of the strip, below the Add Insert button.
    auto triggerBounds = triggerStrip->getBounds();
    auto fxBounds     = triggerStrip->getFXSummaryBounds();

    auto panelBounds = getBounds();
    const float masterX  = panelBounds.x + panelBounds.width - MASTER_STRIP_WIDTH;
    const float inspectorLeft = masterX - STRIP_SPACING - inspectorWidth();
    const float viewportRight = inspectorLeft - STRIP_SPACING;

    constexpr float DROP_W = 240.0f;
    float dropX = triggerBounds.x; // left edge of strip, per design spec
    if (dropX + DROP_W > viewportRight) {
        // Strip is too far right – park dropdown on the left side of the viewport
        // so it is unmistakably clear of the inspector.
        dropX = panelBounds.x + 40.0f;
    }
    dropX = std::max(dropX, panelBounds.x);

    NUIRect anchor{dropX, fxBounds.y, fxBounds.width, fxBounds.height};

    m_pluginDropdown->bringToFront();
    m_pluginDropdown->showAt(anchor, panelBounds.bottom());
}

void UIMixerPanel::loadPluginToSelectedChannel(const std::string& pluginId)
{
    if (!m_viewModel)
        return;
    loadPluginToChannel(m_viewModel->getSelectedChannel(), pluginId);
}

bool UIMixerPanel::loadPluginToChannel(Aestra::ChannelViewModel* vmChannel, const std::string& pluginId) {
    if (!m_trackManager || !vmChannel || !vmChannel->channel)
        return false;

    auto& pm = Aestra::Audio::PluginManager::getInstance();
    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        AESTRA_LOG_ERROR("Failed to create plugin instance for: " + pluginId);
        return false;
    }

    if (!instance->initialize(pm.getDefaultSampleRate(), pm.getDefaultBlockSize())) {
        AESTRA_LOG_ERROR("Failed to initialize plugin instance for: " + pluginId);
        return false;
    }
    if (auto delay = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDelay>(instance)) {
        delay->setBPM(static_cast<float>(m_trackManager->getPlaylistModel().getBPM()));
    }
    instance->activate();

    auto& chain = vmChannel->channel->getEffectChain();
    size_t slot = chain.getFirstEmptySlot();
    if (slot < Aestra::Audio::EffectChain::MAX_SLOTS) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<Aestra::Audio::AddPluginCommand>(*vmChannel->channel, slot, std::move(instance)));
        // The playback graph only picks up chain changes on rebuild (which also
        // re-prepares chains with the live sample rate/block size) — without
        // this, the plugin sits in the chain but never processes already-playing
        // tracks until something else dirties the graph.
        m_trackManager->requestAudioGraphRebuild(Aestra::Audio::GraphDirtyReason::EffectChainChanged);
        // Ensure inspector reflects the new insert
        if (m_inspector) {
            m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
        }
        return true;
    }

    AESTRA_LOG_WARNING("No empty effect slots on channel " + std::to_string(vmChannel->id));
    return false;
}

// ============================================================================
// IDropTarget (#395) — plugin drops target the strip under the cursor
// ============================================================================

UIMixerStrip* UIMixerPanel::stripAt(const NUIPoint& position) const {
    // The inspector and master strip are pinned on the right and render above
    // scrolled channel strips — resolve them first so occluded strips can't win.
    if (m_inspector && m_inspector->isVisible() && m_inspector->getBounds().contains(position)) {
        return nullptr; // inspector is not a drop surface
    }
    if (m_masterStrip && m_masterStrip->isVisible() && m_masterStrip->getBounds().contains(position)) {
        return m_masterStrip.get();
    }

    // Channel strips are clipped to the viewport left of the inspector.
    auto panelBounds = getBounds();
    const float masterX = panelBounds.x + panelBounds.width - MASTER_STRIP_WIDTH;
    const float viewportRight = masterX - STRIP_SPACING - inspectorWidth() - STRIP_SPACING;
    if (position.x > viewportRight) {
        return nullptr;
    }

    for (const auto& strip : m_strips) {
        if (strip && strip->isVisible() && strip->getBounds().contains(position)) {
            return strip.get();
        }
    }
    return nullptr;
}

Aestra::ChannelViewModel* UIMixerPanel::channelForStrip(const UIMixerStrip* strip) const {
    if (!strip || !m_viewModel)
        return nullptr;
    if (m_masterStrip && strip == m_masterStrip.get()) {
        return m_viewModel->getMaster();
    }
    return m_viewModel->getChannelById(strip->getChannelId());
}

DropFeedback UIMixerPanel::onDragEnter(const DragData& data, const NUIPoint& position) {
    return onDragOver(data, position);
}

DropFeedback UIMixerPanel::onDragOver(const DragData& data, const NUIPoint& position) {
    m_dropHoverChannelId = -1;
    if (data.type != DragDataType::Plugin && data.type != DragDataType::AudioSourceRoute) {
        return DropFeedback::Invalid;
    }
    auto* strip = stripAt(position);
    if (!strip || !channelForStrip(strip)) {
        return DropFeedback::Invalid;
    }
    m_dropHoverChannelId = static_cast<int64_t>(strip->getChannelId());
    return data.type == DragDataType::AudioSourceRoute ? DropFeedback::Move : DropFeedback::Copy;
}

void UIMixerPanel::onDragLeave() {
    m_dropHoverChannelId = -1;
}

DropResult UIMixerPanel::onDrop(const DragData& data, const NUIPoint& position) {
    DropResult result;
    m_dropHoverChannelId = -1;

    // The mixer claims every drop over its bounds, including ones it rejects:
    // letting a rejected drop fall through to the timeline behind the mixer is
    // exactly the misrouting this target exists to prevent (#395).
    if (data.type != DragDataType::Plugin && data.type != DragDataType::AudioSourceRoute) {
        result.accepted = false;
        result.message = "Only plugins and audio-source routes can be dropped on the mixer";
        return result;
    }

    auto* strip = stripAt(position);
    auto* vmChannel = channelForStrip(strip);
    if (!strip || !vmChannel) {
        result.accepted = false;
        result.message = "No mixer strip under drop";
        return result;
    }

    if (data.type == DragDataType::AudioSourceRoute) {
        const auto* patternValue = std::any_cast<uint64_t>(&data.customData);
        if (!patternValue || *patternValue == 0 || !m_trackManager) {
            result.accepted = false;
            result.message = "Drag data missing audio source";
            return result;
        }

        const Aestra::Audio::PatternID patternId(*patternValue);
        const auto* pattern = m_trackManager->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isAudio()) {
            result.accepted = false;
            result.message = "Audio source is no longer available";
            return result;
        }

        const uint32_t destinationId =
            vmChannel->id == 0 ? Aestra::Audio::MASTER_MIXER_CHANNEL_ID : vmChannel->id;
        if (pattern->getMixerChannelId() != destinationId) {
            auto command = std::make_shared<Aestra::Audio::SetAudioPatternMixerChannelCommand>(
                *m_trackManager, patternId, destinationId);
            m_trackManager->getCommandHistory().pushAndExecute(command);
        }
        if (m_viewModel) {
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(vmChannel->id));
        }

        result.accepted = true;
        result.targetTrackIndex = static_cast<int>(vmChannel->id);
        result.message = "Routed " + (data.displayName.empty() ? pattern->name : data.displayName) + " to " +
                         (vmChannel->id == 0 ? "Master" : vmChannel->name);
        Aestra::Log::info("[UIMixerPanel] Audio source drop: " + result.message);
        return result;
    }

    const std::string& pluginId = data.sourceClipIdString;
    if (pluginId.empty()) {
        result.accepted = false;
        result.message = "Drag data missing plugin id";
        return result;
    }

    // Select the drop target so the inspector's Inserts tab shows the channel
    // the plugin actually landed on — same call a strip click makes.
    if (m_viewModel) {
        m_viewModel->setSelectedChannelId(static_cast<int32_t>(strip->getChannelId()));
    }

    if (!loadPluginToChannel(vmChannel, pluginId)) {
        result.accepted = false;
        result.message = "Failed to load plugin (see log)";
        return result;
    }

    result.accepted = true;
    result.targetTrackIndex = static_cast<int>(vmChannel->id);
    result.message = "Loaded " + (data.displayName.empty() ? pluginId : data.displayName) + " on '" +
                     vmChannel->channel->getName() + "'";
    Aestra::Log::info("[UIMixerPanel] Plugin drop: " + result.message);
    return result;
}

NUIRect UIMixerPanel::getDropBounds() const {
    return getBounds();
}

} // namespace AestraUI
