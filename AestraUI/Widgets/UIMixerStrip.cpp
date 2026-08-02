// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerStrip.h"

#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "MixerViewModel.h"
#include "TrackColorPalette.h"
#include "ContinuousParamBuffer.h"
#include "ChannelSlotMap.h"
#include "MeterSnapshot.h"
#include "Plugin/AestraComp.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <sstream>

namespace AestraUI {

namespace {
    constexpr float HEADER_H = 30.0f;
    constexpr float KNOB_H = 32.0f;
    constexpr float FX_H = 30.0f;
    constexpr float BUTTONS_H = 26.0f;
    constexpr float FOOTER_H = 22.0f;
    constexpr float PAD = 8.0f;
    constexpr float GAP = 8.0f;
    constexpr float SIDE_INSET = 4.0f;
    constexpr float SECTION_GAP = 8.0f;
    constexpr float METER_W = 22.0f;
    constexpr float MASTER_METER_W = 36.0f;
    // Labelled Peak / LUFS / Gain blocks at the foot of the master strip.
    // Each block is a label line stacked over a value line; two of them sit in
    // the meter column, so the band must fit 2 * BLOCK_H exactly or the second
    // value falls off the bottom of the strip.
    constexpr float MASTER_LABEL_H = 12.0f;
    constexpr float MASTER_VALUE_H = 14.0f;
    constexpr float MASTER_BLOCK_H = MASTER_LABEL_H + MASTER_VALUE_H;  // 26
    // Two stacked blocks plus bottom slack. With only 4px of slack the second
    // value landed on the strip's bottom edge and was not drawn at all.
    constexpr float MASTER_READOUT_H = MASTER_BLOCK_H * 2.0f + 14.0f;  // 66

    constexpr float SELECT_TOP_H = 3.0f;
    constexpr float MIXER_MIN_CHANNEL_HEIGHT = 220.0f;

    // Resolve a palette index to a renderer colour (matches UIMixerHeader::colorFromARGB).
    NUIColor trackColorFromIndex(int index)
    {
        const uint32_t argb = paletteIndexToARGB(index);
        return NUIColor(((argb >> 16) & 0xFF) / 255.0f,
                        ((argb >> 8) & 0xFF) / 255.0f,
                        (argb & 0xFF) / 255.0f,
                        ((argb >> 24) & 0xFF) / 255.0f);
    }

    std::string compactRouteName(uint32_t targetId, const std::string& targetName)
    {
        if (targetId == 0 || targetName == "Master" || targetName == "MASTER") {
            return "M";
        }
        const std::string channelPrefix = "Channel ";
        if (targetName.rfind(channelPrefix, 0) == 0) {
            return "C" + targetName.substr(channelPrefix.size());
        }
        // Projects saved before strips were renamed still carry "Insert N".
        const std::string legacyInsertPrefix = "Insert ";
        if (targetName.rfind(legacyInsertPrefix, 0) == 0) {
            return "I" + targetName.substr(legacyInsertPrefix.size());
        }
        const std::string legacyTrackPrefix = "Track ";
        if (targetName.rfind(legacyTrackPrefix, 0) == 0) {
            return "I" + targetName.substr(legacyTrackPrefix.size());
        }
        const std::string lanePrefix = "Lane ";
        if (targetName.rfind(lanePrefix, 0) == 0) {
            return "L" + targetName.substr(lanePrefix.size());
        }
        if (targetName.size() <= 6) {
            return targetName;
        }
        return targetName.substr(0, 6);
    }

