// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Ownership contract — units, sampler audio and persistence (contract:ownership).
//
// Modes (argv[1]):
//   duplicate-no-notes         guards F12  §3.4/D2 duplicating a unit creates no notes and retargets none
//   sampler-shared-source      guards F23  §3.1 a sampler's audio is a Source owned by SourceManager
//   sampler-reverse-persist    guards F13  §3.4/D3 a reversed sample survives save -> reload; file untouched
//   sampler-normalize-persist  guards F13  §3.4/D3 a normalized sample survives save -> reload; file untouched
//   source-id-width            guards F20  §3.2 ClipSourceID is 64-bit and round-trips
//   roundtrip-stable           guards F25  §3.7 save -> load -> save is stable
//   sampler-params-persist     guards F26  §3.1/§3.7 every sampler parameter a unit owns survives save -> reload
//
// Expected outcomes are the contract (Aestra-Internals: Ownership & Reference
// Contract, rev 3), not the current behavior. Nothing here changes production code.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Contract/ContractSupport.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/CreateTrackWithLaneCommand.h"
#include "Commands/SplitClipCommand.h"
#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/SourceManager.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Plugin/PluginHost.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using AestraContract::contractSetup;
using AestraContract::str;
using AestraContract::Verdict;

namespace {

constexpr uint32_t kRate = 48000;

bool writeMonoWav16(const std::filesystem::path& path, const std::vector<float>& samples) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2);
    auto u32 = [&](uint32_t x) { out.write(reinterpret_cast<const char*>(&x), 4); };
    auto u16 = [&](uint16_t x) { out.write(reinterpret_cast<const char*>(&x), 2); };
    out.write("RIFF", 4);
    u32(36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(1);
    u32(kRate);
    u32(kRate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(dataSize);
    for (float s : samples) {
        const int16_t v = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    return out.good();
}

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::shared_ptr<Plugins::SamplerPlugin> samplerOf(TrackManager& tm, UnitID id) {
    return std::dynamic_pointer_cast<Plugins::SamplerPlugin>(tm.getUnitManager().getUnitPlugin(id));
}

// Render one note at the sampler's root and return |L|max|R| per frame. The
// attack is made instant for measurement only; call after any save.
std::vector<float> renderNote(Plugins::SamplerPlugin& sampler, uint32_t frames) {
    sampler.setEnvelope(0.001f, 2.0f, 1.0f, 0.5f);
    sampler.requestHardResetVoices();
    if (!sampler.isActive())
        sampler.activate();
    std::vector<float> env(frames, 0.0f);
    std::vector<float> l(256), r(256);
    float* ch[2] = {l.data(), r.data()};
    for (uint32_t start = 0; start < frames; start += 256) {
        const uint32_t n = std::min<uint32_t>(256, frames - start);
        MidiBuffer midi;
        if (start == 0) {
            const uint8_t on[3] = {0x90, static_cast<uint8_t>(sampler.getRootMidiNote()), 127};
            midi.addEvent(0, on, 3);
        }
        sampler.process(nullptr, ch, 0, 2, n, &midi, nullptr);
        for (uint32_t i = 0; i < n; ++i)
            env[start + i] = std::max(std::abs(l[i]), std::abs(r[i]));
    }
    return env;
}

float meanIn(const std::vector<float>& env, uint32_t from, uint32_t to) {
    double acc = 0.0;
    for (uint32_t i = from; i < to && i < env.size(); ++i)
        acc += env[i];
    return static_cast<float>(acc / std::max<uint32_t>(1, to - from));
}

float peakIn(const std::vector<float>& env, uint32_t from, uint32_t to) {
    float p = 0.0f;
    for (uint32_t i = from; i < to && i < env.size(); ++i)
        p = std::max(p, env[i]);
    return p;
}

// Mirror AestraContent's wiring: TrackManager alone does not hand UnitManager
// its PatternManager, so units would get no home pattern.
void wireLikeApp(TrackManager& tm) {
    tm.getPlaylistModel().setPatternManager(&tm.getPatternManager());
    tm.getUnitManager().setPatternManager(&tm.getPatternManager());
}

// Not named `near`: <windows.h> defines near as an empty macro.
bool withinRelative(float a, float b, float relTol) {
    return std::abs(a - b) <= relTol * std::max(std::abs(a), std::abs(b));
}

void initPlugins() {
    contractSetup(PluginManager::getInstance().initialize(), "PluginManager initialize failed");
}

UnitID makeSamplerUnit(TrackManager& tm, const std::string& name, const std::filesystem::path& wav) {
    auto& units = tm.getUnitManager();
    const UnitID id = units.createUnit(name, UnitType::Sampler);
    contractSetup(id != 0, "createUnit failed");
    units.setUnitEnabled(id, true);
    units.setUnitAudioClip(id, wav.string());
    contractSetup(samplerOf(tm, id) != nullptr, "unit has no SamplerPlugin after setUnitAudioClip");
    return id;
}

std::shared_ptr<TrackManager> saveAndReload(const std::shared_ptr<TrackManager>& tm,
                                            const std::filesystem::path& projectPath) {
    contractSetup(ProjectSerializer::save(projectPath.string(), tm, 120.0, 0.0), "ProjectSerializer::save failed");
    auto reloaded = std::make_shared<TrackManager>();
    wireLikeApp(*reloaded);
    const auto result = ProjectSerializer::load(projectPath.string(), reloaded);
    contractSetup(result.ok, "ProjectSerializer::load failed");
    return reloaded;
}

size_t notesForUnit(PatternManager& pm, UnitID unit) {
    size_t count = 0;
    for (const auto& p : pm.getAllPatterns()) {
        if (!p || !p->isMidi())
            continue;
        for (const auto& n : p->getMidiNotes()) {
            if (n.unitId == unit)
                ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------- F12
int duplicateNoNotes() {
    Verdict v("F12");
    TrackManager tm;
    wireLikeApp(tm);
    auto& units = tm.getUnitManager();
    auto& pm = tm.getPatternManager();
    const UnitID a = units.createUnit("Original", UnitType::Sampler);
    contractSetup(a != 0, "createUnit failed");
    const PatternID home = units.getUnit(a)->defaultPatternId;
    contractSetup(home.isValid(), "unit has no home pattern to hold notes");
    pm.applyPatch(home, [a](PatternSource& p) {
        p.getMidiNotes().push_back(MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, a});
        p.getMidiNotes().push_back(MidiNote{62, 1.0, 1.0, 0.8f, 0.0f, a});
    });
    const size_t notesForABefore = notesForUnit(pm, a);
    const size_t patternsBefore = pm.getAllPatterns().size();

    const UnitID dup = units.duplicateUnit(a);
    contractSetup(dup != 0 && dup != a, "duplicateUnit failed");

    v.check(notesForUnit(pm, a) == notesForABefore,
            "notes targeting the original unit unchanged (before=" + str(notesForABefore) +
                " after=" + str(notesForUnit(pm, a)) + ", patterns " + str(patternsBefore) + "->" +
                str(pm.getAllPatterns().size()) + ")");
    v.check(notesForUnit(pm, dup) == 0, "no notes target the duplicate");
    if (const auto* d = units.getUnit(dup); d && d->defaultPatternId.isValid()) {
        const auto* dupHome = pm.getPattern(d->defaultPatternId);
        const bool clean = !dupHome || !dupHome->isMidi() || dupHome->getMidiNotes().empty();
        v.check(clean, "the duplicate's home pattern holds no notes (it holds " +
                           str(dupHome && dupHome->isMidi() ? dupHome->getMidiNotes().size() : 0) + ")");
    }
    return v.finish();
}

// ---------------------------------------------------------------- F23
int samplerSharedSource() {
    Verdict v("F23");
    initPlugins();
    const Aestra::Tests::ScopedTempDirectory dir{"ContractSharedSource"};
    const auto wav = dir.path() / "shared.wav";
    std::vector<float> tone(kRate / 4);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.5f * std::sin(0.05f * static_cast<float>(i));
    contractSetup(writeMonoWav16(wav, tone), "write wav failed");

    TrackManager tm;
    makeSamplerUnit(tm, "Kick A", wav);
    makeSamplerUnit(tm, "Kick B", wav);
    const ClipSourceID owned = tm.getSourceManager().findSourceByPath(wav.string());
    v.check(owned.isValid(), "SourceManager owns a Source for a file used by two sampler units");
    return v.finish();
}

// ---------------------------------------------------------------- F13
int samplerDestructivePersist(bool reverse) {
    Verdict v("F13");
    initPlugins();
    const Aestra::Tests::ScopedTempDirectory dir{reverse ? "ContractReverse" : "ContractNormalize"};
    const auto wav = dir.path() / (reverse ? "ramp.wav" : "quiet.wav");
    std::vector<float> audio(kRate / 2);
    for (size_t i = 0; i < audio.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(audio.size());
        audio[i] = reverse ? 0.8f * t // rising ramp
                           : 0.2f * std::sin(6.2831853f * 220.0f * static_cast<float>(i) / kRate);
    }
    contractSetup(writeMonoWav16(wav, audio), "write wav failed");
    const std::string originalBytes = fileBytes(wav);

    auto tm = std::make_shared<TrackManager>();
    wireLikeApp(*tm);
    const UnitID unit = makeSamplerUnit(*tm, reverse ? "Reversed" : "Normalized", wav);
    auto sampler = samplerOf(*tm, unit);

    const uint32_t frames = kRate / 5;
    auto metric = [&](const std::vector<float>& env) {
        // Reverse: energy in the first 10..100 ms (a rising ramp is quiet there,
        // its reverse is loud). Normalize: peak level.
        return reverse ? meanIn(env, kRate / 100, kRate / 10) : peakIn(env, kRate / 100, frames);
    };
    const float untouched = metric(renderNote(*sampler, frames));
    contractSetup(reverse ? sampler->reverseSample() : sampler->normalizeSample(), "destructive edit failed");
    const float edited = metric(renderNote(*sampler, frames));
    contractSetup(edited > 2.0f * untouched, "the edit is not audible in memory; the probe cannot discriminate");

    // Undo is not measured here: no command exists yet (F21, structural check).
    auto reloaded = saveAndReload(tm, dir.path() / "project.aes");
    auto reloadedSampler = samplerOf(*reloaded, unit);
    contractSetup(reloadedSampler != nullptr, "reloaded unit has no SamplerPlugin");
    const float afterReload = metric(renderNote(*reloadedSampler, frames));

    v.check(withinRelative(afterReload, edited, 0.10f),
            std::string(reverse ? "reversed" : "normalized") + " sound survives save -> reload (untouched=" +
                str(untouched) + " edited=" + str(edited) + " reloaded=" + str(afterReload) + ")");
    v.check(fileBytes(wav) == originalBytes, "the original file on disk is unchanged");
    return v.finish();
}

// ---------------------------------------------------------------- F20
int sourceIdWidth() {
    Verdict v("F20");
    const Aestra::Tests::ScopedTempDirectory dir{"ContractSourceIdWidth"};
    const auto wav = dir.path() / "wide.wav";
    contractSetup(writeMonoWav16(wav, std::vector<float>(kRate / 10, 0.25f)), "write wav failed");

    auto tm = std::make_shared<TrackManager>();
    auto& pm = tm->getPatternManager();
    auto& playlist = tm->getPlaylistModel();
    playlist.setPatternManager(&pm);
    const ClipSourceID wide{(uint64_t{1} << 32) + 7};
    const ClipSourceID id = tm->getSourceManager().getOrCreateSourceWithId(wide, wav.string());
    contractSetup(id == wide, "could not mint a 64-bit source id");

    AudioSlicePayload payload;
    payload.audioSourceId = id;
    AudioSlice slice;
    slice.startSamples = 0.0;
    slice.lengthSamples = static_cast<double>(kRate / 10);
    payload.slices.push_back(slice);
    const PatternID pid = pm.createAudioPattern("Wide", 1.0, payload);
    const PlaylistLaneID lane = playlist.createLane("A");
    contractSetup(playlist.addClipFromPattern(lane, pid, 0.0, 1.0).isValid(), "addClipFromPattern failed");

    auto reloaded = saveAndReload(tm, dir.path() / "project.aes");
    const PatternSource* restored = nullptr;
    for (const auto& p : reloaded->getPatternManager().getAllPatterns()) {
        if (p && p->isAudio() && p->name == "Wide")
            restored = p.get();
    }
    contractSetup(restored != nullptr, "audio pattern missing after reload");
    const ClipSourceID restoredId = std::get<AudioSlicePayload>(restored->payload).audioSourceId;
    const ClipSource* src = reloaded->getSourceManager().getSource(restoredId);
    v.check(restoredId == wide, "region keeps source id " + str(wide.value) + " (got " + str(restoredId.value) + ")");
    // Compare as paths: the project stores generic separators ('/'), so on
    // Windows the reloaded string differs from wav.string() but names the same file.
    v.check(src != nullptr && std::filesystem::path(src->getFilePath()) == wav,
            "region's source id resolves to the same file");
    return v.finish();
}

// ---------------------------------------------------------------- §3.7
int roundtripStable() {
    Verdict v("F25");
    initPlugins();
    const Aestra::Tests::ScopedTempDirectory dir{"ContractRoundtrip"};
    const auto wav = dir.path() / "loop.wav";
    std::vector<float> tone(kRate);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.3f * std::sin(0.03f * static_cast<float>(i));
    contractSetup(writeMonoWav16(wav, tone), "write wav failed");

    auto tm = std::make_shared<TrackManager>();
    wireLikeApp(*tm);
    auto& pm = tm->getPatternManager();
    auto& playlist = tm->getPlaylistModel();
    playlist.setBPM(120.0);

    // Audio region + split clip with instance edits.
    const ClipSourceID src = tm->getSourceManager().getOrCreateSource(wav.string());
    contractSetup(src.isValid(), "source creation failed");
    AudioSlicePayload payload;
    payload.audioSourceId = src;
    payload.durationSeconds = 1.0;
    AudioSlice slice;
    slice.lengthSamples = static_cast<double>(kRate);
    payload.slices.push_back(slice);
    const PatternID region = pm.createAudioPattern("Loop", 2.0, payload);
    // Lanes are created the way the app creates them (track + lane), so the
    // fixture is a project shape the app can actually produce.
    auto newTrackLane = [&](const std::string& name) {
        CreateTrackWithLaneCommand create(*tm, name);
        create.execute();
        contractSetup(create.getLaneId().isValid(), "CreateTrackWithLaneCommand failed");
        return create.getLaneId();
    };
    const PlaylistLaneID lane = newTrackLane("Audio");
    const ClipInstanceID audioClip = playlist.addClipFromPattern(lane, region, 0.0, 2.0);
    contractSetup(audioClip.isValid(), "audio clip failed");
    ClipEdits edits = ClipEdits::forNewAudioClip();
    edits.pitchSemitones = 3.0f;
    edits.fadeInBeats = 0.25f;
    contractSetup(playlist.setClipEdits(audioClip, edits), "setClipEdits failed");
    SplitClipCommand split(playlist, audioClip, 1.0);
    split.execute();

    // Sampler unit with notes in its home pattern, and a MIDI clip of that pattern.
    const UnitID unit = makeSamplerUnit(*tm, "Keys", wav);
    const PatternID home = tm->getUnitManager().getUnit(unit)->defaultPatternId;
    contractSetup(home.isValid(), "unit has no home pattern");
    pm.applyPatch(home, [unit](PatternSource& p) {
        p.getMidiNotes().push_back(MidiNote{60, 0.0, 1.0, 0.8f, 0.0f, unit});
        p.getMidiNotes().push_back(MidiNote{67, 2.0, 0.5, 0.6f, -0.3f, unit});
    });
    const PlaylistLaneID midiLane = newTrackLane("Keys");
    contractSetup(playlist.addClipFromPattern(midiLane, home, 4.0, 8.0).isValid(), "midi clip failed");

    const auto first = ProjectSerializer::serialize(tm, 120.0, 0.0, 2);
    contractSetup(first.ok, "first serialize failed");
    const auto projectPath = dir.path() / "project.aes";
    contractSetup(ProjectSerializer::writeAtomically(projectPath.string(), first.contents), "write failed");
    auto reloaded = std::make_shared<TrackManager>();
    wireLikeApp(*reloaded);
    contractSetup(ProjectSerializer::load(projectPath.string(), reloaded).ok, "load failed");
    const auto second = ProjectSerializer::serialize(reloaded, 120.0, 0.0, 2);
    contractSetup(second.ok, "second serialize failed");
    // Keep the resaved text next to the first save for diffing when artifacts
    // are kept (AESTRA_KEEP_TEST_ARTIFACTS=1).
    contractSetup(ProjectSerializer::writeAtomically((dir.path() / "resaved.aes").string(), second.contents),
                  "write of the resaved project failed");

    // Report the first differing content line. The integrity "hash" line is a
    // checksum over the content, so it differs whenever anything else does; it
    // is skipped so the report names the field that actually changed.
    auto splitLines = [](const std::string& text) {
        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t nl = text.find('\n', pos);
            lines.push_back(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
        return lines;
    };
    const auto a = splitLines(first.contents);
    const auto b = splitLines(second.contents);
    std::string firstDiff = "identical";
    size_t contentDiffs = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const std::string la = i < a.size() ? a[i] : "<eof>";
        const std::string lb = i < b.size() ? b[i] : "<eof>";
        if (la == lb || (la.find("\"hash\"") != std::string::npos && lb.find("\"hash\"") != std::string::npos)) {
            continue;
        }
        if (contentDiffs++ == 0) {
            firstDiff = "first content difference at line " + str(i + 1) + ": '" + la + "' vs '" + lb + "'";
        }
    }
    if (contentDiffs > 0)
        firstDiff +=
            " (" + str(contentDiffs) + " differing lines; line counts " + str(a.size()) + " vs " + str(b.size()) + ")";
    v.check(first.contents == second.contents, "save -> load -> save is byte-stable (" + firstDiff + ")");
    return v.finish();
}

// ---------------------------------------------------------------- F26
// The unit owns all plugin parameters (§3.1); all of them survive reload (§3.7).
// Both unit types are covered because type defaults are applied at load time.
int samplerParamsPersist() {
    Verdict v("F26");
    initPlugins();
    const Aestra::Tests::ScopedTempDirectory dir{"ContractSamplerParams"};
    const auto wav = dir.path() / "tone.wav";
    std::vector<float> tone(kRate / 4);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.4f * std::sin(0.04f * static_cast<float>(i));
    contractSetup(writeMonoWav16(wav, tone), "write wav failed");

    struct Params {
        float a, d, s, r, coarse, fine, loopStart, loopEnd, glide;
        int root, voices;
        Plugins::SamplerPlugin::LoopMode loop;
        bool mono, cutSelf;
    };
    auto read = [](Plugins::SamplerPlugin& p) {
        return Params{p.getAttack(),          p.getDecay(),         p.getSustain(),       p.getRelease(),
                      p.getCoarseSemitones(), p.getFineTuneCents(), p.getLoopStartNorm(), p.getLoopEndNorm(),
                      p.getGlideTimeMs(),     p.getRootMidiNote(),  p.getMaxVoices(),     p.getLoopMode(),
                      p.isMonoMode(),         p.isCutSelfMode()};
    };
    auto describe = [](const Params& p) {
        return "A=" + str(p.a) + " D=" + str(p.d) + " S=" + str(p.s) + " R=" + str(p.r) + " coarse=" + str(p.coarse) +
               " fine=" + str(p.fine) + " loop=" + str(static_cast<int>(p.loop)) + "[" + str(p.loopStart) + "," +
               str(p.loopEnd) + "] root=" + str(p.root) + " voices=" + str(p.voices) + " mono=" + str(p.mono) +
               " cutSelf=" + str(p.cutSelf) + " glide=" + str(p.glide);
    };
    auto same = [](const Params& x, const Params& y) {
        auto eq = [](float a, float b) { return std::abs(a - b) <= 1e-4f; };
        return eq(x.a, y.a) && eq(x.d, y.d) && eq(x.s, y.s) && eq(x.r, y.r) && eq(x.coarse, y.coarse) &&
               eq(x.fine, y.fine) && eq(x.loopStart, y.loopStart) && eq(x.loopEnd, y.loopEnd) && eq(x.glide, y.glide) &&
               x.root == y.root && x.voices == y.voices && x.loop == y.loop && x.mono == y.mono &&
               x.cutSelf == y.cutSelf;
    };

    auto tm = std::make_shared<TrackManager>();
    wireLikeApp(*tm);
    const UnitID drum = makeSamplerUnit(*tm, "Drum", wav);
    const UnitID bass = makeSamplerUnit(*tm, "808", wav);
    contractSetup(tm->getUnitManager().setUnitType(bass, UnitType::PitchedSampler), "setUnitType failed");

    // Non-default values a user can set from the sample editor.
    auto dial = [](Plugins::SamplerPlugin& p, bool mono, float glide) {
        p.setEnvelope(0.25f, 0.4f, 0.5f, 1.25f);
        p.setCoarseSemitones(-5.0f);
        p.setFineTuneCents(12.0f);
        p.setSampleWindow(0.1f, 0.8f);
        p.setLoopMode(Plugins::SamplerPlugin::LoopMode::PingPong);
        p.setRootMidiNote(48);
        p.setMaxVoices(8);
        p.setMonoMode(mono);
        p.setCutSelfMode(true);
        p.setGlideTimeMs(glide);
    };
    dial(*samplerOf(*tm, drum), /*mono=*/true, 35.0f);   // Sampler type defaults to poly
    dial(*samplerOf(*tm, bass), /*mono=*/false, 220.0f); // PitchedSampler defaults to mono, 80 ms glide
    const Params drumSet = read(*samplerOf(*tm, drum));
    const Params bassSet = read(*samplerOf(*tm, bass));

    auto reloaded = saveAndReload(tm, dir.path() / "project.aes");
    auto drumBack = samplerOf(*reloaded, drum);
    auto bassBack = samplerOf(*reloaded, bass);
    contractSetup(drumBack && bassBack, "reloaded units have no SamplerPlugin");
    v.check(same(drumSet, read(*drumBack)),
            "Sampler unit: set {" + describe(drumSet) + "} reloaded {" + describe(read(*drumBack)) + "}");
    v.check(same(bassSet, read(*bassBack)),
            "808 unit: set {" + describe(bassSet) + "} reloaded {" + describe(read(*bassBack)) + "}");
    return v.finish();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = AestraContract::contractMode(argc, argv);
    if (mode == "duplicate-no-notes")
        return duplicateNoNotes();
    if (mode == "sampler-shared-source")
        return samplerSharedSource();
    if (mode == "sampler-reverse-persist")
        return samplerDestructivePersist(true);
    if (mode == "sampler-normalize-persist")
        return samplerDestructivePersist(false);
    if (mode == "source-id-width")
        return sourceIdWidth();
    if (mode == "roundtrip-stable")
        return roundtripStable();
    if (mode == "sampler-params-persist")
        return samplerParamsPersist();
    contractSetup(false, "unknown mode " + mode);
    return 2;
}
