// © 2025 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerUI — drop target (IDropTarget) and drop preview/delete animations.
// Split out of the former monolithic TrackManagerUI.cpp — bodies moved verbatim.
#include "TrackManagerUI.h"

#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "AudioFileValidator.h"
#include "ClipSource.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateLaneCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/SplitClipCommand.h"
#include "MiniAudioDecoder.h"
#include "MixerChannel.h"
#include "PluginManager.h"
#include "TrackManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>

#include "TrackManagerUIInternal.h"

namespace Aestra {
namespace Audio {

// =============================================================================
// Drop Target Implementation (IDropTarget)
// =============================================================================

AestraUI::DropFeedback TrackManagerUI::onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    Log::info("[TrackManagerUI] Drag entered");

    // Accept source content, patterns, and MIDI clips. Effects belong to the
    // mixer and never imply a Playlist-lane/mixer-insert association. (Timeline clip
    // moves are an internal mouse drag, not a DragData transfer — see
    // m_draggedClipId.)
    if (data.type != AestraUI::DragDataType::File && data.type != AestraUI::DragDataType::MidiClip &&
        data.type != AestraUI::DragDataType::Pattern) {
        return AestraUI::DropFeedback::Invalid;
    }

    // Early reject unsupported formats (cheap extension check; full validation happens on drop).
    if (data.type == AestraUI::DragDataType::File && !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        m_showDropPreview = false;
        setDirty(true);
        return AestraUI::DropFeedback::Invalid;
    }

    // Calculate target track and time
    m_dropTargetTrack = getTrackAtPosition(position.y);
    m_dropTargetTime = getTimeAtPosition(position.x);

    // Allow dropping on existing tracks OR appending a new track
    int trackCount = static_cast<int>(m_trackManager->getTrackCount());

    // If dragging below last track, target the next available slot
    if (m_dropTargetTrack >= trackCount) {
        m_dropTargetTrack = trackCount;
    }

    if (m_dropTargetTrack >= 0 && m_dropTargetTrack <= trackCount) {
        m_showDropPreview = true;
        setDirty(true);
        return AestraUI::DropFeedback::Copy;
    }

    return AestraUI::DropFeedback::Invalid;
}

AestraUI::DropFeedback TrackManagerUI::onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    // Same payload allowlist as onDragEnter. Without it, dragging an effect across
    // the timeline showed Copy feedback for the whole traverse — onDragEnter refused
    // it and onDrop refuses it, but the cursor said it would land.
    if (data.type != AestraUI::DragDataType::File && data.type != AestraUI::DragDataType::MidiClip &&
        data.type != AestraUI::DragDataType::Pattern) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
        }
        return AestraUI::DropFeedback::Invalid;
    }

    // Keep feedback "Invalid" for unsupported formats while hovering.
    if (data.type == AestraUI::DragDataType::File && !AudioFileValidator::hasValidAudioExtension(data.filePath)) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
        }
        return AestraUI::DropFeedback::Invalid;
    }

    // Update target track and time as mouse moves
    int newTrack = getTrackAtPosition(position.y);

    // Explicit mapping: Workspace -> Grid -> Beat
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    float controlWidth = theme.getLayoutDimensions().trackControlsWidth;
    float gridStartX = getBounds().x + controlWidth + kTimelineGridInsetX;

    // REJECTION: If dropping on the control area
    if (position.x < gridStartX) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
            Log::info("[TrackManagerUI] Drag over rejected: Cursor in control area");
        }
        return AestraUI::DropFeedback::Invalid;
    }

    double rawTimeBeats = gridOffsetToBeat(position.x - gridStartX);
    double snappedBeats = snapBeatToGrid(rawTimeBeats);
    double newTime = m_trackManager->getPlaylistModel().beatToSeconds(snappedBeats);

    int trackCount = static_cast<int>(m_trackManager->getTrackCount());

    // If dragging below last track, target the next available slot
    if (newTrack >= trackCount) {
        newTrack = trackCount;
    }

    // Only update if changed (performance optimization)
    if (newTrack != m_dropTargetTrack || std::abs(newTime - m_dropTargetTime) > 0.001) {
        m_dropTargetTrack = newTrack;
        m_dropTargetTime = std::max(0.0, newTime); // Don't allow negative time

        if (m_dropTargetTrack >= 0 && m_dropTargetTrack <= trackCount) {
            m_showDropPreview = true;
            setDirty(true);
            return AestraUI::DropFeedback::Copy;
        } else {
            m_showDropPreview = false;
            setDirty(true);
            return AestraUI::DropFeedback::Invalid;
        }
    }

    // Return appropriate feedback based on preview state
    if (m_showDropPreview) {
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::Invalid;
}

