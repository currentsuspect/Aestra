// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The track header's identity marks (FD-50, Unified Panels 2 board 8): a colour bar in the track's colour (the
// same colour as its clips, mixer strip and Arsenal row), a 3 px level meter at the header's right edge, and a
// second line under the name saying what plays the track and where it goes. Kept out of TrackUIComponent.cpp,
// which is at its size pin.
#include "TrackUIComponent.h"

#include "ChannelSlotMap.h"
#include "MeterSnapshot.h"
#include "MixerChannel.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TimelineClipPaint.h"
#include "TrackHeaderText.h"
#include "TrackManager.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Aestra {
namespace Audio {

namespace {
// The line is rebuilt from the model at most this often: the header paints every frame, and walking every note
// of every clip each frame would be wasted work for a label that changes when the user edits.
constexpr double kSourceLineRefreshSeconds = 0.5;
} // namespace

const std::string& TrackUIComponent::headerSourceLine() {
    const double now = Aestra::Platform::getUtils()->getTime();
    if (m_headerSourceLineAt >= 0.0 && now - m_headerSourceLineAt < kSourceLineRefreshSeconds) {
        return m_headerSourceLine;
    }
    m_headerSourceLineAt = now;
    m_headerSourceLine.clear();
    if (!m_trackManager || m_isNestedLane) return m_headerSourceLine;

    // Lanes carry no mixer channel of their own: what a lane "goes to" is the channel its Arsenal units play
    // into. When every unit on the lane shares one channel that channel is named (unless it is Master) and its
    // level drives the header meter; when they split, neither is shown.
    TrackHeader::LaneSource source;
    bool sharedTarget = true;
    bool anyUnit = false;
    uint32_t target = 0;
    if (const PlaylistLane* lane = m_trackManager->getPlaylistModel().getLane(m_laneId)) {
        std::unordered_set<uint64_t> seen;
        const auto& units = m_trackManager->getUnitManager();
        for (const auto& clip : lane->clips) {
            auto pattern = m_trackManager->getPatternManager().getPattern(clip.patternId);
            if (!pattern) continue;
            if (pattern->isAudio()) {
                source.hasAudio = true;
                continue;
            }
            if (!pattern->isMidi()) continue;
            for (const auto& note : std::get<MidiPayload>(pattern->payload).notes) {
                if (!seen.insert(note.unitId).second) continue;
                const UnitInfo* unit = units.getUnit(note.unitId);
                if (!unit) continue;
                source.unitNames.push_back(unit->name);
                if (!anyUnit) target = unit->targetMixerChannelId;
                else if (unit->targetMixerChannelId != target) sharedTarget = false;
                anyUnit = true;
            }
        }
    }

    std::string output;
    m_headerMeterChannelId = 0;
    if (anyUnit && sharedTarget && target != MASTER_MIXER_CHANNEL_ID) {
        if (auto channel = m_trackManager->getChannelById(target)) {
            m_headerMeterChannelId = target; // the meter shows what this lane plays
            // Each unit usually plays into its own channel of the same name, so that hop says nothing. The line
            // names the bus the channel feeds, if it feeds one ("Hi Hat - Crisp → Drum Bus"); a channel straight
            // to Master names nothing.
            const uint32_t next = channel->getMainOutputId();
            if (!source.hasAudio && next != 0xFFFFFFFFu && next != MASTER_MIXER_CHANNEL_ID) {
                if (auto bus = m_trackManager->getChannelById(next)) output = bus->getName();
            }
        }
    }
    m_headerSourceLine = TrackHeader::sourceLine(source, output);
    return m_headerSourceLine;
}

void TrackUIComponent::paintHeaderIdentity(AestraUI::NUIRenderer& renderer,
                                           const AestraUI::NUIRect& controlAreaBounds) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const PlaylistLane* lane = m_trackManager ? m_trackManager->getPlaylistModel().getLane(m_laneId) : nullptr;

    // Colour bar: the track's colour at full strength, 3 px, full row height.
    if (lane && !m_isLoading) {
        renderer.fillRect({controlAreaBounds.x, controlAreaBounds.y, 3.0f, controlAreaBounds.height},
                          ClipPaint::identityFromARGB(lane->colorRGBA));
    }

    // Level meter: 3 px at the header's right edge, rising from the bottom, in the mixer's meter colours.
    (void)headerSourceLine(); // refreshes m_headerMeterChannelId
    auto meterChannel = (m_trackManager && m_headerMeterChannelId != 0)
                            ? m_trackManager->getChannelById(m_headerMeterChannelId)
                            : nullptr;
    if (meterChannel && !meterChannel->isMuted()) {
        auto snapshots = m_trackManager->getMeterSnapshots();
        auto slots = m_trackManager->getChannelSlotMapShared();
        if (snapshots && slots) {
            const uint32_t slot = slots->getSlotIndex(meterChannel->getChannelId());
            if (slot != ChannelSlotMap::INVALID_SLOT) {
                const auto readout = snapshots->readSnapshot(slot);
                const float level = std::clamp((readout.peakL + readout.peakR) * 0.5f, 0.0f, 1.0f);
                if (level > 0.001f) {
                    const float visual = std::sqrt(level); // perceptual, as the header's old meter
                    const float span = controlAreaBounds.height - 8.0f;
                    const float h = std::max(1.0f, span * visual);
                    const AestraUI::NUIColor color = visual > 0.9f   ? theme.getColor("meterCrit")
                                                     : visual > 0.7f ? theme.getColor("meterWarn")
                                                                     : theme.getColor("meterSafe");
                    renderer.fillRect({controlAreaBounds.right() - 5.0f, controlAreaBounds.bottom() - 4.0f - h, 3.0f, h},
                                      color);
                }
            }
        }
    }

    // What plays it → where it goes, under the name, when the row is tall enough for two lines.
    if (m_nameLabel && TrackHeader::showsSourceLine(controlAreaBounds.height)) {
        const std::string& line = headerSourceLine();
        if (!line.empty()) {
            const auto name = m_nameLabel->getBounds();
            const float fontSize = theme.getFontSize("xs");
            const float maxWidth = std::max(0.0f, name.width);
            std::string shown = line;
            if (renderer.measureText(shown, fontSize).width > maxWidth) {
                // Trim whole UTF-8 characters until the text and an ellipsis fit.
                const std::string ellipsis = "\u2026";
                while (!shown.empty() && renderer.measureText(shown + ellipsis, fontSize).width > maxWidth) {
                    unsigned char removed = 0;
                    do { // one whole character: continuation bytes, then its lead byte
                        removed = static_cast<unsigned char>(shown.back());
                        shown.pop_back();
                    } while (!shown.empty() && (removed & 0xC0) == 0x80);
                }
                shown = shown.empty() ? std::string{} : shown + ellipsis;
            }
            renderer.drawText(shown, AestraUI::NUIPoint(name.x, name.bottom() + 2.0f), fontSize,
                              theme.getColor("textSecondary").withAlpha(m_selected ? 0.72f : 0.52f));
        }
    }
}

} // namespace Audio
} // namespace Aestra