    std::string buildStripRouteSummary(const Aestra::ChannelViewModel& channel)
    {
        if (channel.id == 0) {
            return channel.routeName;
        }

        const bool busOnly = !channel.masterSendEnabled && channel.mainOutputId != 0;
        int audibleSendCount = 0;
        int sidechainSendCount = 0;
        std::string audibleTarget;
        std::string sidechainTarget;
        std::unordered_set<uint32_t> audibleTargets;
        std::unordered_set<uint32_t> sidechainTargets;

        for (const auto& send : channel.sends) {
            if (send.muted) continue;
            if (send.sidechainOnly) {
                ++sidechainSendCount;
                sidechainTargets.insert(send.targetId);
                if (sidechainTarget.empty()) {
                    sidechainTarget = compactRouteName(send.targetId, send.targetName);
                }
                continue;
            }
            ++audibleSendCount;
            audibleTargets.insert(send.targetId);
            if (audibleTarget.empty()) {
                audibleTarget = compactRouteName(send.targetId, send.targetName);
            }
        }

        std::ostringstream summary;
        bool first = true;
        auto appendToken = [&](const std::string& token) {
            if (token.empty()) return;
            if (!first) summary << " • ";
            summary << token;
            first = false;
        };

        if (busOnly) {
            appendToken("Bus " + compactRouteName(channel.mainOutputId, channel.routeName));
        }
        if (audibleSendCount > 0) {
            if (audibleTargets.size() == 1) {
                appendToken("Snd " + audibleTarget);
            } else {
                appendToken("S+" + std::to_string(static_cast<int>(audibleTargets.size())));
            }
        }
        if (sidechainSendCount > 0) {
            if (sidechainTargets.size() == 1) {
                appendToken("SC " + sidechainTarget);
            } else {
                appendToken("SC+" + std::to_string(static_cast<int>(sidechainTargets.size())));
            }
        }

        return summary.str();
    }

    std::string buildFxStatus(const Aestra::ChannelViewModel& channel)
    {
        if (!channel.channel) {
            return {};
        }

        bool hasSidechainSend = false;
        for (const auto& send : channel.sends) {
            if (!send.muted && send.sidechainOnly) {
                hasSidechainSend = true;
                break;
            }
        }

        auto& chain = channel.channel->getEffectChain();
        for (size_t i = 0; i < Aestra::Audio::EffectChain::MAX_SLOTS; ++i) {
            auto plugin = chain.getPlugin(i);
            auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(plugin);
            if (!comp) {
                continue;
            }

            const float grDb = comp->getCurrentGainReductionDb();
            if (grDb >= 0.1f) {
                std::ostringstream out;
                out.setf(std::ios::fixed);
                out.precision(grDb >= 10.0f ? 0 : 1);
                out << "GR " << grDb;
                return out.str();
            }

            if (hasSidechainSend && channel.sidechainPeak > 0.001f) {
                return "SC live";
            }

            return {};
        }

        return {};
    }

    std::string formatDuckGain(float gain, const std::string& sourceLabel)
    {
        const std::string prefix = sourceLabel.empty() ? "DUCK" : sourceLabel;
        if (!std::isfinite(gain) || gain <= 0.0f) {
            return prefix;
        }
        const float db = 20.0f * std::log10(std::clamp(gain, 1.0e-6f, 1.0f));
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(std::abs(db) >= 9.5f ? 0 : 1);
        out << prefix << " " << db << "dB";
        return out.str();
    }
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
    m_header->onColorChanged = [this](int index) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;
        channel->trackColorIndex = index;
        if (channel->channel) {
            channel->channel->setTrackColorIndex(index);
        }
        invalidateStaticCache();
    };
    addChild(m_header);

    // Input Selector REMOVED (Moved to Inspector)

    m_trimKnob = std::make_shared<UIMixerKnob>(UIMixerKnobType::Trim);
    m_trimKnob->setVisible(true);
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
    m_panKnob->setVisible(true);
    m_panKnob->onValueChanged = [this](float pan) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->pan = pan;
        m_continuousParams->setPan(channel->slotIndex, pan);