void TrackManagerUI::onDragLeave() {
    Log::info("[TrackManagerUI] Drag left");
    clearDropPreview();
    setDirty(true);
}

AestraUI::DropResult TrackManagerUI::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    AestraUI::DropResult result;

    // 1. Calculate drop location
    int laneIndex = getTrackAtPosition(position.y);
    double rawTimeSeconds = std::max(0.0, getTimeAtPosition(position.x));

    // v3.0: We work strictly in beats for arrangement
    double rawTimeBeats = m_trackManager->getPlaylistModel().secondsToBeats(rawTimeSeconds);

    // Snap-to-grid logic (Canonical Beat-Space)
    double timePositionBeats = snapBeatToGrid(rawTimeBeats);

    auto& playlist = m_trackManager->getPlaylistModel();
    size_t laneCount = playlist.getLaneCount();

    Log::info("[TrackManagerUI] onDrop: position.y=" + std::to_string(position.y) +
              ", laneIndex=" + std::to_string(laneIndex) + ", laneCount=" + std::to_string(laneCount));

    if (laneIndex < 0 || laneIndex > static_cast<int>(laneCount)) {
        result.accepted = false;
        result.message = "Invalid track position";
        clearDropPreview();
        return result;
    }

    // Set target track index for logging
    result.targetTrackIndex = laneIndex;

    // Reject unsupported payloads before appending a lane.
    if (data.type == AestraUI::DragDataType::Plugin) {
        result.accepted = false;
        result.message = "Drop effects on a mixer insert";
        clearDropPreview();
        return result;
    }
    if (data.type == AestraUI::DragDataType::MidiClip) {
        result.accepted = false;
        result.message = "MIDI track creation not yet implemented";
        clearDropPreview();
        return result;
    }
    if (data.type == AestraUI::DragDataType::File && !AudioFileValidator::isValidAudioFile(data.filePath)) {
        result.accepted = false;
        result.message = "Unsupported file format";
        clearDropPreview();
        return result;
    }

    // 2. Resolve target lane
    //
    // A drop that has to append a lane must undo as ONE step: the lane is part of
    // the edit, not scaffolding around it. Creating it through CreateLaneCommand
    // rather than calling playlist.createLane() directly is what lets it join the
    // clip in a single transaction — otherwise Ctrl+Z removes the clip and leaves
    // an empty orphan lane the user never asked for and cannot undo away.
    PlaylistLaneID targetLaneId;
    std::shared_ptr<CreateLaneCommand> laneCommand;
    if (laneIndex == static_cast<int>(laneCount)) {
        // When a new lane is created for a file drop, name it from the sample
        // rather than "Track N" — the moment a clip lands on a track, the track
        // should inherit the content name.  MIDI patterns keep the sequential
        // default until the user renames it.
        std::string laneName = "Track " + std::to_string(laneIndex + 1);
        if (data.type == AestraUI::DragDataType::File && !data.filePath.empty()) {
            namespace fs = std::filesystem;
            laneName = fs::path(data.filePath).stem().string();
        }
        laneCommand = std::make_shared<CreateLaneCommand>(playlist, laneName);
        laneCommand->execute();
        targetLaneId = laneCommand->getLaneId();
        if (!targetLaneId.isValid()) {
            laneCommand.reset();
            result.accepted = false;
            result.message = "Could not create track";
            clearDropPreview();
            return result;
        }
        // FD-14: a new lane belongs to a new Track (ownership by stable id).
        m_trackManager->createTrack(targetLaneId, laneName);

        Log::info("[TrackManagerUI] Created new lane " + std::to_string(laneIndex) + " for drop.");
    } else {
        targetLaneId = playlist.getLaneId(laneIndex);
    }

    // Undo the appended lane without recording anything: a drop that failed is not
    // history. Reused lanes are left alone.
    auto rollbackLane = [&laneCommand]() {
        if (laneCommand) {
            laneCommand->undo();
        }
    };

    // Adopt an already-executed clip command, plus the lane it needed, as one undo
    // step. The members were executed stepwise — each validated against the state
    // its predecessor produced — so the transaction is adopted with
    // markExecuted()/pushExecuted() rather than replayed by the history.
    auto commitDrop = [this, &laneCommand](const std::string& name,
                                           const std::shared_ptr<ICommand>& clipCommand) {
        auto transaction = std::make_shared<CommandTransaction>(name);
        if (laneCommand) {
            transaction->add(laneCommand);
        }
        transaction->add(clipCommand);
        transaction->markExecuted();
        return m_trackManager->getCommandHistory().pushExecuted(transaction);
    };

    // 3. Handle Pattern Drop
    if (data.type == AestraUI::DragDataType::Pattern) {
        PatternID pid;

        // Try to extract PatternID from customData
        try {
            if (data.customData.has_value()) {
                pid = std::any_cast<PatternID>(data.customData);
            }
        } catch (const std::bad_any_cast&) {
            Log::error("[TrackManagerUI] Failed to cast pattern ID from drag data");
        }

        if (pid.isValid()) {
            auto pattern = m_trackManager->getPatternManager().getPattern(pid);
            if (pattern) {
                double duration = pattern->lengthBeats;
                // Create clip instance manually and use command for undo support
                ClipInstance clip;
                clip.id = ClipInstanceID::generate();
                clip.startBeat = timePositionBeats;
                clip.durationBeats = duration;
                clip.patternId = pid;
                clip.sourceId = pid.value;
                if (pattern->isAudio()) {
                    clip.edits = ClipEdits::forNewAudioClip();
                }

                auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                cmd->execute();

                if (!playlist.getClip(clip.id)) {
                    rollbackLane();
                    result.accepted = false;
                    result.message = "Could not place pattern";
                    clearDropPreview();
                    return result;
                }

                commitDrop("Add Pattern Clip", cmd);

                result.accepted = true;
                result.message = "Pattern added: " + pattern->name;
                Log::info("[TrackManagerUI] Pattern added to timeline: " + pattern->name);

                refreshTracks();
                invalidateCache();
                scheduleTimelineMinimapRebuild();
            } else {
                rollbackLane();
                result.accepted = false;
                result.message = "Pattern not found";
            }
        } else {
            rollbackLane();
            result.accepted = false;
            result.message = "Invalid pattern ID";
        }
        clearDropPreview();
        return result;
    }

    // 4. Handle File Drop (New Audio Content)
    if (data.type == AestraUI::DragDataType::File) {
        Log::info("[TrackManagerUI] File drop received: " + data.filePath);

        // Register file with SourceManager
        auto& sourceManager = m_trackManager->getSourceManager();
        ClipSourceID sourceId = sourceManager.getOrCreateSource(data.filePath);
        ClipSource* source = sourceManager.getSource(sourceId);

        if (source) {
            std::weak_ptr<AestraUI::NUIComponent> weakSelf = weak_from_this();

            // Helper to create clip once source is ready. Async decode and queued tasks capture only weakSelf so
            // they cannot call back into a destroyed TrackManagerUI.
            //
            // Returns whether a clip was actually placed. The already-decoded branch
            // below calls this synchronously and used to report success no matter what
            // happened inside — a file whose duration decoded as zero, or whose pattern
            // could not be created, still told the user "Imported".
            auto createClipFromSource = [weakSelf, sourceId, displayName = data.displayName, targetLaneId,
                                         laneCommand, timePositionBeats]() -> bool {
                auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                if (!self || !self->m_trackManager)
                    return false;

                auto& sourceManager = self->m_trackManager->getSourceManager();
                ClipSource* source = sourceManager.getSource(sourceId);

                // Remove from pending imports (animation cleanup)
                auto it = std::remove_if(
                    self->m_pendingImports.begin(), self->m_pendingImports.end(), [&](const PendingImport& pi) {
                        return pi.laneId == targetLaneId && std::abs(pi.startBeat - timePositionBeats) < 0.001 &&
                               pi.displayName == displayName;
                    });
                if (it != self->m_pendingImports.end()) {
                    self->m_pendingImports.erase(it, self->m_pendingImports.end());
                    self->setDirty(true);
                }

                if (self->m_onClipLibraryChanged) {
                    self->m_onClipLibraryChanged();
                }

                auto rollbackCreatedLane = [&]() {
                    if (!laneCommand) {
                        return;
                    }
                    laneCommand->undo();
                    self->refreshTracks();
                    self->invalidateCache();
                    self->scheduleTimelineMinimapRebuild();
                };

                // Same one-undo-step contract as the synchronous paths: the lane the
                // import had to append belongs to the import, so it is adopted into the
                // transaction alongside the clip rather than left outside history.
                auto commitImport = [&](const std::shared_ptr<ICommand>& clipCommand) {
                    auto transaction = std::make_shared<CommandTransaction>("Import Audio Clip");
                    if (laneCommand) {
                        transaction->add(laneCommand);
                    }
                    transaction->add(clipCommand);
                    transaction->markExecuted();
                    self->m_trackManager->getCommandHistory().pushExecuted(transaction);
                };

                if (!source || !source->isReady()) {
                    rollbackCreatedLane();
                    return false;
                }

                double durationSeconds = source->getDurationSeconds();
                double durationBeats = self->secondsToBeats(durationSeconds);
                if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0 ||
                    !std::isfinite(durationBeats) || durationBeats <= 0.0) {
                    rollbackCreatedLane();
                    Log::error("[TrackManagerUI] Invalid decoded duration for imported source");
                    return false;
                }
                Log::info("[TrackManagerUI] Duration: " + std::to_string(durationSeconds) +
                          "s, beats: " + std::to_string(durationBeats));

                // Create Audio Pattern
                AudioSlicePayload payload;
                payload.audioSourceId = sourceId;
                payload.durationSeconds = durationSeconds;
                // Populate the sample-domain fields the serializer persists
                // (startSamples/lengthSamples). The old {0.0, numFrames} form set
                // startOffset/duration instead, so the slice saved as start:0 length:0.
                AudioSlice fullSlice;
                fullSlice.startSamples = 0.0;
                fullSlice.lengthSamples = static_cast<double>(source->getNumFrames());
                payload.slices.push_back(fullSlice);

                auto& patternManager = self->m_trackManager->getPatternManager();
                PatternID patternId = patternManager.createAudioPattern(displayName, durationBeats, payload);

                if (patternId.isValid()) {
                    auto& playlist = self->m_trackManager->getPlaylistModel();

                    // Create clip instance manually and use command for undo support
                    ClipInstance clip;
                    clip.id = ClipInstanceID::generate();
                    clip.startBeat = timePositionBeats;
                    clip.durationBeats = durationBeats;
                    clip.durationSeconds = durationSeconds;
                    clip.patternId = patternId;
                    clip.sourceId = patternId.value;
                    clip.edits = ClipEdits::forNewAudioClip();

                    auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                    cmd->execute();
                    if (!playlist.getClip(clip.id)) {
                        patternManager.removePattern(patternId);
                        rollbackCreatedLane();
                        Log::error("[TrackManagerUI] Failed to add imported clip to target lane; removed orphan audio pattern");
                        return false;
                    }

                    commitImport(cmd);

                    self->refreshTracks();
                    self->invalidateCache();
                    self->scheduleTimelineMinimapRebuild();
                    Log::info("[TrackManagerUI] Clip added successfully via command");
                    return true;
                }

                rollbackCreatedLane();
                Log::error("[TrackManagerUI] PatternManager::createAudioPattern failed");
                return false;
            };

            if (!source->isReady()) {
                Log::info("[TrackManagerUI] Decoding new source (ASYNC): " + data.filePath);

                // Add to pending imports for visualizer
                PendingImport pending;
                pending.displayName = data.displayName;
                pending.laneId = targetLaneId;
                pending.startBeat = timePositionBeats;
                m_pendingImports.push_back(pending);
                setDirty(true);

                // Capture necessary data for async thread
                std::string filePath = data.filePath;
                std::string displayName = data.displayName;
                std::shared_ptr<TrackManager> tm = m_trackManager;

                // Launch decoding in background thread to prevent UI freeze
                std::thread([weakSelf, tm, sourceId, filePath, displayName, createClipFromSource, targetLaneId,
                             timePositionBeats]() {
                    std::vector<float> decodedData;
                    uint32_t sampleRate = 0;
                    uint32_t numChannels = 0;

                    auto lastUpdate =
                        std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());

                    // Capture by value for the progress callback to avoid reference lifetime issues
                    auto progressCb = [weakSelf, targetLaneId, timePositionBeats, displayName, lastUpdate](float p) {
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastUpdate).count() < 16 &&
                            p < 1.0f)
                            return;
                        *lastUpdate = now;

                        auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                        if (!self)
                            return;

                        std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                        self->m_pendingTasks.push_back([weakSelf, targetLaneId, timePositionBeats, displayName, p]() {
                            auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                            if (!self)
                                return;

                            for (auto& pi : self->m_pendingImports) {
                                if (pi.laneId == targetLaneId && std::abs(pi.startBeat - timePositionBeats) < 0.001 &&
                                    pi.displayName == displayName) {
                                    pi.progress = p;
                                    self->setDirty(true);
                                    break;
                                }
                            }
                        });
                    };

                    // Heavy IO/Decoding operation
                    bool success = decodeAudioFile(filePath, decodedData, sampleRate, numChannels, progressCb);

                    // Post completion task to main thread
                    if (auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock())) {
                        std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                        self->m_pendingTasks.push_back([weakSelf, tm, sourceId, success,
                                                        decodedData = std::move(decodedData), sampleRate, numChannels,
                                                        displayName, createClipFromSource, filePath]() mutable {
                            auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                            if (!self)
                                return;

                            auto& sm = tm->getSourceManager();
                            ClipSource* src = sm.getSource(sourceId);
                            if (!src)
                                return;

                            if (success) {
                                auto buffer = std::make_shared<AudioBufferData>();
                                buffer->interleavedData = std::move(decodedData);
                                buffer->sampleRate = sampleRate;
                                buffer->numChannels = numChannels;
                                buffer->numFrames = buffer->interleavedData.size() / numChannels;
                                src->setBuffer(buffer);
                                const uint64_t sourceRevision = src->getContentRevision();

                                Log::info("[TrackManagerUI] Async load complete for: " + filePath);

                                // Trigger waveform cache build
                                self->m_waveformBuilder.buildAsync(
                                    *src, [weakSelf, sourceId,
                                           sourceRevision](std::shared_ptr<Aestra::Audio::WaveformCache> cache) {
                                        if (cache) {
                                            auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                                            if (!self)
                                                return;

                                            std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                                            self->m_pendingTasks.push_back([weakSelf, sourceId, sourceRevision, cache]() {
                                                auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                                                if (!self)
                                                    return;
                                                if (!self->m_trackManager)
                                                    return;

                                                auto* src = self->m_trackManager->getSourceManager().getSource(sourceId);
                                                if (!src)
                                                    return;
                                                if (src->getContentRevision() != sourceRevision)
                                                    return;

                                                src->setWaveformCache(cache);
                                                Log::info("Waveform cache ready for: " + src->getName());
                                                self->invalidateCache();
                                                self->m_backgroundNeedsUpdate = true;
                                                self->setDirty(true);
                                            });
                                        }
                                    });

                                // Queue clip creation to main thread via pending tasks
                                // This ensures UI updates happen on the main thread, not the background decode thread
                                std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                                self->m_pendingTasks.push_back([createClipFromSource]() { createClipFromSource(); });

                            } else {
                                Log::error("[TrackManagerUI] Failed to decode file async: " + filePath);
                                // Queue cleanup to main thread
                                std::lock_guard<std::mutex> lock(self->m_pendingTasksMutex);
                                self->m_pendingTasks.push_back([createClipFromSource]() {
                                    createClipFromSource(); // Cleanup UI
                                });
                            }
                        });
                    }
                }).detach();

                result.accepted = true;
                result.message = "Importing...";
            } else {
                // Already loaded, proceed immediately — and report what actually
                // happened. createClipFromSource rolls back the appended lane on every
                // failure path, so a false here means nothing was left behind either.
                if (createClipFromSource()) {
                    result.accepted = true;
                    result.message = "Imported: " + data.displayName;
                } else {
                    result.accepted = false;
                    result.message = "Could not import " + data.displayName;
                }
            }
        } else {
            rollbackLane();
            result.accepted = false;
            result.message = "Failed to create source";
        }

        clearDropPreview();
        return result;
    }

    result.accepted = false;
    result.message = "Unknown drop type";
    clearDropPreview();
    return result;
}

