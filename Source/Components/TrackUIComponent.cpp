// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TrackUIComponent.h"
#include "TrackManagerUI.h"
#include "TrackManagerUIMath.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "MixerChannel.h"
#include "TrackManager.h"
#include "PlaylistModel.h"
#include "PatternManager.h"
#include "WaveformCache.h"
#include "MeterSnapshot.h"
#include "ChannelSlotMap.h"
#include "NUIContextMenu.h"
#include "Commands/MakeClipPatternUniqueCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/TrimClipCommand.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Graphics/NUISVGParser.h"
#include "../AestraUI/Widgets/TrackControlIcons.h"
#include "../AestraUI/Helpers/TimelineGridRenderer.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Widgets/TrackColorPalette.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <chrono>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace Aestra {
namespace Audio {

namespace {

AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowContextMenu(AestraUI::NUIComponent* owner,
                              const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

// Track control glyphs (mute/solo/record/monitor). Parsed once, rasterized
// and cached per size+tint by NUISVGRenderer, so per-frame cost is a texture
// draw. Geometry is authored on a 24x24 grid like the toolbar icons.
const AestraUI::NUISVGDocument* trackControlIcon(const char* svg) {
    static std::unordered_map<const char*, std::shared_ptr<AestraUI::NUISVGDocument>> docs;
    auto& doc = docs[svg];
    if (!doc) doc = AestraUI::NUISVGParser::parse(svg);
    return doc.get();
}

using AestraUI::kMonitorIconSvg;
using AestraUI::kMuteIconSvg;
using AestraUI::kRecordIconSvg;
using AestraUI::kSoloIconSvg;
using AestraUI::kLaneStackIconSvg;
using AestraUI::kChevronUpSvg;
using AestraUI::kChevronDownSvg;

bool parseTrailingTrackNumber(const std::string& trackName, uint32_t& trackNumberOut) {
    const size_t numberPos = trackName.find_last_not_of("0123456789");
    if (numberPos == std::string::npos || numberPos >= trackName.length() - 1) return false;
    const std::string_view numberStr(trackName.c_str() + numberPos + 1, trackName.length() - numberPos - 1);
    uint32_t parsed = 0;
    const char* begin = numberStr.data();
    const char* end = begin + numberStr.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) return false;
    trackNumberOut = parsed;
    return true;
}

std::string truncateClipLabel(const std::string& text, float availableWidth, float charWidth, size_t minChars = 4) {
    if (text.empty() || availableWidth <= 0.0f || charWidth <= 0.0f) return {};
    const size_t maxChars = static_cast<size_t>(availableWidth / charWidth);
    if (maxChars < minChars) return {};
    if (text.length() <= maxChars) return text;
    if (maxChars <= 2) return {};
    return text.substr(0, maxChars - 2) + "..";
}

TrackSelectionIntent selectionIntentFor(const AestraUI::NUIMouseEvent& event) {
    const bool toggleModifier =
        (event.modifiers & AestraUI::NUIModifiers::Ctrl) || (event.modifiers & AestraUI::NUIModifiers::Super);
    return trackSelectionIntentForModifierState(toggleModifier, event.modifiers & AestraUI::NUIModifiers::Shift);
}

// Clip and lane tones all come from AestraUI/Widgets/TrackColorPalette.h now
// (clipBodyTone / waveformTintTone / restrainLaneIdentityColor), so nothing here
// open-codes a restraint the minimap cannot see.

// Keep Playlist clip selection in the same visual language as Piano Roll
// notes: a lifted secondary accent, crisp white edge, grounded shadow, and
// subtle edit handles. Clip identity colour remains visible underneath.
void drawPianoRollStyleSelection(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float radius) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto selectedColor = themeManager.getColor("accentSecondary").lightened(0.12f);
    renderer.drawShadow({bounds.x, bounds.y + 1.0f, bounds.width, bounds.height},
                        0.0f, 2.0f, 5.0f, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.22f));
    renderer.fillRoundedRect(bounds, radius, selectedColor.withAlpha(0.12f));
    renderer.strokeRoundedRect(bounds, radius, 1.5f, AestraUI::NUIColor::white().withAlpha(0.78f));
    const float handleHeight = std::max(2.0f, bounds.height - 6.0f);
    renderer.fillRoundedRect({bounds.x + 1.0f, bounds.y + 3.0f, 2.0f, handleHeight},
                             1.0f, AestraUI::NUIColor::white().withAlpha(0.68f));
    renderer.fillRoundedRect({bounds.right() - 3.0f, bounds.y + 3.0f, 2.0f, handleHeight},
                             1.0f, AestraUI::NUIColor::white().withAlpha(0.68f));
}

// Waveform ink derived from the clip color so the waveform reads as part of
// the clip rather than a white overlay: a deep shade of the clip hue on
// bright clips, lifted toward white on dark ones.
struct WaveformInk {
    AestraUI::NUIColor rms;
    AestraUI::NUIColor envTop;
    AestraUI::NUIColor envBottom;
    AestraUI::NUIColor centerLine;
};

WaveformInk deriveWaveformInk(const AestraUI::NUIColor& base) {
    // Bold, near-solid waveform so it reads as a clear waveform shape (not faint
    // texture) against the clip fill: a bright lift of the clip hue at high alpha,
    // with the min/max envelope nearly as opaque as the RMS body. The lift amount
    // is shared with clipBodyTone()'s counterpart so the contrast contract has one
    // authority (see TrackColorPalette.h).
    const AestraUI::NUIColor bright = AestraUI::liftWaveformInk(base);
    WaveformInk ink;
    ink.rms = bright.withAlpha(0.97f);
    ink.envTop = bright.withAlpha(0.90f);
    ink.envBottom = bright.withAlpha(0.90f);
    ink.centerLine = bright.withAlpha(0.22f);
    return ink;
}

} // namespace

// =============================================================================
// SECTION: Construction & Destruction
// =============================================================================

TrackUIComponent::TrackUIComponent(PlaylistLaneID laneId, std::shared_ptr<MixerChannel> channel, TrackManager* trackManager)
    : m_laneId(laneId)
    , m_channel(channel)
    , m_trackManager(trackManager)
{
    // Log::info("TrackUIComponent ctor: " + m_laneId.toString());
    // Create track name label
    m_nameLabel = std::make_shared<AestraUI::NUILabel>();
    
    std::string name = "Track";
    if (m_trackManager) {
        if (auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
            name = lane->name;
        }
    }
    m_nameLabel->setText(name.empty() ? "Track" : name);

    {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        // Use large font for track names
        m_nameLabel->setFontSize(themeManager.getFontSize("l"));
    }
    m_nameLabel->setEllipsize(true);
    updateTrackNameColors();
    addChild(m_nameLabel);

    // FD-14 scope §10 (lane visibility): the track's primary row carries a
    // lane-stack icon + count so a multi-lane track is discoverable at a glance.
    m_laneCountIcon = std::make_shared<AestraUI::NUIIcon>();
    m_laneCountIcon->loadSVG(kLaneStackIconSvg);
    m_laneCountIcon->setIconSize(13.0f, 13.0f);
    m_laneCountIcon->setVisible(false);
    addChild(m_laneCountIcon);

    m_laneCountLabel = std::make_shared<AestraUI::NUILabel>();
    m_laneCountLabel->setFontSize(11.0f);
    m_laneCountLabel->setEllipsize(false);
    m_laneCountLabel->setVisible(false);
    addChild(m_laneCountLabel);

    auto configureFlatTrackButton = [](const std::shared_ptr<AestraUI::NUIButton>& button) {
        button->setStyle(AestraUI::NUIButton::Style::Text);
        button->setBackgroundColor(AestraUI::NUIColor::transparent());
        button->setHoverColor(AestraUI::NUIColor::transparent());
        button->setPressedColor(AestraUI::NUIColor::transparent());
        button->setBorderEnabled(false);
        button->setGlowEnabled(false);
        button->setTextColor(AestraUI::NUIColor::white().withAlpha(0.48f));
        button->setFontSize(11.0f);
        button->setCornerRadius(0.0f);
    };

    // Create mute button (glyph drawn by drawControlIcon; no text)
    m_muteButton = std::make_shared<AestraUI::NUIButton>();
    m_muteButton->setText("");
    configureFlatTrackButton(m_muteButton);
    m_muteButton->setToggleable(true);
    m_muteButton->setOnToggle([this](bool) { onMuteToggled(); });
    m_muteButton->setTooltip("Mute Track (M)");
    addChild(m_muteButton);

    // Create solo button
    m_soloButton = std::make_shared<AestraUI::NUIButton>();
    m_soloButton->setText("");
    configureFlatTrackButton(m_soloButton);
    m_soloButton->setToggleable(true);
    m_soloButton->setOnToggle([this](bool) { onSoloToggled(); });
    m_soloButton->setTooltip("Solo Track (S)");
    addChild(m_soloButton);

    // FD-14 #6: record arm lives on the Track (setTrackArmed). The button is
    // the track's arm control for every owned lane — it must not depend on a
    // mixer channel being passed in, because the playlist builds components
    // without one (TrackManagerUI.cpp). Input monitoring stays on the
    // right-click menu, gated on a resolvable channel.
    m_recordButton = std::make_shared<AestraUI::NUIButton>();
    m_recordButton->setText("");
    configureFlatTrackButton(m_recordButton);
    m_recordButton->setToggleable(true);
    m_recordButton->setOnToggle([this](bool) { onRecordToggled(); });
    addChild(m_recordButton);

    // FD-14 §10 expansion toggle: hit target only, glyph drawn in the control
    // overlay. Visible on the track's primary row when it owns multiple lanes.
    m_expandButton = std::make_shared<AestraUI::NUIButton>();
    m_expandButton->setVisible(false);
    m_expandButton->setOnClick([this]() {
        if (m_onExpandToggled) {
            m_onExpandToggled();
        }
    });
    addChild(m_expandButton);

    updateUI();
}


TrackUIComponent::~TrackUIComponent() {
    // Torn down mid-drag: cancel the capture so the bridge never routes to a
    // dangling owner and the cursor is never stranded hidden.
    if (m_platformBridge && m_platformBridge->isCursorCaptureOwner(this)) {
        m_platformBridge->cancelCursorCapture();
    }
    detachContextMenu(m_recordModeMenu);
    detachContextMenu(m_clipRoutingMenu);
    Log::debug("TrackUIComponent destroyed for lane: " + m_laneId.toString());
}

void TrackUIComponent::showClipRoutingMenu(const ClipInstanceID& clipId, const AestraUI::NUIPoint& position) {
    detachContextMenu(m_clipRoutingMenu);
    if (!m_trackManager) {
        return;
    }

    const auto* clip = m_trackManager->getPlaylistModel().getClip(clipId);
    auto* pattern = clip ? m_trackManager->getPatternManager().getPattern(clip->patternId) : nullptr;
    m_clipRoutingMenu = std::make_shared<AestraUI::NUIContextMenu>();
    m_clipRoutingMenu->setOnHide([this]() { detachContextMenu(m_clipRoutingMenu); });

    auto addAction = [this](const std::string& label, const std::string& shortcut, std::function<void()> action) {
        auto item = std::make_shared<AestraUI::NUIContextMenuItem>(label);
        item->setShortcut(shortcut);
        item->setOnClick(std::move(action));
        m_clipRoutingMenu->addItem(item);
    };

    if (pattern && pattern->isMidi() && m_onPatternClipOpenRequested) {
        addAction("Open in Piano Roll", "Enter", [this, patternId = pattern->id]() {
            m_onPatternClipOpenRequested(patternId);
        });
    } else if (pattern && pattern->isAudio() && m_onAudioClipOpenRequested) {
        addAction("Open in Audio Editor", "Enter", [this, clipId]() { m_onAudioClipOpenRequested(clipId); });
    }

    if (auto* parentManager = dynamic_cast<TrackManagerUI*>(getParent())) {
        if (m_clipRoutingMenu->getItemCount() > 0) {
            m_clipRoutingMenu->addSeparator();
        }
        addAction("Cut", "Ctrl+X", [parentManager]() { parentManager->cutSelectedClip(); });
        addAction("Copy", "Ctrl+C", [parentManager]() { parentManager->copySelectedClip(); });
        addAction("Duplicate", "Ctrl+D", [parentManager]() { parentManager->duplicateSelectedClip(); });
        if (pattern && m_trackManager) {
            // Break shared pattern identity for this clip only (Extra Session
            // 2026-08-21: duplicate → make unique workflow).
            addAction("Make Unique", "", [this, clipId]() {
                if (!m_trackManager)
                    return;
                auto cmd = std::make_shared<Aestra::Audio::MakeClipPatternUniqueCommand>(*m_trackManager, clipId);
                m_trackManager->getCommandHistory().pushAndExecute(cmd);
                repaint();
                if (m_onCacheInvalidationCallback) {
                    m_onCacheInvalidationCallback();
                }
            });
        }
    }

    if (pattern && pattern->isAudio()) {
        const uint32_t selectedId = pattern->getMixerChannelId();
        auto routingMenu = std::make_shared<AestraUI::NUIContextMenu>();
        const auto addDestination = [this, routingMenu, patternId = pattern->id, selectedId](uint32_t channelId,
                                                                                            const std::string& label) {
            routingMenu->addRadioItem(label, "ClipSourceRouting", selectedId == channelId,
                                      [this, patternId, channelId]() {
                if (m_trackManager->setAudioPatternMixerChannel(patternId, channelId)) {
                    repaint();
                    if (m_onCacheInvalidationCallback) {
                        m_onCacheInvalidationCallback();
                    }
                }
            });
        };

        m_clipRoutingMenu->addSeparator();
        addDestination(0, "Master");
        for (size_t i = 0; i < m_trackManager->getChannelCount(); ++i) {
            if (const auto* channel = m_trackManager->getChannel(i)) {
                addDestination(channel->getChannelId(), std::to_string(i + 1) + "  " + channel->getName());
            }
        }
        m_clipRoutingMenu->addSubmenu("Route Linked Clips", routingMenu);
    }

    m_clipRoutingMenu->addSeparator();
    auto deleteItem = std::make_shared<AestraUI::NUIContextMenuItem>("Delete Clip");
    deleteItem->setShortcut("Delete");
    deleteItem->setOnClick([this, clipId, position]() {
        if (m_onClipDeletedCallback) {
            m_onClipDeletedCallback(this, clipId, position);
        }
    });
    m_clipRoutingMenu->addItem(deleteItem);
    attachAndShowContextMenu(this, m_clipRoutingMenu, position);
}

double TrackUIComponent::getSnapGridSizeBeats() const {
    // Delegate to MusicTheory which handles all snap types including Triplet
    return AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
}

double TrackUIComponent::snapBeatToGrid(double beat) const {
    double gridSize = getSnapGridSizeBeats();
    if (gridSize <= 0.0) return beat; // No snap
    return std::round(beat / gridSize) * gridSize;
}

// =============================================================================
// SECTION: UI Callbacks
// =============================================================================

void TrackUIComponent::onVolumeChanged(float volume) {
    if (m_channel && m_trackManager) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetVolumeCommand>(*m_trackManager, *m_channel, volume));
        Log::info("Lane " + m_laneId.toString() + " volume: " + std::to_string(volume));
    }
}

void TrackUIComponent::onPanChanged(float pan) {
    if (m_channel && m_trackManager) {
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<SetPanCommand>(*m_trackManager, *m_channel, pan));
        Log::info("Lane " + m_laneId.toString() + " pan: " + std::to_string(pan));
    }
}


