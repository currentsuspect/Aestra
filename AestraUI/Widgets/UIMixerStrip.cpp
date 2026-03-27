// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerStrip.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "MixerViewModel.h"
#include "ContinuousParamBuffer.h"
#include "ChannelSlotMap.h"
#include "MeterSnapshot.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
    constexpr float HEADER_H = 28.0f;
    constexpr float KNOB_H = 30.0f;
    constexpr float FX_H = 24.0f;
    constexpr float BUTTONS_H = 24.0f;
    constexpr float FOOTER_H = 20.0f;
    constexpr float PAD = 6.0f;
    constexpr float GAP = 4.0f;
    constexpr float METER_W = 26.0f;
    constexpr float MASTER_METER_W = 34.0f;

    constexpr float SELECT_TOP_H = 2.0f;
}

UIMixerStrip::UIMixerStrip(uint32_t channelId,
                           int trackNumber,
                           Aestra::MixerViewModel* viewModel,
                           std::shared_ptr<Aestra::Audio::MeterSnapshotBuffer> meterSnapshots,
                           std::shared_ptr<Aestra::Audio::ContinuousParamBuffer> continuousParams)
    : m_channelId(channelId)
    , m_trackNumber(trackNumber)
    , m_viewModel(viewModel)
    , m_meterSnapshots(std::move(meterSnapshots))
    , m_continuousParams(std::move(continuousParams))
{
    m_staticCacheId = reinterpret_cast<uint64_t>(this);
    cacheThemeColors();

    m_header = std::make_shared<UIMixerHeader>();
    m_header->setIsMaster(m_channelId == 0);
    m_header->setIsMaster(m_channelId == 0);
    addChild(m_header);

    m_header->setIsMaster(m_channelId == 0);
    addChild(m_header);

    // Input Selector REMOVED (Moved to Inspector)

    m_trimKnob = std::make_shared<UIMixerKnob>(UIMixerKnobType::Trim);
    // Reduce visual noise: show channel controls only when hovered/selected.
    m_trimKnob->setVisible(false);
    m_trimKnob->onValueChanged = [this](float db) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->trimDb = db;
        m_continuousParams->setTrimDb(channel->slotIndex, db);
    };
    addChild(m_trimKnob);

    m_fxSummary = std::make_shared<UIMixerFXSummary>();
    m_fxSummary->onInvalidateRequested = [this]() { invalidateStaticCache(); };
    m_fxSummary->onClicked = [this]() {
        if (onFXClicked) onFXClicked(m_channelId);
    };
    addChild(m_fxSummary);

    // Pan Knob (Always visible)
    m_panKnob = std::make_shared<UIMixerKnob>(UIMixerKnobType::Pan);
    m_panKnob->setVisible(false);
    m_panKnob->onValueChanged = [this](float pan) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->pan = pan;
        m_continuousParams->setPan(channel->slotIndex, pan);
    };
    addChild(m_panKnob);

    // Width Knob (Always visible)
    m_widthKnob = std::make_shared<UIMixerKnob>(UIMixerKnobType::Width);
    m_widthKnob->setVisible(false);
    m_widthKnob->onValueChanged = [this](float width) {
        if (!m_viewModel) return; 
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->width = width;
        if (auto mc = channel->channel) {
             mc->setWidth(width);
        }
    };
    addChild(m_widthKnob);

    m_buttons = std::make_shared<UIMixerButtonRow>();
    m_buttons->onInvalidateRequested = [this]() { invalidateStaticCache(); };
    m_buttons->onMuteToggled = [this](bool muted) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel || channel->id == 0) return;

        channel->muted = muted;
        invalidateStaticCache();

        if (auto mc = channel->channel) {
            mc->setMute(muted);
            if (muted && mc->isSoloed()) {
                mc->setSolo(false);
                channel->soloed = false;
            }
        }

    };
    m_buttons->onSoloToggled = [this](bool soloed, NUIModifiers modifiers) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel || channel->id == 0) return;

        // Solo Safe Logic (Ctrl + Click)
        const bool isCtrl = (modifiers & NUIModifiers::Ctrl);
        if (isCtrl) {
            // Revert the local toggle because pure solo state shouldn't change
            // But wait, the button row just toggled its internal visual state.
            // For Solo Safe, we actually want to toggle a *different* visual state 
            // (maybe different color or icon?), but for now let's just update the logic.
            // NOTE: UIMixerButtonRow tracks 'soloed' boolean. If we are setting 'solo safe',
            // we should probably revert the 'soloed' state on the button if it wasn't already soloed.
            // Or, ideally, Solo Safe is an independent state.
            // Given the button row is simple, let's treat it as:
            // Ctrl+Click -> Toggle Safe. Restore Solo button state to what it was.
            
            // Revert button state visually (hacky but works for stateless widget)
            m_buttons->setSoloed(!soloed); 
            
            // Toggle proper safe state
            if (auto mc = channel->channel) {
               bool newSafe = !mc->isSoloSafe();
               mc->setSoloSafe(newSafe);
               // Visual feedback? UIMixerStrip doesn't have a distinct 'Safe' icon yet.
               // We might rely on the user knowing they did it, or add a small indicator later.
            }
            return;
        }

        // Exclusive solo: clear other solos first (matches playlist behavior).
        if (soloed) {
            const size_t count = m_viewModel->getChannelCount();
            for (size_t i = 0; i < count; ++i) {
                auto* other = m_viewModel->getChannelByIndex(i);
                if (!other || other->id == channel->id) continue;
                if (auto otherMC = other->channel) {
                    otherMC->setSolo(false);
                }
                other->soloed = false;
            }
        }

        channel->soloed = soloed;
        invalidateStaticCache();

        if (auto mc = channel->channel) {
            mc->setSolo(soloed);
            if (soloed && mc->isMuted()) {
                mc->setMute(false);
                channel->muted = false;
            }
        }
    };
    m_buttons->onArmToggled = [this](bool armed) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel || channel->id == 0) return;

        // v3.0: Recording is handled by PlaylistModel/TrackManager transport logic, not MixerChannel.
        channel->armed = armed;
        invalidateStaticCache();

        if (auto mc = channel->channel) {
            mc->setArmed(armed);
        }
    };
    addChild(m_buttons);

    m_meter = std::make_shared<UIMixerMeter>();
    m_meter->setShowCorrelation(true);
    m_meter->onClipCleared = [this]() {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (channel) {
            channel->clipLatchL = false;
            channel->clipLatchR = false;
            // Also inform audio thread? The latch is arguably UI-side, 
            // but if MeterSnapshot has clip bit set, it will re-latch next frame.
            // For now, clearing UI latch logic is enough as snapshot only sends 'current' clip frame 
            // OR we need to clear snapshot "sticky" bit if it exists.
            // Analysis of MeterSnapshot.h shows it sends 'current' clip flags (CLIP_L, CLIP_R).
            // Logic in MixerViewModel accumulates it into clipLatch.
            // So simply setting channel->clipLatch = false here works, as long as audio isn't *currently* clipping.
            invalidateStaticCache();
        }
    };
    addChild(m_meter);

    m_fader = std::make_shared<UIMixerFader>();
    m_fader->setRangeDb(-90.0f, 6.0f);
    m_fader->setDefaultDb(0.0f);
    m_fader->onValueChanged = [this](float db) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->faderGainDb = db;
        m_continuousParams->setFaderDb(channel->slotIndex, db);
    };
    addChild(m_fader);

    m_footer = std::make_shared<UIMixerFooter>();
    m_footer->onInvalidateRequested = [this]() { invalidateStaticCache(); };
    m_footer->setTrackNumber(m_trackNumber);
    addChild(m_footer);

    // Clip clear callback
    m_meter->onClipCleared = [this]() {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        m_viewModel->clearClipLatch(channel->id);
        if (m_meterSnapshots) {
            m_meterSnapshots->clearClip(channel->slotIndex);
        }
    };

    // Hide strip buttons for master (keeps it visually distinct and avoids non-sense M/S/R).
    if (m_channelId == 0 && m_buttons) {
        m_buttons->setVisible(false);
    }
    if (m_channelId == 0 && m_panKnob) {
        m_panKnob->setVisible(false);
    }
    // Also hide Width for master (typically mastering width is a separate plugin, or we can add it later if requested)
    // For now, standard mixer master bus usually doesn't have per-strip width control unless explicitly added.
    if (m_channelId == 0 && m_widthKnob) {
        m_widthKnob->setVisible(false);
    }
    if (m_channelId == 0 && m_footer) {
        m_footer->setVisible(false);
    }

    layoutChildren();
}

