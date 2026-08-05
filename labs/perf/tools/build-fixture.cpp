// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Folio baseline fixture builder.
//
// Constructs the ONE canonical performance-baseline session and writes it
// through ProjectSerializer::save(). Everything here is deterministic: fixed
// tempo, fixed sample rate, fixed clip placement, fixed parameter values, fixed
// source identities. Running this twice into two empty directories must produce
// byte-identical trees.
//
// WHY THE SOURCE PATHS ARE RELATIVE
//
// Two independent subsystems resolve asset paths, and they do NOT agree:
//
//   * Audio sources go through ProjectSerializer, which stores the path
//     verbatim and resolves a relative one against the PROJECT FILE's parent
//     directory (resolveProjectAssetPath).
//   * The MIDI instrument's sample rides inside SamplerPlugin::saveState, and
//     SamplerPlugin::loadState hard-REJECTS absolute paths
//     (isSafeSamplerStatePath) before resolving what is left against the
//     PROCESS CWD.
//
// A relative path is therefore the only form that satisfies both. It is also
// the only form that makes the .aes relocatable, which is what lets two
// generations into different directories hash identically. The CWD dependency
// is real and is declared in CAPABILITIES.json: anything that loads this
// fixture and expects the MIDI lanes to sound must run with CWD = fixture root.
//
// WHY NOT HeadlessMusicGenerator FOR PATTERNS/CLIPS
//
// Its pattern/note/clip calls only buffer into private members; the sole flush
// is exportTo(), which also writes a sampler tone to
// $TMPDIR/aestra_headless_tone_<heap-address>.wav and deletes it on the way
// out. That path varies per run and is absolute, so it would both break byte
// determinism and be rejected by isSafeSamplerStatePath on reload. We use the
// generator only for the calls that write straight through to the model
// (tempo, sample rate, channels, volume, pan) and build the arrangement with
// the direct model APIs.

#include "Core/ProjectSerializer.h"
#include "Headless/HeadlessMusicGenerator.h"
#include "Models/TrackManager.h"
#include "Plugin/AestraFilter.h"
#include "Plugin/AestraSat.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Plugin/PluginManager.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Fixture constants. Every one of these is load-bearing for determinism and is
// mirrored into CAPABILITIES.json so the validator checks the same numbers.
// ---------------------------------------------------------------------------

constexpr double kTempoBPM = 120.0;
constexpr double kSampleRate = 48000.0;

// 120 BPM => 2 beats/second. 256 beats = 128 s, comfortably past the 120 s
// floor without relying on the last clip's tail to get there.
constexpr double kArrangementBeats = 256.0;

// Asset durations, in seconds, fixed by generate-assets.py. Kept as constants
// rather than probed from the decoded buffers so the builder stays honest if an
// asset ever goes missing: a wrong duration must fail validation, not silently
// resize the arrangement.
constexpr double kKickSeconds = 0.320;    // 15360 frames @ 48k
constexpr double kChordSeconds = 2.000;   // 96000 frames @ 48k
constexpr double kTextureSeconds = 1.500; // 72000 frames @ 48k

// The single real slice: a strict sub-region of texture.wav. Both bounds are
// interior, so the slice is nontrivial in both directions (it neither starts at
// zero nor runs to the end).
constexpr double kSliceOffsetSeconds = 0.500;
constexpr double kSliceDurationSeconds = 0.750;

// Stable source identities. Minted explicitly so the saved file does not depend
// on registration order.
constexpr uint64_t kKickSourceId = 9001;
constexpr uint64_t kChordSourceId = 9002;
constexpr uint64_t kTextureSourceId = 9003;

constexpr float kSendGain = 0.35f;

// ---------------------------------------------------------------------------
// Deterministic identity minting.
//
// AestraUUID::generate() builds every id from a per-PROCESS salt
// (std::random_device XOR high_resolution_clock) in the high half plus a
// monotonic counter in the low half. The counter is reproducible; the salt is
// not. Left alone, every lane and clip id — and therefore the integrity hash
// derived from them — changes on each generation, which is precisely the thing
// a fixture tree hash must not do.
//
// So the fixture mints its own ids: a fixed high half, a monotonic low half.
// Uniqueness within the project is all the model requires of these values, and
// a constant salt still delivers it.
// ---------------------------------------------------------------------------
constexpr uint64_t kFixtureUuidSalt = 0xF0110BA5E11E0001ull;