void TrackUIComponent::onMuteToggled() {
    if (m_trackManager) {
        bool isMuted = m_muteButton->isToggled();
        if (auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
            lane->muted = isMuted;
            m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
            m_trackManager->markModified();
        }

        Log::info("Lane " + m_laneId.toString() + " muted: " + (isMuted ? "ON" : "OFF"));
        updateUI();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
    }
}


void TrackUIComponent::onSoloToggled() {
    if (m_trackManager) {
        bool newSolo = m_soloButton->isToggled(); // Use button state
        if (auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
            lane->solo = newSolo;
            m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
            m_trackManager->markModified();
        }

        if (m_onSoloToggledCallback) {
            m_onSoloToggledCallback(this);
        }

        updateUI();
        repaint();
        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
        Log::info("Lane " + m_laneId.toString() + " solo: " + (newSolo ? "ON" : "OFF"));
    }
}


void TrackUIComponent::onRecordToggled() {
    if (!m_trackManager) return;
    auto* track = m_trackManager->getTrackForLane(m_laneId);
    if (!track) return;
    const bool armed = m_recordButton && m_recordButton->isToggled();
    m_trackManager->setTrackArmed(track->trackId, armed);
    m_trackManager->markModified();
    Log::info("Track " + std::to_string(track->trackId) + " armed: " + (armed ? "ON" : "OFF"));
    updateUI();
    repaint();
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

void TrackUIComponent::showRecordModeMenu(const AestraUI::NUIPoint& position) {
    auto* channel = resolveMonitorChannel();
    if (!channel) {
        return;
    }

    detachContextMenu(m_recordModeMenu);
    m_recordModeMenu = std::make_shared<AestraUI::NUIContextMenu>();
    m_recordModeMenu->setOnHide([this]() { detachContextMenu(m_recordModeMenu); });
    m_recordModeMenu->addRadioItem("Input Monitoring: Off", "record_mode", !channel->isMonitoringEnabled(), [this, channel]() {
        channel->setMonitoringEnabled(false);
        if (m_trackManager) {
            m_trackManager->publishInputMonitoringSnapshot();
            m_trackManager->markModified();
        }
        updateUI();
        repaint();
    });
    m_recordModeMenu->addRadioItem("Input Monitoring: On", "record_mode", channel->isMonitoringEnabled(), [this, channel]() {
        channel->setMonitoringEnabled(true);
        if (m_trackManager) {
            m_trackManager->publishInputMonitoringSnapshot();
            m_trackManager->markModified();
        }
        updateUI();
        repaint();
    });
    attachAndShowContextMenu(this, m_recordModeMenu, position);
}

MixerChannel* TrackUIComponent::resolveMonitorChannel() const {
    if (m_channel) {
        return m_channel.get();
    }
    if (!m_trackManager) {
        return nullptr;
    }
    auto* track = m_trackManager->getTrackForLane(m_laneId);
    if (!track) {
        return nullptr;
    }
    return m_trackManager->getChannelById(static_cast<uint32_t>(track->channelId));
}

std::string TrackUIComponent::recordButtonTooltipText() const {
    if (auto* channel = resolveMonitorChannel()) {
        const char* monitorText = channel->isMonitoringEnabled() ? "On" : "Off";
        return std::string("Arm for Recording (O) • Right-click: Input Monitoring: ") + monitorText;
    }
    return "Arm for Recording (O)";
}

void TrackUIComponent::updateRecordTooltip() {
    if (!m_recordButton) {
        return;
    }
    m_recordButton->setTooltip(recordButtonTooltipText());
}


void TrackUIComponent::updateUI() {
    // Invalidate parent cache since button colors are changing
    if (m_onCacheInvalidationCallback) {
        m_onCacheInvalidationCallback();
    }

    // Update track name colors with bright colors based on number
    updateTrackNameColors();

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    const AestraUI::NUIColor inactiveBg = AestraUI::NUIColor::transparent();
    const AestraUI::NUIColor inactiveHover = themeManager.getColor("controlHover");
    const AestraUI::NUIColor inactiveText = themeManager.getColor("textSecondary");
    const AestraUI::NUIColor activeText = themeManager.getColor("textPrimary").withAlpha(0.96f);
    const auto configureStatusButton = [&](const auto& button, bool active,
                                           const AestraUI::NUIColor& statusColor) {
        if (!button) return;
        button->setToggled(active);
        button->setGlowEnabled(false);
        button->setBackgroundColor(active ? statusColor.withAlpha(0.14f) : inactiveBg);
        button->setHoverColor(active ? statusColor.withAlpha(0.20f) : inactiveHover);
        button->setTextColor(active ? activeText : inactiveText);
        button->setBorderEnabled(active);
        if (active) button->setBorderColor(statusColor.withAlpha(0.48f));
    };

    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
    const auto* track = lane ? m_trackManager->getTrack(lane->trackId) : nullptr;
    configureStatusButton(m_muteButton, lane && lane->muted, themeManager.getColor("muted"));
    configureStatusButton(m_soloButton, lane && lane->solo, themeManager.getColor("soloed"));
    configureStatusButton(m_recordButton, track && track->armed, themeManager.getColor("armed"));

    if (m_recordButton) {
        updateRecordTooltip();
    }

    // FD-14 §10 nesting: record arm is track-exclusive, so nested lane rows
    // carry M/S only; the expansion chevron lives on the primary row of a
    // multi-lane track.
    if (m_recordButton) {
        m_recordButton->setVisible(!m_isNestedLane);
    }

    if (m_laneCountLabel && m_laneCountIcon) {
        const bool isPrimaryRow = track && !track->laneIds.empty() && track->laneIds.front() == m_laneId;
        const bool multiLaneTrack = track && track->laneIds.size() > 1;
        if (m_expandButton) {
            m_expandButton->setVisible(isPrimaryRow && multiLaneTrack);
        }
        if (isPrimaryRow && multiLaneTrack) {
            // FD-14 §10: the counter counts the lanes NESTED under the primary
            // row — what the stack glyph depicts and what the chevron reveals.
            // A 3-take track reads "≡ 3", not "≡ 4" (laneIds includes the
            // primary row itself, which is never "inside" the chevron).
            m_laneCountLabel->setText(std::to_string(track->laneIds.size() - 1));
            m_laneCountLabel->setTextColor(themeManager.getColor("textSecondary").withAlpha(0.72f));
            m_laneCountLabel->setVisible(true);
            m_laneCountIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.72f));
            m_laneCountIcon->setVisible(true);
        } else {
            m_laneCountLabel->setVisible(false);
            m_laneCountIcon->setVisible(false);
        }
    }

}


void TrackUIComponent::updateTrackNameColors() {
    if (!m_nameLabel || !m_trackManager) return;
    if (const auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
        (void)lane;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        m_nameLabel->setTextColor(theme.getColor("textPrimary").withAlpha(m_selected ? 0.92f : 0.72f));
    }
}

void TrackUIComponent::generateWaveformCache(int, int) {
    // Waveform caching for entire track is deprecated in v3.0 (clips have their own caching)
}

// =============================================================================
// SECTION: Waveform & Clip Drawing
// =============================================================================

// Draw waveform for a specific clip using zoom-aware LOD peaks.
void TrackUIComponent::drawWaveformForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                          const ClipInstance& clip, float offsetRatio, float visibleRatio) {
    if (!m_trackManager) return;

    auto& patternMgr = m_trackManager->getPatternManager();
    auto& sourceMgr = m_trackManager->getSourceManager();

    auto pattern = patternMgr.getPattern(clip.patternId);
    if (!pattern || !pattern->isAudio()) return;

    auto audioPayload = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!audioPayload) return;

    auto source = sourceMgr.getSource(audioPayload->audioSourceId);
    if (!source || !source->isReady()) {
        return;
    }

    auto bufferPtr = source->getBuffer();
    if (!bufferPtr || bufferPtr->numFrames == 0) {
        return;
    }

    const auto& audioData = *bufferPtr;

    float width = bounds.width;
    float height = bounds.height;
    if (width <= 0.0f || height <= 0.0f) return;

    size_t numChannels = audioData.numChannels;
    size_t totalFrames = audioData.numFrames;

    double sampleRate = source->getSampleRate();
    double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
    double secondsPerBeat = 60.0 / bpm;
    double clipDurationSeconds = clip.durationBeats * secondsPerBeat;
    const double timelineFrames = std::max(1.0, clipDurationSeconds * sampleRate);

    // Use the same project-rate source start as the runtime snapshot. This is
    // what keeps a split/trim waveform on the same source region as playback,
    // including legacy beat-domain clips and instance-level slips.
    const auto& playlist = m_trackManager->getPlaylistModel();
    const double projectSampleRate = playlist.getProjectSampleRate();
    if (!std::isfinite(projectSampleRate) || projectSampleRate <= 0.0) return;
    const double scaledSourceOffset =
        static_cast<double>(playlist.getClipSourceStartSamples(clip)) * (sampleRate / projectSampleRate);
    if (!std::isfinite(scaledSourceOffset) || scaledSourceOffset < 0.0 ||
        scaledSourceOffset >= static_cast<double>(totalFrames)) {
        return;
    }
    // Pitch and speed share the varispeed envelope, matching the render path
    // (ClipRenderKernel) so the preview consumes source frames like the audio.
    const double varispeed = static_cast<double>(clip.edits.effectiveVarispeed());

    const double visibleOutputStart = static_cast<double>(offsetRatio) * timelineFrames;
    const double visibleOutputEnd = static_cast<double>(offsetRatio + visibleRatio) * timelineFrames;
    const double exactStart = scaledSourceOffset + visibleOutputStart * varispeed;
    const double exactEnd = scaledSourceOffset + visibleOutputEnd * varispeed;
    const double sourceStart = std::clamp(exactStart, 0.0, static_cast<double>(totalFrames));
    const double sourceEnd = std::clamp(exactEnd, 0.0, static_cast<double>(totalFrames));
    if (sourceStart >= sourceEnd) return;

    // The clip rectangle stays fixed on the timeline while varispeed changes
    // how quickly source frames are consumed. If the source ends before this
    // visible timeline interval, draw only the audible fraction and leave the
    // remainder empty instead of stretching the old waveform across silence.
    AestraUI::NUIRect audibleBounds = bounds;
    const double visibleOutputFrames = visibleOutputEnd - visibleOutputStart;
    const double availableOutputEnd =
        (static_cast<double>(totalFrames) - scaledSourceOffset) / varispeed;
    if (visibleOutputFrames > 0.0 && availableOutputEnd < visibleOutputEnd) {
        const double audibleOutputFrames = std::max(0.0, availableOutputEnd - visibleOutputStart);
        audibleBounds.width *= static_cast<float>(std::clamp(audibleOutputFrames / visibleOutputFrames, 0.0, 1.0));
    }
    if (audibleBounds.width <= 0.0f)
        return;

    // Waveform ink base is the clip hue at full brightness; deriveWaveformInk()
    // lifts it bright + near-opaque so the wave reads boldly over the fill.
    const bool clipSelected = (clip.id == m_selectedClipId);
    const AestraUI::NUIColor clipTint = AestraUI::waveformTintTone(resolveClipDisplayColor(clip), clipSelected);

    // Keep source-frame coordinates fractional until bins are formed. This makes
    // the deep and cached paths use identical pixel geometry at every zoom level.
    const double samplesPerPixel = (sourceEnd - sourceStart) / static_cast<double>(audibleBounds.width);

    // One peak column per pixel: filled-strip rendering needs full density,
    // and the mip cache makes per-pixel queries cheap at any zoom
    const int numBars = std::max(1, static_cast<int>(audibleBounds.width));

    // Reusable member buffers avoid per-frame allocations
    m_waveformPeaksL.clear();
    m_waveformPeaksR.clear();

    if (samplesPerPixel < static_cast<double>(Aestra::Audio::WaveformCache::DEFAULT_BASE_SAMPLES_PER_PEAK)) {
        // Zoomed past the finest mip level: compute peaks directly from the
        // buffer. Bounded work — at most base-mip samples per visible pixel.
        computeDirectPeaks(audioData, 0, sourceStart, sourceEnd, numBars, m_waveformPeaksL);
        if (numChannels > 1) {
            computeDirectPeaks(audioData, 1, sourceStart, sourceEnd, numBars, m_waveformPeaksR);
        }
    } else {
        // Normal zoom: precomputed mip peaks only; no per-render scanning
        auto waveformCache = source->getWaveformCache();
        if (!waveformCache || !waveformCache->isReady()) {
            // Fallback: faint center line
            float centerY = audibleBounds.y + height * 0.5f;
            renderer.drawLine(AestraUI::NUIPoint(audibleBounds.x, centerY),
                              AestraUI::NUIPoint(audibleBounds.x + audibleBounds.width, centerY), 1.0f,
                              deriveWaveformInk(clipTint).centerLine);
            return;
        }

        waveformCache->getPeaksForRangePrecise(0, sourceStart, sourceEnd, numBars, m_waveformPeaksL);
        if (numChannels > 1) {
            waveformCache->getPeaksForRangePrecise(1, sourceStart, sourceEnd, numBars, m_waveformPeaksR);
        }
    }

    // Always one combined waveform. Split L/R lanes read as a thin "doubled"
    // texture at clip heights; a single filled waveform is clearer. The
    // deep-zoom path above combines too, so layout never jumps across the LOD
    // threshold.
    drawCombinedWaveform(renderer, audibleBounds, m_waveformPeaksL, m_waveformPeaksR, numChannels, clipTint);
}

