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

    // Accept file drops, plugins, patterns, and MIDI clips. (Timeline clip
    // moves are an internal mouse drag, not a DragData transfer — see
    // m_draggedClipId.)
    if (data.type != AestraUI::DragDataType::File && data.type != AestraUI::DragDataType::Plugin &&
        data.type != AestraUI::DragDataType::MidiClip && data.type != AestraUI::DragDataType::Pattern) {
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
    float gridStartX = getBounds().x + controlWidth + 5.0f;

    // REJECTION: If dropping on the control area
    if (position.x < gridStartX) {
        if (m_showDropPreview) {
            m_showDropPreview = false;
            setDirty(true);
            Log::info("[TrackManagerUI] Drag over rejected: Cursor in control area");
        }
        return AestraUI::DropFeedback::Invalid;
    }

    double gridX = position.x - gridStartX;
    double rawTimeBeats = (gridX + m_timelineScrollOffset) / m_pixelsPerBeat;
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
        result.message = "Invalid lane position";
        clearDropPreview();
        return result;
    }

    // Set target track index for logging
    result.targetTrackIndex = laneIndex;

    // 2. Resolve target lane
    PlaylistLaneID targetLaneId;
    bool createdTargetLane = false;
    uint32_t createdChannelId = 0;
    if (laneIndex == static_cast<int>(laneCount)) {
        // Create new lane if dropping at the end
        targetLaneId = playlist.createLane("Lane " + std::to_string(laneIndex + 1));
        createdTargetLane = targetLaneId.isValid();

        // Ensure we also have a mixer channel (we maintain 1:1 mapping for now)
        if (m_trackManager->getChannelCount() <= static_cast<size_t>(laneIndex)) {
            if (auto* channel =
                    m_trackManager->addChannel("Channel " + std::to_string(m_trackManager->getChannelCount() + 1))) {
                createdChannelId = channel->getChannelId();
            }
        }

        Log::info("[TrackManagerUI] Created new lane " + std::to_string(laneIndex) + " for drop.");
    } else {
        targetLaneId = playlist.getLaneId(laneIndex);
    }

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
                auto& unitManager = m_trackManager->getUnitManager();
                if (pattern->isMidi()) {
                    const auto& midi = std::get<MidiPayload>(pattern->payload);
                    std::unordered_set<UnitID> routedUnits;
                    for (const auto& note : midi.notes) {
                        if (note.unitId == 0 || routedUnits.find(note.unitId) != routedUnits.end()) {
                            continue;
                        }
                        if (auto* unit = unitManager.getUnit(note.unitId)) {
                            unitManager.assignUnitToTimelineLane(note.unitId, laneIndex);
                            Log::info("[TrackManagerUI] Routed note unit " + std::to_string(note.unitId) +
                                      " to timeline lane " + std::to_string(laneIndex));
                            routedUnits.insert(note.unitId);
                        }
                    }
                } else {
                    for (const auto unitId : unitManager.getAllUnitIDs()) {
                        if (auto* unit = unitManager.getUnit(unitId); unit && unit->defaultPatternId == pid) {
                            unitManager.assignUnitToTimelineLane(unitId, laneIndex);
                            Log::info("[TrackManagerUI] Routed owner unit " + std::to_string(unitId) +
                                      " to timeline lane " + std::to_string(laneIndex));
                            break;
                        }
                    }
                }
                // Create clip instance manually and use command for undo support
                ClipInstance clip;
                clip.id = ClipInstanceID::generate();
                clip.startBeat = timePositionBeats;
                clip.durationBeats = duration;
                clip.patternId = pid;
                clip.sourceId = pid.value;

                auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                m_trackManager->getCommandHistory().pushAndExecute(cmd);

                result.accepted = true;
                result.message = "Pattern added: " + pattern->name;
                Log::info("[TrackManagerUI] Pattern added to timeline: " + pattern->name);

                refreshTracks();
                invalidateCache();
                scheduleTimelineMinimapRebuild();
            } else {
                result.accepted = false;
                result.message = "Pattern not found";
            }
        } else {
            result.accepted = false;
            result.message = "Invalid pattern ID";
        }
        clearDropPreview();
        return result;
    }

    // 4. Handle File Drop (New Audio Content)
    if (data.type == AestraUI::DragDataType::File) {
        Log::info("[TrackManagerUI] File drop received: " + data.filePath);

        if (!AudioFileValidator::isValidAudioFile(data.filePath)) {
            result.accepted = false;
            result.message = "Unsupported file format";
            Log::warning("[TrackManagerUI] File rejected (validator): " + data.filePath);
            clearDropPreview();
            return result;
        }

        // Register file with SourceManager
        auto& sourceManager = m_trackManager->getSourceManager();
        ClipSourceID sourceId = sourceManager.getOrCreateSource(data.filePath);
        ClipSource* source = sourceManager.getSource(sourceId);

        if (source) {
            std::weak_ptr<AestraUI::NUIComponent> weakSelf = weak_from_this();

            // Helper to create clip once source is ready. Async decode and queued tasks capture only weakSelf so
            // they cannot call back into a destroyed TrackManagerUI.
            auto createClipFromSource = [weakSelf, sourceId, displayName = data.displayName, targetLaneId,
                                         createdTargetLane, createdChannelId, timePositionBeats]() {
                auto self = std::dynamic_pointer_cast<TrackManagerUI>(weakSelf.lock());
                if (!self || !self->m_trackManager)
                    return;

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

                auto rollbackCreatedTrack = [&]() {
                    if (!createdTargetLane) {
                        return;
                    }
                    if (createdChannelId != 0) {
                        self->m_trackManager->removeChannelById(createdChannelId);
                    }
                    self->m_trackManager->getPlaylistModel().removeLane(targetLaneId);
                    self->refreshTracks();
                    self->invalidateCache();
                    self->scheduleTimelineMinimapRebuild();
                };

                if (!source || !source->isReady()) {
                    rollbackCreatedTrack();
                    return;
                }

                double durationSeconds = source->getDurationSeconds();
                double durationBeats = self->secondsToBeats(durationSeconds);
                if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0 ||
                    !std::isfinite(durationBeats) || durationBeats <= 0.0) {
                    rollbackCreatedTrack();
                    Log::error("[TrackManagerUI] Invalid decoded duration for imported source");
                    return;
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

                    auto cmd = std::make_shared<AddClipCommand>(playlist, targetLaneId, clip);
                    self->m_trackManager->getCommandHistory().pushAndExecute(cmd);
                    if (!playlist.getClip(clip.id)) {
                        patternManager.removePattern(patternId);
                        rollbackCreatedTrack();
                        Log::error("[TrackManagerUI] Failed to add imported clip to target lane; removed orphan audio pattern");
                        return;
                    }

                    self->refreshTracks();
                    self->invalidateCache();
                    self->scheduleTimelineMinimapRebuild();
                    Log::info("[TrackManagerUI] Clip added successfully via command");
                } else {
                    rollbackCreatedTrack();
                    Log::error("[TrackManagerUI] PatternManager::createAudioPattern failed");
                }
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
                // Already loaded, proceed immediately
                createClipFromSource();
                result.accepted = true;
                result.message = "Imported: " + data.displayName;
            }
        } else {
            result.accepted = false;
            result.message = "Failed to create source";
        }

        clearDropPreview();
        return result;
    }

    // 5b. Handle MIDI Clip Drop (stub — full MIDI track creation is a future spec)
    if (data.type == AestraUI::DragDataType::MidiClip) {
        Log::info("[TrackManagerUI] MIDI drag received: " + data.filePath + " — MIDI track creation not yet implemented");
        result.accepted = false;
        result.message = "MIDI track creation not yet implemented";
        clearDropPreview();
        return result;
    }

    // 5. Handle Plugin Drop
    if (data.type == AestraUI::DragDataType::Plugin) {
        Log::info("[TrackManagerUI] Plugin drop received: " + data.displayName);

        std::string pluginId = data.sourceClipIdString;
        if (!pluginId.empty()) {
            auto& pluginManager = Aestra::Audio::PluginManager::getInstance();

            // Validate target channel exists
            int channelIndex = laneIndex;
            auto channel = m_trackManager->getTrack(channelIndex);

            if (!channel) {
                result.accepted = false;
                result.message = "Target channel not found";
                clearDropPreview();
                return result;
            }

            // Check if chain has space (pre-check)
            auto& chain = channel->getEffectChain();
            if (chain.getFirstEmptySlot() >= Aestra::Audio::EffectChain::MAX_SLOTS) {
                result.accepted = false;
                result.message = "Effect chain full";
                clearDropPreview();
                return result;
            }

            // Request Plugin Creation (Async)
            // Captured variables must be kept alive. m_trackManager is a shared_ptr.
            // NOTE: The callback runs on a worker thread (PluginManager's factory thread),
            // not the main/UI thread. We dispatch the actual plugin insertion to the main
            // thread via m_pendingTasks to ensure proper sequencing with graph rebuild.
            // This is documented in the plugin/effect-chain lifetime audit
            // (labs/memory/plugin_effect_lifetime_audit.md).
            std::string displayName = data.displayName;
            auto trackManager = m_trackManager;

            pluginManager.createInstanceByIdAsync(pluginId, [this, trackManager, channelIndex, displayName,
                                                             pluginId](Aestra::Audio::PluginInstancePtr instance) {
                if (!instance) {
                    Log::error("[TrackManagerUI] Plugin creation failed for ID: " + pluginId);
                    return;
                }

                std::lock_guard<std::mutex> lock(m_pendingTasksMutex);
                m_pendingTasks.push_back([trackManager, channelIndex, displayName, pluginId, instance]() {
                    auto channel = trackManager->getTrack(channelIndex);
                    if (!channel)
                        return;

                    auto& pluginManager = Aestra::Audio::PluginManager::getInstance();
                    if (instance->initialize(pluginManager.getDefaultSampleRate(),
                                             pluginManager.getDefaultBlockSize())) {
                        instance->activate();

                        auto& chain = channel->getEffectChain();
                        chain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
                        size_t slot = chain.getFirstEmptySlot();

                        if (slot < Aestra::Audio::EffectChain::MAX_SLOTS) {
                            chain.insertPlugin(slot, instance);
                            Log::info("[TrackManagerUI] Added plugin (Async): " + displayName);
                        } else {
                            Log::warning("[TrackManagerUI] Effect chain became full during async load");
                        }
                    } else {
                        Log::error("[TrackManagerUI] Plugin initialization failed for ID: " + pluginId);
                    }

                    // Request the same non-RT graph rebuild path used by timeline edits.
                    trackManager->requestAudioGraphRebuild(
                        Aestra::Audio::TrackManager::GraphDirtyReason::EffectChainChanged);
                });
            });

            // Return immediate acceptance
            result.accepted = true;
            result.message = "Loading " + data.displayName + "...";

        } else {
            result.accepted = false;
            result.message = "Invalid plugin ID";
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
    float gridStartX = controlAreaWidth + 5;

    // Relative X position in grid area
    float relativeX = x - bounds.x - gridStartX + m_timelineScrollOffset;

    if (relativeX < 0) {
        return 0.0; // Before grid start
    }

    // Convert pixels to beats, then to seconds
    // pixels / pixelsPerBeat = beats
    // beats / beatsPerMinute * 60 = seconds
    double beats = relativeX / m_pixelsPerBeat;
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
    float gridStartX = bounds.x + controlAreaWidth + 5;

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
