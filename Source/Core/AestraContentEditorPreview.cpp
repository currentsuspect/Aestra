// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The Sample Editor's one-off actions (normalize, reverse) and both editors' Preview. Preview in the Sample Editor
// sounds the unit itself (AuditionUnit at its root note, for the trimmed sample's length), so trim, envelope and
// pitch are all heard; in the Audio Clip Editor it plays the clip's part of its source through the preview engine.
#include "../Panels/AudioClipEditorPanel.h"
#include "../Panels/SampleEditorPanel.h"
#include "AestraContent.h"
#include "AudioEngine.h"
#include "Plugin/SamplerPlugin.h"
#include "PreviewEngine.h"
#include "TrackManager.h"

void AestraContent::wireEditorActions() {
    if (!m_sampleEditorPanel || !m_audioClipEditorPanel)
        return; // called once both editors are built
    m_sampleEditorPanel->onNormalizeRequested = [this]() {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->normalizeSample();
    };
    m_sampleEditorPanel->onReverseRequested = [this]() {
        if (!m_trackManager || !m_sampleEditorUnitId) {
            return;
        }
        auto plugin = m_trackManager->getUnitManager().getUnitPlugin(m_sampleEditorUnitId);
        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(plugin);
        if (!sampler) {
            return;
        }
        sampler->reverseSample();
    };
    // Preview sounds the unit itself (AuditionUnit at its root note), so trim, envelope and pitch are all heard.
    m_sampleEditorPanel->onPreviewRequested = [this](bool start, double seconds) {
        if (!m_audioEngine || !m_sampleEditorUnitId)
            return;
        Aestra::Audio::AudioQueueCommand cmd;
        cmd.type = Aestra::Audio::AudioQueueCommandType::AuditionUnit;
        cmd.trackIndex = static_cast<uint32_t>(m_sampleEditorUnitId);
        if (!start) {
            cmd.value1 = -1.0f; // stop this unit's preview
        } else {
            cmd.value1 = static_cast<float>(m_sampleEditorPanel->getPitchTune().rootMidiNote);
            cmd.value2 = 100.0f / 127.0f;
            cmd.samplePos = static_cast<uint64_t>(seconds * static_cast<double>(m_audioEngine->getSampleRate()));
        }
        m_audioEngine->commandQueue().push(cmd);
    };
    // Clip preview: the clip's part of its source through the preview engine (the browser's), then seek to its start.
    m_audioClipEditorPanel->setOnPreviewRequested(
        [this](const std::string& path, double startSeconds, double lengthSeconds, float rate, float gainDb) {
            auto* preview = getPreviewEngine();
            if (!preview)
                return;
            if (path.empty()) {
                preview->stop();
                return;
            }
            preview->play(path, gainDb, lengthSeconds, rate);
            if (startSeconds > 0.0)
                preview->seek(startSeconds);
        });
}