void TrackUIComponent::drawChannelWaveform(AestraUI::NUIRenderer& renderer, float x, float y, float w, float h,
                                            const std::vector<Aestra::Audio::WaveformPeak>& peaks,
                                            const AestraUI::NUIColor& tint) {
    if (peaks.empty() || w <= 0.0f || h <= 0.0f) return;

    const float centerY = y + h * 0.5f;
    const float halfDrawH = std::max(1.0f, h * 0.5f - 2.0f);
    const int numPoints = static_cast<int>(peaks.size());

    // Ink follows the clip color. Outer min/max envelope is translucent;
    // inner RMS body is near-opaque — the two layers together give the dense
    // "pro DAW" waveform read.
    const WaveformInk ink = deriveWaveformInk(tint);
    const AestraUI::NUIColor& envTopColor = ink.envTop;
    const AestraUI::NUIColor& envBottomColor = ink.envBottom;
    const AestraUI::NUIColor& rmsColor = ink.rms;
    const AestraUI::NUIColor& centerLineColor = ink.centerLine;

    // A strip needs two columns; degenerate spans draw a single bar
    if (numPoints < 2) {
        const auto& peak = peaks[0];
        float normMin = std::max(-1.0f, std::min(1.0f, peak.min));
        float normMax = std::max(-1.0f, std::min(1.0f, peak.max));
        float topY = centerY - normMax * halfDrawH;
        float bottomY = centerY - normMin * halfDrawH;
        renderer.fillRect(AestraUI::NUIRect(x, topY, std::max(1.0f, w), std::max(1.0f, bottomY - topY)),
                          envTopColor);
        return;
    }

    const float step = w / static_cast<float>(numPoints);

    // Layer 1: min/max envelope as a filled strip
    m_waveformTopPts.clear();
    m_waveformBottomPts.clear();
    m_waveformTopPts.reserve(static_cast<size_t>(numPoints));
    m_waveformBottomPts.reserve(static_cast<size_t>(numPoints));

    // Display gain so moderate-level audio fills the clip height rather than a
    // thin band; clamped so it never overshoots the lane.
    constexpr float kWaveDisplayGain = 1.45f;
    for (int i = 0; i < numPoints; ++i) {
        const auto& peak = peaks[i];
        float normMin = std::max(-1.0f, std::min(1.0f, peak.min * kWaveDisplayGain));
        float normMax = std::max(-1.0f, std::min(1.0f, peak.max * kWaveDisplayGain));

        float topY = centerY - normMax * halfDrawH;
        float bottomY = centerY - normMin * halfDrawH;

        // Minimum nonzero visual height for quiet audio; true silence stays at center
        float barHeight = bottomY - topY;
        if (barHeight > 0.0f && barHeight < 1.0f) {
            float mid = (topY + bottomY) * 0.5f;
            topY = mid - 0.5f;
            bottomY = mid + 0.5f;
        }

        float px = x + (static_cast<float>(i) + 0.5f) * step;
        m_waveformTopPts.emplace_back(px, topY);
        m_waveformBottomPts.emplace_back(px, bottomY);
    }

    renderer.fillWaveformGradient(m_waveformTopPts.data(), m_waveformBottomPts.data(), numPoints, envTopColor,
                                  envBottomColor);

    // Layer 2: RMS body, clamped inside the envelope (asymmetric signals can
    // have symmetric ±rms poke past the true min/max edge). Reuses the same
    // point buffers in place: envelope Y is read before being overwritten.
    for (int i = 0; i < numPoints; ++i) {
        float rms = std::max(0.0f, std::min(1.0f, peaks[i].rms * kWaveDisplayGain));
        float rmsTopY = std::max(m_waveformTopPts[i].y, centerY - rms * halfDrawH);
        float rmsBottomY = std::min(m_waveformBottomPts[i].y, centerY + rms * halfDrawH);
        if (rmsBottomY < rmsTopY) rmsBottomY = rmsTopY;
        m_waveformTopPts[i].y = rmsTopY;
        m_waveformBottomPts[i].y = rmsBottomY;
    }

    renderer.fillWaveform(m_waveformTopPts.data(), m_waveformBottomPts.data(), numPoints, rmsColor);

    renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x + w, centerY), 1.0f, centerLineColor);
}

void TrackUIComponent::drawCombinedWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                                             const std::vector<Aestra::Audio::WaveformPeak>& peaksL,
                                             const std::vector<Aestra::Audio::WaveformPeak>& peaksR,
                                             size_t numChannels, const AestraUI::NUIColor& tint) {
    if (peaksL.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    if (numChannels > 1 && !peaksR.empty()) {
        // Merge channels per column (weighted-RMS merge), then render as one lane
        m_waveformPeaksMerged = peaksL;
        const size_t n = std::min(m_waveformPeaksMerged.size(), peaksR.size());
        for (size_t i = 0; i < n; ++i) {
            m_waveformPeaksMerged[i].merge(peaksR[i]);
        }
        drawChannelWaveform(renderer, bounds.x, bounds.y, bounds.width, bounds.height, m_waveformPeaksMerged, tint);
    } else {
        drawChannelWaveform(renderer, bounds.x, bounds.y, bounds.width, bounds.height, peaksL, tint);
    }
}

void TrackUIComponent::computeDirectPeaks(const Aestra::Audio::AudioBufferData& buffer, uint32_t channel,
                                          double startFrame, double endFrame, int numColumns,
                                          std::vector<Aestra::Audio::WaveformPeak>& outPeaks) {
    outPeaks.clear();
    if (numColumns <= 0 || buffer.numChannels == 0 || buffer.numFrames == 0) return;

    if (!std::isfinite(startFrame) || !std::isfinite(endFrame)) return;
    startFrame = std::clamp(startFrame, 0.0, static_cast<double>(buffer.numFrames));
    endFrame = std::clamp(endFrame, 0.0, static_cast<double>(buffer.numFrames));
    if (startFrame >= endFrame) return;
    channel = std::min(channel, buffer.numChannels - 1);

    const float* data = buffer.interleavedData.data();
    const size_t stride = buffer.numChannels;
    const double framesPerColumn = (endFrame - startFrame) / static_cast<double>(numColumns);
    const auto readSample = [data, stride, channel](size_t frame) {
        const float sample = data[frame * stride + channel];
        return std::isfinite(sample) ? sample : 0.0f;
    };

    outPeaks.reserve(static_cast<size_t>(numColumns));
    for (int col = 0; col < numColumns; ++col) {
        const double pixelStart = startFrame + static_cast<double>(col) * framesPerColumn;
        const double pixelEnd = startFrame + static_cast<double>(col + 1) * framesPerColumn;
        size_t f0 = static_cast<size_t>(std::floor(pixelStart));
        size_t f1 = static_cast<size_t>(std::ceil(pixelEnd));
        f0 = std::min(f0, static_cast<size_t>(buffer.numFrames) - 1);
        f1 = std::max(std::min(f1, static_cast<size_t>(buffer.numFrames)), f0 + 1);

        float minVal = readSample(f0);
        float maxVal = minVal;
        double sumSq = 0.0;
        for (size_t f = f0; f < f1; ++f) {
            const float s = readSample(f);
            minVal = std::min(minVal, s);
            maxVal = std::max(maxVal, s);
            sumSq += static_cast<double>(s) * s;
        }

        const uint32_t count = static_cast<uint32_t>(
            std::min<size_t>(f1 - f0, std::numeric_limits<uint32_t>::max()));
        const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)));
        outPeaks.emplace_back(minVal, maxVal, rms, count);
        outPeaks.back().sanitize();
    }
}

AestraUI::NUIColor TrackUIComponent::resolveClipDisplayColor(const ClipInstance& clip) const {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    AestraUI::NUIColor clipColor = themeManager.getColor("primary");

    if (!m_trackManager) return clipColor;
    auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
    if (!pattern) return clipColor;

    // Audio-source color follows its mixer destination without coupling the
    // Playlist lane itself to that insert.
    auto routeChannel = pattern->isAudio() ? m_trackManager->getChannelById(pattern->getMixerChannelId()) : nullptr;
    if (routeChannel) {
        int colorIndex = routeChannel->getTrackColorIndex();
        if (colorIndex < 0 || colorIndex >= AestraUI::PALETTE_SIZE) {
            uint32_t trackId = routeChannel->getChannelId();
            colorIndex = static_cast<int>((trackId - 1) % AestraUI::PALETTE_SIZE);
        }
        clipColor = AestraUI::NUIColor::fromARGB(AestraUI::paletteIndexToARGB(colorIndex));
    } else {
        clipColor = AestraUI::NUIColor::fromARGB(clip.colorRGBA);
    }
    return clipColor;
}

void TrackUIComponent::drawSampleClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                            const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip,
                                            bool seamLeft, bool seamRight) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    const float clipRadius = themeManager.getRadius("s");

    AestraUI::NUIColor clipColor = resolveClipDisplayColor(clip);
    std::string sampleName = "Clip";
    int patternRefCount = 1;  // How many clips share this pattern
    int patternInstanceIndex = 1;  // This clip's instance number

    if (m_trackManager) {
        if (auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId)) {
            sampleName = pattern->name;

            // Count how many clips reference this pattern across all lanes
            auto& playlist = m_trackManager->getPlaylistModel();
            int count = 0;
            int indexForThisClip = 0;
            for (size_t l = 0; l < playlist.getLaneCount(); ++l) {
                if (auto lane = playlist.getLane(playlist.getLaneId(l))) {
                    for (const auto& c : lane->clips) {
                        if (c.patternId == clip.patternId) {
                            ++count;
                            if (c.id == clip.id) {
                                indexForThisClip = count;
                            }
                        }
                    }
                }
            }
            patternRefCount = count;
            patternInstanceIndex = indexForThisClip;
        }
    }
    
    bool clipSelected = (clip.id == m_selectedClipId);
    // Selection eases the base look up slightly; the border and glow carry
    // the state so the fill doesn't visibly "pop" on click-and-hold
    // Deeper, less-saturated base so the clip reads rich rather than neon.
    // Body sits lower than the waveform on purpose. Both tones are derived
    // together in TrackColorPalette.h so the gap between them cannot drift: the
    // clip stops reading as a bright slab and starts reading as a surface with
    // audio drawn on it.
    const AestraUI::NUIColor clipBase = AestraUI::clipBodyTone(clipColor, clipSelected);
    AestraUI::NUIColor tintFill = clipBase.withAlpha(clipSelected ? 0.88f : 0.80f);
    // Opaque deep base so the timeline grid doesn't bleed through the clip body.
    renderer.fillRoundedRect(clipBounds, clipRadius, themeManager.getColor("backgroundPrimary"));
    renderer.fillRoundedRect(clipBounds, clipRadius, tintFill);

    // Adjacent slices share a timeline edge, but rounded corners and antialiasing
    // can expose the grid between their independently rendered bodies. Square the
    // internal sides and overlap by one pixel so a visual split stays gapless.
    constexpr float kSeamOverlap = 1.0f;
    const float seamFillWidth = clipRadius + kSeamOverlap;
    if (seamLeft) {
        renderer.fillRect({clipBounds.x - kSeamOverlap, clipBounds.y,
                           seamFillWidth + kSeamOverlap, clipBounds.height}, tintFill);
    }
    if (seamRight) {
        renderer.fillRect({clipBounds.right() - seamFillWidth, clipBounds.y,
                           seamFillWidth + kSeamOverlap, clipBounds.height}, tintFill);
    }

    AestraUI::NUIColor borderColor = clipBase.lightened(0.10f).withAlpha(clipSelected ? 0.94f : 0.58f);
    float borderWidth = 1.0f;

    if (clipSelected) {
        borderColor = clipBase.lightened(0.16f).withAlpha(0.88f);
        borderWidth = 1.25f;
    }
    
    // Ghost instance check
    bool isGhostInstance = (patternRefCount > 1 && patternInstanceIndex > 1);

    if (isGhostInstance) {
        // Ghost instances: Dimmer border
        borderColor = borderColor.withAlpha(0.4f);
        // Dashed border? For now just dim.
    }
    
    renderer.strokeRoundedRect(clipBounds, clipRadius, borderWidth, borderColor);
    // Subtle top inner highlight for depth
    renderer.fillRect({clipBounds.x + 3.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 6.0f), 1.0f},
                      AestraUI::NUIColor::white().withAlpha(0.08f));
    // Selection reads through the brighter border, fill, and waveform ink —
    // no shadow or ring around the clip

    // Label + header band are drawn by drawSampleClipHeader() AFTER the waveform,
    // so the waveform can fill the full clip height without hiding the filename.
    (void)sampleName;
}

// Distinct dark header scrim + filename, drawn on top of the full-height waveform.
void TrackUIComponent::drawSampleClipHeader(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                            const ClipInstance& clip, bool seamLeft, bool seamRight) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float clipRadius = themeManager.getRadius("s");
    constexpr float kClipHeaderHeight = 15.0f;
    if (clipBounds.height <= kClipHeaderHeight + 4.0f || clipBounds.width <= 28.0f) return;

    std::string sampleName = "Clip";
    if (m_trackManager) {
        if (auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId)) {
            sampleName = pattern->name;
        }
    }
    const bool clipSelected = (clip.id == m_selectedClipId);

    const float headerLeft = clipBounds.x + (seamLeft ? 0.0f : 1.0f);
    const float headerRight = clipBounds.right() - (seamRight ? 0.0f : 1.0f);
    const AestraUI::NUIRect headerRect(headerLeft, clipBounds.y + 1.0f,
                                       std::max(0.0f, headerRight - headerLeft), kClipHeaderHeight);
    // Own opaque title strip (the waveform lives below it, not behind it), with a
    // divider so the label band reads as its own section.
    const auto headerFill = themeManager.getColor("backgroundPrimary").withAlpha(clipSelected ? 0.98f : 0.94f);
    renderer.fillRoundedRect(headerRect, clipRadius - 1.0f, headerFill);
    constexpr float kSeamOverlap = 1.0f;
    const float seamFillWidth = clipRadius + kSeamOverlap;
    if (seamLeft) {
        renderer.fillRect({clipBounds.x - kSeamOverlap, headerRect.y,
                           seamFillWidth + kSeamOverlap, headerRect.height}, headerFill);
    }
    if (seamRight) {
        renderer.fillRect({clipBounds.right() - seamFillWidth, headerRect.y,
                           seamFillWidth + kSeamOverlap, headerRect.height}, headerFill);
    }
    renderer.drawLine(
        AestraUI::NUIPoint(clipBounds.x + (seamLeft ? 0.0f : 2.0f), clipBounds.y + kClipHeaderHeight + 1.0f),
        AestraUI::NUIPoint(clipBounds.right() - (seamRight ? 0.0f : 2.0f),
                           clipBounds.y + kClipHeaderHeight + 1.0f),
        1.0f, themeManager.getCurrentTheme().textPrimary.withAlpha(0.16f));

    const std::string displayName = truncateClipLabel(sampleName, clipBounds.width - 16.0f, 6.0f);
    if (!displayName.empty()) {
        const float kClipLabelFontSize = themeManager.getFontSize("micro");
        const auto metrics = renderer.getFontMetrics(kClipLabelFontSize);
        const float textBoxH = (metrics.ascent > 0.0f && metrics.descent > 0.0f)
                                   ? (metrics.ascent + metrics.descent)
                                   : kClipLabelFontSize;
        const float textY = headerRect.y + (headerRect.height - textBoxH) * 0.5f;
        renderer.drawText(displayName,
                          AestraUI::NUIPoint(clipBounds.x + 6.0f, textY),
                          kClipLabelFontSize,
                          themeManager.getCurrentTheme().textPrimary.withAlpha(clipSelected ? 0.95f : 0.85f));
    }
}

void TrackUIComponent::drawSampleClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds) {
    // Legacy stub - do nothing or implement if needed for backward compatibility
}