AestraUUID nextFixtureUuid() {
    static uint64_t counter = 1;
    AestraUUID id;
    id.high = kFixtureUuidSalt;
    id.low = counter++;
    return id;
}

double beatsFromSeconds(double seconds) { return seconds * kTempoBPM / 60.0; }

void fail(const std::string& msg) {
    std::cerr << "[build-fixture] FAIL: " << msg << "\n";
    std::exit(1);
}

void require(bool cond, const std::string& msg) {
    if (!cond) {
        fail(msg);
    }
}

/// Place one audio clip. `startBeat` is arrangement position; the source window
/// is expressed in seconds because that is the canonical domain for audio clips
/// (PlaylistModel::buildRuntimeSnapshot prefers *Seconds whenever
/// durationSeconds > 0).
ClipInstanceID placeAudioClip(PlaylistModel& playlist, const PlaylistLaneID& laneId, PatternID patternId,
                              const std::string& name, double startBeat, double sourceOffsetSeconds,
                              double durationSeconds) {
    ClipInstance clip;
    clip.id = ClipInstanceID(nextFixtureUuid());
    clip.name = name;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = startBeat;
    clip.durationSeconds = durationSeconds;
    clip.durationBeats = beatsFromSeconds(durationSeconds);
    clip.sourceOffsetSeconds = sourceOffsetSeconds;
    clip.sourceOffset = beatsFromSeconds(sourceOffsetSeconds);
    // Unity clip gain: the fixture's gain story is told by channel volume and
    // the automation curve. Leaving the -5 dB new-clip default in would put a
    // second, silent scaling term in front of every probe.
    clip.edits.gainLinear = 1.0f;
    return playlist.addClip(laneId, clip);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string outDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        }
    }
    if (outDir.empty()) {
        std::cerr << "Usage: build-fixture --out <fixture-root>\n";
        return 1;
    }

    std::error_code ec;
    fs::create_directories(outDir, ec);
    require(!ec, "cannot create fixture root: " + outDir);

    // Anchor the process at the fixture root so that every relative path in
    // this builder — and, later, in the sampler's CWD-relative reload — means
    // the same thing.
    fs::current_path(outDir, ec);
    require(!ec, "cannot chdir to fixture root: " + outDir);

    const std::string kickRel = "assets/kick.wav";
    const std::string chordRel = "assets/chord.wav";
    const std::string textureRel = "assets/texture.wav";
    for (const auto& rel : {kickRel, chordRel, textureRel}) {
        require(fs::is_regular_file(rel), "missing generated asset: " + rel + " (run generate-assets.py first)");
    }

    // -----------------------------------------------------------------------
    // Engine-side setup
    // -----------------------------------------------------------------------
    BuiltInPlugins::registerCoreBuiltIns();
    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager failed to initialize");

    auto trackManager = std::make_shared<TrackManager>();
    auto& playlist = trackManager->getPlaylistModel();
    auto& patterns = trackManager->getPatternManager();
    auto& sources = trackManager->getSourceManager();
    auto& units = trackManager->getUnitManager();

    playlist.setPatternManager(&patterns);
    playlist.setProjectSampleRate(kSampleRate);
    playlist.setBPM(kTempoBPM);

    // -----------------------------------------------------------------------
    // Channels.
    //
    // Order matters. ProjectSerializer's legacy lanes[] block pairs lane N with
    // getChannel(N) POSITIONALLY on save, so every lane-backed channel is
    // created first and the send-return channel — which owns no lane — is
    // created last, where no lane can be paired to it. (That legacy block is
    // inert on load for any project carrying a mixerChannels[] array, which the
    // current serializer always writes, but the fixture should not depend on
    // that to stay coherent.)
    // -----------------------------------------------------------------------
    struct LaneSpec {
        const char* name;
        MixerChannel* channel = nullptr;
        PlaylistLaneID laneId;
    };

    std::vector<LaneSpec> lanes;
    for (const char* name : {"Kick", "Chord", "Texture", "Sub", "Lead", "Pad", "Texture Alt", "Chord Alt"}) {
        lanes.push_back(LaneSpec{name, nullptr, PlaylistLaneID{}});
    }

    for (auto& lane : lanes) {
        lane.channel = trackManager->addChannel(lane.name);
        require(lane.channel != nullptr, std::string("failed to create channel: ") + lane.name);
        lane.laneId = playlist.createLaneWithId(PlaylistLaneID(nextFixtureUuid()), lane.name);
        require(lane.laneId.isValid(), std::string("failed to create lane: ") + lane.name);
    }

    MixerChannel* returnChannel = trackManager->addChannel("Return");
    require(returnChannel != nullptr, "failed to create return channel");
    returnChannel->setMainOutputId(0xFFFFFFFFu); // Master
    returnChannel->setMute(false);
    returnChannel->setVolume(1.0f);

    // Named handles for the channels the topology actually reasons about.
    MixerChannel* chordChannel = lanes[1].channel;   // automation + Filter -> Sat
    MixerChannel* textureChannel = lanes[2].channel; // send -> return, plus the slice

    // Channel volumes: non-unity but never silent, so a per-channel mistake is
    // visible in a render rather than masked by unity gain everywhere.
    lanes[0].channel->setVolume(0.85f); // Kick
    chordChannel->setVolume(0.80f);
    textureChannel->setVolume(0.75f);
    lanes[3].channel->setVolume(0.70f); // Sub
    lanes[4].channel->setVolume(0.72f); // Lead
    lanes[5].channel->setVolume(0.68f); // Pad
    lanes[6].channel->setVolume(0.65f); // Texture Alt
    lanes[7].channel->setVolume(0.78f); // Chord Alt

    lanes[3].channel->setPan(-0.25f);
    lanes[6].channel->setPan(0.30f);
    lanes[7].channel->setPan(-0.15f);

    // -----------------------------------------------------------------------
    // Sources + audio patterns.
    //
    // An audio clip does NOT reach its source directly: the runtime snapshot
    // walks clip -> pattern -> AudioSlicePayload -> source, and takes the
    // clip's mixer destination from the PATTERN. So routing is set per pattern.
    // -----------------------------------------------------------------------
    const ClipSourceID kickSource = sources.getOrCreateSourceWithId(ClipSourceID{kKickSourceId}, kickRel, "Kick");
    const ClipSourceID chordSource = sources.getOrCreateSourceWithId(ClipSourceID{kChordSourceId}, chordRel, "Chord");
    const ClipSourceID textureSource =
        sources.getOrCreateSourceWithId(ClipSourceID{kTextureSourceId}, textureRel, "Texture");
    require(kickSource.isValid() && chordSource.isValid() && textureSource.isValid(), "source registration failed");

    auto makeAudioPattern = [&](const std::string& name, ClipSourceID source, double offsetSeconds,
                                double durationSeconds, uint32_t channelId) {
        AudioSlicePayload payload;
        payload.audioSourceId = source;
        payload.startOffsetSeconds = offsetSeconds;
        payload.durationSeconds = durationSeconds;
        const PatternID id = patterns.createAudioPattern(name, beatsFromSeconds(durationSeconds), payload);
        require(id.isValid(), "failed to create audio pattern: " + name);
        require(patterns.setPatternMixerChannel(id, channelId), "failed to route audio pattern: " + name);
        return id;
    };

    const PatternID kickPattern =
        makeAudioPattern("Kick", kickSource, 0.0, kKickSeconds, lanes[0].channel->getChannelId());
    const PatternID chordPattern =
        makeAudioPattern("Chord", chordSource, 0.0, kChordSeconds, chordChannel->getChannelId());
    const PatternID texturePattern =
        makeAudioPattern("Texture", textureSource, 0.0, kTextureSeconds, textureChannel->getChannelId());
    const PatternID subPattern =
        makeAudioPattern("Sub", kickSource, 0.0, kKickSeconds, lanes[3].channel->getChannelId());
    const PatternID textureAltPattern =
        makeAudioPattern("Texture Alt", textureSource, 0.0, kTextureSeconds, lanes[6].channel->getChannelId());
    const PatternID chordAltPattern =
        makeAudioPattern("Chord Alt", chordSource, 0.0, kChordSeconds, lanes[7].channel->getChannelId());

    // The slice: a strict interior sub-region of texture.wav, routed to the
    // same texture channel so it shares the send path.
    const PatternID slicePattern = makeAudioPattern("Texture Slice", textureSource, kSliceOffsetSeconds,
                                                    kSliceDurationSeconds, textureChannel->getChannelId());

    // -----------------------------------------------------------------------
    // Arrangement — repeated deterministic clips across the full 256 beats.
    // -----------------------------------------------------------------------
    int audioClipCount = 0;

    for (double beat = 0.0; beat < kArrangementBeats; beat += 1.0) {
        placeAudioClip(playlist, lanes[0].laneId, kickPattern, "Kick", beat, 0.0, kKickSeconds);
        ++audioClipCount;
    }
    for (double beat = 0.0; beat < kArrangementBeats; beat += 2.0) {
        placeAudioClip(playlist, lanes[3].laneId, subPattern, "Sub", beat + 0.5, 0.0, kKickSeconds);
        ++audioClipCount;
    }
    for (double beat = 0.0; beat < kArrangementBeats; beat += 4.0) {
        placeAudioClip(playlist, lanes[1].laneId, chordPattern, "Chord", beat, 0.0, kChordSeconds);
        ++audioClipCount;
    }
    for (double beat = 0.0; beat < kArrangementBeats; beat += 8.0) {
        placeAudioClip(playlist, lanes[2].laneId, texturePattern, "Texture", beat, 0.0, kTextureSeconds);
        ++audioClipCount;
    }
    for (double beat = 4.0; beat < kArrangementBeats; beat += 8.0) {
        placeAudioClip(playlist, lanes[6].laneId, textureAltPattern, "Texture Alt", beat, 0.0, kTextureSeconds);
        ++audioClipCount;
    }
    for (double beat = 2.0; beat < kArrangementBeats; beat += 8.0) {
        placeAudioClip(playlist, lanes[7].laneId, chordAltPattern, "Chord Alt", beat, 0.0, kChordSeconds);
        ++audioClipCount;
    }

    // Sliced clips ride the texture lane between the full-length hits.
    int sliceClipCount = 0;
    for (double beat = 6.0; beat < kArrangementBeats; beat += 8.0) {
        placeAudioClip(playlist, lanes[2].laneId, slicePattern, "Texture Slice", beat, kSliceOffsetSeconds,
                       kSliceDurationSeconds);
        ++sliceClipCount;
        ++audioClipCount;
    }
    require(sliceClipCount > 0, "no sliced clips were placed");

    // -----------------------------------------------------------------------
    // MIDI / pattern playback.
    //
    // Each MIDI lane gets a sampler unit whose sample is the committed
    // kick.wav — a real asset inside the fixture tree, stored relative so
    // SamplerPlugin::loadState accepts it (absolute paths are hard-rejected).
    // -----------------------------------------------------------------------
    auto makeMidiLane = [&](size_t laneIdx, const std::string& label, const std::vector<int>& degrees,
                            double stepBeats, int rootPitch) {
        auto sampler = pluginManager.createInstanceById("com.Aestrastudios.sampler");
        require(sampler != nullptr, "failed to create sampler for " + label);
        require(sampler->initialize(kSampleRate, 512), "failed to initialize sampler for " + label);
        sampler->activate();

        const UnitID unit = units.createUnit(label, UnitType::Sampler);
        units.attachPlugin(unit, "com.Aestrastudios.sampler", sampler);
        units.setUnitEnabled(unit, true);
        units.setUnitAudioClip(unit, kickRel);
        units.setUnitMixerChannel(unit, lanes[laneIdx].channel->getChannelId());
        units.assignUnitToTimelineLane(unit, static_cast<int>(laneIdx));

        // One bar of notes, repeated across the arrangement as clips.
        MidiPayload payload;
        for (size_t i = 0; i < degrees.size(); ++i) {
            MidiNote note;
            note.pitch = static_cast<uint8_t>(rootPitch + degrees[i]);
            note.startBeat = static_cast<double>(i) * stepBeats;
            note.durationBeats = stepBeats * 0.9;
            note.velocity = 0.78f;
            note.unitId = unit;
            payload.notes.push_back(note);
        }
        const double patternBeats = static_cast<double>(degrees.size()) * stepBeats;
        const PatternID pattern = patterns.createMidiPattern(label, patternBeats, payload);
        require(pattern.isValid(), "failed to create MIDI pattern: " + label);

        // Placed through addClip rather than addClipFromPattern: the latter
        // mints its own ClipInstanceID via the process-salted generator, and
        // there is no overload that accepts one. For a MIDI pattern its only
        // other work is copying the pattern id and span, which is reproduced
        // here exactly.
        int placed = 0;
        for (double beat = 0.0; beat < kArrangementBeats; beat += patternBeats) {
            ClipInstance clip;
            clip.id = ClipInstanceID(nextFixtureUuid());
            clip.name = label;
            clip.patternId = pattern;
            clip.sourceId = pattern.value;
            clip.startBeat = beat;
            clip.durationBeats = patternBeats;
            const ClipInstanceID id = playlist.addClip(lanes[laneIdx].laneId, clip);
            require(id.isValid(), "failed to place MIDI clip on " + label);
            ++placed;
        }
        // Capture the sampler's state (including its relative sample path) so
        // the instrument survives the round trip.
        units.captureUnitPluginState(unit);
        return placed;
    };

    const int leadClips = makeMidiLane(4, "Lead", {0, 3, 7, 10}, 1.0, 60);
    const int padClips = makeMidiLane(5, "Pad", {0, 5, 9}, 2.0, 48);
    require(leadClips > 0 && padClips > 0, "MIDI clips were not placed");

    // -----------------------------------------------------------------------
    // Volume automation on the CHORD lane only.
    //
    // Deliberately not on the texture/send lane: keeping automation, send
    // routing and effect processing on independent channels is what makes each
    // one separately falsifiable by a probe.
    // -----------------------------------------------------------------------
    {
        const double samplesPerBeat = (kSampleRate * 60.0) / kTempoBPM;
        AutomationCurve curve("Volume", AutomationTarget::Volume);
        curve.mixerChannelId = chordChannel->getChannelId();
        curve.defaultValue = 0.65f;
        curve.addPoint(0.0, 0.65f, samplesPerBeat);
        curve.addPoint(84.0, 0.90f, samplesPerBeat);
        curve.addPoint(168.0, 0.70f, samplesPerBeat);
        curve.addPoint(252.0, 0.85f, samplesPerBeat);

        auto* lane = playlist.getLane(lanes[1].laneId);
        require(lane != nullptr, "chord lane vanished before automation attach");
        lane->automationCurves.push_back(curve);
    }

    // -----------------------------------------------------------------------
    // Post-fader send: texture channel -> return channel.
    // -----------------------------------------------------------------------
    {
        AudioRoute route;
        route.targetChannelId = returnChannel->getChannelId();
        route.gain = kSendGain;
        route.pan = 0.0f;
        route.postFader = true;
        route.mute = false;
        route.sidechainOnly = false;
        textureChannel->addSend(route);
        // The direct path stays live: the send is additive colour, not a
        // replacement for the channel's own output.
        textureChannel->setMainOutputId(0xFFFFFFFFu);
    }

    // -----------------------------------------------------------------------
    // Effects on the chord channel: Filter (slot 0) -> Sat (slot 1).
    //
    // Built through the internal registry rather than by direct construction,
    // and given moderate but clearly non-default values. Filter envelope
    // modulation is pinned to its neutral centre (0.5 == off) so nothing in the
    // chain is signal-dependent in a way a probe could not reproduce.
    // -----------------------------------------------------------------------
    {
        auto& registry = InternalPluginRegistry::instance();
        auto& chain = chordChannel->getEffectChain();

        auto filter = registry.createInstance("com.Aestrastudios.filter");
        require(filter != nullptr, "registry failed to create Aestra Filter");
        require(filter->initialize(kSampleRate, 512), "failed to initialize Aestra Filter");
        filter->setParameter(Plugins::AestraFilter::kType, 0.0f);        // low-pass
        filter->setParameter(Plugins::AestraFilter::kCutoff, 0.55f);     // default 1.0
        filter->setParameter(Plugins::AestraFilter::kReso, 0.35f);       // default 0.116
        filter->setParameter(Plugins::AestraFilter::kDrive, 0.20f);      // default 0.0
        filter->setParameter(Plugins::AestraFilter::kEnvAmount, 0.5f);   // neutral: modulation off
        filter->setParameter(Plugins::AestraFilter::kMix, 1.0f);         // fully in circuit
        filter->setParameter(Plugins::AestraFilter::kBypass, 0.0f);      // not bypassed
        filter->activate();
        require(chain.insertPlugin(0, filter), "failed to insert Aestra Filter at slot 0");

        auto sat = registry.createInstance("com.Aestrastudios.sat");
        require(sat != nullptr, "registry failed to create Aestra Sat");
        require(sat->initialize(kSampleRate, 512), "failed to initialize Aestra Sat");
        sat->setParameter(Plugins::AestraSat::kDrive, 0.45f);  // default 0.25
        sat->setParameter(Plugins::AestraSat::kMode, 0.5f);    // Tube; default 0.0 (Tape)
        sat->setParameter(Plugins::AestraSat::kTone, 0.70f);   // default 1.0
        sat->setParameter(Plugins::AestraSat::kOutput, 0.5f);  // unity trim, deliberate
        sat->setParameter(Plugins::AestraSat::kMix, 1.0f);
        sat->setParameter(Plugins::AestraSat::kBypass, 0.0f);
        sat->activate();
        require(chain.insertPlugin(1, sat), "failed to insert Aestra Sat at slot 1");
    }

    // -----------------------------------------------------------------------
    // Save through the real serializer.
    // -----------------------------------------------------------------------
    const std::string projectPath = "folio-baseline.aes";
    require(ProjectSerializer::save(projectPath, trackManager, kTempoBPM, 0.0), "ProjectSerializer::save failed");

    // Drop the editor history snapshot that save() writes alongside the project.
    // Its filename embeds a wall-clock timestamp
    // (folio-baseline_save_<YYYYMMDD>_<HHMMSS>_<ms>_<n>.aes), so leaving it in
    // place would make the fixture tree hash differ on every generation.
    // setHistoryLimits() cannot suppress it — passing 0 entries is remapped to
    // the built-in default — so the directory is removed outright. It holds
    // nothing but a copy of the file we just wrote.
    {
        std::error_code rmEc;
        const fs::path historyDir = fs::path(projectPath).stem().string() + ".history";
        fs::remove_all(historyDir, rmEc);
        require(!rmEc, "failed to remove history directory: " + historyDir.string());
        require(!fs::exists(historyDir), "history directory survived removal");
    }

    const double totalBeats = playlist.getTotalDurationBeats();
    const double totalSeconds = totalBeats * 60.0 / kTempoBPM;
    require(totalSeconds >= 120.0, "arrangement is shorter than 120 s");

    std::cout << "[build-fixture] wrote " << projectPath << "\n"
              << "  lanes:        " << playlist.getLaneCount() << "\n"
              << "  channels:     " << trackManager->getChannelCount() << "\n"
              << "  audio clips:  " << audioClipCount << " (" << sliceClipCount << " sliced)\n"
              << "  midi clips:   " << (leadClips + padClips) << "\n"
              << "  arrangement:  " << totalBeats << " beats / " << totalSeconds << " s\n";
    return 0;
}
