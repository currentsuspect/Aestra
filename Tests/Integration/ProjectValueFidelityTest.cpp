// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ProjectValueFidelityTest — numeric values must survive save → load EXACTLY,
// and stay stable across repeated save/load generations.
//
// Complements ProjectRoundTripTest (which checks a small value set at short,
// low-precision magnitudes) by asserting bit-exact roundtrips for every
// serialized numeric domain, deliberately using values that need more than 6
// significant digits and beat positions deep into a long arrangement:
//   - tempo / playhead
//   - lane volume/pan/mute/solo, channel width/armed/soloSafe/trackColorIndex
//   - clip start/duration(Seconds)/sourceOffset(Seconds) + all clip edits
//   - MIDI note pitch/start/duration/velocity/pitchOffset/gate/slide
//   - audio slice startSamples/lengthSamples
//   - automation curve target/default + point beat/value/curve tension
// The full assertion set runs against generation 2 (load of the original
// save) AND generation 3 (load of generation 2's save), so any value that
// only survives one cycle still fails.
//
// Guards three shipped bugs (2026-07): JSON doubles truncated to 6
// significant digits by default stream precision; AutomationCurve::addPoint
// dropping point beat/tension so every loaded or freshly drawn point re-saved
// at beat 0; and the slice loader filling startOffset/duration instead of the
// startSamples/lengthSamples fields the writer persists.
//
// NOT asserted here: byte-identical save->load->save output and identity
// stability — ProjectIdentityStabilityTest owns those (#446: the loader now
// restores clip/lane UUIDs and pattern/source numeric IDs, and the writer
// emits patterns/sources in sorted order).

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Core/AutomationCurve.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

// Minimal PCM 16-bit mono WAV writer (enough to satisfy SourceManager loading).
bool writeMinimalWavMono16(const std::filesystem::path& path, int sampleRate, int numSamples) {
    if (sampleRate <= 0 || numSamples <= 0)
        return false;
    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const int byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(numSamples * blockAlign);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    auto writeU32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    writeU32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16u);
    writeU16(1u); // PCM
    writeU16(static_cast<std::uint16_t>(numChannels));
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(byteRate));
    writeU16(static_cast<std::uint16_t>(blockAlign));
    writeU16(static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeU32(dataSize);
    std::vector<char> silence(dataSize, 0);
    out.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    return static_cast<bool>(out);
}

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