// Helper to draw a clip at its calculated position (for multi-clip lane support)
void TrackUIComponent::drawClipAtPosition(AestraUI::NUIRenderer& renderer, const ClipInstance& clip,
                                          const AestraUI::NUIRect& bounds, float controlAreaWidth) {
    
    // Calculate waveform position in timeline space
    double startBeat = clip.startBeat;
    double durationBeats = clip.durationBeats;
    
    // Calculate waveform dimensions in pixel space
    double relStartX = (startBeat * m_pixelsPerBeat) - static_cast<double>(m_timelineScrollOffset);
    float waveformStartX = timelineGridStartX(bounds.x, controlAreaWidth) + static_cast<float>(relStartX);
    float waveformWidthInPixels = static_cast<float>(durationBeats * m_pixelsPerBeat);
    
    // Only draw if waveform is visible in the current viewport
    float gridStartX = timelineGridStartX(bounds.x, controlAreaWidth);
    float gridWidth = bounds.width - controlAreaWidth - 10;
    float gridEndX = gridStartX + gridWidth;
    
    // Culling padding for smooth scrolling
    float cullPaddingLeft = 400.0f;
    float cullPaddingRight = 400.0f;
    
    // Check if waveform intersects with visible area
    if (waveformStartX + waveformWidthInPixels > gridStartX - cullPaddingLeft &&
        waveformStartX < gridEndX + cullPaddingRight) {
        
        // Determine the visible portion
        float visibleStartX = std::max(waveformStartX, gridStartX);
        float visibleEndX = std::min(waveformStartX + waveformWidthInPixels, gridEndX);
        float visibleWidth = visibleEndX - visibleStartX;
        
        if (visibleWidth > 0) {
            // Calculate offset and ratio for visible portion
            float offsetRatio = 0.0f;
            float visibleRatio = 1.0f;
            
            if (waveformStartX < gridStartX) {
                offsetRatio = (gridStartX - waveformStartX) / waveformWidthInPixels;
            }
            
            if (waveformStartX + waveformWidthInPixels > gridEndX) {
                float endRatio = (gridEndX - waveformStartX) / waveformWidthInPixels;
                visibleRatio = endRatio - offsetRatio;
            }
            
            // Clip bounds for drawing
            float clipStartX = std::max(waveformStartX, gridStartX);
            float clipEndX = std::min(waveformStartX + waveformWidthInPixels, gridEndX);
            float clipWidth = clipEndX - clipStartX;
            
            if (clipWidth > 0) {
                bool seamLeft = false;
                bool seamRight = false;
                if (m_trackManager) {
                    auto& playlist = m_trackManager->getPlaylistModel();
                    if (const PlaylistLane* lane = playlist.getLane(m_laneId)) {
                        constexpr double kSeamToleranceBeats = 1.0e-6;
                        for (const auto& otherClip : lane->clips) {
                            if (otherClip.id == clip.id) continue;
                            seamLeft = seamLeft || std::abs(otherClip.endBeat() - clip.startBeat) <= kSeamToleranceBeats;
                            seamRight = seamRight || std::abs(clip.endBeat() - otherClip.startBeat) <= kSeamToleranceBeats;
                            if (seamLeft && seamRight) break;
                        }
                    }
                }

                const AestraUI::NUIRect fullClipBounds(
                    waveformStartX,
                    bounds.y + 2,
                    waveformWidthInPixels,
                    bounds.height - 4
                );

                // Store FULL clip bounds for hit testing
                m_allClipBounds[clip.id] = fullClipBounds;

                AestraUI::NUIRect clippedClipBounds(
                    clipStartX,
                    bounds.y + 2,
                    clipWidth,
                    bounds.height - 4
                );
                const AestraUI::NUIRect insetFullClipBounds = fullClipBounds;
                const AestraUI::NUIRect insetClippedClipBounds = clippedClipBounds;

                // Check if this is a pattern clip or sample clip
                bool isPattern = false;
                if (m_trackManager && clip.patternId.isValid()) {
                    auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                    if (pattern && pattern->isMidi()) {
                        isPattern = true;
                    }
                }

                if (isPattern) {
                    drawPatternClipForClip(renderer, insetClippedClipBounds, insetFullClipBounds, clip);
                } else {
                    drawSampleClipForClip(renderer, insetClippedClipBounds, insetFullClipBounds, clip, seamLeft, seamRight);
                    // The label gets its own reserved strip at the top; the waveform
                    // fills the whole area BELOW it (bold + gained, so it's full, not
                    // squashed under the label).
                    constexpr float kHeaderStripH = 16.0f;
                    const float waveformPadLeft = seamLeft ? 0.0f : 3.0f;
                    const float waveformPadRight = seamRight ? 0.0f : 3.0f;
                    constexpr float waveformPadBottom = 3.0f;
                    const float waveTop = insetClippedClipBounds.y + kHeaderStripH;
                    const AestraUI::NUIRect waveformInsideClip(
                        insetClippedClipBounds.x + waveformPadLeft,
                        waveTop + 1.0f,
                        std::max(1.0f, insetClippedClipBounds.width - waveformPadLeft - waveformPadRight),
                        std::max(1.0f, (insetClippedClipBounds.bottom() - waveformPadBottom) - (waveTop + 1.0f))
                    );
                    drawWaveformForClip(renderer, waveformInsideClip, clip, offsetRatio, visibleRatio);
                    drawSampleClipHeader(renderer, insetClippedClipBounds, clip, seamLeft, seamRight);
                }
                if (clip.id == m_selectedClipId) {
                    drawPianoRollStyleSelection(renderer, insetClippedClipBounds,
                                                AestraUI::NUIThemeManager::getInstance().getRadius("s"));
                }
            }
        }
    }
}

void TrackUIComponent::drawPatternClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                              const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    AestraUI::NUIColor baseColor = AestraUI::NUIColor::fromHex(clip.colorRGBA);
    // Unset patterns default to a flat grey RGBA, which reads as an unfinished
    // placeholder. Fall back to the theme accent so pattern clips carry colour.
    {
        const float mx = std::max({baseColor.r, baseColor.g, baseColor.b});
        const float mn = std::min({baseColor.r, baseColor.g, baseColor.b});
        if (mx - mn < 0.06f) {
            baseColor = themeManager.getColor("accentPrimary");
        }
    }
    bool isSelected = (clip.id == m_selectedClipId);

    if (clip.edits.muted) {
        baseColor = baseColor.withAlpha(0.4f);
    }

    const float clipRadius = themeManager.getRadius("s");
    renderer.fillRoundedRect(clipBounds, clipRadius, themeManager.getColor("elevatedPanel").withAlpha(0.96f));
    renderer.fillRoundedRect(clipBounds, clipRadius, baseColor.withAlpha(isSelected ? 0.38f : 0.28f));
    renderer.strokeRoundedRect(
        clipBounds,
        clipRadius,
        1.0f,
        isSelected ? baseColor.lightened(0.28f).withAlpha(0.92f) : baseColor.lightened(0.18f).withAlpha(0.66f)
    );

    // Subtle top inner highlight for depth
    renderer.fillRect({clipBounds.x + 3.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 6.0f), 1.0f},
                      AestraUI::NUIColor::white().withAlpha(0.05f));

    const float headerHeight = std::min(18.0f, std::max(14.0f, clipBounds.height * 0.26f));
    const AestraUI::NUIRect headerRect(clipBounds.x + 1.0f, clipBounds.y + 1.0f, std::max(0.0f, clipBounds.width - 2.0f), headerHeight);
    renderer.fillRoundedRect(headerRect, clipRadius - 1.0f, baseColor.withAlpha(isSelected ? 0.42f : 0.34f));
    renderer.fillRoundedRect(
        {clipBounds.x + 1.5f, clipBounds.y + 1.5f, 4.0f, std::max(0.0f, clipBounds.height - 3.0f)},
        2.0f,
        baseColor.lightened(0.20f).withAlpha(0.95f)
    );
    renderer.drawLine(
        AestraUI::NUIPoint(clipBounds.x + 6.0f, clipBounds.y + headerHeight + 1.0f),
        AestraUI::NUIPoint(clipBounds.right() - 6.0f, clipBounds.y + headerHeight + 1.0f),
        1.0f,
        themeManager.getCurrentTheme().textPrimary.withAlpha(0.08f)
    );

    std::string clipName = clip.name;
    if (clipName.empty()) {
        if (m_trackManager) {
            auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
            if (pattern) clipName = pattern->name;
        }
    }

    const std::string displayName = truncateClipLabel(clipName, clipBounds.width - 46.0f, 5.9f);
    if (!displayName.empty()) {
        // Centred in the header band rather than offset from the clip's top edge:
        // headerHeight is clamped, so a fixed offset drifts as the band resizes and
        // left the label flush against the band's lower edge at short clip heights.
        constexpr float kClipLabelFontSize = 9.5f;
        renderer.drawText(displayName,
                          AestraUI::NUIPoint(clipBounds.x + 10.0f,
                                             renderer.calculateTextY(headerRect, kClipLabelFontSize)),
                          kClipLabelFontSize, themeManager.getCurrentTheme().textPrimary);
    }

    if (m_trackManager && clip.patternId.isValid()) {
        auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
        if (pattern && pattern->isMidi()) {
            const auto& midiPayload = std::get<MidiPayload>(pattern->payload);
            
            float noteAreaY = fullClipBounds.y + headerHeight + 2.0f;
            float noteAreaHeight = std::max(8.0f, fullClipBounds.height - headerHeight - 4.0f);

            int minPitch = 127;
            int maxPitch = 0;
            if (midiPayload.notes.empty()) {
                minPitch = 36; maxPitch = 84;
            } else {
                for (const auto& n : midiPayload.notes) {
                    minPitch = std::min(minPitch, (int)n.pitch);
                    maxPitch = std::max(maxPitch, (int)n.pitch);
                }
                // Add some padding
                minPitch = std::max(0, minPitch - 2);
                maxPitch = std::min(127, maxPitch + 2);
            }
            int pitchRange = std::max(12, maxPitch - minPitch);

            const float laneHeight = std::max(1.0f, noteAreaHeight / static_cast<float>(pitchRange));
            const int guideCount = std::min(8, pitchRange);
            for (int i = 1; i < guideCount; ++i) {
                const float y = noteAreaY + (noteAreaHeight / guideCount) * i;
                renderer.drawLine(
                    AestraUI::NUIPoint(clipBounds.x + 7.0f, y),
                    AestraUI::NUIPoint(clipBounds.right() - 4.0f, y),
                    1.0f,
                    themeManager.getCurrentTheme().textPrimary.withAlpha(0.06f)
                );
            }

            const int stepCount = std::max(4, static_cast<int>(std::round(clip.durationBeats * 2.0)));
            const float bodyX = clipBounds.x + 6.0f;
            const float bodyWidth = std::max(0.0f, clipBounds.width - 12.0f);
            for (int i = 1; i < stepCount; ++i) {
                const float stepX = bodyX + (bodyWidth / stepCount) * i;
                const bool major = (i % 4) == 0;
                renderer.drawLine(
                    AestraUI::NUIPoint(stepX, noteAreaY + 1.0f),
                    AestraUI::NUIPoint(stepX, clipBounds.bottom() - 4.0f),
                    1.0f,
                    themeManager.getCurrentTheme().textPrimary.withAlpha(major ? 0.08f : 0.04f)
                );
            }
            
            const auto& themeProps = themeManager.getCurrentTheme();
            for (const auto& n : midiPayload.notes) {
                float noteStartX = fullClipBounds.x + (n.startBeat / pattern->lengthBeats) * fullClipBounds.width;
                float noteWidth = (n.durationBeats / pattern->lengthBeats) * fullClipBounds.width;

                float normalizedPitch = (float)(n.pitch - minPitch) / pitchRange;
                float noteY = noteAreaY + noteAreaHeight * (1.0f - normalizedPitch) - laneHeight;
                float noteHeight = std::max(2.0f, laneHeight - 1.5f);

                AestraUI::NUIRect noteRect(noteStartX, noteY, std::max(1.0f, noteWidth), std::max(1.0f, noteHeight));

                if (noteRect.x + noteRect.width > clipBounds.x && noteRect.x < clipBounds.x + clipBounds.width) {
                    if (noteRect.x < clipBounds.x) {
                        noteRect.width -= (clipBounds.x - noteRect.x);
                        noteRect.x = clipBounds.x;
                    }
                    if (noteRect.x + noteRect.width > clipBounds.x + clipBounds.width) {
                        noteRect.width = (clipBounds.x + clipBounds.width) - noteRect.x;
                    }
                    renderer.fillRoundedRect(noteRect, themeProps.radiusXS, themeManager.getCurrentTheme().textPrimary.withAlpha(isSelected ? 0.88f : 0.78f));
                    if (noteRect.width > 6.0f && noteRect.height > 2.5f) {
                        renderer.fillRoundedRect(
                            {noteRect.x + 1.0f, noteRect.y + 1.0f, std::max(0.0f, noteRect.width - 2.0f), std::max(0.0f, noteRect.height - 2.0f)},
                            1.5f,
                            baseColor.lightened(0.26f).withAlpha(0.42f)
                        );
                    }
                }
            }
        }
    }
}


// =============================================================================
// SECTION: Main Render Entry
// =============================================================================

void TrackUIComponent::renderStatic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    
    // Clear clip bounds map - will be repopulated during drawClipAtPosition
    // m_allClipBounds.clear(); // Handled in render logic now
    if (!m_trackManager) return;
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    // Get theme colors and layout
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Zebra striping moved to TrackManagerUI for guaranteed rendering order

    // No zebra: the grid is uniform pure black (owner direction). Only the
    // selection/hover states below tint the row.
    AestraUI::NUIColor trackBgColor = AestraUI::NUIColor::transparent();

    // Selection Highlight (Static base)
    if (isSelected()) {
         trackBgColor = themeManager.getColor("accentSecondary").lightened(0.12f).withAlpha(0.045f);
    } else if (isHovered()) {
         trackBgColor = themeManager.getCurrentTheme().textPrimary.withAlpha(0.026f);
    }
    
    // Apply background
    renderer.fillRect(bounds, trackBgColor);
    if (isSelected()) {
        const auto selectionEdge = AestraUI::NUIColor::white().withAlpha(0.18f);
        renderer.drawLine({bounds.x, bounds.y}, {bounds.right(), bounds.y}, 1.0f, selectionEdge);
        renderer.drawLine({bounds.x, bounds.bottom() - 1.0f}, {bounds.right(), bounds.bottom() - 1.0f},
                          1.0f, selectionEdge.withAlpha(0.12f));
    }

    // Row separation is the light gap strip drawn by TrackManagerUI between
    // lanes — no per-row line here, so separators never stack.
    AestraUI::NUIColor borderColor = themeManager.getColor("border");
    
    float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    
    if (m_isPrimaryForLane) {
        AestraUI::NUIRect controlBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);

        // Control Area base: near-black chrome on dark themes (owner
        // direction — surfaceTertiary read bluish-grey), clean surface on light.
        AestraUI::NUIColor baseControlColor = themeManager.getColor("trackChrome");

        // Static Playlist-lane state is independent from mixer insert state.
        if (lane) {
            if (m_selected) {
                 baseControlColor = themeManager.getColor("accentSecondary").withAlpha(0.035f);
            } else if (lane->solo) {
                 baseControlColor = themeManager.getColor("accentCyan").withAlpha(0.10f);
            } else if (lane->muted) {
                 baseControlColor = themeManager.getColor("backgroundSecondary");
             } else if (isHovered()) {
                 baseControlColor = themeManager.getColor("surfaceRaised").withAlpha(0.70f);
             }
        }

        // Render Control Area Background with a soft vertical elevation gradient
        // (state color stays the base; the gradient just adds depth). This runs
        // inside the playlist FBO cache, so the extra fills cost nothing per frame.
        renderer.fillRect(controlBounds, baseControlColor);
        // Elevation via shade ONLY — no light component at all. Even ~1% white
        // reads as a sheen on near-black (and this pipeline amplifies low-alpha
        // fills), so depth comes purely from the darker bottom.
        renderer.fillRectGradient(controlBounds, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.0f),
                                  AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.070f),
                                  /*vertical=*/true);
        // Separator Line between Controls and Timeline (single, quiet boundary)
        renderer.drawLine(
            AestraUI::NUIPoint(controlBounds.right(), controlBounds.y),
            AestraUI::NUIPoint(controlBounds.right(), controlBounds.bottom()),
            1.0f,
            themeManager.getColor("borderSubtle").withAlpha(0.24f)
        );
        
        // Lane color strip (identity)
        if (lane) {
            uint32_t argb = lane->colorRGBA;
            float a = ((argb >> 24) & 0xFF) / 255.0f;
            float r = ((argb >> 16) & 0xFF) / 255.0f;
            float g = ((argb >> 8) & 0xFF) / 255.0f;
            float b = (argb & 0xFF) / 255.0f;
            AestraUI::NUIColor stripColor(r, g, b, a > 0.0f ? a : 1.0f);
            
            const float stripWidth = 3.0f;
            const float stripAlpha = (m_selected || lane->solo) ? 0.82f : 0.40f;
            const auto stripBright = AestraUI::restrainLaneIdentityColor(stripColor, stripAlpha);
            renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripBright);
        }

        // Keep separator clean and flat; no extra chrome shadow strip.

        drawPlaylistGrid(renderer, bounds);
    }

    // Render Clips (Heavy part)
    m_allClipBounds.clear();
    // Use stored modId for caching check if needed later, but here we just draw
    float clipOpacity = (m_playlistMode == PlaylistMode::Automation) ? 0.3f : 1.0f;
    renderer.setOpacity(clipOpacity);
    for (const auto& clip : lane->clips) {
        drawClipAtPosition(renderer, clip, bounds, controlAreaWidth);
    }
    renderer.setOpacity(1.0f);
}


