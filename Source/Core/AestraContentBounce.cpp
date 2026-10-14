// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Bounce in Place (V8-W5): the app-side wrapper around Audio::bounceClipsInPlace.
#include "AestraContent.h"

#include "../App/ServiceLocator.h"
#include "AudioDeviceManager.h"
#include <tuple>
#include "AuditionEngine.h"
#include "../Panels/AuditionHub.h"
#include "FileBrowser.h"
#include "IO/AudioExporter.h"
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

bool AestraContent::renderMixToBuffer(std::vector<float>& interleaved, uint32_t& channels, uint32_t& sampleRate) {
    if (!m_audioEngine || !m_trackManager) return false;
    const double beats = m_trackManager->getPlaylistModel().getTotalDurationBeats();
    if (beats < 0.1) {
        AESTRA_LOG_INFO("[Versions] Nothing on the timeline to bounce");
        return false;
    }
    auto* deviceManager = Aestra::ServiceLocator::get<Aestra::Audio::AudioDeviceManager>();
    const bool resume = deviceManager && deviceManager->isStreamRunning();
    if (resume) deviceManager->stopStream();
    Aestra::Audio::AudioExporter exporter(*m_audioEngine, *m_trackManager);
    Aestra::Audio::AudioExporter::Config config;
    config.scope = Aestra::Audio::AudioExporter::RenderScope::FullSong;
    config.startBeat = 0.0;
    config.endBeat = beats;
    config.sampleRate = 44100;
    config.tailSeconds = 1.0;
    Aestra::Audio::AudioExporter::RenderedAudio rendered;
    // Pattern and MIDI clips reach their units through the pattern-playback engine, which only holds what play
    // scheduled: put the timeline in for the render, and take it out again so playback starts clean.
    auto& patterns = m_trackManager->getPatternPlaybackEngine();
    patterns.clearScheduledInstances();
    m_trackManager->scheduleTimelineForOfflineRender(0.0);
    const auto result = exporter.renderToBuffer(config, rendered);
    patterns.clearScheduledInstances();
    if (resume) deviceManager->startStream();
    if (!result.ok() || rendered.interleaved.empty()) {
        AESTRA_LOG_WARNING("[Versions] Bounce render failed: " + result.errorMessage);
        return false;
    }
    interleaved = std::move(rendered.interleaved);
    channels = rendered.numChannels;
    sampleRate = rendered.sampleRate;
    return true;
}

bool AestraContent::isPreviewFilePlaying(const std::string& path) const {
    if (!m_auditionEngine || !m_auditionEngine->isPlaying()) return false;
    const auto item = m_auditionEngine->getCurrentItem();
    return item && item->filePath == path;
}

bool AestraContent::togglePreviewFile(const std::string& path) {
    if (path.empty()) return false;
    if (isPreviewFilePlaying(path)) {
        m_auditionEngine->stop();
        return false;
    }
    if (!m_auditionEngine) {
        m_auditionEngine = std::make_shared<Aestra::Audio::AuditionEngine>();
        if (m_audioEngine) {
            m_audioEngine->setAuditionEngine(m_auditionEngine.get());
            m_auditionEngine->setSampleRate(static_cast<double>(m_audioEngine->getSampleRate()));
        }
    }
    m_auditionEngine->addToQueue(path);
    m_auditionEngine->jumpToTrack(m_auditionEngine->getQueue().size() - 1);
    m_auditionEngine->play();
    return true;
}

void AestraContent::applyAuditionHooks() {
    if (!m_auditionPanel) return;
    m_auditionPanel->setProjectProvider([this]() {
        Aestra::AuditionHub::Project project;
        if (m_auditionHooks.project) std::tie(project.name, project.path) = m_auditionHooks.project();
        return project;
    });
    m_auditionPanel->setLibraryFile(m_auditionHooks.libraryFile);
    m_auditionPanel->setOnImportReference(m_auditionHooks.importReference);
    m_auditionPanel->setOnBounceNow(m_auditionHooks.bounceNow);
    m_auditionPanel->refresh();
}

void AestraContent::addAuditionReference(const std::string& path) {
    if (m_auditionPanel) m_auditionPanel->addReference(path);
}

void AestraContent::setAuditionActive(bool active) {
    if (!m_auditionPanel) return;
    m_auditionPanel->setVisible(active);
    if (!m_fileBrowser) return;
    if (active && !m_auditionPanel->hasBrowser()) {
        if (m_workspaceLayer) m_workspaceLayer->removeChild(m_fileBrowser);
        m_auditionPanel->setBrowser(m_fileBrowser);
    } else if (!active && m_auditionPanel->hasBrowser()) {
        m_auditionPanel->releaseBrowser();
        if (m_workspaceLayer) m_workspaceLayer->addChild(m_fileBrowser);
        m_fileBrowser->setVisible(true);
    }
}
