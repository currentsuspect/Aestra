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
    constexpr float PAN_H = 8.0f;
    constexpr float PAN_VALUE_H = 12.0f;
    constexpr float MIN_METER_H = 120.0f;
    constexpr float OUTPUT_CHIP_H = 17.0f;
    constexpr int INSERT_ROWS = 3;
    constexpr float BUTTONS_H = 26.0f;
    constexpr float FOOTER_H = 18.0f;
    constexpr float PAD = 8.0f;
    constexpr float GAP = 8.0f;
    constexpr float SIDE_INSET = 4.0f;
    constexpr float SECTION_GAP = 8.0f;
    constexpr float METER_W = 24.0f;     // two 11 px bars and the gap between them
    constexpr float FADER_COL_W = 14.0f; // the handle column beside the meters
    constexpr float MASTER_METER_W = 36.0f;
    // Technical master readouts (held peak dBFS, integrated LUFS, gain) are a
    // permanent fixture under the master's meter/fader columns — the master
    // strip has no footer, so this is the strip's live level dashboard.
    constexpr float MASTER_LABEL_H = 12.0f;
    constexpr float MASTER_VALUE_H = 14.0f;
    constexpr float MASTER_BLOCK_H = MASTER_LABEL_H + MASTER_VALUE_H;  // 26
    // The master reserves a labelled Peak/LUFS/Gain block at the foot of the
    // strip (its footer is hidden, so the space is otherwise dead). Two stacked
    // blocks plus bottom slack: with only 4px of slack the second value landed
    // on the strip's bottom edge and was not drawn at all.
    constexpr float MASTER_ROW_H = 15.0f;
    // Integrated, short-term, true peak, correlation (with its bar), gain: one line each.
    constexpr float MASTER_READOUT_H = MASTER_ROW_H * 6.0f + 8.0f;

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
        for (size_t i = 0, slotTotal = chain.slotCount(); i < slotTotal; ++i) {
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

    m_insertList = std::make_shared<UIMixerInsertList>();
    m_insertList->onInvalidateRequested = [this]() { invalidateStaticCache(); };
    m_insertList->onClicked = [this]() {
        if (onFXClicked) onFXClicked(m_channelId);
    };
    addChild(m_insertList);

    // Where this channel goes, by name and colour (FD-28). The master has no output to name.
    m_outputChip = std::make_shared<UIOutputChip>();
    m_outputChip->setVisible(m_channelId != 0);
    m_outputChip->onClicked = [this]() {
        if (onOutputClicked) onOutputClicked(m_channelId);
    };
    addChild(m_outputChip);

    // Pan as a bar with its value under it (the strip's pan, V81-M1): drag along it, double-click to centre.
    m_panBar = std::make_shared<UIParamBar>(UIParamBar::Look::Bar);
    m_panBar->setRange(-1.0f, 1.0f, 0.0f);
    m_panBar->setBipolar(true);
    m_panBar->onValueChanged = [this](float pan) {
        if (!m_viewModel || !m_continuousParams) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;

        channel->pan = pan;
        m_continuousParams->setPan(channel->slotIndex, pan);

        // Fire undo/redo callback
        if (onPanChanged) onPanChanged(pan);
    };
    addChild(m_panBar);

    // Sends by destination and level; a click opens the channel's inspector.
    m_sendList = std::make_shared<UIMixerSendList>();
    m_sendList->onClicked = [this]() {
        if (m_viewModel) m_viewModel->setSelectedChannelId(static_cast<int32_t>(m_channelId));
    };
    addChild(m_sendList);

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
    m_buttons->onMonitorToggled = [this](bool monitored) {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel || channel->id == 0) return;

        // FD-14 #6: the mixer slot is input monitoring, not record arm. The
        // engine's monitor routes gate on channel armed + monitorInput.
        channel->monitored = monitored;
        invalidateStaticCache();

        // Engine side goes through the command history (SetMonitoringCommand)
        // for dirty/undo parity with mute/solo/pan. The snapshot refresh
        // rides setArmed's own notification.
        if (onMonitorToggled) onMonitorToggled(monitored);
    };
    addChild(m_buttons);

    m_meter = std::make_shared<UIMixerMeter>();
    m_meter->setShowCorrelation(true);
    m_meter->setMasterMode(m_channelId == 0);
    addChild(m_meter);

    m_fader = std::make_shared<UIMixerFader>();
    m_fader->setRangeDb(-90.0f, 6.0f);
    m_fader->setDefaultDb(0.0f);
    // The master strip is wide enough to label its unity mark; channel strips draw the cap across the meters.
    m_fader->setShowScaleLabels(m_channelId == 0);
    if (m_channelId != 0) m_fader->setOverlay(true, METER_W + 2.0f);
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
    m_footer->onResetPeakRequested = [this]() {
        if (!m_viewModel) return;
        auto* channel = m_viewModel->getChannelById(m_channelId);
        if (!channel) return;
        channel->peakSinceResetDb = Aestra::MixerMath::DB_MIN;
        m_viewModel->clearClipLatch(channel->id);
        if (m_meterSnapshots) m_meterSnapshots->clearClip(channel->slotIndex);
        if (m_meter) m_meter->setClipLatch(false, false);
    };
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
    if (m_channelId == 0 && m_panBar) {
        m_panBar->setVisible(false);
    }
    if (m_channelId == 0 && m_sendList) {
        m_sendList->setVisible(false);
    }
    if (m_channelId == 0 && m_footer) {
        m_footer->setVisible(false);
    }

    layoutChildren();
}