// Render Dynamic Overlays (Grid Area Only: Mute/Solo dimming, Automation, Selection)
void TrackUIComponent::renderDynamic(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);

    // Automation Layer (v3.1)
    if (m_playlistMode == PlaylistMode::Automation) {
        renderAutomationLayer(renderer, bounds, bounds.x + controlAreaWidth);
    }

    // Live Recording Waveform (v3.0.2) - Dynamic real-time update
    drawLiveWaveform(renderer, bounds, controlAreaWidth);

    // Apply overlay for muted/solo state (Grid Area Only)
    if (m_isPrimaryForLane) {
        const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
        const bool soloSuppressed = m_anyPlaylistLaneSoloed && lane && !lane->solo;

        AestraUI::NUIRect gridArea(
            bounds.x + controlAreaWidth,
            bounds.y,
            bounds.width - controlAreaWidth,
            bounds.height
        );
        
        if (lane && lane->solo) {
            renderer.fillRect(gridArea, themeManager.getColor("accentCyan").withAlpha(0.06f));
        }

        float dimAlpha = 0.0f;
        if (soloSuppressed) dimAlpha = std::max(dimAlpha, 0.28f);
        if (lane && lane->muted) dimAlpha = std::max(dimAlpha, 0.40f);

        if (dimAlpha > 0.0f) {
            renderer.fillRect(gridArea, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, dimAlpha));
        }
    }
}

void TrackUIComponent::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("TrackUI_Render");
    renderStatic(renderer);
    renderDynamic(renderer);
}


void TrackUIComponent::renderControlOverlay(AestraUI::NUIRenderer& renderer) {
    if (!m_isPrimaryForLane) return;
    
    const AestraUI::NUIRect bounds = getBounds();
    if (bounds.isEmpty()) return;

    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    const AestraUI::NUIRect controlAreaBounds(bounds.x, bounds.y, controlAreaWidth, bounds.height);

    // Quiet control slab overlay (recedes so clips stay Level 1).
    renderer.fillRect(controlAreaBounds, themeManager.getColor("surfaceRaised").withAlpha(isHovered() ? 0.12f : 0.04f));

    AestraUI::NUIRect highlightRect = controlAreaBounds;
    highlightRect.height = 1.0f;
    renderer.fillRect(highlightRect, themeManager.getColor("textPrimary").withAlpha(0.025f));

    // Inline Volume Meter (Behind Name) - Uses real audio levels from MeterSnapshotBuffer
    if (m_channel && !m_channel->isMuted() && m_trackManager) {
        // Get meter data from MeterSnapshotBuffer via TrackManager
        auto meterSnapshots = m_trackManager->getMeterSnapshots();
        auto slotMapPtr = m_trackManager->getChannelSlotMapShared();
        
        if (meterSnapshots && slotMapPtr) {
            uint32_t slotIndex = slotMapPtr->getSlotIndex(m_channel->getChannelId());
            if (slotIndex != ChannelSlotMap::INVALID_SLOT) {
                auto readout = meterSnapshots->readSnapshot(slotIndex);
                
                // Use peak levels (linear 0..1+), average L/R for mono display
                // Note: MeterSnapshotBuffer already reports post-fader levels,
                // do NOT multiply by track volume again.
                float level = (readout.peakL + readout.peakR) * 0.5f;

                if (level > 0.001f) {
                    level = std::min(1.0f, std::max(0.0f, level));
                    float visualLevel = std::pow(level, 0.5f); // Perceptual scaling
                    
                    float meterX = bounds.x + 20.0f; 
                    float meterY = bounds.y + 13.0f;
                    float meterW = 140.0f * visualLevel;
                    float meterH = 22.0f;
                    
                    AestraUI::NUIRect meterRect(meterX, meterY, meterW, meterH);
                    
                    AestraUI::NUIColor meterColor = themeManager.getColor("success").withAlpha(0.055f);
                    if (visualLevel > 0.8f) meterColor = themeManager.getColor("error").withAlpha(0.08f);
                    else if (visualLevel > 0.5f) meterColor = themeManager.getColor("warning").withAlpha(0.075f);

                    const auto& themeProps = themeManager.getCurrentTheme();
                    renderer.fillRoundedRect(meterRect, themeProps.radiusS, meterColor);
                }
            }
        }
    }

    // Holographic Loading Feedback (Tech Aesthetic)
    if (m_isLoading) {
        auto cyan = themeManager.getColor("accentCyan");
        float time = static_cast<float>(Aestra::Platform::getUtils()->getTime());
        
        // Background Glow
        renderer.fillRect(controlAreaBounds, cyan.withAlpha(0.05f));
        
        // Scanning Bar
        float scanProgress = std::fmod(time * 1.2f, 1.0f);
        float scanX = controlAreaBounds.x + (controlAreaBounds.width * scanProgress);
        renderer.drawLine(AestraUI::NUIPoint(scanX, controlAreaBounds.y), 
                         AestraUI::NUIPoint(scanX, controlAreaBounds.bottom()), 
                         2.0f, cyan.withAlpha(0.3f * (1.0f - std::abs(scanProgress - 0.5f) * 2.0f)));

        // Animated "IMPORTING..." Text
        std::string loadingText = "IMPORTING";
        int dots = (int)(time * 3.0f) % 4;
        for(int i=0; i<dots; ++i) loadingText += ".";
        
        // Center text in control area
        float fontSize = themeManager.getFontSize("s");
        auto textSize = renderer.measureText(loadingText, fontSize);
        renderer.drawText(loadingText, 
                         AestraUI::NUIPoint(controlAreaBounds.x + (controlAreaBounds.width - textSize.width) * 0.5f, 
                                          controlAreaBounds.y + controlAreaBounds.height - 12.0f), 
                         fontSize, cyan.withAlpha(0.8f));
    }

    const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;

    // Apply highlight overlay (Selection / Solo / Mute)
    if (lane) {
        const bool soloSuppressed = m_anyPlaylistLaneSoloed && !lane->solo;

        if (lane->muted) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("backgroundSecondary").withAlpha(0.62f));
        } else if (lane->solo) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentCyan").withAlpha(0.10f));
        } else if (soloSuppressed) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("backgroundSecondary").withAlpha(0.44f));
        }

        // SELECTION OVERLAY
        if (m_selected) {
            renderer.fillRect(controlAreaBounds, themeManager.getColor("accentSecondary").withAlpha(0.035f));
            AestraUI::NUIRect selectionBar(controlAreaBounds.x, controlAreaBounds.y, 3.0f, controlAreaBounds.height);
            renderer.fillRect(selectionBar,
                              themeManager.getColor("accentSecondary").lightened(0.12f).withAlpha(0.82f));
        }
    }

    // Track color strip (identity)
    if (lane) {
        AestraUI::NUIColor stripColor;
        
        if (m_isLoading) {
            stripColor = themeManager.getColor("accentCyan");
            // Add a subtle pulse to the strip during loading
            float pulse = (std::sin(static_cast<float>(Aestra::Platform::getUtils()->getTime()) * 8.0f) * 0.5f + 0.5f);
            stripColor = stripColor.withAlpha(0.6f + pulse * 0.4f);
        } else {
            uint32_t argb = lane->colorRGBA;
            float a = ((argb >> 24) & 0xFF) / 255.0f;
            float r = ((argb >> 16) & 0xFF) / 255.0f;
            float g = ((argb >> 8) & 0xFF) / 255.0f;
            float b = (argb & 0xFF) / 255.0f;
            stripColor = AestraUI::NUIColor(r, g, b, a > 0.0f ? a : 1.0f);
        }
        
        const float stripWidth = 5.0f;
        const float stripAlpha = (m_selected || lane->solo) ? 0.84f : 0.62f;
        // Was 0.86f here against 0.84f in the static pass, so one lane strip had
        // two brightnesses depending on which pass drew it. Both now go through
        // the shared lane-identity restraint the minimap uses.
        stripColor = AestraUI::restrainLaneIdentityColor(stripColor, stripAlpha);
        
        // Draw strip
        renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.y, stripWidth, bounds.height), stripColor);

        // If loading, draw a small progress bar on the strip itself
        if (m_isLoading && m_loadProgress > 0.0f) {
            float progressHeight = bounds.height * std::min(1.0f, m_loadProgress);
            renderer.fillRect(AestraUI::NUIRect(bounds.x, bounds.bottom() - progressHeight, stripWidth, progressHeight), themeManager.getColor("textPrimary").withAlpha(0.75f));
        }

        // Selection Highlight Line (Inner Glow)
        if (m_selected) {
            auto glowColor = AestraUI::NUIColor::white().withAlpha(0.20f);
            // Top highlight line inside control area (skipping strip)
            renderer.fillRect(AestraUI::NUIRect(bounds.x + stripWidth, bounds.y, controlAreaWidth - stripWidth, 1.0f), glowColor);
            // Bottom highlight
            renderer.fillRect(AestraUI::NUIRect(bounds.x + stripWidth, bounds.y + bounds.height - 1.0f, controlAreaWidth - stripWidth, 1.0f), glowColor.withAlpha(0.12f));
        }
    }

    // A single quiet bottom rule separates rows without boxing every header.
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, bounds.bottom() - 1),
        AestraUI::NUIPoint(bounds.x + controlAreaWidth, bounds.bottom() - 1),
        1.0f,
        themeManager.getColor("borderSubtle").withAlpha(0.10f)
    );


    // Render the track name directly; track control widgets remain hit targets only.
    // Drawing the button widgets here reintroduces bordered/pill artifacts in cached rows.
    if (m_nameLabel) {
        m_nameLabel->setTextColor(themeManager.getColor("textPrimary").withAlpha(m_selected ? 0.92f
                                                                                         : isHovered() ? 0.82f
                                                                                                       : 0.70f));
        m_nameLabel->onRender(renderer);
    }

    if (lane) {
        const auto textIdle = themeManager.getColor("textSecondary").withAlpha(isHovered() ? 0.65f : 0.42f);
        const auto muteActive = themeManager.getColor("warning").withAlpha(0.92f);
        const auto soloActive = themeManager.getColor("success").withAlpha(0.92f);
        const auto recordActive = themeManager.getColor("error").withAlpha(0.92f);

        const auto drawButtonShell = [&](const std::shared_ptr<AestraUI::NUIButton>& button,
                                         bool active,
                                         AestraUI::NUIColor activeColor) {
            if (!button) {
                return;
            }
            const auto rect = button->getBounds();
            const bool hovered = button->isHovered() && button->isEnabled();
            AestraUI::NUIColor bg = AestraUI::NUIColor::white().withAlpha(hovered ? 0.070f : 0.0f);
            AestraUI::NUIColor border = themeManager.getColor("border").withAlpha(hovered ? 0.30f : 0.0f);
            if (active) {
                bg = activeColor.withAlpha(0.18f);
                border = activeColor.withAlpha(0.55f);
            }
            const float pillRadius = rect.height * 0.5f;
            if (hovered || active) {
                renderer.fillRoundedRect(rect, pillRadius, bg);
                renderer.strokeRoundedRect(rect, pillRadius, 1.0f, border);
            }
            if (active) {
                renderer.strokeRoundedRect(rect, pillRadius, 1.0f, activeColor.withAlpha(0.45f));
            }
        };

        const auto drawControlIcon = [&](const std::shared_ptr<AestraUI::NUIButton>& button,
                                         const char* iconSvg,
                                         AestraUI::NUIColor color,
                                         bool active,
                                         AestraUI::NUIColor activeColor) {
            if (!button) {
                return;
            }
            drawButtonShell(button, active, activeColor);
            const auto* doc = trackControlIcon(iconSvg);
            if (!doc) {
                return;
            }
            const auto rect = button->getBounds();
            const float iconSize = 11.0f;
            const AestraUI::NUIRect iconRect(std::round(rect.x + (rect.width - iconSize) * 0.5f),
                                             std::round(rect.y + (rect.height - iconSize) * 0.5f),
                                             iconSize, iconSize);
            AestraUI::NUISVGRenderer::render(renderer, *doc, iconRect, color);
        };

        drawControlIcon(m_muteButton, kMuteIconSvg, lane->muted ? muteActive : textIdle, lane->muted, muteActive);
        drawControlIcon(m_soloButton, kSoloIconSvg, lane->solo ? soloActive : textIdle, lane->solo, soloActive);
        // Record arm is track-exclusive (FD-14): nested lane rows carry M/S only.
        if (!m_isNestedLane) {
            auto* armTrack = m_trackManager ? m_trackManager->getTrackForLane(m_laneId) : nullptr;
            const bool isArmed = armTrack && armTrack->armed;
            drawControlIcon(m_recordButton, kRecordIconSvg, isArmed ? recordActive : textIdle, isArmed, recordActive);
        }
    }

    // FD-14 §10 expansion chevron; visibility set during updateUI (primary row
    // of a multi-lane track only).
    if (m_expandButton && m_expandButton->isVisible()) {
        const auto* doc = trackControlIcon(m_trackCollapsed ? kChevronDownSvg : kChevronUpSvg);
        if (doc) {
            const auto rect = m_expandButton->getBounds();
            const float iconSize = 12.0f;
            const AestraUI::NUIRect iconRect(std::round(rect.x + (rect.width - iconSize) * 0.5f),
                                             std::round(rect.y + (rect.height - iconSize) * 0.5f),
                                             iconSize, iconSize);
            AestraUI::NUISVGRenderer::render(renderer, *doc, iconRect,
                                             themeManager.getColor("textSecondary").withAlpha(0.42f));
        }
    }

    // Lane-count indicator (FD-14 scope §10): TrackUIComponent::onRender does
    // not call NUIComponent::renderChildren, so the icon/label widgets never
    // auto-draw. Render them explicitly, like the name label and buttons.
    if (m_laneCountIcon && m_laneCountIcon->isVisible()) {
        m_laneCountIcon->onRender(renderer);
    }
    if (m_laneCountLabel && m_laneCountLabel->isVisible()) {
        m_laneCountLabel->onRender(renderer);
    }

    // Track number marker (left of name): recedes as quiet metadata (Level 4).
    if (m_nameLabel && lane) {
        constexpr float stripWidth = 3.0f;
        const auto nameBounds = m_nameLabel->getBounds();
        std::string numberText;
        if (m_isNestedLane) {
            const auto* nestedTrack = m_trackManager ? m_trackManager->getTrack(lane->trackId) : nullptr;
            numberText = "Lane " + std::to_string(nestedTrack ? nestedTrack->laneNumber(m_laneId) : 0);
        } else {
            uint32_t trackNumber = static_cast<uint32_t>(lane->index + 1);
            const auto laneName = m_nameLabel->getText();
            uint32_t parsedNumber = 0;
            if (parseTrailingTrackNumber(laneName, parsedNumber)) {
                trackNumber = parsedNumber;
            }
            numberText = std::to_string(trackNumber);
        }
        renderer.drawText(numberText,
                          AestraUI::NUIPoint(controlAreaBounds.x + stripWidth + 8.0f, nameBounds.y + 2.0f),
                          themeManager.getFontSize("xs"),
                          themeManager.getColor("textSecondary").withAlpha(m_selected ? 0.58f : 0.36f));
    }
}