void UIMixerStrip::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    // Selection: Neon Purple (Glassy)
    m_selectedTint = theme.getColor("accentPrimary").withAlpha(0.08f);
    m_selectedOutline = theme.getColor("accentPrimary").withAlpha(0.5f);
    m_selectedGlow = theme.getColor("accentPrimary").withAlpha(0.3f);
    m_selectedTopHighlight = theme.getColor("accentPrimary").withAlpha(0.8f);
    
    // Master: Distinct Dark Glass
    m_masterBackground = AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.3f);
    m_mutedOverlay = NUIColor(0.0f, 0.0f, 0.0f, 0.4f);
    
    // Standard Strip: Use Theme Glass Border/Hover for consistency
    // m_stripBg = AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.02f); 
    // Use theme.glassHover (0.04f) or similar. Let's use a custom weak glass for strips.
    m_stripBg = theme.getColor("glassBorder").withAlpha(0.04f);
    
    // Master Border
    m_masterBorder = theme.getColor("glassBorder");
}

void UIMixerStrip::layoutChildren()
{
    auto bounds = getBounds();

    float y = bounds.y;

    if (m_header) {
        m_header->setBounds(bounds.x, y, bounds.width, HEADER_H);
        y += HEADER_H;
    }

    // Input Dropdown Removed from Strip Layout

    const bool hasButtons = (m_buttons && m_buttons->isVisible());
    if (hasButtons) {
        m_buttons->setBounds(bounds.x, y, bounds.width, BUTTONS_H);
        y += BUTTONS_H;
    }

    if (m_trimKnob && m_trimKnob->isVisible()) {
        m_trimKnob->setBounds(bounds.x, y, bounds.width, KNOB_H);
        y += KNOB_H;
    }

    if (m_fxSummary && m_fxSummary->isVisible()) {
        m_fxSummary->setBounds(bounds.x + PAD, y, std::max(1.0f, bounds.width - PAD * 2), FX_H);
        y += FX_H;
    }

    // Side-by-side Pan and Width
    const bool showPan = (m_panKnob && m_panKnob->isVisible());
    const bool showWidth = (m_widthKnob && m_widthKnob->isVisible());
    
    if (showPan && showWidth) {
        const float halfW = bounds.width * 0.5f;
        m_panKnob->setBounds(bounds.x, y, halfW, KNOB_H);
        m_widthKnob->setBounds(bounds.x + halfW, y, halfW, KNOB_H);
        y += KNOB_H;
    } else if (showPan) {
        m_panKnob->setBounds(bounds.x, y, bounds.width, KNOB_H);
        y += KNOB_H;
    } else if (showWidth) {
        m_widthKnob->setBounds(bounds.x, y, bounds.width, KNOB_H);
        y += KNOB_H;
    }

    const bool hasFooter = (m_footer && m_footer->isVisible());
    const float footerH = hasFooter ? FOOTER_H : 0.0f;
    const float footerY = bounds.y + bounds.height - footerH;
    if (hasFooter) {
        m_footer->setBounds(bounds.x, footerY, bounds.width, FOOTER_H);
    }

    const float contentY = y;
    const float contentH = std::max(1.0f, footerY - y);

    const float meterW = (m_channelId == 0) ? MASTER_METER_W : METER_W;

    const float availableH = std::max(1.0f, contentH - PAD * 2);
    // User requested "half their height positioned at the bottom"
    const float meterH = availableH * 0.5f; 
    const float meterY = footerY - PAD - meterH;
    const float meterX = bounds.x + PAD;

    if (m_meter) {
        m_meter->setBounds(meterX, meterY, meterW, meterH);
    }

    if (m_fader) {
        const float faderX = meterX + meterW + GAP;
        const float faderW = std::max(10.0f, bounds.width - (faderX - bounds.x) - PAD);
        m_fader->setBounds(faderX, meterY, faderW, meterH);
    }
}