        // Fire undo/redo callback
        if (onPanChanged) onPanChanged(pan);
    };
    addChild(m_panKnob);

    // Width Knob (Always visible)
    m_widthKnob = std::make_shared<UIMixerKnob>(UIMixerKnobType::Width);
    m_widthKnob->setVisible(true);
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

        // Fire undo/redo callback
        if (onMuteChanged) onMuteChanged(muted);
    };
    m_buttons->onSoloToggled = [this](bool soloed, NUIModifiers modifiers) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel || channel->id == 0) return;

        // Solo Safe Logic (Ctrl + Click)
        const bool isCtrl = (modifiers & NUIModifiers::Ctrl);
        if (isCtrl) {
            // Solo-safe is independent from solo. Restore the button's prior
            // state, then toggle only the processing-safe flag.
            m_buttons->setSoloed(!soloed);
            if (auto mc = channel->channel) {
                mc->setSoloSafe(!mc->isSoloSafe());
            }
            return;
        }

        channel->soloed = soloed;
        invalidateStaticCache();

        // Fire undo/redo callback
        if (onSoloChanged) onSoloChanged(soloed);
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
    m_meter->setMasterMode(m_channelId == 0);
    addChild(m_meter);

    m_fader = std::make_shared<UIMixerFader>();
    m_fader->setRangeDb(-90.0f, 6.0f);
    m_fader->setDefaultDb(0.0f);
    // The master strip is wide enough to label its unity mark.
    m_fader->setShowScaleLabels(m_channelId == 0);
    m_fader->onValueChanged = [this](float db) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->faderGainDb = db;
        m_continuousParams->setFaderDb(channel->slotIndex, db);
        
        // Fire undo/redo callback
        if (onFaderChanged) onFaderChanged(db);
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
        m_meter->setClipLatch(false, false);
        invalidateStaticCache();
    };

    // Hide strip buttons for master (keeps it visually distinct and avoids non-sense M/S/R).
    if (m_channelId == 0 && m_buttons) {
        m_buttons->setVisible(false);
    }
    if (m_channelId == 0 && m_panKnob) {
        m_panKnob->setVisible(false);
    }
    // Hide Trim/Width for master (master bus doesn't have per-strip trim or width)
    if (m_channelId == 0 && m_trimKnob) {
        m_trimKnob->setVisible(false);
    }
    if (m_channelId == 0 && m_widthKnob) {
        m_widthKnob->setVisible(false);
    }
    if (m_channelId == 0 && m_footer) {
        m_footer->setVisible(false);
    }

    layoutChildren();
}

void UIMixerStrip::setPlatformBridge(NUIPlatformBridge* bridge)
{
    if (m_trimKnob) m_trimKnob->setPlatformBridge(bridge);
    if (m_panKnob) m_panKnob->setPlatformBridge(bridge);
    if (m_widthKnob) m_widthKnob->setPlatformBridge(bridge);
    if (m_fader) m_fader->setPlatformBridge(bridge);
}

void UIMixerStrip::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_selectedTint = theme.getColor("primary").withAlpha(0.022f);
    m_selectedOutline = theme.getColor("primary").withAlpha(0.18f);
    m_selectedTopHighlight = theme.getColor("primary").withAlpha(0.18f);
    
    // Master: Distinct Dark Glass
    m_masterBackground = theme.getColor("surfaceRaised").withAlpha(0.78f);
    m_mutedOverlay = NUIColor(0.0f, 0.0f, 0.0f, 0.4f);
    
    // Standard Strip: Use Theme Glass Border/Hover for consistency
    // m_stripBg = AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.02f); 
    // Use theme.glassHover (0.04f) or similar. Let's use a custom weak glass for strips.
    m_stripBg = theme.getColor("surfaceTertiary").withAlpha(0.58f);
    
    // Master Border
    m_masterBorder = theme.getColor("border").withAlpha(0.36f);
}

