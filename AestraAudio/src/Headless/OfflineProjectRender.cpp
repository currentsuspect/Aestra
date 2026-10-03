// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Headless/OfflineProjectRender.h"

#include "Core/AudioGraphBuilder.h"
#include "Models/UnitManager.h"

#include <memory>

namespace Aestra {
namespace Audio {

AudioExporter::Result renderProjectOffline(AudioEngine& engine, TrackManager& trackManager,
                                           const OfflineRenderRequest& request) {
    // AudioExporter::render drives AudioEngine::processBlock -- the same path the
    // live engine uses -- so committed MIDI clips synthesize through their
    // sampler units (live/export parity, AGENTS.md §20).
    //
    // Restores everything this render mutates on every exit path, so a
    // synchronous export leaves the caller's engine and session unchanged:
    //  - engine wiring (weak TrackManager ref, raw UnitManager*, owned slot map,
    //    graph copy, pattern-engine pointer) is cleared;
    //  - the pattern-playback engine is cleared so the timeline instances this
    //    render scheduled do not leak into the caller's TrackManager.
    // The transport flags/position are never touched (see
    // scheduleTimelineForOfflineRender below), so there is nothing else to undo.
    struct RenderStateGuard {
        AudioEngine& engine;
        TrackManager& trackManager;
        ~RenderStateGuard() {
            engine.setGraph(AudioGraph{});
            engine.setPatternPlaybackEngine(nullptr);
            engine.setUnitManager(nullptr);
            engine.setChannelSlotMap(nullptr);
            engine.setTrackManager(nullptr);
            // Remove, don't rewind: this render's instances must not survive into the
            // caller's session (rewindScheduledInstances() would leave them scheduled and
            // merely restarted).
            trackManager.getPatternPlaybackEngine().clearScheduledInstances();
        }
    };

    // Non-owning shared_ptr: setTrackManager requires shared ownership, but the
    // caller owns trackManager and guarantees it outlives this render. The no-op
    // deleter means the borrowed TrackManager is never freed here.
    std::shared_ptr<TrackManager> borrowedTrackManager(&trackManager, [](TrackManager*) {});
    RenderStateGuard renderGuard{engine, trackManager}; // destroyed before borrowedTrackManager

    engine.setSampleRate(request.sampleRate);
    engine.setBufferConfig(512, 2);
    engine.setBPM(static_cast<float>(request.tempoBpm));
    engine.setTrackManager(borrowedTrackManager);
    engine.setUnitManager(&trackManager.getUnitManager());
    engine.setPatternPlaybackEngine(&trackManager.getPatternPlaybackEngine());
    trackManager.buildAndShareSlotMap();
    if (auto slotMap = trackManager.getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(trackManager));
    engine.initialize();

    // MIDI clips reach their units through the pattern-playback engine
    // (AudioEngine::processBlock pops scheduled notes into unit MIDI routes).
    // Schedule the committed timeline into it WITHOUT starting live transport --
    // the exporter drives the engine's own transport, so the caller's playing
    // flag and position are not mutated. clearScheduledInstances() first removes
    // any prior contents; the render guard clears again on exit so these
    // instances don't leak. rewindScheduledInstances() cannot serve here -- it
    // only REWINDS active instances, so anything already scheduled would have
    // been rendered into the export alongside the timeline asked for.
    trackManager.getPatternPlaybackEngine().clearScheduledInstances();
    trackManager.scheduleTimelineForOfflineRender(0.0);

    AudioExporter exporter(engine, trackManager);
    if (request.progress) {
        exporter.setProgressCallback(request.progress);
    }

    AudioExporter::Config config;
    config.outputPath = request.outputPath;
    config.sampleRate = request.sampleRate;
    config.bitDepth = request.bitDepth;
    config.scope = AudioExporter::RenderScope::FullSong;
    // Result::maxTruePeakdBTP is measured on every render; validateTruePeak only
    // turns it into a pass/fail against a ceiling, which is the caller's call.
    return exporter.render(config);
}

} // namespace Audio
} // namespace Aestra