void UIMixerStrip::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutChildren();
    invalidateStaticCache();
}

void UIMixerStrip::onUpdate(double deltaTime)
{
    (void)deltaTime;
    if (!m_viewModel) {
        updateChildren(deltaTime);
        return;
    }

    auto* channel = m_viewModel->getChannelById(m_channelId);
    if (!channel) {
        updateChildren(deltaTime);
        return;
    }

    const bool selected = (m_viewModel->getSelectedChannelId() == static_cast<int32_t>(m_channelId));
    if (m_cachedSelected != selected) {
        m_cachedSelected = selected;
        invalidateStaticCache();
    }

    const bool draggingControls =
        (m_trimKnob && m_trimKnob->isDragging()) ||
        (m_panKnob && m_panKnob->isDragging()) ||
        (m_widthKnob && m_widthKnob->isDragging());

    // Reduce strip noise: show Trim/Pan/Width only when selected or actively dragged.
    const bool showChannelControls = (m_channelId != 0) && (selected || draggingControls);
    if (m_cachedShowChannelControls != showChannelControls) {
        m_cachedShowChannelControls = showChannelControls;
        if (m_trimKnob) m_trimKnob->setVisible(showChannelControls);
        if (m_panKnob) m_panKnob->setVisible(showChannelControls);
        if (m_widthKnob) m_widthKnob->setVisible(showChannelControls);
        if (m_panKnob) m_panKnob->setVisible(showChannelControls);
        if (m_widthKnob) m_widthKnob->setVisible(showChannelControls);
        layoutChildren();
        invalidateStaticCache();
    }

    // Input Dropdown Logic Removed

    if (m_cachedMuted != channel->muted) {
        m_cachedMuted = channel->muted;
        invalidateStaticCache();
        if (m_meter) {
            m_meter->setDimmed(channel->muted);
        }
    }
    if (m_cachedSoloed != channel->soloed) {
        m_cachedSoloed = channel->soloed;
        invalidateStaticCache();
    }
    if (m_cachedArmed != channel->armed) {
        m_cachedArmed = channel->armed;
        invalidateStaticCache();
    }
    if (m_cachedMonitored != channel->monitored) {
        m_cachedMonitored = channel->monitored;
        invalidateStaticCache();
    }

    if (m_header) {
        if (m_cachedName != channel->name) {
            m_cachedName = channel->name;
            invalidateStaticCache();
        }
        m_header->setTrackName(channel->name);
        if (m_cachedRoute != channel->routeName) {
            m_cachedRoute = channel->routeName;
            invalidateStaticCache();
        }
        m_header->setRouteName(channel->routeName);
        if (m_cachedTrackColorArgb != channel->trackColor) {
            m_cachedTrackColorArgb = channel->trackColor;
            invalidateStaticCache();
        }
        m_header->setTrackColor(channel->trackColor);
        m_header->setSelected(selected);
    }

    if (m_buttons && m_buttons->isVisible()) {
        m_buttons->setMuted(channel->muted);
        m_buttons->setSoloed(channel->soloed);
        m_buttons->setArmed(channel->armed);
    }

    if (m_trimKnob && m_trimKnob->isVisible() && !m_trimKnob->isDragging()) {
        const bool hovered = m_trimKnob->isHovered();
        if (m_cachedTrimHovered != hovered) {
            m_cachedTrimHovered = hovered;
            invalidateStaticCache();
        }
        if (std::abs(m_cachedTrimDb - channel->trimDb) > 1e-3f) {
            m_cachedTrimDb = channel->trimDb;
            invalidateStaticCache();
        }
        m_trimKnob->setValue(channel->trimDb);
    }

    if (m_panKnob && m_panKnob->isVisible() && !m_panKnob->isDragging()) {
        const bool hovered = m_panKnob->isHovered();
        if (m_cachedPanHovered != hovered) {
            m_cachedPanHovered = hovered;
            invalidateStaticCache();
        }
        if (std::abs(m_cachedPan - channel->pan) > 1e-4f) {
            m_cachedPan = channel->pan;
            invalidateStaticCache();
        }
        m_panKnob->setValue(channel->pan);
    }

    if (m_widthKnob && m_widthKnob->isVisible() && !m_widthKnob->isDragging()) {
        const bool hovered = m_widthKnob->isHovered();
        if (m_cachedWidthHovered != hovered) {
            m_cachedWidthHovered = hovered;
            invalidateStaticCache();
        }
        if (std::abs(m_cachedWidth - channel->width) > 1e-4f) {
            m_cachedWidth = channel->width;
            invalidateStaticCache();
        }
        m_widthKnob->setValue(channel->width);
    }

    if (m_fxSummary && m_fxSummary->isVisible()) {
        if (m_cachedFxCount != channel->fxCount) {
            m_cachedFxCount = channel->fxCount;
            invalidateStaticCache();
            m_fxSummary->setFxCount(channel->fxCount);
        }
    }

    if (m_footer && m_footer->isVisible()) {
        m_footer->setTrackNumber(m_trackNumber);
    }

    if (m_meter) {
        m_meter->setLevels(channel->smoothedPeakL, channel->smoothedPeakR);
        m_meter->setRmsLevels(channel->smoothedRmsL, channel->smoothedRmsR);
        m_meter->setPeakOverlay(channel->envPeakL, channel->envPeakR);
        m_meter->setPeakHold(channel->peakHoldL, channel->peakHoldR);
        m_meter->setClipLatch(channel->clipLatchL, channel->clipLatchR);
        
        // Pass correlation to meter (will only be drawn if enabled)
        m_meter->setCorrelation(channel->correlation);
        
        // Pass LUFS only to master meter (ID 0)
        if (m_channelId == 0) {
            m_meter->setIntegratedLufs(channel->integratedLufs);
        }
    }

    if (m_fader && !m_fader->isDragging()) {
        const bool hovered = m_fader->isHovered();
        if (m_cachedFaderHovered != hovered) {
            m_cachedFaderHovered = hovered;
            invalidateStaticCache();
        }

        if (std::abs(m_cachedFaderDb - channel->faderGainDb) > 1e-3f) {
            m_cachedFaderDb = channel->faderGainDb;
            invalidateStaticCache();
        }
        m_fader->setValueDb(channel->faderGainDb);
    }

    updateChildren(deltaTime);
}