void TrackManagerUI::clearDropPreview() {
    m_showDropPreview = false;
    m_dropTargetTrack = -1;
    m_dropTargetTime = 0.0;
}

double TrackManagerUI::snapBeatToGrid(double beat) const {
    if (!m_snapEnabled || m_snapSetting == AestraUI::SnapGrid::None) {
        return beat;
    }

    double grid = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (grid <= 0.00001)
        return beat;

    // Round to nearest grid line
    double snappedBeats = std::round(beat / grid) * grid;

    return std::max(0.0, snappedBeats);
}

double TrackManagerUI::snapBeatToGridForward(double beat) const {
    if (!m_snapEnabled || m_snapSetting == AestraUI::SnapGrid::None) {
        return beat;
    }

    double grid = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (grid <= 0.00001)
        return beat;

    // Snap forward to next grid line
    double snappedBeats = std::floor(beat / grid) * grid + grid;

    return std::max(0.0, snappedBeats);
}

// =============================================================================
// Helper Methods for Drop Target
// =============================================================================

int TrackManagerUI::getTrackAtPosition(float y) const {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    // Get ruler height and track area start
    // MUST match renderTrackManagerDirect layout exactly:
    // header(38) + horizontalScrollbar(24) + ruler(28)
    float headerHeight = kTimelineHeaderHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    float rulerHeight = kTimelineRulerHeight;
    float trackAreaY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;

    // Relative Y position in track area
    float relativeY = y - trackAreaY + m_scrollOffset;

    if (relativeY < 0) {
        return -1; // Above track area
    }

    // Calculate track index based on track height + spacing
    int trackIndex = static_cast<int>(relativeY / (m_trackHeight + m_trackSpacing));

    return trackIndex;
}

