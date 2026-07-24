// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Commands/ArrangePatternCommand.h"
#include "Commands/MakeAudioClipUniqueCommand.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Commands/SetClipEditsCommand.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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
    units.setUnitMixerChannel(unitId, 42);

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

    TrackManager tracks;
    auto* restoredChannel = tracks.addChannelWithId("Restored", 42);
    require(restoredChannel && restoredChannel->getChannelId() == 42, "Persisted channel ID was not restored");
    auto* nextChannel = tracks.addChannel("Next");
    require(nextChannel && nextChannel->getChannelId() == 43, "Channel ID source did not advance past restored ID");
    TrackManager defaultNames;
    const auto* defaultInsert = defaultNames.addChannel();
    require(defaultInsert && defaultInsert->getName() == "Insert 1",
            "New mixer destinations must use Insert terminology");

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

    ClipInstance audioClip;
    audioClip.id = ClipInstanceID::generate();
    audioClip.patternId = audioPattern;
    audioClip.sourceId = audioPattern.value;
    audioClip.durationBeats = 1.0;
    audioClip.durationSeconds = buffer->durationSeconds();
    playlist.addClip(laneA, audioClip);

    ClipEdits edited = audioClip.edits;
    edited.gain = 0.5f;
    edited.gainLinear = 0.5f;
    edited.pan = -0.25f;
    edited.fadeInBeats = 0.25f;
    edited.fadeOutBeats = 0.5f;
    tracks.getCommandHistory().pushAndExecute(std::make_shared<SetClipEditsCommand>(playlist, audioClip.id, edited));

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

    require(tracks.getCommandHistory().undo(), "Clip edit undo was unavailable");
    graph = AudioGraphBuilder::buildFromTrackManager(tracks);
    require(findTrack(graph, 77)->clips.front().gain == 1.0f && findTrack(graph, 77)->clips.front().fadeInSamples == 0,
            "Undo did not restore clip instance properties");
    require(tracks.getCommandHistory().redo(), "Clip edit redo was unavailable");

    auto* alternateDestination = tracks.addChannelWithId("Alternate Insert", 88);
    require(alternateDestination != nullptr, "Alternate insert setup failed");
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
    playlist.removeClip(linkedClip.id);

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