// Draw playlist grid (beat/bar grid)
void TrackUIComponent::drawPlaylistGrid(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds) {
    AESTRA_ZONE("TrackUI_Grid");
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);
    const float desiredGap = 5.0f;
    const float gridGap = std::min(desiredGap, std::max(0.0f, bounds.width - controlAreaWidth));
    const float gridStartX = bounds.x + controlAreaWidth + gridGap;
    const float gridEndX = bounds.right();

    // The arrangement is a large empty canvas much more often than the Piano
    // Roll, so its infrastructure recedes further while retaining the same
    // bar > beat > subdivision grammar.
    const AestraUI::TimelineGridStyle timelineStyle{
        0.018f, // bars
        0.004f, // beats
        0.0015f, // subdivisions
        0.0f    // no empty-canvas zebra
    };
    AestraUI::renderTimelineGrid(
        renderer, bounds, gridStartX, gridEndX, m_timelineScrollOffset, m_pixelsPerBeat, m_beatsPerBar,
        themeManager.getCurrentTheme().textPrimary, timelineStyle);
}

void TrackUIComponent::onMouseEnter() {
    // Ensure parent knows we need updates (if any)
    NUIComponent::onMouseEnter();
    // Force cache invalidation immediately on enter
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

void TrackUIComponent::onMouseLeave() {
    NUIComponent::onMouseLeave();
    
    // Reset trim hover state when mouse leaves track bounds
    if (m_hoverTrimEdge != TrimEdge::None) {
        m_hoverTrimEdge = TrimEdge::None;
        repaint();
    }
    
    // Force cache invalidation immediately on leave
    if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
}

// (Stubs removed)

void TrackUIComponent::onUpdate(double deltaTime) {
    // Only update UI when track state might have changed, not every frame
    // This prevents overriding hover colors unnecessarily

    // Update children state
    if (const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr) {
        bool currentMuted = lane->muted;
        bool currentSoloed = lane->solo;

        // Check if buttons match channel state
        if (m_muteButton && m_muteButton->isToggled() != currentMuted) {
            m_muteButton->setToggled(currentMuted);
        }
        if (m_soloButton && m_soloButton->isToggled() != currentSoloed) {
            m_soloButton->setToggled(currentSoloed);
        }
    }

    // Update children
    NUIComponent::onUpdate(deltaTime);
}

void TrackUIComponent::onResize(int width, int height) {
    AestraUI::NUIRect bounds = getBounds();
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    const float controlAreaWidth = std::min(layout.trackControlsWidth, bounds.width);

    // Buttons cluster (horizontal, right-aligned within the control area)
    // Keep the glyphs visually quiet while meeting a standard 24px pointer
    // target. The hover shell reveals the full interactive area.
    const float buttonW = 24.0f;
    const float buttonH = 24.0f;
    const float spacing = 2.0f;
    // Every lane owns M/S; the record arm button is track-owned and renders
    // without a channel too. No volume slot in the header: volume lives in
    // the mixer only.
    const int numButtons = 3;
    const float buttonsTotalW = numButtons * buttonW + (numButtons - 1) * spacing;

    const float leftPad = 14.0f;
    const float rightPad = 8.0f;
    const float trackNumberWidth = 14.0f;
    const float numberNameGap = 6.0f;

    // FD-14 §10: nested rows pull their chrome inward so the lane reads as
    // indented while the timeline grid stays globally aligned across lanes —
    // a row-x indent would shift clip snapping against the other rows.
    const float chromeIndent = m_isNestedLane ? kNestedLaneIndent : 0.0f;

    // Position relative to component origin, then add absolute offset
    const float localButtonsXStart = controlAreaWidth - rightPad - buttonsTotalW - chromeIndent;
    const float localButtonsY = (bounds.height - buttonH) * 0.5f;

    // Lane-count indicator (FD-14 scope §10) sits just left of the buttons on
    // the track's primary row; the name label flexes around it.
    const float laneIndicatorWidth = 54.0f;
    const float laneIndicatorGap = 6.0f;
    const float laneIndicatorX = localButtonsXStart - laneIndicatorGap - laneIndicatorWidth;

    // FD-14 §10 expansion chevron sits between the indicator and the buttons.
    const float expandButtonW = 20.0f;
    const float expandButtonX = laneIndicatorX - laneIndicatorGap - expandButtonW;
    if (m_expandButton) {
        m_expandButton->setBounds(
            AestraUI::NUIRect(bounds.x + expandButtonX, bounds.y + localButtonsY, expandButtonW, buttonH));
    }

    // Keep track number + name anchored to the left, with flexible space to buttons.
    // Nested lane rows (FD-14 §10) indent for the "Lane N" prefix.
    const float laneNumberPrefixWidth = 48.0f;
    const float localLabelLeft = leftPad + trackNumberWidth + numberNameGap + (m_isNestedLane ? laneNumberPrefixWidth : 0.0f);
    // Reserve space only for widgets actually visible on this row: the
    // indicator/chevron only exist on multi-lane primary rows, so hidden ones
    // must not starve the name label down to its 40px floor.
    const float inlineRightEdge = [&]() {
        if (m_expandButton && m_expandButton->isVisible()) {
            return expandButtonX;
        }
        if (m_laneCountIcon && m_laneCountIcon->isVisible()) {
            return laneIndicatorX;
        }
        return localButtonsXStart;
    }();
    const float localInlineRight = inlineRightEdge - 8.0f;
    const float localInlineWidth = std::max(0.0f, localInlineRight - localLabelLeft);
    const float localNameHeight = std::max(14.0f, layout.trackLabelHeight - 2.0f);
    const float localNameY = localButtonsY + std::max(0.0f, (buttonH - localNameHeight) * 0.5f);
    const float localLabelWidth = std::max(40.0f, localInlineWidth);

    // Name label - use NUIAbsolute for global coordinate system
    if (m_nameLabel) {
        m_nameLabel->setBounds(AestraUI::NUIRect(bounds.x + localLabelLeft, bounds.y + localNameY, localLabelWidth, localNameHeight));
    }
    if (m_laneCountLabel && m_laneCountIcon) {
        const float laneIconY = localButtonsY + std::max(0.0f, (buttonH - 13.0f) * 0.5f);
        m_laneCountIcon->setBounds(
            AestraUI::NUIRect(bounds.x + laneIndicatorX, bounds.y + laneIconY, 13.0f, 13.0f));
        m_laneCountLabel->setBounds(AestraUI::NUIRect(
            bounds.x + laneIndicatorX + 15.0f, bounds.y + localNameY, laneIndicatorWidth - 17.0f, localNameHeight));
    }

    float xCursor = localButtonsXStart;
    if (m_muteButton) {
        m_muteButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_soloButton) {
        m_soloButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
        xCursor += buttonW + spacing;
    }
    if (m_recordButton) {
        m_recordButton->setBounds(AestraUI::NUIRect(bounds.x + xCursor, bounds.y + localButtonsY, buttonW, buttonH));
    }

    AestraUI::NUIComponent::onResize(width, height);
}

// =============================================================================
// SECTION: Event Handling
// =============================================================================

bool TrackUIComponent::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    // 1. Invalidate Cache on mouse movement in control area (for button hover feedback)
    // AestraUI doesn't use event.type enum, so we infer from context.
    // Any mouse event in the control area checks for invalidation.
    if (m_onCacheInvalidationCallback) {
        // Check if we are over the control area
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float controlWidth = layout.trackControlsWidth;
        
        if (event.position.x <= getBounds().x + controlWidth) {
             m_onCacheInvalidationCallback();
        }
    }

    AestraUI::NUIRect bounds = getBounds();
    
    // Early exit: If event is outside our bounds and we're not in an active operation, don't handle it
    bool isInsideBounds = bounds.contains(event.position);
    bool isActiveOperation = m_isTrimming || m_isDraggingClip || m_clipDragPotential || m_isDraggingPoint;
    bool isControlCapture = (m_muteButton && m_muteButton->isPressed()) ||
                            (m_soloButton && m_soloButton->isPressed()) ||
                            (m_recordButton && m_recordButton->isPressed());
    bool controlsNeedEvents = isControlCapture ||
                              (m_muteButton && m_muteButton->isHovered()) ||
                              (m_soloButton && m_soloButton->isHovered()) ||
                              (m_recordButton && m_recordButton->isHovered());
    
    if (!isInsideBounds && !isActiveOperation && !controlsNeedEvents) {
        return false;  // Let parent/siblings handle it (e.g., scrollbar)
    }
    
    // Get theme to determine control area bounds
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    float controlAreaWidth = layout.trackControlsWidth;
    float controlAreaEndX = bounds.x + controlAreaWidth;
    float gridStartX = timelineGridStartX(bounds.x, controlAreaWidth);
    float gridEndX = bounds.x + bounds.width - 5;
    
    // === HOVER EDGE DETECTION (for resize cursor) ===
    // Update hover state on every mouse move (not just press)
    if (!m_isTrimming && isInsideBounds && !event.pressed) {
        TrimEdge newHoverEdge = TrimEdge::None;
        
        for (const auto& [clipId, clipBounds] : m_allClipBounds) {
            if (!clipBounds.contains(event.position)) continue;
            
            float leftEdge = clipBounds.x;
            float rightEdge = clipBounds.x + clipBounds.width;
            
            // Check left edge
            if (std::abs(event.position.x - leftEdge) < TRIM_EDGE_WIDTH &&
                event.position.y >= clipBounds.y && 
                event.position.y <= clipBounds.y + clipBounds.height) {
                newHoverEdge = TrimEdge::Left;
                break;
            }
            
            // Check right edge
            if (std::abs(event.position.x - rightEdge) < TRIM_EDGE_WIDTH &&
                event.position.y >= clipBounds.y && 
                event.position.y <= clipBounds.y + clipBounds.height) {
                newHoverEdge = TrimEdge::Right;
                break;
            }
        }
        
        if (m_hoverTrimEdge != newHoverEdge) {
            m_hoverTrimEdge = newHoverEdge;
            repaint(); // Trigger redraw for cursor feedback
        }
    }
    
    // Keep button hover/press state accurate even when leaving the track row.
    // This is important for cached UIs (and prevents stuck hover/press visuals).
    if (isInsideBounds || controlsNeedEvents) {
        auto routeControlButton = [&](const std::shared_ptr<AestraUI::NUIButton>& button) -> bool {
            if (!button) return false;
            
            // Explicitly handle hover state since we are manually routing
            bool isOver = button->getBounds().contains(event.position);
            if (button->isHovered() != isOver) {
                button->setHovered(isOver);
            }

            const bool handled = button->onMouseEvent(event);
            return handled;
        };

        bool handledByControls = false;

        handledByControls = routeControlButton(m_muteButton) || handledByControls;
        handledByControls = routeControlButton(m_soloButton) || handledByControls;
        handledByControls = routeControlButton(m_recordButton) || handledByControls;
        handledByControls = routeControlButton(m_expandButton) || handledByControls;

        if (!event.cursorCaptured && isInsideBounds) {
            if (m_muteButton && m_muteButton->getBounds().contains(event.position)) {
                const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
                const std::string tooltip =
                    lane && lane->muted && lane->solo ? "Muted • Solo is held (M)" : "Mute Track (M)";
                AestraUI::NUIComponent::showRemoteTooltip(tooltip, event.position, this);
            } else if (m_soloButton && m_soloButton->getBounds().contains(event.position)) {
                const auto* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;
                const std::string tooltip =
                    lane && lane->muted && lane->solo ? "Solo held • Track is muted (S)" : "Solo Track (S)";
                AestraUI::NUIComponent::showRemoteTooltip(tooltip, event.position, this);
            } else if (m_recordButton && m_recordButton->getBounds().contains(event.position)) {
                AestraUI::NUIComponent::showRemoteTooltip(recordButtonTooltipText(), event.position, this);
            } else if (m_expandButton && m_expandButton->getBounds().contains(event.position)) {
                AestraUI::NUIComponent::showRemoteTooltip(m_trackCollapsed ? "Expand lanes" : "Collapse lanes",
                                                          event.position, this);
            } else {
                AestraUI::NUIComponent::hideRemoteTooltip(this);
            }
        }

        if (event.pressed && event.button == AestraUI::NUIMouseButton::Right &&
            m_recordButton && m_recordButton->getBounds().contains(event.position)) {
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, TrackSelectionIntent::Replace);
            }
            if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
            showRecordModeMenu(event.position);
            return true;
        }

        if (handledByControls) {
            // Clicking controls should also select the track (v3.1)
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, selectionIntentFor(event));
                if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
            }
            return true;
        }

        // === TRACK CONTEXT MENU (Right Click on Header/Control Area) ===
        if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && event.position.x <= controlAreaEndX) {
            // Select track on right-click too
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, TrackSelectionIntent::Replace);
            }
            if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});

            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                parentMgr->openTrackContextMenu(event.position, m_onSendToAuditionCallback);
                return true;
            }
        }
    }

    // PRIORITY 3: Automation Layer (v3.1)
    // Handle Mouse Release for automation point dragging FIRST - before any bounds checks
    // This ensures release is processed even when mouse has moved far outside bounds
    if (m_playlistMode == PlaylistMode::Automation && m_isDraggingPoint) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            // Compare the dragged point against its drag-start value BEFORE
            // the index is cleared. A plain click-select (no movement) must
            // not dirty the project; a real move must rebuild the graph so
            // playback follows the edit.
            bool moved = false;
            if (m_trackManager && m_dragStartBeat >= 0.0 && m_draggedPointIndex >= 0) {
                if (auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
                    if (!lane->automationCurves.empty()) {
                        const auto& pts = lane->automationCurves[0].getPoints();
                        if (m_draggedPointIndex < static_cast<int>(pts.size())) {
                            const auto& pt = pts[static_cast<size_t>(m_draggedPointIndex)];
                            moved = std::abs(pt.beat - m_dragStartBeat) >= 1e-9 ||
                                    std::abs(pt.value - m_dragStartValue) >= 1e-9f;
                        }
                    }
                }
            }
            m_isDraggingPoint = false;
            m_draggedPointIndex = -1;
            m_draggedCurveIndex = -1;
            m_dragStartBeat = -1.0;
            m_dragStartValue = -1.0f;

            // Release mouse capture
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (auto win = parentMgr->getPlatformWindow()) {
                    win->setMouseCapture(false);
                }
            }
            if (moved && m_trackManager) {
                m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
                m_trackManager->markModified();
            }
            return true;
        }
    }
    
    if (m_playlistMode == PlaylistMode::Automation && (isInsideBounds || m_isDraggingPoint)) {
        if (event.position.x >= gridStartX || m_isDraggingPoint) {
            double beat =
                timelineGridOffsetToBeat(event.position.x - gridStartX, m_timelineScrollOffset, m_pixelsPerBeat);
            double value = 1.0 - std::clamp((static_cast<double>(event.position.y) - bounds.y) / bounds.height, 0.0, 1.0);
            
            auto& playlist = m_trackManager->getPlaylistModel();
            auto lane = playlist.getLane(m_laneId);

            if (lane) {
                // Left-press only: right-click is the delete gesture, and a
                // right-click on empty space must not mutate the model.
                const bool isLeftPress =
                    event.pressed && event.button == AestraUI::NUIMouseButton::Left;
                if (isLeftPress && lane->automationCurves.empty()) {
                    // First point on an empty lane: create the default Volume
                    // curve bound to this lane's routing channel. FD-14 §15:
                    // automation targets must never resolve through lane
                    // POSITION (getChannel(lane->index)) — they resolve by
                    // ownership: lane -> owning Track -> track channelId,
                    // master when the lane is unowned. defaultValue 1.0 keeps
                    // an empty curve neutral — the old default of 0.0 silenced
                    // the channel until a point was added. Press-only: pointer
                    // moves (hover) must not insert a curve into the model.
                    AutomationCurve curve("Volume", AutomationTarget::Volume);
                    curve.setDefaultValue(1.0f);
                    const uint32_t resolvedId = m_trackManager->resolveLaneChannelId(lane->id);
                    const uint32_t defaultChannelId = resolvedId != 0 ? resolvedId : MASTER_MIXER_CHANNEL_ID;
                    if (const auto* ch = m_trackManager->getChannelById(defaultChannelId)) {
                        curve.mixerChannelId = ch->getChannelId();
                    }
                    lane->automationCurves.push_back(std::move(curve));
                }
                if (lane->automationCurves.empty()) {
                    return true;
                }
                auto& curve = lane->automationCurves[0]; // For now, automate first curve (Volume)

                // Right Click -> Delete Point
                if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (AestraUI::distance({px, py}, event.position) < 12.0f) {
                            curve.removePoint(i);
                            setDirty(true);
                            repaint();
                            if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                            if (m_trackManager) {
                                m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
                                m_trackManager->markModified();
                            }
                            return true;
                        }
                    }
                }

                // Left Click -> Select/Add Point
                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
                    int hitIndex = -1;
                    auto& points = curve.getPoints();
                    for (int i = 0; i < (int)points.size(); ++i) {
                        float px = gridStartX + (static_cast<float>(points[i].beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                        float py = bounds.y + (1.0f - static_cast<float>(points[i].value)) * bounds.height;
                        
                        if (AestraUI::distance({px, py}, event.position) < 12.0f) {
                            hitIndex = i;
                            break;
                        }
                    }

                    if (hitIndex != -1) {
                        m_isDraggingPoint = true;
                        m_draggedPointIndex = hitIndex;
                        m_draggedCurveIndex = 0;
                        m_dragStartBeat = curve.getPoints()[static_cast<size_t>(hitIndex)].beat;
                        m_dragStartValue = curve.getPoints()[static_cast<size_t>(hitIndex)].value;
                        
                        // Capture mouse
                        if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                            if (auto win = parentMgr->getPlatformWindow()) {
                                win->setMouseCapture(true);
                            }
                        }
                        repaint(); // Request redraw to show selection state if any
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback(); // Force parent update
                        return true;
                    } else if (isInsideBounds) {
                        // Add new point - Default to smooth curve (0.5 tension)
                        double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
                        double sampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
                        double samplesPerBeat = (sampleRate * 60.0) / std::max(bpm, 1.0);
                        curve.addPoint(beat, value, samplesPerBeat, 0.5f);
                        setDirty(true);
                        repaint(); // Immediate update
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback(); // Force parent update
                        if (m_trackManager) {
                            m_trackManager->requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
                            m_trackManager->markModified();
                        }
                        
                        // Start dragging the new point
                        auto& pts = curve.getPoints();
                        for (int i = 0; i < (int)pts.size(); ++i) {
                            if (std::abs(pts[i].beat - beat) < 0.001) {
                                m_isDraggingPoint = true;
                                m_draggedPointIndex = i;
                                m_draggedCurveIndex = 0;
                                m_dragStartBeat = pts[static_cast<size_t>(i)].beat;
                                m_dragStartValue = pts[static_cast<size_t>(i)].value;
                                
                                // Capture mouse
                                if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                                    if (auto win = parentMgr->getPlatformWindow()) {
                                        win->setMouseCapture(true);
                                    }
                                }
                                break;
                            }
                        }
                        return true;
                    }
                }

                // Dragging Logic
                if (m_isDraggingPoint && m_draggedCurveIndex == 0) {
                    auto& pts = curve.getPoints();
                    if (m_draggedPointIndex >= 0 && m_draggedPointIndex < (int)pts.size()) {
                        double newBeat = std::max(0.0, beat);
                        double newValue = value;
                        
                        pts[m_draggedPointIndex].beat = newBeat;
                        pts[m_draggedPointIndex].value = newValue;
                        
                        curve.sortPoints();
                        
                        // Re-find index after sort
                        for (int i = 0; i < (int)pts.size(); ++i) {
                            if (pts[i].beat == newBeat && pts[i].value == newValue) {
                                m_draggedPointIndex = i;
                                break;
                            }
                        }
                        
                        setDirty(true);
                        repaint(); // Ensure immediate redraw
                        if (m_onCacheInvalidationCallback) m_onCacheInvalidationCallback();
                        return true;
                    }
                }
            }
            
            // Allow selecting track in automation mode if not interacting with points
            if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
                if (m_onTrackSelectedCallback) {
                    m_onTrackSelectedCallback(this, selectionIntentFor(event));
                }
                if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
            }
            
            if (isInsideBounds) return true;
        }
    }
    
    auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
    
    // Handle mouse release - always process to clear state
    if (!event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        bool wasActive = m_isTrimming || m_isDraggingClip || m_clipDragPotential;
        if (m_isTrimming) {
            // The trim was applied to the model live during the drag. Push the
            // command now so the edit survives as unsaved work, round-trips,
            // and can be undone (#744).
            if (auto* clip = m_trackManager->getPlaylistModel().getClip(m_activeClipId)) {
                auto& playlist = m_trackManager->getPlaylistModel();
                const double originalEnd = m_trimOriginalStart + m_trimOriginalDuration;
                const double newStart = clip->startBeat;
                const double newEnd = clip->startBeat + clip->durationBeats;
                if (newStart != m_trimOriginalStart || newEnd != originalEnd) {
                    auto command = std::make_shared<TrimClipCommand>(
                        playlist, m_activeClipId, m_trimOriginalStart, originalEnd,
                        m_trimOriginalSourceOffsetSeconds, m_trimOriginalDurationSeconds, newStart,
                        newEnd);
                    m_trackManager->getCommandHistory().pushAndExecute(command);
                }
            }
            Log::info("Finished trimming clip");
        }
        
        // Instant Drag Finish
        if (m_isDraggingClip) {
             if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                 parentMgr->finishInstantClipDrag();
                 if (auto win = parentMgr->getPlatformWindow()) {
                     win->setMouseCapture(false);
                 }
             }
        }

        m_clipDragPotential = false;
        m_isDraggingClip = false;
        m_isTrimming = false;
        m_trimEdge = TrimEdge::None;
        m_activeClipId = ClipInstanceID{};  // Clear active clip
        
        // Only consume the event if we were doing something
        if (wasActive) {
            return true;
        }
        return false;
    }
    
    // PRIORITY 1.5: Instant Clip Dragging Update
    if (m_isDraggingClip && !event.released && event.button == AestraUI::NUIMouseButton::Left) {
         if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
             parentMgr->updateInstantClipDrag(event.position);
         }
         return true;
    }
    
    // PRIORITY 2: Handle active trimming (mouse move while trimming)
    if (m_isTrimming && m_activeClipId.isValid()) {
        auto& clipBounds = m_allClipBounds[m_activeClipId];
        float deltaX = event.position.x - m_trimDragStartX;
        
        if (m_trackManager && clipBounds.width > 0) {
            auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
            if (lane) {
                for (size_t i = 0; i < lane->clips.size(); ++i) {
                    auto& clip = lane->clips[i];
                    if (clip.id == m_activeClipId) {
                        double deltaBeats = (deltaX / m_pixelsPerBeat);
                        
                        if (m_trimEdge == TrimEdge::Left) {
                            // Trim left: move start beat and reduce duration. The
                            // left edge must never extend before the source start
                            // (that writes a negative source read offset the
                            // renderer cannot honor), so clamp to the source-start
                            // beat for audio clips, using the same rate formula as
                            // TrimClipCommand so drag and command agree.
                            double minStart = 0.0;
                            if (m_trackManager && m_trackManager->getPlaylistModel().isAudioClip(clip)) {
                                auto& playlistModel = m_trackManager->getPlaylistModel();
                                const double secondsPerBeat = playlistModel.beatToSeconds(1.0);
                                const double varispeed = clip.edits.effectiveVarispeed();
                                minStart = std::max(
                                    0.0, m_trimOriginalStart - m_trimOriginalSourceOffsetSeconds / (secondsPerBeat * varispeed));
                            }
                            double newStart = std::max(minStart, m_trimOriginalStart + deltaBeats);
                            newStart = snapBeatToGrid(newStart); // Apply snap
                            newStart = std::max(newStart, minStart); // Snap must not push before source start
                            
                            double endBeat = m_trimOriginalStart + m_trimOriginalDuration;
                            clip.startBeat = std::min(newStart, endBeat - 0.1); // Keep minimum duration
                            clip.durationBeats = endBeat - clip.startBeat;
                            if (m_trackManager && m_trackManager->getPlaylistModel().isAudioClip(clip)) {
                                clip.durationSeconds =
                                    m_trackManager->getPlaylistModel().beatToSeconds(clip.durationBeats) /
                                    clip.edits.effectiveVarispeed();
                            }
                        } else if (m_trimEdge == TrimEdge::Right) {
                            // Trim right: change end position (duration)
                            double newEnd = m_trimOriginalStart + m_trimOriginalDuration + deltaBeats;
                            newEnd = snapBeatToGrid(newEnd); // Apply snap
                            
                            clip.durationBeats = std::max(0.1, newEnd - clip.startBeat);
                            if (m_trackManager && m_trackManager->getPlaylistModel().isAudioClip(clip)) {
                                clip.durationSeconds =
                                    m_trackManager->getPlaylistModel().beatToSeconds(clip.durationBeats) /
                                    clip.edits.effectiveVarispeed();
                            }
                        }
                        
                        if (m_onCacheInvalidationCallback) {
                            m_onCacheInvalidationCallback();
                        }
                        break;
                    }
                }
            }
        }
        return true;
    }
    
    // PRIORITY 3: Handle drag threshold detection on MOUSE MOVE
    if (m_clipDragPotential && !event.pressed && !event.released && !dragManager.isDragging()) {
        float dx = event.position.x - m_clipDragStartPos.x;
        float dy = event.position.y - m_clipDragStartPos.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        const float DRAG_THRESHOLD = 5.0f;
        if (distance >= DRAG_THRESHOLD && m_activeClipId.isValid()) {
            m_clipDragPotential = false;

            // Alt-drag routes the referenced audio source. Normal drag remains
            // arrangement-only, so moving a clip can never change its signal path.
            if ((event.modifiers & AestraUI::NUIModifiers::Alt) && m_trackManager) {
                const auto* clip = m_trackManager->getPlaylistModel().getClip(m_activeClipId);
                const auto* pattern = clip ? m_trackManager->getPatternManager().getPattern(clip->patternId) : nullptr;
                if (clip && pattern && pattern->isAudio()) {
                    AestraUI::DragData routeDrag;
                    routeDrag.type = AestraUI::DragDataType::AudioSourceRoute;
                    routeDrag.displayName = clip->name.empty() ? pattern->name : clip->name;
                    routeDrag.customData = pattern->id.value;
                    const auto boundsIt = m_allClipBounds.find(m_activeClipId);
                    if (boundsIt != m_allClipBounds.end()) {
                        routeDrag.previewWidth = std::max(100.0f, boundsIt->second.width);
                        routeDrag.previewHeight = std::max(24.0f, boundsIt->second.height);
                    }
                    dragManager.beginDrag(routeDrag, m_clipDragStartPos, this);
                    return true;
                }
            }

            m_isDraggingClip = true;

            // Normal clip movement uses the low-latency arrangement drag path.
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (m_trackManager) {
                    auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                    if (lane) {
                        for (const auto& clip : lane->clips) {
                            if (clip.id == m_activeClipId && clip.patternId.isValid()) {
                                auto* pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                                if (pattern && pattern->isMidi() && m_onPatternClipDragStarted) {
                                    m_onPatternClipDragStarted(clip.patternId);
                                }
                                break;
                            }
                        }
                    }
                }
                parentMgr->startInstantClipDrag(this, m_activeClipId, event.position);
                
                // Capture mouse to follow outside bounds
                if (auto win = parentMgr->getPlatformWindow()) {
                     win->setMouseCapture(true);
                }
            }
            
            return true;
        }
    }

    
    // PRIORITY 4: Handle clip manipulation in the grid/playlist area only (mouse press)
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && isInsideBounds) {
        // Position relative to local component origin
        AestraUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);

        // Only process clip manipulation if click is in the grid area (not control area)
        if (localPos.x >= controlAreaWidth) {
            
            // Check if split tool is active
            bool isSplitToolActive = m_isSplitToolActiveCallback ? m_isSplitToolActiveCallback() : false;
            
            // === MULTI-CLIP HIT TESTING ===
            // Find which clip was clicked (check all clips in m_allClipBounds)
            ClipInstanceID clickedClipId = ClipInstanceID{};
            AestraUI::NUIRect clickedClipBounds;
            
            for (const auto& [clipId, clipBounds] : m_allClipBounds) {
                if (clipBounds.contains(event.position)) {
                    clickedClipId = clipId;
                    clickedClipBounds = clipBounds;
                    break;
                }
            }
            
            // Handle SPLIT TOOL - click on clip to split at that position
            if (isSplitToolActive && clickedClipId.isValid()) {
                // Calculate beat position using same math as the visual cursor in TrackManagerUI
                // This ensures the split occurs exactly where the preview line shows
                double mouseBeat =
                    timelineGridOffsetToBeat(event.position.x - gridStartX, m_timelineScrollOffset, m_pixelsPerBeat);

                Log::info("Split requested at " + std::to_string(mouseBeat) + " beats");
                
                if (m_onSplitRequestedCallback) {
                    // Pass the beat position - TrackManagerUI will snap it
                    m_onSplitRequestedCallback(this, mouseBeat);
                }
                return true;
            }

            // Handle PAINT TOOL - Click on empty space
            if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                if (parentMgr->getCurrentTool() == PlaylistTool::Paint && !clickedClipId.isValid()) {
                    double beat = timelineGridOffsetToBeat(event.position.x - gridStartX, m_timelineScrollOffset,
                                                           m_pixelsPerBeat);
                    parentMgr->onPaintClip(this, beat);
                    return true;
                }
            }
            
            // Check if clicking on any clip for drag initiation or trimming
            if (clickedClipId.isValid()) {
                auto now = std::chrono::steady_clock::now();
                const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                const bool manualDoubleClick =
                    (m_lastClickedClipId == clickedClipId) &&
                    (nowMs - m_lastClipClickTimeMs < 400);
                m_lastClickedClipId = clickedClipId;
                m_lastClipClickTimeMs = nowMs;

                if ((manualDoubleClick || event.doubleClick) && m_trackManager) {
                    auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                    if (lane) {
                        for (const auto& clip : lane->clips) {
                            if (clip.id == clickedClipId && clip.patternId.isValid()) {
                                auto* pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
                                if (pattern && pattern->isMidi() && m_onPatternClipOpenRequested) {
                                    m_onPatternClipOpenRequested(clip.patternId);
                                    return true;
                                }
                                if (pattern && pattern->isAudio() && m_onAudioClipOpenRequested) {
                                    m_onAudioClipOpenRequested(clip.id);
                                    return true;
                                }
                                break;
                            }
                        }
                    }
                }

                float leftEdge = clickedClipBounds.x;
                float rightEdge = clickedClipBounds.x + clickedClipBounds.width;
                
                // Left edge trim detection
                if (std::abs(event.position.x - leftEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Left;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_activeClipId = clickedClipId;
                    
                    // Store original state for relative drag
                    if (m_trackManager) {
                        auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                        if (lane) {
                            for (const auto& clip : lane->clips) {
                                if (clip.id == clickedClipId) {
                                    m_trimOriginalStart = clip.startBeat;
                                    m_trimOriginalDuration = clip.durationBeats;
                                    m_trimOriginalSourceOffsetSeconds = clip.sourceOffsetSeconds;
                                    m_trimOriginalDurationSeconds = clip.durationSeconds;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (m_onTrackSelectedCallback) {
                        m_onTrackSelectedCallback(this, selectionIntentFor(event));
                    } else {
                        m_selected = true;
                    }
                    Log::info("Started trimming left edge of clip: " + clickedClipId.toString());
                    return true;
                }
                
                // Right edge trim detection
                if (std::abs(event.position.x - rightEdge) < TRIM_EDGE_WIDTH &&
                    event.position.y >= clickedClipBounds.y && 
                    event.position.y <= clickedClipBounds.y + clickedClipBounds.height) {
                    m_trimEdge = TrimEdge::Right;
                    m_isTrimming = true;
                    m_trimDragStartX = event.position.x;
                    m_activeClipId = clickedClipId;
                    
                    // Store original state for relative drag
                    if (m_trackManager) {
                        auto lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
                        if (lane) {
                            for (size_t i = 0; i < lane->clips.size(); ++i) {
                                const auto& clip = lane->clips[i];
                                if (clip.id == clickedClipId) {
                                    m_trimOriginalStart = clip.startBeat;
                                    m_trimOriginalDuration = clip.durationBeats;
                                    m_trimOriginalSourceOffsetSeconds = clip.sourceOffsetSeconds;
                                    m_trimOriginalDurationSeconds = clip.durationSeconds;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (m_onTrackSelectedCallback) {
                        m_onTrackSelectedCallback(this, selectionIntentFor(event));
                    } else {
                        m_selected = true;
                    }
                    Log::info("Started trimming right edge of clip: " + clickedClipId.toString());
                    return true;
                }
                
                m_clipDragPotential = true;
                m_clipDragStartPos = event.position;
                m_activeClipId = clickedClipId;
                if (m_onTrackSelectedCallback) {
                    m_onTrackSelectedCallback(this, selectionIntentFor(event));
                } else {
                    m_selected = true;
                }
                
                if (m_onClipSelectedCallback) {
                    m_onClipSelectedCallback(this, clickedClipId);
                }
                
                Log::info("Clip selected - ready for drag: " + clickedClipId.toString());
                return true;

            }

            
            // Grid area click (not on any clip) - select track
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, selectionIntentFor(event));
            } else {
                m_selected = true;
            }
            if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
            return true;
        }
        
        // Click in control area (but not on a button) - just select the track
        if (event.position.x < controlAreaEndX) {
            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, selectionIntentFor(event));
            } else {
                m_selected = true;
            }
            if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
            return true;
        }
    }
    
    // Handle right-click delete using the same clip hit path as left-click selection.
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right && isInsideBounds) {
        ClipInstanceID clickedClipId = ClipInstanceID{};
        for (const auto& [clipId, clipBounds] : m_allClipBounds) {
            if (clipBounds.contains(event.position)) {
                clickedClipId = clipId;
                break;
            }
        }

        if (clickedClipId.isValid()) {
            m_activeClipId = clickedClipId;

            if (m_onTrackSelectedCallback) {
                m_onTrackSelectedCallback(this, TrackSelectionIntent::Replace);
            } else {
                m_selected = true;
            }

            if (m_onClipSelectedCallback) {
                m_onClipSelectedCallback(this, clickedClipId);
            }

            if (event.modifiers & AestraUI::NUIModifiers::Shift) {
                // Shift+right-click keeps the full contextual menu (routing,
                // cut/copy/duplicate, editors) while plain right-click takes
                // the fast path: immediate undoable delete.
                showClipRoutingMenu(clickedClipId, event.position);
            } else if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
                parentMgr->deleteSelectedClip();
            }
            return true;
        }

        // Empty timeline space still belongs to its track. Right-click opens
        // the same track menu as the header; marquee selection is an explicit
        // tool gesture, not a hidden alternate meaning for the context button.
        if (m_onTrackSelectedCallback) {
            m_onTrackSelectedCallback(this, TrackSelectionIntent::Replace);
        }
        if (m_onClipSelectedCallback) m_onClipSelectedCallback(this, ClipInstanceID{});
        if (auto parentMgr = dynamic_cast<TrackManagerUI*>(getParent())) {
            parentMgr->openTrackContextMenu(event.position, m_onSendToAuditionCallback);
        }
        return true;
    }

    // Pass through to parent if not handled
    return false;
}


