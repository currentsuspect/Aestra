// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Core/ProjectTempo.h"

#include "AudioEngine.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

void changeProjectTempo(AudioEngine& engine, TrackManager* trackManager, float bpm) {
    const double keepBeat = engine.setTempoKeepingBeat(bpm);
    if (!trackManager) {
        return;
    }
    trackManager->getPlaylistModel().setBPM(bpm);
    trackManager->getTimelineClock().setTempo(bpm);
    trackManager->getPatternPlaybackEngine().rewindScheduledInstances();
    if (keepBeat != 1.0) {
        trackManager->setPosition(trackManager->getPosition() * keepBeat);
        trackManager->setPlayStartPosition(trackManager->getPlayStartPosition() * keepBeat);
    }
}

} // namespace Audio
} // namespace Aestra