void UIMixerStrip::onRender(NUIRenderer& renderer)
{
    Aestra::ChannelViewModel* channel = nullptr;
    if (m_viewModel) {
        channel = m_viewModel->getChannelById(m_channelId);
    }

    const auto bounds = getBounds();
    if (bounds.isEmpty()) return;

    const bool selected = m_viewModel && (m_viewModel->getSelectedChannelId() == static_cast<int32_t>(m_channelId));

    // Unified "Deep Black" background for ALL strips.
    // Use Squircle (Rounded Rect) for standard Apple-like look
    float radius = NUIThemeManager::getInstance().getRadius("radiusM");
    renderer.fillRoundedRect(bounds, radius, m_stripBg);

    // Master gets a slightly different tone to distinguish it (optional, but good for hierarchy)
    if (m_channelId == 0) {
        // Subtle highlight for master
        renderer.strokeRoundedRect(bounds, radius, 1.0f, m_masterBorder); 
    }

    if (selected) {
        renderer.fillRoundedRect(bounds, radius, m_selectedTint);

        // Top highlight "edge" (gives instant selection scent without heavy borders).
        renderer.fillRect(NUIRect{bounds.x, bounds.y, bounds.width, SELECT_TOP_H}, m_selectedTopHighlight);

        // Enhanced Glow (Inner + Outer)
        // Outer glow
        auto glowColor = m_selectedGlow;
        glowColor.a = 0.4f; // Brighter glow
        renderer.strokeRoundedRect(NUIRect{bounds.x - 2.0f, bounds.y - 2.0f, bounds.width + 4.0f, bounds.height + 4.0f}, radius + 2.0f, 2.0f, glowColor);
        
        // Sharp Outline
        renderer.strokeRoundedRect(bounds, radius, 1.5f, m_selectedOutline);
    }

    // While dragging, render live (no caching) so interactive controls update every frame.
    const bool dragging =
        (m_fader && m_fader->isDragging()) ||
        (m_trimKnob && m_trimKnob->isDragging()) ||
        (m_panKnob && m_panKnob->isDragging()) ||
        (m_widthKnob && m_widthKnob->isDragging());
    if (dragging) {
        invalidateStaticCache();
        renderChildren(renderer);
        if (channel && channel->muted) {
            renderer.fillRect(getBounds(), m_mutedOverlay);
        }
        return;
    }

    // Static caching Disabled due to HiDPI blurriness issues.
    // The performance impact of redrawing vector UI is minimal on modern systems.
    /*
    if (m_staticCacheInvalidated) {
        renderer.invalidateCache(m_staticCacheId);
        m_staticCacheInvalidated = false;
    }

    const bool usedCache = renderer.renderCachedOrUpdate(m_staticCacheId, bounds, [&]() {
        renderer.clear(NUIColor(0, 0, 0, 0));
        renderer.pushTransform(-bounds.x, -bounds.y);
        renderStaticLayer(renderer);
        renderer.popTransform();
    });

    if (usedCache) {
        if (m_meter) {
            m_meter->onRender(renderer);
        }
        if (channel && channel->muted) {
            renderer.fillRect(getBounds(), m_mutedOverlay);
        }
        return;
    }
    */

    renderChildren(renderer);
    if (channel && channel->muted) {
        renderer.fillRoundedRect(getBounds(), radius, m_mutedOverlay);
    }
}

