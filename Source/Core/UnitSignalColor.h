// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/TrackManager.h"
#include "NUITypes.h"
#include "Widgets/TrackColorPalette.h"

namespace Aestra {

/**
 * @brief The colour a unit's waveform takes: its mixer channel's own, the one its Arsenal row and clips wear.
 * A unit routed to Master has none, so alpha 0 comes back and the waveform stays the neutral bone.
 */
inline AestraUI::NUIColor unitSignalColor(Audio::TrackManager& trackManager, Audio::UnitID unitId) {
    AestraUI::NUIColor signal{0.0f, 0.0f, 0.0f, 0.0f};
    const auto* unit = trackManager.getUnitManager().getUnit(unitId);
    if (!unit) return signal;
    for (size_t i = 0; i < trackManager.getChannelCount(); ++i) {
        const auto* channel = trackManager.getChannel(i);
        if (channel && channel->getChannelId() == unit->targetMixerChannelId)
            signal = AestraUI::NUIColor::fromARGB(AestraUI::paletteIndexToARGB(channel->getTrackColorIndex()));
    }
    return signal;
}

} // namespace Aestra
