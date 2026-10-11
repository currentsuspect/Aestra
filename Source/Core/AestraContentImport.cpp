// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Loading a browser sample into the arrangement. Decoding a long file takes real time (and compacting
// it, see AudioBufferData::tryCompact), so it runs on a worker thread; the clip is placed on the main
// thread when it is ready. Kept out of AestraContent.cpp, which is at its size pin.
#include "AestraContent.h"

#include "../AestraCore/include/AestraLog.h"
#include "Commands/ClipPlacement.h"
#include "MiniAudioDecoder.h"
#include "TrackManagerUI.h"
#include "TransportBar.h"

#include <filesystem>
#include <thread>

using namespace Aestra;
using namespace Aestra::Audio;

void AestraContent::loadSampleIntoSelectedTrack(const std::string& filePath) {
    AESTRA_LOG_DEBUG("=== Loading sample into arrangement: " + filePath + " ===");

    stopSoundPreview();

    if (!m_trackManager) {
        AESTRA_LOG_ERROR("TrackManager not initialized");
        return;
    }

    auto& sourceManager = m_trackManager->getSourceManager();
    const ClipSourceID sourceId = sourceManager.getOrCreateSource(filePath);
    if (!sourceId.isValid()) {
        AESTRA_LOG_ERROR("Failed to load sample source: " + filePath);
        return;
    }

    ClipSource* source = sourceManager.getSource(sourceId);
    if (source && source->isReady()) {
        placeSampleOnTimeline(sourceId, filePath);
        return;
    }
    if (!source) {
        return;
    }

    // Not decoded yet. A second request for the same file while it decodes would only queue a second clip.
    if (!m_sampleImportsInFlight.insert(filePath).second) {
        return;
    }
    showToast("Importing " + std::filesystem::path(filePath).filename().string() + "...");

    auto weakSelf = weak_from_this();
    std::thread([weakSelf, sourceId, filePath]() {
        std::vector<float> decoded;
        uint32_t sampleRate = 0;
        uint32_t numChannels = 0;
        const bool ok = decodeAudioFile(filePath, decoded, sampleRate, numChannels) && numChannels > 0;

        std::shared_ptr<AudioBufferData> buffer;
        if (ok) {
            buffer = std::make_shared<AudioBufferData>();
            buffer->interleavedData = std::move(decoded);
            buffer->sampleRate = sampleRate;
            buffer->numChannels = numChannels;
            buffer->numFrames = buffer->interleavedData.size() / numChannels;
            if (numChannels <= 2) {
                buffer->tryCompact(); // not published yet, so it is safe here; setBuffer then finds it done
            }
        }

        auto self = std::dynamic_pointer_cast<AestraContent>(weakSelf.lock());
        if (!self) {
            return;
        }
        self->enqueueMainThreadTask([weakSelf, sourceId, filePath, buffer = std::move(buffer)]() mutable {
            auto self = std::dynamic_pointer_cast<AestraContent>(weakSelf.lock());
            if (!self) {
                return;
            }
            self->m_sampleImportsInFlight.erase(filePath);
            if (!self->m_trackManager) {
                return;
            }
            // The project may have changed while this decoded: the source must still be there.
            auto& sources = self->m_trackManager->getSourceManager();
            ClipSource* live = sources.getSource(sourceId);
            if (!live || live->getFilePath() != filePath) {
                return;
            }
            if (!buffer) {
                AESTRA_LOG_ERROR("Failed to decode or ready sample source: " + filePath);
                self->showToast("Could not import " + std::filesystem::path(filePath).filename().string());
                return;
            }
            // Canonical attach: bumps SourceManager's revision when this flips the source ready, so the
            // UI's waveform-cache sweep re-runs even though path dedupe meant no new source id was minted.
            sources.attachBuffer(live, std::move(buffer));
            self->placeSampleOnTimeline(sourceId, filePath);
        });
    }).detach();
}

void AestraContent::placeSampleOnTimeline(ClipSourceID sourceId, const std::string& filePath) {
    if (!m_trackManager) {
        return;
    }
    ClipSource* source = m_trackManager->getSourceManager().getSource(sourceId);
    if (!source || !source->isReady()) {
        AESTRA_LOG_ERROR("Failed to decode or ready sample source: " + filePath);
        return;
    }

    double durationSeconds = source->getDurationSeconds();
    double durationBeats = m_trackManager->getPlaylistModel().secondsToBeats(durationSeconds);

    auto& patternManager = m_trackManager->getPatternManager();
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = durationSeconds;
    AudioSlice slice;
    slice.startOffset = 0.0;
    slice.duration = durationSeconds;
    slice.startSamples = 0.0;
    slice.lengthSamples = static_cast<double>(source->getNumFrames());
    payload.slices.push_back(slice);

    std::string patternName = std::filesystem::path(filePath).filename().string();
    PatternID patternId = patternManager.createAudioPattern(patternName, durationBeats, payload);

    if (!patternId.isValid()) {
        AESTRA_LOG_ERROR("Failed to create pattern for sample");
        return;
    }

    PlaylistLaneID targetLaneId;
    if (m_trackManagerUI) {
        auto* selectedUI = m_trackManagerUI->getSelectedTrackUI();
        if (selectedUI) {
            targetLaneId = selectedUI->getLaneId();
        }
    }

    auto& playlist = m_trackManager->getPlaylistModel();
    if (!targetLaneId.isValid() && playlist.getLaneCount() > 0) {
        targetLaneId = playlist.getLaneId(0); // with no lanes, placement creates one and its Track
    }

    double playheadPositionSeconds = m_transportBar ? m_transportBar->getPosition() : 0.0;
    double startBeat = m_trackManager->getPlaylistModel().secondsToBeats(playheadPositionSeconds);

    Aestra::Audio::ClipInstance clip;
    clip.id = Aestra::Audio::ClipInstanceID::generate();
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    clip.durationSeconds = durationSeconds;
    clip.name = patternName;
    clip.edits = Aestra::Audio::ClipEdits::forNewAudioClip();
    // V8-W6: one undo step through the command history, so it is undoable, dirty and autosaved.
    if (!Aestra::Audio::placeClipAsOneUndoStep(*m_trackManager, targetLaneId, clip, "Sample Lane",
                                               "Add Sample Clip")) {
        patternManager.removePattern(patternId);
        AESTRA_LOG_ERROR("Failed to add sample clip to arrangement; removed orphan pattern");
        return;
    }

    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
        m_trackManagerUI->invalidateCache();
    }

    AESTRA_LOG_DEBUG("Sample loaded into arrangement via v3.0 architecture");
}