bool UIMixerStrip::onMouseEvent(const NUIMouseEvent& event)
{
    bool handledSelection = false;
    if (m_viewModel && event.pressed && event.button == NUIMouseButton::Left) {
        if (getBounds().contains(event.position)) {
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(m_channelId));
            handledSelection = true;
        }
    }

    const bool handledByChildren = NUIComponent::onMouseEvent(event);
    return handledSelection || handledByChildren;
}

void UIMixerStrip::invalidateStaticCache()
{
    m_staticCacheInvalidated = true;
}

void UIMixerStrip::renderStaticLayer(NUIRenderer& renderer)
{
    if (m_header && m_header->isVisible()) {
        m_header->onRender(renderer);
    }
    if (m_trimKnob && m_trimKnob->isVisible()) {
        m_trimKnob->onRender(renderer);
    }
    if (m_fxSummary && m_fxSummary->isVisible()) {
        m_fxSummary->onRender(renderer);
    }
    if (m_panKnob && m_panKnob->isVisible()) {
        m_panKnob->onRender(renderer);
    }
    if (m_widthKnob && m_widthKnob->isVisible()) {
        m_widthKnob->onRender(renderer);
    }
    if (m_buttons && m_buttons->isVisible()) {
        m_buttons->onRender(renderer);
    }
    if (m_fader && m_fader->isVisible()) {
        m_fader->onRender(renderer);
    }
    if (m_footer && m_footer->isVisible()) {
        m_footer->onRender(renderer);
    }
}

} // namespace AestraUI