void requireExactD(double actual, double expected, const std::string& what) {
    if (actual != expected) {
        std::cerr.precision(17);
        std::cerr << "[FAIL] " << what << ": expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

void requireExactF(float actual, float expected, const std::string& what) {
    if (actual != expected) {
        std::cerr.precision(9);
        std::cerr << "[FAIL] " << what << ": expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

// Values chosen to be unrepresentable in 6 significant digits and to sit deep
// in a long arrangement (bar 256+), where the old formatter audibly moved clips.
constexpr double kTempo = 174.998877;
constexpr double kPlayhead = 2.345678901;
constexpr double kMidiClipStart = 1024.125;
constexpr double kMidiClipDuration = 8.53125;
constexpr double kAudioClipStart = 512.0625;
constexpr double kAudioClipDurationSeconds = 3.14159265358979;
constexpr double kAudioClipSourceOffsetSeconds = 0.123456789012345;
constexpr double kNoteStartFar = 1024.125;
constexpr double kNoteDurationOdd = 1.0 / 7.0;
constexpr double kAutoBeatFar = 1024.125;
constexpr double kSliceStartSamples = 480.0625;
constexpr double kSliceLengthSamples = 1920.125;
constexpr float kVolA = 1.0f / 3.0f;
constexpr float kPanA = -0.123456f;
constexpr float kWidthA = 1.23456f;
constexpr float kSendGain = 0.333333343f;
constexpr float kSendPan = 0.111111f;

// The full value-fidelity assertion set. Runs against every load generation so
// values that survive one save/load cycle but not two still fail.
// Note: ProjectSerializer currently lives in the global namespace (its header
// closes `namespace Aestra` before declaring the class) — see the #266 move.
void assertAllValues(Aestra::Audio::TrackManager& tm, const ProjectSerializer::LoadResult& load,
                     const std::string& gen) {
    using namespace Aestra::Audio;
    auto tag = [&gen](const char* what) { return gen + ": " + what; };

    requireExactD(load.tempo, kTempo, tag("tempo"));
    requireExactD(load.playhead, kPlayhead, tag("playhead"));

    auto& playlist = tm.getPlaylistModel();
    const auto* laneA = playlist.getLane(playlist.getLaneId(0));
    const auto* laneB = playlist.getLane(playlist.getLaneId(1));
    require(laneA != nullptr && laneB != nullptr, tag("lanes missing after load"));
    if (laneA == nullptr || laneB == nullptr)
        return;

    requireExactF(laneA->volume, kVolA, tag("lane A volume"));
    requireExactF(laneA->pan, kPanA, tag("lane A pan"));
    require(laneA->muted, tag("lane A muted=true lost"));
    require(laneA->solo, tag("lane A solo=true lost"));

    const auto* chanA = tm.getChannel(0);
    const auto* chanB = tm.getChannel(1);
    require(chanA != nullptr && chanB != nullptr, tag("channels missing after load"));
    if (chanA == nullptr || chanB == nullptr)
        return;
    requireExactF(chanA->getVolume(), kVolA, tag("channel A volume"));
    requireExactF(chanA->getPan(), kPanA, tag("channel A pan"));
    requireExactF(chanA->getWidth(), kWidthA, tag("channel A width"));
    require(chanA->isArmed(), tag("channel A armed=true lost"));
    require(chanA->isSoloSafe(), tag("channel A soloSafe=true lost"));
    require(chanA->getTrackColorIndex() == 5, tag("channel A trackColorIndex"));
    require(chanA->getMainOutputId() == chanB->getChannelId(), tag("main output id"));

    const auto sends = chanA->getSends();
    require(sends.size() == 1, tag("send count"));
    if (sends.size() == 1) {
        requireExactF(sends[0].gain, kSendGain, tag("send gain"));
        requireExactF(sends[0].pan, kSendPan, tag("send pan"));
        require(sends[0].postFader, tag("send postFader=true lost"));
    }

    // MIDI clip position/duration.
    require(laneA->clips.size() == 1, tag("lane A clip count"));
    if (laneA->clips.size() == 1) {
        requireExactD(laneA->clips[0].startBeat, kMidiClipStart, tag("midi clip startBeat"));
        requireExactD(laneA->clips[0].durationBeats, kMidiClipDuration, tag("midi clip durationBeats"));
    }

    // Audio clip seconds-domain values + edits.
    require(laneB->clips.size() == 1, tag("lane B clip count"));
    if (laneB->clips.size() == 1) {
        const auto& c = laneB->clips[0];
        requireExactD(c.startBeat, kAudioClipStart, tag("audio clip startBeat"));
        requireExactD(c.durationSeconds, kAudioClipDurationSeconds, tag("audio clip durationSeconds"));
        requireExactD(c.sourceOffsetSeconds, kAudioClipSourceOffsetSeconds, tag("audio clip sourceOffsetSeconds"));
        requireExactF(c.edits.gainLinear, 0.987654f, tag("clip gain"));
        requireExactF(c.edits.pan, -0.054321f, tag("clip edit pan"));
        require(c.edits.muted, tag("clip muted=true lost"));
        requireExactF(c.edits.playbackRate, 1.5f, tag("clip playbackRate"));
        requireExactD(c.edits.fadeInBeats, 0.125, tag("clip fadeInBeats"));
        requireExactD(c.edits.fadeOutBeats, 0.0625, tag("clip fadeOutBeats"));
        requireExactF(c.edits.sourceStart, 0.75f, tag("clip sourceStart"));
    }

    // Pattern payload values: MIDI notes and audio slices.
    {
        std::shared_ptr<PatternSource> midiPat;
        std::shared_ptr<PatternSource> audioPat;
        for (const auto& p : tm.getPatternManager().getAllPatterns()) {
            if (p == nullptr)
                continue;
            if (p->isAudio())
                audioPat = p;
            else
                midiPat = p;
        }
        require(midiPat != nullptr, tag("loaded MIDI pattern missing"));
        if (midiPat != nullptr) {
            const auto& notes = std::get<MidiPayload>(midiPat->payload).notes;
            require(notes.size() == 2, tag("note count"));
            if (notes.size() == 2) {
                requireExactD(notes[0].startBeat, kNoteStartFar, tag("note1 startBeat"));
                requireExactD(notes[0].durationBeats, 0.4375, tag("note1 durationBeats"));
                requireExactF(notes[0].velocity, 0.87f, tag("note1 velocity"));
                require(notes[0].pitch == 61, tag("note1 pitch"));
                require(notes[0].pitchOffset == -3, tag("note1 pitchOffset"));
                requireExactF(notes[0].gate, 0.75f, tag("note1 gate"));
                require(notes[0].slide, tag("note1 slide=true lost"));
                requireExactD(notes[1].startBeat, 1.0 / 3.0, tag("note2 startBeat"));
                requireExactD(notes[1].durationBeats, kNoteDurationOdd, tag("note2 durationBeats"));
                requireExactF(notes[1].velocity, 0.01f, tag("note2 velocity"));
                require(notes[1].pitch == 127, tag("note2 pitch"));
            }
        }
        require(audioPat != nullptr, tag("loaded audio pattern missing"));
        if (audioPat != nullptr) {
            const auto& slices = std::get<AudioSlicePayload>(audioPat->payload).slices;
            require(slices.size() == 1, tag("slice count"));
            if (slices.size() == 1) {
                requireExactD(slices[0].startSamples, kSliceStartSamples, tag("slice startSamples"));
                requireExactD(slices[0].lengthSamples, kSliceLengthSamples, tag("slice lengthSamples"));
            }
        }
    }

    // Automation curve values — the addPoint identity bug zeroed these.
    require(laneA->automationCurves.size() == 2, tag("automation curve count"));
    if (laneA->automationCurves.size() == 2) {
        const auto& curve = laneA->automationCurves[0];
        require(curve.getAutomationTarget() == AutomationTarget::Volume, tag("automation target"));
        requireExactF(curve.getDefaultValue(), 0.42f, tag("automation default"));
        require(curve.points.size() == 2, tag("automation point count"));
        if (curve.points.size() == 2) {
            requireExactD(curve.points[0].beat, 0.5, tag("automation point 1 beat"));
            requireExactF(curve.points[0].value, 0.1f, tag("automation point 1 value"));
            requireExactF(curve.points[0].curve, 0.25f, tag("automation point 1 tension"));
            requireExactD(curve.points[1].beat, kAutoBeatFar, tag("automation point 2 beat"));
            requireExactF(curve.points[1].value, 0.9f, tag("automation point 2 value"));
            requireExactF(curve.points[1].curve, 0.5f, tag("automation point 2 tension"));
        }

        // Plugin-parameter curve: Custom slot/paramId addressing (#467).
        const auto& param = laneA->automationCurves[1];
        require(param.getAutomationTarget() == AutomationTarget::Custom, tag("param curve target"));
        requireExactF(param.getDefaultValue(), 0.7f, tag("param curve default"));
        require(param.effectSlot == 3, tag("param curve effectSlot"));
        require(param.deviceInstanceId == 0x1000000000001ull, tag("param curve instanceId"));
        require(param.paramId == 17, tag("param curve paramId"));
        require(param.points.size() == 1, tag("param curve point count"));
        if (param.points.size() == 1) {
            requireExactD(param.points[0].beat, 1.0, tag("param curve point beat"));
            requireExactF(param.points[0].value, 0.33f, tag("param curve point value"));
        }
    }
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"ProjectValueFidelity"};
    const auto& tempDir = tempDirScope.path();
    const auto wavPath = tempDir / "fidelity.wav";
    const auto projectPath = tempDir / "fidelity.aes";
    std::cout << "[INFO] TempDir: " << tempDir.string() << "\n";
    if (!writeMinimalWavMono16(wavPath, 48000, 4800)) {
        std::cerr << "[FAIL] could not write test WAV\n";
        return 1;
    }

    // ---------------- Arrange: a project touching every numeric domain
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());
    auto& playlist1 = tm1->getPlaylistModel();
    playlist1.setBPM(kTempo);

    // Lane/channel A — MIDI content, automation, "true" toggle states.
    PlaylistLaneID laneAId = playlist1.createLane("Fidelity A");
    auto* chanA = tm1->addChannel("Fidelity A");
    // Lane/channel B — audio content, send target.
    PlaylistLaneID laneBId = playlist1.createLane("Fidelity B");
    auto* chanB = tm1->addChannel("Fidelity B");
    if (!laneAId.isValid() || !laneBId.isValid() || chanA == nullptr || chanB == nullptr) {
        std::cerr << "[FAIL] setup: lanes/channels\n";
        return 1;
    }

    if (auto* lane = playlist1.getLane(laneAId)) {
        lane->volume = kVolA;
        lane->pan = kPanA;
        lane->muted = true;
        lane->solo = true;
    }
    chanA->setVolume(kVolA);
    chanA->setPan(kPanA);
    chanA->setMute(true);
    chanA->setSolo(true);
    chanA->setWidth(kWidthA);
    chanA->setArmed(true);
    chanA->setSoloSafe(true);
    chanA->setTrackColorIndex(5);

    // MIDI pattern: extended note fields + >6-sig-digit positions.
    MidiPayload midi;
    MidiNote n1;
    n1.pitch = 61;
    n1.startBeat = kNoteStartFar;
    n1.durationBeats = 0.4375;
    n1.velocity = 0.87f;
    n1.pitchOffset = -3;
    n1.gate = 0.75f;
    n1.slide = true;
    midi.notes.push_back(n1);
    MidiNote n2;
    n2.pitch = 127;
    n2.startBeat = 1.0 / 3.0;
    n2.durationBeats = kNoteDurationOdd;
    n2.velocity = 0.01f;
    midi.notes.push_back(n2);
    PatternID midiPatId = tm1->getPatternManager().createMidiPattern("FidelityMelody", 16.0, midi);

    // Audio pattern + source, with explicit slice sample values (the writer
    // persists startSamples/lengthSamples — the loader used to drop them).
    ClipSourceID srcId = tm1->getSourceManager().getOrCreateSource(wavPath.string());
    AudioSlicePayload slicePayload;
    slicePayload.audioSourceId = srcId;
    AudioSlice slice;
    slice.startSamples = kSliceStartSamples;
    slice.lengthSamples = kSliceLengthSamples;
    slicePayload.slices.push_back(slice);
    PatternID audioPatId = tm1->getPatternManager().createAudioPattern("FidelityAudio", 4.0, slicePayload);
    if (midiPatId.value == 0 || srcId.value == 0 || audioPatId.value == 0) {
        std::cerr << "[FAIL] setup: patterns/source\n";
        return 1;
    }

    // Clips: MIDI clip far into the arrangement, audio clip with full edits.
    ClipInstanceID midiClipId = playlist1.addClipFromPattern(laneAId, midiPatId, kMidiClipStart, kMidiClipDuration);
    ClipInstanceID audioClipId = playlist1.addClipFromPattern(laneBId, audioPatId, kAudioClipStart, 4.0);
    if (!midiClipId.isValid() || !audioClipId.isValid()) {
        std::cerr << "[FAIL] setup: clips\n";
        return 1;
    }
    if (auto* clip = playlist1.getClip(audioClipId)) {
        clip->durationSeconds = kAudioClipDurationSeconds;
        clip->sourceOffsetSeconds = kAudioClipSourceOffsetSeconds;
        clip->edits.gainLinear = 0.987654f;
        clip->edits.pan = -0.054321f;
        clip->edits.muted = true;
        clip->edits.playbackRate = 1.5f;
        clip->edits.fadeInBeats = 0.125;
        clip->edits.fadeOutBeats = 0.0625;
        clip->edits.sourceStart = 0.75f;
    }

    // Automation on lane A through the same API the loader and UI use.
    {
        auto* lane = playlist1.getLane(laneAId);
        const double samplesPerBeat = (48000.0 * 60.0) / kTempo;
        AutomationCurve vol("volume", AutomationTarget::Volume);
        vol.setDefaultValue(0.42f);
        vol.addPoint(0.5, 0.1f, samplesPerBeat, 0.25f);
        vol.addPoint(kAutoBeatFar, 0.9f, samplesPerBeat, 0.5f);
        lane->automationCurves.push_back(vol);

        // Plugin-parameter curve: (instanceId, paramId) addressing must roundtrip.
        AutomationCurve param("cutoff", AutomationTarget::Custom);
        param.setDefaultValue(0.7f);
        param.effectSlot = 3; // legacy position retained for compat
        param.deviceInstanceId = 0x1000000000001ull; // beyond 2^48; exact identity
        param.paramId = 17;
        param.addPoint(1.0, 0.33f, samplesPerBeat, 0.5f);
        lane->automationCurves.push_back(param);
    }

    // Routing: A -> B send with awkward float gain.
    chanA->setMainOutputId(chanB->getChannelId());
    AudioRoute send{};
    send.targetChannelId = chanB->getChannelId();
    send.gain = kSendGain;
    send.pan = kSendPan;
    send.postFader = true;
    send.mute = false;
    send.sidechainOnly = false;
    chanA->addSend(send);

    // ---------------- Generation 2: save, load into a fresh manager, assert all values.
    require(ProjectSerializer::save(projectPath.string(), tm1, kTempo, kPlayhead), "save failed");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    ProjectSerializer::LoadResult load1 = ProjectSerializer::load(projectPath.string(), tm2);
    require(load1.ok, "generation-2 load failed");
    if (!load1.ok)
        return 1;
    assertAllValues(*tm2, load1, "gen2");

    // ---------------- Generation 3: save the loaded state, load it again, re-assert.
    // Catches values that survive one cycle but decay on the second (the shipped
    // automation and slice bugs were exactly this shape).
    auto ser2 = ProjectSerializer::serialize(tm2, load1.tempo, load1.playhead, 2);
    require(ser2.ok, "generation-3 serialize failed");
    const auto cyclePath = tempDir / "fidelity_gen3.aes";
    require(ProjectSerializer::writeAtomically(cyclePath.string(), ser2.contents), "generation-3 write failed");

    auto tm3 = std::make_shared<TrackManager>();
    tm3->getPlaylistModel().setPatternManager(&tm3->getPatternManager());
    ProjectSerializer::LoadResult load2 = ProjectSerializer::load(cyclePath.string(), tm3);
    require(load2.ok, "generation-3 load failed");
    if (load2.ok) {
        assertAllValues(*tm3, load2, "gen3");
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] ProjectValueFidelityTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] ProjectValueFidelityTest\n";
    return 0;
}
