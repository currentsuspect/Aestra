// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The app's side of #650 (FD-64): say so when the audio interface disappears, and again when it is back.
// The device manager reports from its health-monitor thread; the toast is touched on the main thread only.
#include "AestraApp.h"

#include "AudioDeviceNotice.h"
#include "../Core/AestraContent.h"
#include "../Core/AestraAudioController.h"

using namespace Aestra;

void AestraApp::watchAudioDevice() {
    if (!m_audioController) return;
    auto* manager = m_audioController->getDeviceManager();
    if (!manager) return;
    manager->setDriverModeChangeCallback(
        [this, manager](Audio::AudioDriverType, Audio::AudioDriverType actual, const std::string&) {
            // Fires outside the manager's lock, so a getter is safe here. Read the name now: by the time the main
            // thread runs the task the device could have changed again.
            const bool lost = (actual == Audio::AudioDriverType::DUMMY);
            std::string name = lost ? std::string() : manager->getPreferredDeviceName();
            enqueueMainThreadTask([this, lost, name = std::move(name)]() {
                const AudioDeviceNotice notice = lost ? m_audioDeviceNotice.onLost() : m_audioDeviceNotice.onRestored(name);
                if (notice.show && m_content) m_content->showToast(notice.text, notice.seconds);
            });
        });
}