double TrackManagerUI::getTimeAtPosition(float x) const {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    // Get control area width (where track buttons are)
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = controlAreaWidth + kTimelineGridInsetX;

    // Same conversion the drag and zoom paths use; this one then clamps and
    // converts to seconds, which is why it could not simply be called from them.
    const double beats = gridOffsetToBeat(x - bounds.x - gridStartX);

    if (beats < 0.0) {
        return 0.0; // Before grid start
    }

    // beats / beatsPerMinute * 60 = seconds
    double bpm = std::max(m_trackManager->getPlaylistModel().getBPM(), 1.0);
    double seconds = (beats / bpm) * 60.0;

    return seconds;
}

void TrackManagerUI::renderDropPreview(AestraUI::NUIRenderer& renderer) {
    if (!m_showDropPreview || m_dropTargetTrack < 0) {
        return;
    }

    // Issue #50: Don't show skeleton preview for existing clip drags
    // Instant clip drag provides real-time visual feedback by moving the actual clip
    if (m_isDraggingClipInstant) {
        return;
    }

    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    // Calculate grid area
    float controlAreaWidth = themeManager.getLayoutDimensions().trackControlsWidth;
    float gridStartX = bounds.x + controlAreaWidth + kTimelineGridInsetX;

    // Calculate track Y position - MUST match layoutTracks() calculation exactly
    // layoutTracks uses: headerHeight(38) + horizontalScrollbarHeight(24) + rulerHeight(28)
    float headerHeight = kTimelineHeaderHeight;
    float horizontalScrollbarHeight = kTimelineHorizontalScrollbarHeight;
    float rulerHeight = kTimelineRulerHeight;
    float trackAreaStartY = bounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
    float trackY = trackAreaStartY + (m_dropTargetTrack * (m_trackHeight + m_trackSpacing)) - m_scrollOffset;

    // Calculate X position from time
    double bpm = m_trackManager->getPlaylistModel().getBPM();
    double beats = (m_dropTargetTime * bpm) / 60.0;
    float timeX = gridStartX + static_cast<float>(beats * m_pixelsPerBeat) - m_timelineScrollOffset;

    // Draw subtle track highlight (just a hint)
    AestraUI::NUIRect trackHighlight(gridStartX, trackY, bounds.width - controlAreaWidth - 20,
                                     static_cast<float>(m_trackHeight));
    AestraUI::NUIColor highlightColor(0.733f, 0.525f, 0.988f, 0.08f); // Very subtle
    renderer.fillRect(trackHighlight, highlightColor);

    // Draw clip preview using the same visual language as placed timeline clips.
    if (timeX >= gridStartX && timeX <= bounds.right() - 20) {
        float previewWidth = 150.0f; // Reasonable preview width

        AestraUI::NUIRect clipPreview(timeX,
                                      trackY + 2, // Same as real clip: bounds.y + 2
                                      previewWidth,
                                      static_cast<float>(m_trackHeight) - 4 // Same as real clip: bounds.height - 4
        );

        const float clipRadius = 6.0f;
        const float labelBarHeight = 14.0f;
        const auto clipColor = themeManager.getColor("accentPrimary");
        const auto clipFill = clipColor.withAlpha(0.30f);
        const auto clipBorder = clipColor.lightened(0.12f).withAlpha(0.70f);
        renderer.fillRoundedRect(clipPreview, clipRadius, clipFill);
        renderer.strokeRoundedRect(clipPreview, clipRadius, 1.0f, clipBorder);
        renderer.fillRoundedRect(
            {clipPreview.x + 1.5f, clipPreview.y + 1.5f, 4.0f, std::max(0.0f, clipPreview.height - 3.0f)}, 2.0f,
            clipColor.lightened(0.18f).withAlpha(0.95f));

        const float headerH = std::min(labelBarHeight, std::max(0.0f, clipPreview.height - 2.0f));
        const AestraUI::NUIRect headerRect(clipPreview.x + 1.0f, clipPreview.y + 1.0f,
                                           std::max(0.0f, clipPreview.width - 2.0f), headerH);
        renderer.fillRoundedRect(headerRect, clipRadius - 1.5f, clipColor.withAlpha(0.50f));

        // Get drag data for display name
        auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
        if (dragManager.isDragging()) {
            const auto& dragData = dragManager.getDragData();
            std::string displayName = dragData.displayName;
            if (displayName.length() > 18) {
                displayName = displayName.substr(0, 15) + "...";
            }
            const float clipLabelFont = themeManager.getFontSize("xs");
            const float labelTextY = headerRect.y + std::max(0.0f, (headerRect.height - clipLabelFont) * 0.5f);
            AestraUI::NUIPoint textPos(clipPreview.x + 6.0f, labelTextY);
            renderer.drawText(displayName, textPos, clipLabelFont, AestraUI::NUIThemeManager::getInstance().getCurrentTheme().textPrimary);
        }
    }
}