void UIMixerStrip::setPlatformBridge(NUIPlatformBridge* bridge)
{
    if (m_fader) m_fader->setPlatformBridge(bridge);
}

void UIMixerStrip::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    // Selection is a navigation state, not a second channel colour. The
    // inspector and a thin top marker carry it; the strip surface stays calm.
    m_selectedTint = theme.getColor("primary").withAlpha(0.008f);
    m_selectedOutline = theme.getColor("primary").withAlpha(0.18f);
    m_selectedTopHighlight = theme.getColor("primary").withAlpha(0.72f);
    
    // Master: Distinct Dark Glass
    m_masterBackground = theme.getColor("surfaceRaised").withAlpha(0.78f);
    m_mutedOverlay = NUIColor(0.0f, 0.0f, 0.0f, 0.4f);
    
    // Strips are layered surfaces, not individual floating cards.
    m_stripBg = theme.getColor("backgroundSecondary").withAlpha(0.88f);
    
    // Master Border
    m_masterBorder = theme.getColor("border").withAlpha(0.36f);
}

void UIMixerStrip::layoutChildren()
{
    auto bounds = getBounds();

    const float contentX = bounds.x + SIDE_INSET;
    const float contentW = std::max(1.0f, bounds.width - SIDE_INSET * 2.0f);
    const bool isMaster = (m_channelId == 0);
    float y = bounds.y + PAD * 0.75f;

    if (m_header) {
        m_header->setBounds(contentX, y, contentW, HEADER_H);
        y += HEADER_H + 2.0f;
    }

    // The output, as a named coloured chip right under the name.
    if (m_outputChip && m_outputChip->isVisible()) {
        m_outputChip->setBounds(contentX, y, contentW, OUTPUT_CHIP_H);
        y += OUTPUT_CHIP_H + 4.0f;
    }

    const bool hasFooter = (m_footer && m_footer->isVisible());
    const float footerH = hasFooter ? FOOTER_H : 0.0f;
    const float footerY = bounds.y + bounds.height - PAD - footerH;
    if (hasFooter) {
        m_footer->setBounds(contentX, footerY, contentW, FOOTER_H);
    }

    // Room is spent in priority order: the meter first, then the keys and the pan, then the insert names,
    // and the sends last. A short strip drops rows before it squeezes the meter.
    const bool hasButtons = (m_buttons && m_buttons->isVisible());
    const bool hasPan = (m_panBar && m_panBar->isVisible());
    const float keysH = hasButtons ? BUTTONS_H + 6.0f : 0.0f;
    const float panH = hasPan ? PAN_H + PAN_VALUE_H + 6.0f : 0.0f;
    const int wantedSends = m_sendList ? std::min(static_cast<int>(m_sendList->rows().size()), UIMixerSendList::kMaxRows) : 0;
    const bool hasInserts = (m_insertList && m_insertList->isVisible());
    const float masterGap = (isMaster && hasInserts) ? 10.0f : 0.0f;
    const float readoutH = isMaster ? MASTER_READOUT_H : 0.0f;

    int insertRows = INSERT_ROWS;
    int sendRows = wantedSends;
    const auto meterHeightFor = [&](int inserts, int sends) {
        const float insertsH = hasInserts ? UIMixerInsertList::heightForRows(inserts) + SECTION_GAP : 0.0f;
        const float sendsH = sends > 0 ? UIMixerSendList::heightForRows(sends) + 4.0f : 0.0f;
        return (footerY - GAP - readoutH) - (y + masterGap + insertsH + sendsH + panH + keysH);
    };
    while (sendRows > 0 && meterHeightFor(insertRows, sendRows) < MIN_METER_H) --sendRows;
    while (insertRows > 1 && meterHeightFor(insertRows, sendRows) < MIN_METER_H) --insertRows;

    y += masterGap;
    if (hasInserts) {
        m_insertList->setBounds(contentX, y, contentW, UIMixerInsertList::heightForRows(insertRows));
        y += UIMixerInsertList::heightForRows(insertRows) + SECTION_GAP;
    }
    if (m_sendList && m_sendList->isVisible()) {
        const float h = UIMixerSendList::heightForRows(sendRows);
        m_sendList->setBounds(contentX, y, contentW, h);
        y += h > 0.0f ? h + 4.0f : 0.0f;
    }
    if (hasPan) {
        m_panBar->setBounds(contentX + 4.0f, y, contentW - 8.0f, PAN_H);
        m_panValueRect = NUIRect{contentX, y + PAN_H, contentW, PAN_VALUE_H};
        y += PAN_H + PAN_VALUE_H + 6.0f;
    } else {
        m_panValueRect = NUIRect{};
    }
    if (hasButtons) {
        m_buttons->setBounds(contentX, y, contentW, BUTTONS_H);
        y += BUTTONS_H + 6.0f;
    }

    // The master reserves a labelled Peak/LUFS/Gain block at the foot of the
    // strip; its footer is hidden, so that space is otherwise unused.
    const float meterW = isMaster ? MASTER_METER_W : METER_W;

    // Meter + fader fill the strip between the channel controls and the footer.
    const float meterY = y;
    const float meterH = std::max(16.0f, (footerY - GAP - readoutH) - y);

    if (isMaster) {
        const float meterX = contentX + 2.0f;
        if (m_meter) m_meter->setBounds(meterX, meterY, meterW, meterH);
        if (m_fader) {
            const float faderX = meterX + meterW + GAP;
            const float faderW = std::max(16.0f, (contentX + contentW) - faderX);
            m_fader->setBounds(faderX, meterY, faderW, meterH);
        }
        // Readouts sit under the column they describe: observed signal (Peak, LUFS)
        // under the meter, user-controlled gain under the fader.
        const float readoutY = footerY - MASTER_READOUT_H;
        m_masterMeterReadoutRect = NUIRect{contentX, readoutY, contentW, MASTER_READOUT_H};
        m_masterGainReadoutRect = NUIRect{};
        return;
    }

    // Channel strips: the meters sit centred with the fader's handle column on their right edge, and the cap
    // line drawn by the fader runs back across both bars.
    const float groupW = meterW + 2.0f + FADER_COL_W;
    const float meterX = std::floor(contentX + (contentW - groupW) * 0.5f);
    if (m_meter) m_meter->setBounds(meterX, meterY, meterW, meterH);
    if (m_fader) {
        // The meter reserves its clip lamp at the top; the fader's track starts where the bars do.
        constexpr float kClipLamp = 6.0f;
        constexpr float kHandleHalf = 9.0f;
        m_fader->setBounds(meterX + meterW + 1.0f, meterY + kClipLamp - kHandleHalf, FADER_COL_W,
                           meterH - kClipLamp + kHandleHalf * 2.0f);
    }
    m_masterMeterReadoutRect = NUIRect{};
    m_masterGainReadoutRect = NUIRect{};
}