void UIMixerStrip::layoutChildren()
{
    auto bounds = getBounds();

    const float contentX = bounds.x + SIDE_INSET;
    const float contentW = std::max(1.0f, bounds.width - SIDE_INSET * 2.0f);
    float y = bounds.y + PAD * 0.75f;

    if (m_header) {
        m_header->setBounds(contentX, y, contentW, HEADER_H);
        y += HEADER_H + SECTION_GAP;
    }

    // Input Dropdown Removed from Strip Layout

    // Three-knob row: Trim + Pan + Width (channel controls grouped together)
    const bool showTrim = (m_trimKnob && m_trimKnob->isVisible());
    const bool showPan = (m_panKnob && m_panKnob->isVisible());
    const bool showWidth = (m_widthKnob && m_widthKnob->isVisible());
    int visibleKnobCount = (showTrim ? 1 : 0) + (showPan ? 1 : 0) + (showWidth ? 1 : 0);
    if (visibleKnobCount > 0) {
        const float knobW = (contentW - GAP * (visibleKnobCount - 1)) / visibleKnobCount;
        float knobX = contentX;
        if (showTrim) {
            m_trimKnob->setBounds(knobX, y, knobW, KNOB_H);
            knobX += knobW + GAP;
        }
        if (showPan) {
            m_panKnob->setBounds(knobX, y, knobW, KNOB_H);
            knobX += knobW + GAP;
        }
        if (showWidth) {
            m_widthKnob->setBounds(knobX, y, knobW, KNOB_H);
        }
        y += KNOB_H + SECTION_GAP;
    }

    // M/S/R buttons moved below knobs, above FX summary
    const bool hasButtons = (m_buttons && m_buttons->isVisible());
    if (hasButtons) {
        m_buttons->setBounds(contentX, y, contentW, BUTTONS_H);
        y += BUTTONS_H + 6.0f;
    }

    // FX Summary (Add Insert) below the channel controls
    if (m_fxSummary && m_fxSummary->isVisible()) {
        if (m_channelId == 0) {
            y += 10.0f;
        }
        m_fxSummary->setBounds(contentX, y, contentW, FX_H);
        y += FX_H + SECTION_GAP;
    }

    const bool hasFooter = (m_footer && m_footer->isVisible());
    const float footerH = hasFooter ? FOOTER_H : 0.0f;
    const float footerY = bounds.y + bounds.height - PAD - footerH;
    if (hasFooter) {
        m_footer->setBounds(contentX, footerY, contentW, FOOTER_H);
    }

    const bool isMaster = (m_channelId == 0);
    const float meterW = isMaster ? MASTER_METER_W : METER_W;

    // The master reserves a labelled Peak/LUFS/Gain block at the foot of the
    // strip; its footer is hidden, so that space is otherwise unused.
    const float readoutH = isMaster ? MASTER_READOUT_H : 0.0f;

    // Meter + fader fill the strip between the channel controls and the footer.
    // (Previously sized to 62% of the free height and bottom-anchored, which
    // left a large dead gap above the fader — the tallest, most-used control.)
    const float meterY = y;
    const float meterH = std::max(16.0f, (footerY - GAP - readoutH) - y);
    const float meterX = contentX + 2.0f;

    if (m_meter) {
        m_meter->setBounds(meterX, meterY, meterW, meterH);
    }

    if (m_fader) {
        const float faderX = meterX + meterW + GAP;
        const float faderW = std::max(16.0f, (contentX + contentW) - faderX);
        m_fader->setBounds(faderX, meterY, faderW, meterH);
    }

    // Readouts sit under the column they describe: observed signal (Peak, LUFS)
    // under the meter, user-controlled gain under the fader. Keeping all three
    // in one bottom block made the eye travel from the live bars to a detached
    // table to interpret state, and blurred which side owns which number.
    if (isMaster) {
        const float readoutY = footerY - MASTER_READOUT_H;
        m_masterMeterReadoutRect = NUIRect{meterX, readoutY, meterW + GAP, MASTER_READOUT_H};
        const float gainX = meterX + meterW + GAP;
        m_masterGainReadoutRect =
            NUIRect{gainX, readoutY, std::max(16.0f, (contentX + contentW) - gainX), MASTER_READOUT_H};
    } else {
        m_masterMeterReadoutRect = NUIRect{};
        m_masterGainReadoutRect = NUIRect{};
    }
}