void TrackManagerUI::renderDeleteAnimations(AestraUI::NUIRenderer& renderer) {
    if (m_deleteAnimations.empty()) {
        return;
    }

    // Update and render each animation
    auto it = m_deleteAnimations.begin();
    while (it != m_deleteAnimations.end()) {
        DeleteAnimation& anim = *it;

        // Update progress (assume ~60fps, so ~16ms per frame)
        anim.progress += (1.0f / 60.0f) / anim.duration;

        if (anim.progress >= 1.0f) {
            // Animation complete, remove it
            it = m_deleteAnimations.erase(it);
            continue;
        }

        // Subtle red ripple expanding from click point

        // Calculate ripple radius (smaller, more subtle)
        float maxRadius = 50.0f;
        float currentRadius = anim.progress * maxRadius;

        // Ripple alpha fades out as it expands
        float rippleAlpha = (1.0f - anim.progress) * 0.4f;

        // Draw single subtle expanding ring
        if (currentRadius > 0) {
            AestraUI::NUIColor ringColor(1.0f, 0.3f, 0.3f, rippleAlpha);

            // Draw a circle using lines
            const int segments = 24;
            for (int i = 0; i < segments; i++) {
                float angle1 = (float)i / segments * 2.0f * 3.14159f;
                float angle2 = (float)(i + 1) / segments * 2.0f * 3.14159f;

                AestraUI::NUIPoint p1(anim.rippleCenter.x + std::cos(angle1) * currentRadius,
                                      anim.rippleCenter.y + std::sin(angle1) * currentRadius);
                AestraUI::NUIPoint p2(anim.rippleCenter.x + std::cos(angle2) * currentRadius,
                                      anim.rippleCenter.y + std::sin(angle2) * currentRadius);

                renderer.drawLine(p1, p2, 1.5f, ringColor);
            }
        }

        // Force continuous redraw during animation
        invalidateCache();

        ++it;
    }
}

} // namespace Audio
} // namespace Aestra
