// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Commands/ArrangePatternCommand.h"
#include "Commands/AssignUnitToFirstFreeInsertCommand.h"
#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "Commands/SetUnitMixerChannelCommand.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    UnitManager units;
    const UnitID unitId = units.createUnit("Drums", UnitType::Sampler);
    require(units.getUnitMixerChannel(unitId) == MASTER_MIXER_CHANNEL_ID, "New units must route safely to Master");

    units.assignUnitToTimelineLane(unitId, 7);
    require(units.getUnitMixerChannel(unitId) == MASTER_MIXER_CHANNEL_ID,
            "Timeline placement must not change mixer output");

    units.setUnitMixerChannel(unitId, 42);
    require(units.getUnitMixerChannel(unitId) == 42, "Explicit mixer destination was not stored");
    auto snapshot = units.getAudioSnapshot();
    require(snapshot && snapshot->units.size() == 1, "Unit snapshot missing");
    require(snapshot->units.front().mixerChannelId == 42, "Mixer destination missing from audio snapshot");

    units.setUnitMixerChannel(unitId, static_cast<int64_t>(UINT32_MAX));
    require(units.getUnitMixerChannel(unitId) == MASTER_MIXER_CHANNEL_ID,
            "Reserved mixer destination must fall back to Master");
    units.setUnitMixerChannel(unitId, UINT32_MAX - 1);
    require(units.getUnitMixerChannel(unitId) == UINT32_MAX - 1,
            "High unsigned mixer destination was truncated or redirected");
    units.setUnitMixerChannel(unitId, 42);
    units.setUnitGain(unitId, std::numeric_limits<float>::quiet_NaN());
    snapshot = units.getAudioSnapshot();
    require(snapshot && std::isfinite(snapshot->units.front().gain) && snapshot->units.front().gain == 1.0f,
            "Non-finite unit gain reached the audio snapshot");

    const auto saved = units.saveToJSON();
    UnitManager restored;
    restored.loadFromJSON(saved);
    require(restored.getUnitMixerChannel(unitId) == 42, "Mixer destination did not round-trip");

    Aestra::JSON legacyRoot = Aestra::JSON::object();
    legacyRoot.set("nextId", Aestra::JSON(2.0));
    Aestra::JSON legacyUnits = Aestra::JSON::array();
    Aestra::JSON legacyUnit = Aestra::JSON::object();
    legacyUnit.set("id", Aestra::JSON(1.0));
    legacyUnit.set("name", Aestra::JSON("Legacy"));
    legacyUnit.set("targetMixerRoute", Aestra::JSON(1.0));
    legacyUnits.push(legacyUnit);
    legacyRoot.set("units", legacyUnits);

    UnitManager migrated;
    migrated.loadFromJSON(legacyRoot);
    migrated.migrateLegacyMixerRoutes({11, 42});
    require(migrated.getUnitMixerChannel(1) == 42,
            "Legacy lane-index destination did not migrate to the matching stable ID");

    legacyUnit.set("targetMixerChannelId", Aestra::JSON("malformed"));
    legacyUnits = Aestra::JSON::array();
    legacyUnits.push(legacyUnit);
    legacyRoot.set("units", legacyUnits);
    UnitManager malformedStableRoute;
    malformedStableRoute.loadFromJSON(legacyRoot);
    malformedStableRoute.migrateLegacyMixerRoutes({11, 42});
    require(malformedStableRoute.getUnitMixerChannel(1) == 42,
            "Malformed stable destination blocked valid legacy route migration");

    // TrackManager embeds large fixed-capacity RT queues. Keep the independent
    // fixtures off the smaller default Windows process stack.
    auto tracksOwner = std::make_unique<TrackManager>();
    auto& tracks = *tracksOwner;
    auto* restoredChannel = tracks.addChannelWithId("Restored", 42);
    require(restoredChannel && restoredChannel->getChannelId() == 42, "Persisted channel ID was not restored");
    auto* nextChannel = tracks.addChannel("Next");
    require(nextChannel && nextChannel->getChannelId() == 43, "Channel ID source did not advance past restored ID");
    auto defaultNamesOwner = std::make_unique<TrackManager>();
    auto& defaultNames = *defaultNamesOwner;
    const auto* defaultInsert = defaultNames.addChannel();
    require(defaultInsert && defaultInsert->getName() == "Channel 1",
            "New mixer destinations must use Channel terminology — 'Insert' is "
            "reserved for the effect slots that live on a channel");
    require(defaultNames.removeChannelById(defaultInsert->getChannelId()), "Default channel deletion failed");
    const auto* postDeleteInsert = defaultNames.addChannel();
    require(postDeleteInsert && postDeleteInsert->getName() == "Channel 2",
            "Default channel name reused a deleted channel number");

    auto firstFreeTracksOwner = std::make_unique<TrackManager>();
    auto& firstFreeTracks = *firstFreeTracksOwner;
    const UnitID firstFreeUnit = firstFreeTracks.getUnitManager().createUnit("Bass", UnitType::Instrument);
    firstFreeTracks.getCommandHistory().pushAndExecute(
        std::make_shared<AssignUnitToFirstFreeInsertCommand>(firstFreeTracks, firstFreeUnit, "Bass", 0xFF336699));
    require(firstFreeTracks.getChannelCount() == 1 && firstFreeTracks.getPlaylistModel().getLaneCount() == 1,
            "First-free route did not atomically create its insert and lane");
    const uint32_t createdDestinationId = firstFreeTracks.getUnitManager().getUnitMixerChannel(firstFreeUnit);
    const auto createdLaneId = firstFreeTracks.getPlaylistModel().getLaneId(0);
    require(createdDestinationId != MASTER_MIXER_CHANNEL_ID &&
                firstFreeTracks.getChannel(0)->getColor() == 0xFF336699 &&
                firstFreeTracks.getPlaylistModel().getLane(createdLaneId)->colorRGBA == 0xFF336699,
            "First-free route did not apply destination identity and color");
    require(firstFreeTracks.getCommandHistory().undo(), "First-free route undo was unavailable");
    require(firstFreeTracks.getChannelCount() == 0 && firstFreeTracks.getPlaylistModel().getLaneCount() == 0 &&
                firstFreeTracks.getUnitManager().getUnitMixerChannel(firstFreeUnit) == MASTER_MIXER_CHANNEL_ID,
            "First-free route undo left created project state behind");
    require(firstFreeTracks.getCommandHistory().redo(), "First-free route redo was unavailable");
    require(firstFreeTracks.getChannelCount() == 1 && firstFreeTracks.getPlaylistModel().getLaneCount() == 1 &&
                firstFreeTracks.getChannel(0)->getChannelId() == createdDestinationId &&
                firstFreeTracks.getPlaylistModel().getLaneId(0) == createdLaneId &&
                firstFreeTracks.getUnitManager().getUnitMixerChannel(firstFreeUnit) == createdDestinationId,
            "First-free route redo did not restore stable identities atomically");

    auto patternOccupiedTracksOwner = std::make_unique<TrackManager>();
    auto& patternOccupiedTracks = *patternOccupiedTracksOwner;
    const auto* patternInsert = patternOccupiedTracks.addChannel("Audio Source");
    const auto* availableInsert = patternOccupiedTracks.addChannel("Available");
    require(patternInsert && availableInsert, "Pattern-occupied route setup could not create inserts");
    const uint32_t patternInsertId = patternInsert->getChannelId();
    const uint32_t availableInsertId = availableInsert->getChannelId();
    AudioSlicePayload occupiedPayload;
    const PatternID occupiedPattern =
        patternOccupiedTracks.getPatternManager().createAudioPattern("Occupied", 4.0, occupiedPayload);
    require(patternOccupiedTracks.setAudioPatternMixerChannel(occupiedPattern, patternInsertId),
            "Pattern-occupied route setup could not route its audio source");
    const UnitID patternAwareUnit =
        patternOccupiedTracks.getUnitManager().createUnit("Pattern Aware", UnitType::Instrument);
    require(assignUnitToFirstFreeInsert(patternOccupiedTracks, patternAwareUnit, "Pattern Aware", 0xFF446688),
            "First-free routing failed with an existing pattern-owned insert");
    require(patternOccupiedTracks.getUnitManager().getUnitMixerChannel(patternAwareUnit) == availableInsertId,
            "First-free routing reused an insert owned by an audio pattern");
    require(patternOccupiedTracks.getChannelCount() == 2 &&
                patternOccupiedTracks.getPlaylistModel().getLaneCount() == 0,
            "First-free routing created project structure despite an available insert");

    auto missingRedoTracksOwner = std::make_unique<TrackManager>();
    auto& missingRedoTracks = *missingRedoTracksOwner;
    const auto* redoInsert = missingRedoTracks.addChannel("Redo");
    require(redoInsert, "Unit-route redo setup could not create an insert");
    const UnitID missingRedoUnit =
        missingRedoTracks.getUnitManager().createUnit("Transient", UnitType::Instrument);
    auto missingRedoCommand =
        std::make_shared<SetUnitMixerChannelCommand>(missingRedoTracks, missingRedoUnit, redoInsert->getChannelId());
    missingRedoCommand->execute();
    missingRedoCommand->undo();
    require(missingRedoTracks.getUnitManager().removeUnit(missingRedoUnit),
            "Unit-route redo setup could not remove its unit");
    missingRedoTracks.setModified(false);
    missingRedoCommand->redo();
    require(!missingRedoCommand->isUndoable() && !missingRedoTracks.isModified(),
            "Unit-route redo reported a project change after its unit was removed");

    auto shortcutTracksOwner = std::make_unique<TrackManager>();
    auto& shortcutTracks = *shortcutTracksOwner;
    const UnitID shortcutUnit = shortcutTracks.getUnitManager().createUnit("Shortcut", UnitType::Instrument);
    require(assignUnitToFirstFreeInsert(shortcutTracks, shortcutUnit, "Shortcut", 0xFF224466),
            "Ctrl+L routing helper did not handle a selected unit");
    require(shortcutTracks.getUnitManager().getUnitMixerChannel(shortcutUnit) != MASTER_MIXER_CHANNEL_ID,
            "Ctrl+L routing helper did not route the selected unit");
    const size_t shortcutChannelCount = shortcutTracks.getChannelCount();
    const size_t shortcutLaneCount = shortcutTracks.getPlaylistModel().getLaneCount();
    require(!assignUnitToFirstFreeInsert(shortcutTracks, 0, "Missing", 0xFFFFFFFF),
            "Ctrl+L routing helper handled a missing selection");
    require(!assignUnitToFirstFreeInsert(shortcutTracks, shortcutUnit + 999, "Missing", 0xFFFFFFFF),
            "Ctrl+L routing helper handled a failed unit destination");
    require(shortcutTracks.getChannelCount() == shortcutChannelCount &&
                shortcutTracks.getPlaylistModel().getLaneCount() == shortcutLaneCount,
            "Failed Ctrl+L routing changed project structure");

    auto& patternManager = tracks.getPatternManager();
    auto& trackUnits = tracks.getUnitManager();
    const UnitID arrangedUnit = trackUnits.createUnit("Lead", UnitType::Instrument);
    trackUnits.setUnitMixerChannel(arrangedUnit, 42);
    MidiPayload midi;
    midi.notes.push_back({60, 0.0, 1.0, 0.8f, 0.0f, arrangedUnit});
    const PatternID patternId = patternManager.createMidiPattern("Lead Pattern", 4.0, midi);

    ArrangePatternCommand arrange(tracks, patternId, 0, 0.0);
    arrange.execute();
    require(trackUnits.getUnitMixerChannel(arrangedUnit) == 42,
            "Arranging a pattern changed its unit mixer destination");
    require(trackUnits.getUnitTimelineLane(arrangedUnit) < 0,
            "Arranging a pattern created hidden timeline routing state");
    arrange.undo();
    require(trackUnits.getUnitMixerChannel(arrangedUnit) == 42,
            "Undoing arrangement changed its unit mixer destination");

    // Audio clips follow their source's stable destination, never their lane.
    auto* audioDestination = tracks.addChannelWithId("Audio Insert", 77);
    require(audioDestination && audioDestination->getChannelId() == 77, "Audio insert ID was not restored");
    auto& playlist = tracks.getPlaylistModel();
    const PlaylistLaneID laneA = playlist.createLane("Arrangement A");
    playlist.createLane("Spacer");
    const PlaylistLaneID laneB = playlist.createLane("Arrangement B");
    playlist.createLane("Extra lane without an insert");
    require(playlist.getLaneCount() != tracks.getChannelCount(), "Test setup accidentally retained lane/insert parity");

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = 44100;
    buffer->numChannels = 1;
    buffer->numFrames = 64;
    buffer->interleavedData.assign(64, 0.25f);
    const ClipSourceID sourceId =
        tracks.getSourceManager().createRecordedSource("unit-routing.wav", "Audio Source", buffer);
    require(sourceId.isValid(), "Audio source setup failed");

    AudioSlicePayload audioPayload;
    audioPayload.audioSourceId = sourceId;
    audioPayload.durationSeconds = buffer->durationSeconds();
    AudioSlice slice;
    slice.lengthSamples = static_cast<double>(buffer->numFrames);
    audioPayload.slices.push_back(slice);
    const PatternID audioPattern = patternManager.createAudioPattern("Audio Source", 1.0, audioPayload);
    require(patternManager.setPatternMixerChannel(audioPattern, 77), "Audio source route was not stored");

    const ClipInstanceID defaultAudioClip = playlist.addClipFromPattern(laneA, audioPattern, 4.0, 1.0);
    const auto* defaultAudioClipState = playlist.getClip(defaultAudioClip);
    require(defaultAudioClipState &&
                std::abs(defaultAudioClipState->edits.gainLinear - DEFAULT_AUDIO_CLIP_GAIN_LINEAR) < 1.0e-7f,
            "New audio clip factory did not preserve default headroom");
    playlist.removeClip(defaultAudioClip);

    ClipInstance audioClip;
    audioClip.id = ClipInstanceID::generate();
    audioClip.patternId = audioPattern;
    audioClip.sourceId = audioPattern.value;
    audioClip.durationBeats = 1.0;
    audioClip.durationSeconds = buffer->durationSeconds();
    playlist.addClip(laneA, audioClip);

    ClipEdits edited = audioClip.edits;
    edited.gainLinear = 0.5f;
    edited.pan = -0.25f;
    edited.fadeInBeats = 0.25f;
    edited.fadeOutBeats = 0.5f;
    edited.playbackRate = 1.5f;
    edited.sourceStart = 12.0;
    tracks.getCommandHistory().pushAndExecute(std::make_shared<SetClipEditsCommand>(playlist, audioClip.id, edited));

    auto* malformedOffsetClip = playlist.getClip(audioClip.id);
    require(malformedOffsetClip != nullptr, "Audio clip was unavailable for oversized offset coverage");
    malformedOffsetClip->sourceOffsetSeconds = std::numeric_limits<double>::max();
    ClipEdits oversizedOffsetEdits = edited;
    oversizedOffsetEdits.sourceStart = std::numeric_limits<double>::max();
    require(playlist.setClipEdits(audioClip.id, oversizedOffsetEdits), "Oversized finite clip offsets were rejected");
    const auto oversizedOffsetSnapshot =
        playlist.buildRuntimeSnapshot(tracks.getPatternManager(), tracks.getSourceManager());
    require(oversizedOffsetSnapshot && !oversizedOffsetSnapshot->lanes.empty() &&
                !oversizedOffsetSnapshot->lanes.front().clips.empty() &&
                oversizedOffsetSnapshot->lanes.front().clips.front().sourceStart ==
                    std::numeric_limits<uint64_t>::max(),
            "Oversized finite clip offsets did not saturate safely");
    malformedOffsetClip->sourceOffsetSeconds = 0.0;
    require(playlist.setClipEdits(audioClip.id, edited), "Finite clip edits were not restored after offset coverage");

    auto findTrack = [](AudioGraph& graph, uint32_t id) -> TrackRenderState* {
        auto it = std::find_if(graph.tracks.begin(), graph.tracks.end(),
                               [id](const TrackRenderState& state) { return state.trackId == id; });
        return it == graph.tracks.end() ? nullptr : &*it;
    };

    auto graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && findTrack(graph, 77)->clips.size() == 1,
            "Audio source did not reach its explicit mixer insert");
    require(graph.masterClips.empty(), "Explicitly routed audio source leaked to Master");
    const auto& renderedEdit = findTrack(graph, 77)->clips.front();
    require(renderedEdit.gain == 0.5f && renderedEdit.pan == -0.25f,
            "Clip editor gain/pan did not reach the audio graph");
    require(renderedEdit.fadeInSamples > 0 && renderedEdit.fadeOutSamples > renderedEdit.fadeInSamples,
            "Clip editor fades did not reach the audio graph");
    require(renderedEdit.playbackRate == 1.5f && renderedEdit.sampleOffset > 0.0,
            "Clip editor speed/source-start did not reach the audio graph");

    require(tracks.getCommandHistory().undo(), "Clip edit undo was unavailable");
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77)->clips.front().gain == 1.0f && findTrack(graph, 77)->clips.front().fadeInSamples == 0,
            "Undo did not restore clip instance properties");
    require(tracks.getCommandHistory().redo(), "Clip edit redo was unavailable");

    auto* alternateDestination = tracks.addChannelWithId("Alternate Insert", 88);
    require(alternateDestination != nullptr, "Alternate insert setup failed");
    AudioQueueCommand capturedMixerCommand{};
    alternateDestination->setCommandSink(
        [&capturedMixerCommand](const AudioQueueCommand& command) { capturedMixerCommand = command; });
    alternateDestination->setPan(0.25f);
    require(capturedMixerCommand.type == AudioQueueCommandType::SetTrackPan && capturedMixerCommand.channelId == 88,
            "Mixer control command used a derived dense index instead of its stable Insert ID");
    alternateDestination->setCommandSink({});
    alternateDestination->setSolo(true);
    alternateDestination->setMute(true);
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(alternateDestination->isSoloed() && alternateDestination->isMuted() && !graph.anySolo,
            "Muted mixer solo did not preserve state or incorrectly suppressed other inserts");
    alternateDestination->setMute(false);
    alternateDestination->setSolo(false);
    tracks.getCommandHistory().pushAndExecute(
        std::make_shared<SetAudioPatternMixerChannelCommand>(tracks, audioPattern, 88));
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 88) && findTrack(graph, 88)->clips.size() == 1,
            "GUI-equivalent source route command did not reroute linked audio");
    require(tracks.getCommandHistory().undo(), "Source route undo was unavailable");
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && findTrack(graph, 77)->clips.size() == 1,
            "Undo did not restore the source mixer destination");

    ClipInstance linkedClip = audioClip;
    linkedClip.id = ClipInstanceID::generate();
    linkedClip.startBeat = 2.0;
    playlist.addClip(laneB, linkedClip);

    auto* laneAState = playlist.getLane(laneA);
    auto* laneBState = playlist.getLane(laneB);
    require(laneAState && laneBState, "Solo-domain test lanes disappeared");
    laneAState->solo = true;
    laneBState->solo = true;
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && findTrack(graph, 77)->clips.size() == 2,
            "Additive Playlist solos did not keep both lanes audible");
    laneAState->muted = true;
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(laneAState->solo && findTrack(graph, 77) && findTrack(graph, 77)->clips.size() == 1,
            "Playlist mute erased solo state or failed to gate its own lane");
    laneAState->muted = false;
    laneAState->solo = false;
    laneBState->solo = false;

    tracks.getCommandHistory().pushAndExecute(std::make_shared<MakeAudioClipUniqueCommand>(tracks, audioClip.id));
    const auto* uniqueClip = playlist.getClip(audioClip.id);
    const auto* stillLinkedClip = playlist.getClip(linkedClip.id);
    require(uniqueClip && stillLinkedClip && uniqueClip->patternId != stillLinkedClip->patternId,
            "Make unique did not create an independent source identity");
    const auto uniquePatternId = uniqueClip->patternId;
    tracks.getCommandHistory().pushAndExecute(
        std::make_shared<SetAudioPatternMixerChannelCommand>(tracks, uniquePatternId, 88));
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 88) && findTrack(graph, 88)->clips.size() == 1 && findTrack(graph, 77) &&
                findTrack(graph, 77)->clips.size() == 1,
            "Unique clip routing changed another linked instance");
    require(tracks.getCommandHistory().undo(), "Unique source route undo was unavailable");
    require(tracks.getCommandHistory().undo(), "Make unique undo was unavailable");
    uniqueClip = playlist.getClip(audioClip.id);
    stillLinkedClip = playlist.getClip(linkedClip.id);
    require(uniqueClip && stillLinkedClip && uniqueClip->patternId == stillLinkedClip->patternId,
            "Make unique undo did not restore the shared source identity");
    require(tracks.getCommandHistory().redo(), "Make unique redo was unavailable");
    uniqueClip = playlist.getClip(audioClip.id);
    require(uniqueClip && uniqueClip->patternId == uniquePatternId,
            "Make unique redo did not restore the original unique source identity");
    require(patternManager.getPattern(uniquePatternId) &&
                patternManager.getPattern(uniquePatternId)->getMixerChannelId() == 77,
            "Make unique redo did not restore the unique source route");
    require(tracks.getCommandHistory().undo(), "Second make unique undo was unavailable");
    playlist.removeClip(linkedClip.id);

    ClipEdits poisonedEdits = audioClip.edits;
    poisonedEdits.gainLinear = std::numeric_limits<float>::quiet_NaN();
    poisonedEdits.pan = std::numeric_limits<float>::infinity();
    poisonedEdits.fadeInBeats = std::numeric_limits<float>::quiet_NaN();
    poisonedEdits.fadeOutBeats = std::numeric_limits<float>::infinity();
    require(playlist.setClipEdits(audioClip.id, poisonedEdits), "Failed to install non-finite clip edits");
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && !findTrack(graph, 77)->clips.empty(), "Poisoned clip vanished from graph");
    const auto& sanitizedClip = findTrack(graph, 77)->clips.front();
    require(std::isfinite(sanitizedClip.gain) && sanitizedClip.gain == 1.0f && std::isfinite(sanitizedClip.pan) &&
                sanitizedClip.pan == 0.0f && sanitizedClip.fadeInSamples == 0 && sanitizedClip.fadeOutSamples == 0,
            "Non-finite clip edits reached the render graph");
    require(playlist.setClipEdits(audioClip.id, audioClip.edits), "Failed to restore finite clip edits");

    playlist.moveClip(audioClip.id, 8.0, laneB);
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && findTrack(graph, 77)->clips.size() == 1,
            "Moving an audio clip between lanes changed its mixer destination");

    auto* mutedLane = playlist.getLane(laneB);
    require(mutedLane != nullptr, "Moved clip lane disappeared");
    mutedLane->muted = true;
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77) && findTrack(graph, 77)->clips.empty(),
            "Playlist lane mute did not suppress its clip independently");
    mutedLane->muted = false;

    patternManager.getPattern(audioPattern)->setMixerChannelId(9999);
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(graph.masterClips.size() == 1, "Missing audio destination did not fail safe to Master");

    patternManager.setPatternMixerChannel(audioPattern, 77);
    require(tracks.removeChannelById(77), "Mixer insert removal failed");
    require(patternManager.getPattern(audioPattern)->getMixerChannelId() == 0,
            "Deleting a mixer insert did not reset its audio sources to Master");
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(graph.masterClips.size() == 1, "Source reset after insert deletion was not audible on Master");

    std::cout << "[PASS] UnitMixerRoutingTest\n";
    return 0;
}
