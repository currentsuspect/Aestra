// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// ProjectFixtureCorpusTest — checked-in project files are the compatibility
// contract. v1_rich.aes is a hand-authored v1 project exercising every
// serialized domain (routing/sends, channel state, clip edits, extended MIDI
// note fields, audio slices, automation curves, scale context) with values
// that need more than 6 significant digits.
//
// The fixture file must NEVER be regenerated: it pins what shipped. If this
// test breaks, the loader stopped reading real users' v1 projects correctly —
// fix the loader or add a migration, never the fixture (AGENTS.md §12,
// philosophy.md: "Session files must open correctly across versions").
//
// Complements ProjectLoadRegressionTest (v1_minimal.aes: skeleton + migration
// smoke) and ProjectValueFidelityTest (current-version roundtrip fidelity).

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Music/ScaleContext.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

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

// Expected values — these mirror the literals in v1_rich.aes. Float fields are
// written in the fixture as the exact decimal expansion of the float bit
// pattern, so float comparisons are exact after double->float narrowing.
constexpr double kTempo = 174.998877;
constexpr double kPlayhead = 2.345678901;
constexpr float kVolA = 0.333333343f;
constexpr float kPanA = -0.123456f;
constexpr float kWidthA = 1.23456f;
constexpr float kSendGain = 0.333333343f;
constexpr float kSendPan = 0.111111f;