void TrackUIComponent::renderAutomationLayer(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX) {
    auto& playlist = m_trackManager->getPlaylistModel();
    auto lane = playlist.getLane(m_laneId);
    if (!lane) return;

    auto& theme = AestraUI::NUIThemeManager::getInstance();
    
    // Automation Area bounds (exclude controls)
    AestraUI::NUIRect gridArea(gridStartX, bounds.y, bounds.width - (gridStartX - bounds.x), bounds.height);

    for (const auto& curve : lane->automationCurves) {
        if (!curve.isVisible()) continue;

        const auto& points = curve.getPoints();
        if (points.empty()) {
            // Draw a flat line at default value if no points
            float y = gridArea.y + (1.0f - static_cast<float>(curve.getDefaultValue())) * gridArea.height;
            renderer.drawLine(AestraUI::NUIPoint(gridArea.x, y),
                              AestraUI::NUIPoint(gridArea.right(), y),
                              1.5f, theme.getColor("accentCyan").withAlpha(0.4f));
            continue;
        }

        double bpm = m_trackManager ? m_trackManager->getPlaylistModel().getBPM() : 120.0;
        double sampleRate = m_trackManager ? m_trackManager->getPlaylistModel().getProjectSampleRate() : 48000.0;
        double samplesPerBeat = (sampleRate * 60.0) / std::max(bpm, 1.0);

        AestraUI::NUIColor curveColor = theme.getColor("accentCyan");

        // Draw lines between points
        // Draw lines between points
        std::vector<AestraUI::NUIPoint> polyPoints;
        polyPoints.reserve(1024);

        for (size_t i = 1; i < points.size(); ++i) {
            const auto& p1 = points[i-1];
            const auto& p2 = points[i];
            
            // Adaptive subdivision based on screen space length
            float sx1 = gridStartX + (static_cast<float>(p1.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float sy1 = gridArea.y + (1.0f - static_cast<float>(p1.value)) * gridArea.height;
            float sx2 = gridStartX + (static_cast<float>(p2.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float sy2 = gridArea.y + (1.0f - static_cast<float>(p2.value)) * gridArea.height;
            
            float dist = std::sqrt(std::pow(sx2 - sx1, 2) + std::pow(sy2 - sy1, 2));
            
            // Use fine subdivisions for smooth curves - 1 vertex per pixel
            int steps = static_cast<int>(dist);
            steps = std::clamp(steps, 4, 512); // Min 4 for smooth curves, Max 512
            
            for (int s = 0; s <= steps; ++s) {
                 double t = static_cast<double>(s) / static_cast<double>(steps);
                 double beat = p1.beat + (p2.beat - p1.beat) * t;
                 double val = curve.getValueAtBeat(beat, samplesPerBeat);
                 
                 float x = gridStartX + (static_cast<float>(beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
                 float y = gridArea.y + (1.0f - static_cast<float>(val)) * gridArea.height;
                 
                 polyPoints.emplace_back(x, y);
            }
        }
        
        if (polyPoints.size() >= 2) {
            // Use thick 2px capsules - high subdivision minimizes angle between segments so joints are invisible
            renderer.drawPolyline(polyPoints.data(), static_cast<int>(polyPoints.size()), 2.0f, curveColor);
        }
            
        
        // Draw endpoints before/after first/last point if they are within view
        if (!points.empty()) {
            const auto& first = points.front();
            float fx = gridStartX + (static_cast<float>(first.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float fy = gridArea.y + (1.0f - static_cast<float>(first.value)) * gridArea.height;
            if (fx > gridArea.x) {
                renderer.drawLine(AestraUI::NUIPoint(gridArea.x, fy), AestraUI::NUIPoint(fx, fy), 1.5f, curveColor.withAlpha(0.5f));
            }
            
            const auto& last = points.back();
            float lx = gridStartX + (static_cast<float>(last.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float ly = gridArea.y + (1.0f - static_cast<float>(last.value)) * gridArea.height;
            if (lx < gridArea.right()) {
                renderer.drawLine(AestraUI::NUIPoint(lx, ly), AestraUI::NUIPoint(gridArea.right(), ly), 1.5f, curveColor.withAlpha(0.5f));
            }
        }

        // Draw point handles
        for (const auto& p : points) {
            float x = gridStartX + (static_cast<float>(p.beat) * m_pixelsPerBeat) - m_timelineScrollOffset;
            float y = gridArea.y + (1.0f - static_cast<float>(p.value)) * gridArea.height;
            
            if (x < gridArea.x || x > gridArea.right()) continue;
            
            AestraUI::NUIColor ptColor = p.selected ? theme.getColor("primary") : curveColor;
            renderer.fillRect(AestraUI::NUIRect(x - 3, y - 3, 6, 6), ptColor);
            renderer.strokeRect(AestraUI::NUIRect(x - 4, y - 4, 8, 8), 1.0f, theme.getColor("border"));
        }
    }
}


void TrackUIComponent::drawLiveWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float controlAreaWidth) {
    if (!m_trackManager || !m_channel) return;
    if (!m_trackManager->isRecording()) return;
    auto* lane = m_trackManager->getPlaylistModel().getLane(m_laneId);
    auto* track = lane ? m_trackManager->getTrack(lane->trackId) : nullptr;
    if (!track || !track->armed) return;

    std::vector<float> recordingData;
    double startBeat = 0.0;
    bool gotSnapshot = m_trackManager->getRecordingDataSnapshot(track->trackId, recordingData, startBeat);
    
    if (!gotSnapshot || recordingData.empty()) return;

    // Layout parameters
    const float gridStartX = timelineGridStartX(bounds.x, controlAreaWidth);
    const float centerY = bounds.y + bounds.height * 0.5f;
    const float halfHeight = bounds.height * 0.5f; // Use full height for live wave
    
    double bpm = m_trackManager->getPlaylistModel().getBPM();
    double sampleRate = m_trackManager->getOutputSampleRate(); // Usage of Output Rate is safe here as recording is captured at this rate
    if (sampleRate <= 0.0) sampleRate = 48000.0;
    
    // Map samples to pixels
    // pixels_per_sample = pixels_per_beat * beats_per_second / samples_per_second
    // beats_per_second = bpm / 60
    double bitsPerSecond = bpm / 60.0;
    double samplesPerPixel = sampleRate / (bitsPerSecond * m_pixelsPerBeat);
    
    // Calculate start X in screen coordinates
    float startX = gridStartX + (static_cast<float>(startBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    
    size_t totalSamples = recordingData.size();
    float endX = startX + (totalSamples / static_cast<float>(samplesPerPixel));
    
    if (endX < gridStartX || startX > bounds.right()) return;

    // Drawing Loop (Decimated)
    AestraUI::NUIColor waveColor = AestraUI::NUIThemeManager::getInstance().getColor("error"); // Red for recording
    
    std::vector<AestraUI::NUIPoint> topPoints;
    std::vector<AestraUI::NUIPoint> bottomPoints;
    
    float visibleStartPixel = std::max(gridStartX, startX) - startX;
    float visibleEndPixel = std::min(bounds.right(), endX) - startX;
    
    if (visibleEndPixel <= visibleStartPixel) return;
    
    int startPixelInt = static_cast<int>(visibleStartPixel);
    int endPixelInt = static_cast<int>(visibleEndPixel);
    
    size_t numPoints = endPixelInt - startPixelInt;
    topPoints.reserve(numPoints);
    bottomPoints.reserve(numPoints);
    
    for (int p = startPixelInt; p < endPixelInt; ++p) {
        size_t sampleIndex = static_cast<size_t>(p * samplesPerPixel);
        size_t nextSampleIndex = static_cast<size_t>((p + 1) * samplesPerPixel);
        
        if (sampleIndex >= totalSamples) break;
        if (nextSampleIndex > totalSamples) nextSampleIndex = totalSamples;
        
        float peak = 0.0f;
        for (size_t i = sampleIndex; i < nextSampleIndex; ++i) {
            float val = std::abs(recordingData[i]);
            if (val > peak) peak = val;
        }
        
        float env = std::pow(std::min(1.0f, peak), 0.75f);
        
        float screenX = startX + p;
        float topY = centerY - env * halfHeight;
        float bottomY = centerY + env * halfHeight;
        
        if (bottomY - topY < 1.0f) {
            topY = centerY - 0.5f;
            bottomY = centerY + 0.5f;
        }
        
        topPoints.push_back(AestraUI::NUIPoint(screenX, topY));
        bottomPoints.push_back(AestraUI::NUIPoint(screenX, bottomY));
    }
    
    if (!topPoints.empty()) {
        const AestraUI::NUIColor topFill = waveColor.withAlpha(0.35f);
        const AestraUI::NUIColor bottomFill = waveColor.withAlpha(0.14f);
        const AestraUI::NUIColor peakLine = waveColor.withAlpha(0.55f);
        const AestraUI::NUIColor centerLine = waveColor.withAlpha(0.15f);

        for (size_t i = 0; i < topPoints.size(); ++i) {
            const float x = topPoints[i].x;
            const float topY = topPoints[i].y;
            const float bottomY = bottomPoints[i].y;

            renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x, topY), 1.0f, topFill);
            renderer.drawLine(AestraUI::NUIPoint(x, centerY), AestraUI::NUIPoint(x, bottomY), 1.0f, bottomFill);
            renderer.drawLine(AestraUI::NUIPoint(x, topY), AestraUI::NUIPoint(x, bottomY), 1.0f, peakLine);
        }

        renderer.drawLine(
            AestraUI::NUIPoint(topPoints.front().x, centerY),
            AestraUI::NUIPoint(topPoints.back().x, centerY),
            1.0f,
            centerLine
        );
    }
}

} // namespace Audio
} // namespace Aestra