void UIMixerStrip::renderPanValue(NUIRenderer& renderer, const Aestra::ChannelViewModel& channel)
{
    if (m_panValueRect.isEmpty()) return;
    // C, or the side and how far: L25, R10 (percent of full pan).
    const int percent = static_cast<int>(std::lround(std::fabs(channel.pan) * 100.0f));
    const std::string text = percent == 0 ? std::string("C")
                                           : std::string(channel.pan < 0.0f ? "L" : "R") + std::to_string(percent);
    renderer.drawTextCentered(text, m_panValueRect, 9.5f,
                              NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.85f));
}

void UIMixerStrip::renderMasterReadout(NUIRenderer& renderer,
                                       const Aestra::ChannelViewModel& channel)
{
    const NUIRect& card = m_masterMeterReadoutRect;
    if (card.width <= 0.0f || card.height <= 0.0f) return;

    auto& theme = NUIThemeManager::getInstance();
    const NUIColor labelColor = theme.getColor("textSecondary").withAlpha(0.864f);
    const NUIColor valueColor = theme.getColor("textPrimary").withAlpha(0.978f);
    const NUIColor warnColor = theme.getColor("meterWarn");
    const NUIColor clipColor = theme.getColor("meterCrit");

    renderer.fillRoundedRect(card, 5.0f, theme.getColor("buttonBgDefault").withAlpha(0.55f));

    constexpr const char* kSilence = "\xE2\x88\x92\xE2\x88\x9E"; // minus infinity
    char buf[32];
    int row = 0;
    // One line per reading: the label on the left, the value (sign always kept) on the right, in LU or dB.
    auto line = [&](const char* label, const std::string& value, const char* unit, const NUIColor& color) {
        const float y = card.y + 4.0f + static_cast<float>(row++) * MASTER_ROW_H;
        const NUIRect labelRect{card.x + 7.0f, y, card.width * 0.5f, MASTER_ROW_H};
        const NUIRect valueRect{card.x + card.width * 0.38f, y, card.width * 0.62f - 7.0f, MASTER_ROW_H};
        renderer.drawText(label, {labelRect.x, y + (MASTER_ROW_H - 8.5f) * 0.5f - 1.0f}, 8.5f, labelColor);
        const std::string text = value + (unit[0] ? std::string(" ") + unit : std::string());
        const float w = renderer.measureText(text, 10.0f).width;
        renderer.drawText(text, {valueRect.right() - w, y + (MASTER_ROW_H - 10.0f) * 0.5f - 1.0f}, 10.0f, color);
    };
    auto lufsText = [&](float lufs) {
        if (lufs <= -100.0f) return std::string(kSilence);
        std::snprintf(buf, sizeof(buf), "%.1f", lufs);
        return std::string(buf);
    };

    // Integrated is undefined until the gate has material; short-term ramps in over the first three seconds.
    line("INTEGRATED", lufsText(channel.integratedLufs), "LUFS", valueColor);
    line("SHORT-TERM", lufsText(channel.shortTermLufs), "LUFS", valueColor);

    // True peak, held. Red once it passes 0 dBTP or the output has clipped, amber within 1 dB of it.
    const bool clipped = channel.clipLatchL || channel.clipLatchR;
    const float tp = channel.truePeakDb;
    std::string tpText = kSilence;
    if (tp > -100.0f) {
        std::snprintf(buf, sizeof(buf), "%.1f", tp);
        tpText = buf;
    }
    line(clipped ? "OVER" : "TRUE PEAK", tpText, "dBTP", (clipped || tp > 0.0f) ? clipColor : (tp > -1.0f ? warnColor : valueColor));

    // Phase correlation: a value and a bar from -1 (out of phase) through 0 to +1 (mono).
    std::snprintf(buf, sizeof(buf), "%+.2f", channel.correlation);
    line("CORRELATION", buf, "", channel.correlation < 0.0f ? warnColor : valueColor);
    {
        const NUIRect bar{card.x + 7.0f, card.y + 4.0f + static_cast<float>(row) * MASTER_ROW_H - 1.0f,
                          card.width - 14.0f, 3.0f};
        renderer.fillRoundedRect(bar, 1.5f, theme.getColor("border").withAlpha(0.35f));
        const float mid = bar.x + bar.width * 0.5f;
        const float pos = mid + std::clamp(channel.correlation, -1.0f, 1.0f) * bar.width * 0.5f;
        renderer.fillRect(NUIRect{std::min(mid, pos), bar.y, std::max(1.5f, std::abs(pos - mid)), bar.height},
                          (channel.correlation < 0.0f ? warnColor : theme.getColor("accentPrimary")).withAlpha(0.9f));
        renderer.fillRect(NUIRect{mid - 0.5f, bar.y - 1.0f, 1.0f, bar.height + 2.0f}, labelColor.withAlpha(0.6f));
    }

    // The fader's gain: the one user-controlled number, kept with the readings it governs.
    std::string gainText = kSilence;
    if (channel.faderGainDb > UIMixerFader::FADER_FLOOR_THRESHOLD) {
        std::snprintf(buf, sizeof(buf), "%.1f", channel.faderGainDb);
        gainText = buf;
    }
    ++row; // room under the correlation bar
    line("GAIN", gainText, "dB", valueColor);
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
        // BUS / RETURN under the name; plain tracks say nothing.
        const std::string tag = channel->id == 0 ? std::string()
            : channel->role == Aestra::ChannelViewModel::Role::Bus ? std::string("BUS")
            : channel->role == Aestra::ChannelViewModel::Role::Return ? std::string("RETURN") : std::string();
        if (m_cachedTag != tag) {
            m_cachedTag = tag;
            invalidateStaticCache();
        }
        m_header->setRouteName(channel->id == 0 ? channel->routeName : tag);
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
        // The monitor button reflects the engine's arm flag — post-FD-14 #6
        // that flag is the input-monitoring gate, never a recording control.
        m_buttons->setMonitored(channel->armed);
    }

    if (m_panBar && m_panBar->isVisible() && !m_panBar->isDragging()) {
        if (std::abs(m_cachedPan - channel->pan) > 1e-4f) {
            m_cachedPan = channel->pan;
            invalidateStaticCache();
        }
        m_panBar->setValue(channel->pan);
    }

    if (m_sendList && m_sendList->isVisible()) {
        std::vector<UIMixerSendList::Row> rows;
        std::string sig;
        for (const auto& send : channel->sends) {
            UIMixerSendList::Row row;
            row.name = send.targetName;
            row.sidechain = send.sidechainOnly;
            row.muted = send.muted;
            row.levelDb = send.gain > 1e-6f ? 20.0f * std::log10(send.gain) : -90.0f;
            const auto* target = m_viewModel->getChannelById(send.targetId);
            row.color = (target && target->trackColorIndex >= 0) ? trackColorFromIndex(target->trackColorIndex)
                                                                  : NUIThemeManager::getInstance().getColor("accentPrimary");
            sig += row.name + ":" + std::to_string(static_cast<int>(row.levelDb * 10.0f)) + (row.sidechain ? "s" : "") +
                   (row.muted ? "m" : "") + ";";
            rows.push_back(std::move(row));
        }
        if (m_cachedSendSig != sig) {
            const bool countChanged = m_sendList->rows().size() != rows.size();
            m_cachedSendSig = sig;
            m_sendList->setRows(std::move(rows));
            invalidateStaticCache();
            if (countChanged) layoutChildren(); // the send rows ask for height; re-spend the strip's room
        }
    }

    if (m_insertList && m_insertList->isVisible()) {
        std::vector<UIMixerInsertList::Row> rows;
        std::string sig;
        for (const auto& insert : channel->inserts) {
            if (insert.isEmpty || insert.pendingRemoval)
                continue;
            rows.push_back({insert.name, insert.bypassed});
            sig += insert.name + (insert.bypassed ? "~|" : "|");
        }
        if (m_cachedInsertSig != sig) {
            m_cachedInsertSig = sig;
            invalidateStaticCache();
            m_insertList->setRows(std::move(rows));
        }
        const std::string fxStatus = buildFxStatus(*channel);
        if (m_cachedFxStatus != fxStatus) {
            m_cachedFxStatus = fxStatus;
            invalidateStaticCache();
            m_insertList->setStatusText(fxStatus);
        }
    }

    if (m_outputChip && m_outputChip->isVisible()) {
        const auto* target = m_viewModel->getChannelById(channel->mainOutputId);
        const int colorIndex = (channel->mainOutputId != 0 && target) ? target->trackColorIndex : -1;
        if (m_cachedOutputLabel != channel->routeName || m_cachedOutputColorIndex != colorIndex) {
            m_cachedOutputLabel = channel->routeName;
            m_cachedOutputColorIndex = colorIndex;
            m_outputChip->setLabel(channel->routeName);
            m_outputChip->setColor(colorIndex >= 0 ? trackColorFromIndex(colorIndex)
                                                    : NUIThemeManager::getInstance().getColor("accentPrimary"));
            invalidateStaticCache();
        }
        // A channel whose main output is off (bus-only elsewhere or muted path) shows its chip dimmed.
        m_outputChip->setDimmed(channel->mainOutputId == 0 && !channel->masterSendEnabled);
    }

    if (m_footer && m_footer->isVisible()) {
        m_footer->setGainDb(channel->faderGainDb);
        m_footer->setPeakDb(channel->peakSinceResetDb, channel->clipLatchL || channel->clipLatchR);
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

    // Strips form one continuous instrument surface. Gaps and tonal contrast
    // establish columns; rounded card outlines would fragment the mixer.
    constexpr float radius = 4.0f;
    renderer.fillRect(bounds, m_stripBg);

    // Master gets a slightly different tone to distinguish it (optional, but good for hierarchy)
    if (m_channelId == 0) {
        // Master is differentiated by a quiet tonal step and its fixed place,
        // not by an always-on purple frame.
        renderer.fillRect(bounds, m_masterBackground);
    }

    if (selected) {
        // Selection = a slight tinted body + a strong top edge. No outline.
        //
        // This started at five layers (tint, top bar, inner highlight, outer
        // glow ring, outline). The outline was the last redundant one: with a
        // tinted body and a bright edge the strip is already unmistakable, and
        // the border was what made a selected strip read as a different kind of
        // component rather than the same strip, selected.
        renderer.fillRect(bounds, m_selectedTint);
        renderer.fillRect(NUIRect{bounds.x, bounds.y, bounds.width, SELECT_TOP_H}, m_selectedTopHighlight);
    }

    // While dragging, render live (no caching) so interactive controls update every frame.
    const bool dragging =
        (m_fader && m_fader->isDragging()) ||
        (m_panBar && m_panBar->isDragging());
    if (dragging) {
        invalidateStaticCache();
        renderChildren(renderer);
        if (m_channelId == 0 && channel) {
            renderMasterReadout(renderer, *channel);
        } else if (channel) {
            renderPanValue(renderer, *channel);
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
    } else if (channel) {
        renderPanValue(renderer, *channel);
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
        renderer.drawTextCentered(label, badge, 8.4f, text.withAlpha(0.964f));
    }
    if (channel && channel->muted) {
        renderer.fillRect(getBounds(), m_mutedOverlay);
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
    if (m_panBar && m_panBar->isVisible()) {
        m_panBar->onRender(renderer);
    }
    if (m_sendList && m_sendList->isVisible()) {
        m_sendList->onRender(renderer);
    }
    if (m_buttons && m_buttons->isVisible()) {
        m_buttons->onRender(renderer);
    }
    if (m_outputChip && m_outputChip->isVisible()) {
        m_outputChip->onRender(renderer);
    }
    if (m_insertList && m_insertList->isVisible()) {
        m_insertList->onRender(renderer);
    }
    if (m_fader && m_fader->isVisible()) {
        m_fader->onRender(renderer);
    }
    if (m_footer && m_footer->isVisible()) {
        m_footer->onRender(renderer);
    }
}

NUIRect UIMixerStrip::meterBarsRect() const
{
    if (!m_meter) return NUIRect{};
    constexpr float kClipLamp = 6.0f; // the meter reserves this at the top for its clip indicator
    const auto b = m_meter->getBounds();
    return NUIRect{b.x, b.y + kClipLamp, b.width, std::max(0.0f, b.height - kClipLamp)};
}

NUIRect UIMixerStrip::getFXSummaryBounds() const
{
    return m_insertList ? m_insertList->getBounds() : NUIRect{};
}

void UIMixerStrip::dismissFaderEdit(const NUIPoint& position)
{
    if (m_fader) {
        m_fader->dismissEditAt(position);
    }
}

} // namespace AestraUI