// The full expected-value set for v1_rich.aes. Runs against the direct fixture
// load AND against a load of its re-save, so values must survive migration and
// a full write cycle.
void assertRichFixture(Aestra::Audio::TrackManager& tm, const ProjectSerializer::LoadResult& load,
                       const std::string& gen) {
    using namespace Aestra::Audio;
    auto tag = [&gen](const char* what) { return gen + ": " + what; };

    requireExactD(load.tempo, kTempo, tag("tempo"));
    requireExactD(load.playhead, kPlayhead, tag("playhead"));

    auto& playlist = tm.getPlaylistModel();
    const auto* laneA = playlist.getLane(playlist.getLaneId(0));
    const auto* laneB = playlist.getLane(playlist.getLaneId(1));
    require(laneA != nullptr && laneB != nullptr, tag("lanes missing"));
    if (laneA == nullptr || laneB == nullptr)
        return;

    require(laneA->name == "Rich Lane A", tag("lane A name"));
    requireExactF(laneA->volume, kVolA, tag("lane A volume"));
    requireExactF(laneA->pan, kPanA, tag("lane A pan"));
    require(laneA->muted, tag("lane A mute=true lost"));
    require(laneA->solo, tag("lane A solo=true lost"));
    require(laneA->colorRGBA == 4278253260u, tag("lane A color"));

    const auto* chanA = tm.getChannel(0);
    const auto* chanB = tm.getChannel(1);
    require(chanA != nullptr && chanB != nullptr, tag("channels missing"));
    if (chanA == nullptr || chanB == nullptr)
        return;
    requireExactF(chanA->getWidth(), kWidthA, tag("channel A width"));
    require(chanA->isArmed(), tag("channel A armed=true lost"));
    require(chanA->isSoloSafe(), tag("channel A soloSafe=true lost"));
    require(chanA->getTrackColorIndex() == 5, tag("channel A trackColorIndex"));
    require(chanA->getMainOutputId() == chanB->getChannelId(), tag("main output routes to lane B channel"));

    const auto sends = chanA->getSends();
    require(sends.size() == 1, tag("send count"));
    if (sends.size() == 1) {
        require(sends[0].targetChannelId == chanB->getChannelId(), tag("send target"));
        requireExactF(sends[0].gain, kSendGain, tag("send gain"));
        requireExactF(sends[0].pan, kSendPan, tag("send pan"));
        require(sends[0].postFader, tag("send postFader=true lost"));
        require(!sends[0].sidechainOnly, tag("send sidechainOnly"));
    }

    // MIDI clip deep in the arrangement.
    require(laneA->clips.size() == 1, tag("lane A clip count"));
    if (laneA->clips.size() == 1) {
        require(laneA->clips[0].name == "Rich MIDI Clip", tag("midi clip name"));
        requireExactD(laneA->clips[0].startBeat, 1024.125, tag("midi clip startBeat"));
        requireExactD(laneA->clips[0].durationBeats, 8.53125, tag("midi clip durationBeats"));
    }

    // Audio clip: seconds-domain fields + full edits.
    require(laneB->clips.size() == 1, tag("lane B clip count"));
    if (laneB->clips.size() == 1) {
        const auto& c = laneB->clips[0];
        requireExactD(c.startBeat, 512.0625, tag("audio clip startBeat"));
        requireExactD(c.durationSeconds, 3.14159265358979, tag("audio clip durationSeconds"));
        requireExactD(c.sourceOffsetSeconds, 0.123456789012345, tag("audio clip sourceOffsetSeconds"));
        requireExactF(c.edits.gainLinear, 0.987654f, tag("clip gain"));
        requireExactF(c.edits.pan, -0.054321f, tag("clip edit pan"));
        require(c.edits.muted, tag("clip muted=true lost"));
        requireExactF(c.edits.playbackRate, 1.5f, tag("clip playbackRate"));
        requireExactD(c.edits.fadeInBeats, 0.125, tag("clip fadeInBeats"));
        requireExactD(c.edits.fadeOutBeats, 0.0625, tag("clip fadeOutBeats"));
        requireExactF(c.edits.sourceStart, 0.75f, tag("clip sourceStart"));
    }

    // Patterns: MIDI notes (extended fields), scale context, audio slices.
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
        require(midiPat != nullptr, tag("MIDI pattern missing"));
        if (midiPat != nullptr) {
            require(midiPat->name == "Rich MIDI Pattern", tag("MIDI pattern name"));
            requireExactD(midiPat->lengthBeats, 16.0, tag("MIDI pattern length"));
            const auto& notes = std::get<MidiPayload>(midiPat->payload).notes;
            require(notes.size() == 2, tag("note count"));
            if (notes.size() == 2) {
                require(notes[0].pitch == 61, tag("note1 pitch"));
                requireExactD(notes[0].startBeat, 1024.125, tag("note1 startBeat"));
                requireExactD(notes[0].durationBeats, 0.4375, tag("note1 durationBeats"));
                requireExactF(notes[0].velocity, 0.87f, tag("note1 velocity"));
                require(notes[0].unitId == 7, tag("note1 unitId"));
                require(notes[0].pitchOffset == -3, tag("note1 pitchOffset"));
                requireExactF(notes[0].gate, 0.75f, tag("note1 gate"));
                require(notes[0].slide, tag("note1 slide=true lost"));
                require(notes[1].pitch == 127, tag("note2 pitch"));
                requireExactD(notes[1].startBeat, 1.0 / 3.0, tag("note2 startBeat"));
                requireExactD(notes[1].durationBeats, 1.0 / 7.0, tag("note2 durationBeats"));
                requireExactF(notes[1].velocity, 0.01f, tag("note2 velocity"));
            }
            require(midiPat->scaleOverride.has_value(), tag("scale context missing"));
            if (midiPat->scaleOverride.has_value()) {
                require(midiPat->scaleOverride->rootKey == 9, tag("scale rootKey"));
                require(midiPat->scaleOverride->scaleKind == ScaleKind::Minor, tag("scale kind"));
                require(midiPat->scaleOverride->snapToScale, tag("scale snap"));
            }
        }
        require(audioPat != nullptr, tag("audio pattern missing"));
        if (audioPat != nullptr) {
            const auto& slices = std::get<AudioSlicePayload>(audioPat->payload).slices;
            require(slices.size() == 1, tag("slice count"));
            if (slices.size() == 1) {
                requireExactD(slices[0].startSamples, 480.0625, tag("slice startSamples"));
                requireExactD(slices[0].lengthSamples, 1920.125, tag("slice lengthSamples"));
            }
        }
    }

    // Automation.
    require(laneA->automationCurves.size() == 1, tag("automation curve count"));
    if (laneA->automationCurves.size() == 1) {
        const auto& curve = laneA->automationCurves[0];
        require(curve.getAutomationTarget() == AutomationTarget::Volume, tag("automation target"));
        requireExactF(curve.getDefaultValue(), 0.42f, tag("automation default"));
        require(curve.points.size() == 2, tag("automation point count"));
        if (curve.points.size() == 2) {
            requireExactD(curve.points[0].beat, 0.5, tag("automation point 1 beat"));
            requireExactF(curve.points[0].value, 0.1f, tag("automation point 1 value"));
            requireExactF(curve.points[0].curve, 0.25f, tag("automation point 1 tension"));
            requireExactD(curve.points[1].beat, 1024.125, tag("automation point 2 beat"));
            requireExactF(curve.points[1].value, 0.9f, tag("automation point 2 value"));
            requireExactF(curve.points[1].curve, 0.5f, tag("automation point 2 tension"));
        }
    }
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const auto fixturePath = std::filesystem::path(AESTRA_PROJECT_FIXTURE_DIR) / "v1_rich.aes";
    if (!std::filesystem::exists(fixturePath)) {
        std::cerr << "[FAIL] fixture missing: " << fixturePath.string() << "\n";
        return 1;
    }

    // ---------------- Direct load of the immutable v1 fixture.
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());
    ProjectSerializer::LoadResult load1 = ProjectSerializer::load(fixturePath.string(), tm1);
    require(load1.ok, "v1_rich load failed: " + load1.errorMessage);
    if (!load1.ok)
        return 1;
    assertRichFixture(*tm1, load1, "v1-load");

    // ---------------- Migration proof: a re-save must be stamped current (v2).
    auto resave = ProjectSerializer::serialize(tm1, load1.tempo, load1.playhead, 2);
    require(resave.ok, "re-serialize failed");
    require(resave.contents.find("\"version\": 3") != std::string::npos ||
                resave.contents.find("\"version\":3") != std::string::npos,
            "re-save not stamped with current version (migration did not run)");

    // ---------------- The re-save must load with every value intact.
    const Aestra::Tests::ScopedTempDirectory tempDirScope{"ProjectFixtureCorpus"};
    const auto& tempDir = tempDirScope.path();
    const auto resavePath = tempDir / "v1_rich_resaved.aes";
    require(ProjectSerializer::writeAtomically(resavePath.string(), resave.contents), "re-save write failed");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    ProjectSerializer::LoadResult load2 = ProjectSerializer::load(resavePath.string(), tm2);
    require(load2.ok, "re-save load failed: " + load2.errorMessage);
    if (load2.ok) {
        assertRichFixture(*tm2, load2, "resave-load");
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] ProjectFixtureCorpusTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] ProjectFixtureCorpusTest\n";
    return 0;
}