void UIMixerStrip::renderMasterReadout(NUIRenderer& renderer,
                                       const Aestra::ChannelViewModel& channel)
{
    const NUIRect& meterArea = m_masterMeterReadoutRect;
    const NUIRect& gainArea = m_masterGainReadoutRect;
    if (meterArea.width <= 0.0f || meterArea.height <= 0.0f) return;

    auto& theme = NUIThemeManager::getInstance();
    const NUIColor labelColor = theme.getColor("textSecondary").withAlpha(0.72f);
    const NUIColor valueColor = theme.getColor("textPrimary").withAlpha(0.95f);
    const NUIColor clipColor = theme.getColor("meterCrit");

    const bool clipped = channel.clipLatchL || channel.clipLatchR;

    // Peak: prefer the held value, which is what the meter's hold line shows.
    float peakDb = std::max(channel.smoothedPeakL, channel.smoothedPeakR);
    if (channel.peakHoldL > Aestra::MixerMath::DB_MIN ||
        channel.peakHoldR > Aestra::MixerMath::DB_MIN) {
        peakDb = std::max(channel.peakHoldL, channel.peakHoldR);
    }

    char buf[32];
    // Stacked label-over-value: the master columns are too narrow for a
    // side-by-side label and a signed value.
    auto block = [&](const NUIRect& area, int index, const char* label,
                     const std::string& value, const NUIColor& color) {
        const float y = area.y + static_cast<float>(index) * MASTER_BLOCK_H;
        const NUIRect labelRect{area.x, y, area.width, MASTER_LABEL_H};
        const NUIRect valueRect{area.x, y + MASTER_LABEL_H, area.width, MASTER_VALUE_H};
        renderer.drawTextCentered(label, labelRect, 8.5f, labelColor);
        renderer.drawTextCentered(value, valueRect, 10.0f, color);
    };

    // --- Observed signal, under the meter ---
    // PEAK — dBFS, explicitly. Doubles as the clip/over indicator.
    std::string peakText;
    if (peakDb <= Aestra::MixerMath::DB_MIN) {
        peakText = "\xE2\x88\x92\xE2\x88\x9E";
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", peakDb);
        peakText = buf;
    }
    block(meterArea, 0, clipped ? "OVER" : "PEAK dBFS", peakText, clipped ? clipColor : valueColor);

    // LUFS — integrated, and the sign always survives (see UIMixerMeter).
    // Integrated LUFS is undefined until the gate has material; show the same
    // silence marker the peak readout uses rather than an ambiguous dash.
    std::string lufsText = "\xE2\x88\x92\xE2\x88\x9E";
    if (channel.integratedLufs > -100.0f) {
        std::snprintf(buf, sizeof(buf), "%.1f", channel.integratedLufs);
        lufsText = buf;
    }
    block(meterArea, 1, "LUFS INT", lufsText, valueColor);

    // --- User-controlled gain, under the fader ---
    if (gainArea.width > 0.0f) {
        std::snprintf(buf, sizeof(buf), "%.1f", channel.faderGainDb);
        block(gainArea, 0, "GAIN dB", buf, valueColor);
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
        const std::string stripRoute = buildStripRouteSummary(*channel);
        if (m_cachedRoute != stripRoute) {
            m_cachedRoute = stripRoute;
            invalidateStaticCache();
        }
        m_header->setRouteName(stripRoute);
        if (m_cachedTrackColorArgb != static_cast<uint32_t>(channel->trackColorIndex + 1)) {
            m_cachedTrackColorArgb = static_cast<uint32_t>(channel->trackColorIndex + 1);
            invalidateStaticCache();

            // The track colour is the channel's IDENTITY, so it is carried by the
            // header (and the overview strip) only. It used to be threaded into
            // the fader fill and all three knob arcs as well, which left every
            // control looking permanently "active" and made the coloured fader
            // rail read as a second level meter. The fader now takes the accent
            // for its engaged handle edge alone; the knobs stay neutral.
            if (m_channelId != 0 && channel->trackColorIndex >= 0) {
                const NUIColor accent = trackColorFromIndex(channel->trackColorIndex);
                if (m_fader) m_fader->setAccentColor(accent);
            }
        }
        m_header->setTrackColorIndex(channel->trackColorIndex);
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
        const std::string fxStatus = buildFxStatus(*channel);
        if (m_cachedFxStatus != fxStatus) {
            m_cachedFxStatus = fxStatus;
            invalidateStaticCache();
            m_fxSummary->setStatusText(fxStatus);
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
    float radius = NUIThemeManager::getInstance().getRadius("m");
    renderer.fillRoundedRect(bounds, radius, m_stripBg);

    // Master gets a slightly different tone to distinguish it (optional, but good for hierarchy)
    if (m_channelId == 0) {
        // Subtle accent-tinted vertical gradient for visual hierarchy
        const NUIColor accent = NUIThemeManager::getInstance().getColor("primary");
        renderer.fillRoundedRect(bounds, radius,
                                  accent.withAlpha(0.04f));
        renderer.fillRectGradient({bounds.x, bounds.y, bounds.width, 90.0f},
                                   accent.withAlpha(0.10f), accent.withAlpha(0.0f), true);
        renderer.strokeRoundedRect(bounds, radius, 1.0f, m_masterBorder);
    }

    if (selected) {
        // Selection = a slight tinted body + a strong top edge. No outline.
        //
        // This started at five layers (tint, top bar, inner highlight, outer
        // glow ring, outline). The outline was the last redundant one: with a
        // tinted body and a bright edge the strip is already unmistakable, and
        // the border was what made a selected strip read as a different kind of
        // component rather than the same strip, selected.
        renderer.fillRoundedRect(bounds, radius, m_selectedTint);
        renderer.fillRect(NUIRect{bounds.x, bounds.y, bounds.width, SELECT_TOP_H}, m_selectedTopHighlight);
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
        if (m_channelId == 0 && channel) {
            renderMasterReadout(renderer, *channel);
        }
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
    if (m_channelId == 0 && channel) {
        renderMasterReadout(renderer, *channel);
    }
    if (m_channelId == 0 && m_viewModel && m_viewModel->isPreviewDuckingActive()) {
        auto& theme = NUIThemeManager::getInstance();
        const float duckGain = m_viewModel->getPreviewDuckGain();
        const std::string label = formatDuckGain(duckGain, m_viewModel->getPreviewDuckSourceLabel());
        const NUIColor accent = theme.getColor("warning");
        const NUIColor text = theme.getColor("textPrimary");
        const float badgeW = std::min(86.0f, std::max(66.0f, bounds.width - 20.0f));
        const auto fxBounds = getFXSummaryBounds();
        const float badgeY = fxBounds.height > 0.0f ? fxBounds.y + fxBounds.height + 6.0f : bounds.y + 72.0f;
        const NUIRect badge(bounds.x + (bounds.width - badgeW) * 0.5f, badgeY, badgeW, 17.0f);
        renderer.fillRoundedRect(badge, 5.0f, accent.withAlpha(0.18f));
        renderer.strokeRoundedRect(badge, 5.0f, 1.0f, accent.withAlpha(0.52f));
        renderer.drawTextCentered(label, badge, 8.4f, text.withAlpha(0.92f));
    }
    if (channel && channel->muted) {
        renderer.fillRoundedRect(getBounds(), radius, m_mutedOverlay);
    }
}

bool UIMixerStrip::onMouseEvent(const NUIMouseEvent& event)
{
    if (m_meter && m_viewModel && event.pressed &&
        (event.button == NUIMouseButton::Left || event.button == NUIMouseButton::Right) &&
        getBounds().contains(event.position)) {
        const auto stripBounds = getBounds();
        const auto meterBounds = m_meter->getBounds();
        auto* channel = m_viewModel->getChannelById(m_channelId);
        std::ostringstream hitLog;
        hitLog << "[MixerClipHit] channel=" << m_channelId
               << " button=" << static_cast<int>(event.button)
               << " click=(" << event.position.x << "," << event.position.y << ")"
               << " strip=(" << stripBounds.x << "," << stripBounds.y << ","
               << stripBounds.width << "," << stripBounds.height << ")"
               << " meter=(" << meterBounds.x << "," << meterBounds.y << ","
               << meterBounds.width << "," << meterBounds.height << ")"
               << " inMeter=" << (meterBounds.contains(event.position) ? 1 : 0)
               << " latchL=" << (channel ? (channel->clipLatchL ? 1 : 0) : -1)
               << " latchR=" << (channel ? (channel->clipLatchR ? 1 : 0) : -1);
        AESTRA_LOG_INFO(hitLog.str());
    }

    if (m_meter && m_viewModel && event.pressed &&
        (event.button == NUIMouseButton::Left || event.button == NUIMouseButton::Right) &&
        m_meter->getBounds().contains(event.position)) {
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (channel && (channel->clipLatchL || channel->clipLatchR)) {
            m_viewModel->clearClipLatch(channel->id);
            if (m_meterSnapshots) {
                m_meterSnapshots->clearClip(channel->slotIndex);
            }
            m_meter->setClipLatch(false, false);
            invalidateStaticCache();
        }
        return true;
    }

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
    if (m_panKnob && m_panKnob->isVisible()) {
        m_panKnob->onRender(renderer);
    }
    if (m_widthKnob && m_widthKnob->isVisible()) {
        m_widthKnob->onRender(renderer);
    }
    if (m_buttons && m_buttons->isVisible()) {
        m_buttons->onRender(renderer);
    }
    if (m_fxSummary && m_fxSummary->isVisible()) {
        m_fxSummary->onRender(renderer);
    }
    if (m_fader && m_fader->isVisible()) {
        m_fader->onRender(renderer);
    }
    if (m_footer && m_footer->isVisible()) {
        m_footer->onRender(renderer);
    }
}

NUIRect UIMixerStrip::getFXSummaryBounds() const
{
    return m_fxSummary ? m_fxSummary->getBounds() : NUIRect{};
}

void UIMixerStrip::dismissFaderEdit(const NUIPoint& position)
{
    if (m_fader) {
        m_fader->dismissEditAt(position);
    }
}

} // namespace AestraUI
