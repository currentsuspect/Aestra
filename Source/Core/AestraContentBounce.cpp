// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Bounce in Place (V8-W5): the app-side wrapper around Audio::bounceClipsInPlace.
#include "AestraContent.h"

#include "../App/ServiceLocator.h"
#include "AudioDeviceManager.h"
#include "IO/BounceInPlace.h"

void AestraContent::bounceClipsInPlace(const std::vector<Aestra::Audio::ClipInstanceID>& clipIds) {
    if (!m_audioEngine || !m_trackManager) {
        return;
    }
    // The render drives the engine by hand, as Export does: the live callback must not be
    // running it at the same time.
    auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>();
    const bool resume = deviceManager && deviceManager->isStreamRunning();
    if (resume) {
        deviceManager->stopStream();
    }
    const auto result = Aestra::Audio::bounceClipsInPlace(*m_audioEngine, *m_trackManager, clipIds);
    if (resume) {
        deviceManager->startStream();
    }
    showToast(result.ok ? "Bounced in place. Undo puts the clips and effects back." : result.error);
}
