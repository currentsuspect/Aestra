// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Characterization of the runtime clip render path — AestraDocs/specs/consolidate-audio-range.md §4b.
//
// This exists to be written BEFORE the clip-local kernel is extracted out of
// AudioEngine::processBlock, and to be sabotaged before it is trusted. It
// captures what the engine does today for the cases a consolidation renderer
// must reproduce: unity-rate, resampled, panned, faded, overlapping and short
// clips.
//
// Two kinds of assertion, deliberately separated:
//
//   * Semantic, tolerance-based — portable, safe for cross-platform CI.
//   * Determinism — rendering the same project twice must produce identical
//     bytes. Portable to assert, and it catches accumulation-order drift that
//     tolerance checks sail past.
//
// The exact per-case digests are PRINTED, not asserted: float output is not
// bit-portable across architectures. The refactor gate is to run this before
// the extraction, keep the digests, run it after, and require them unchanged
// within the same build and environment.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::Audio::AudioBufferData;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::ClipEdits;
using Aestra::Audio::ClipInstance;
using Aestra::Audio::ClipInstanceID;
using Aestra::Audio::ClipSourceID;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::PatternID;
using Aestra::Audio::PlaylistLaneID;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

/** Exactly one of these is set; both empty means semantic + determinism only. */
std::string g_recordDir;
std::string g_verifyDir;
/** Counts cases actually compared against a pre-existing baseline. */
int g_verifiedCases = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

/** FNV-1a over the raw sample bytes: a stable digest within one build. */
uint64_t digestOf(const std::vector<float>& data) {
    uint64_t h = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const size_t n = data.size() * sizeof(float);
    for (size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string request(const std::string& verb, const JSON& args) {
    JSON req = JSON::object();
    req.set("id", JSON(1.0));
    req.set("verb", JSON(verb));
    req.set("args", args);
    return req.toString();
}

/**
 * A source whose content makes render defects visible.
 *
 * Deliberately discontinuous: a step train, not a smooth tone. Smooth material
 * sits near zero at a clip boundary and would hide a missing or doubled edge
 * fade entirely (§7 of the spec). The two channels differ so a pan or a
 * channel swap cannot cancel out.
 */
std::shared_ptr<AudioBufferData> makeStepSource(uint32_t sampleRate, uint64_t frames) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = sampleRate;
    buffer->numChannels = 2;
    buffer->numFrames = frames;
    buffer->interleavedData.resize(static_cast<size_t>(frames) * 2);
    for (uint64_t i = 0; i < frames; ++i) {
        // Square-ish steps every 64 frames: hard edges everywhere.
        const float step = ((i / 64) % 2 == 0) ? 0.7f : -0.7f;
        buffer->interleavedData[i * 2] = step;
        buffer->interleavedData[i * 2 + 1] = step * 0.5f; // R differs from L
    }
    return buffer;
}

bool readFloatWav(const std::string& path, std::vector<float>& out, uint32_t& channels) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char riff[4], wave[4];
    uint32_t chunkSize = 0;
    if (std::fread(riff, 1, 4, f) != 4 || std::fread(&chunkSize, 4, 1, f) != 1 ||
        std::fread(wave, 1, 4, f) != 4 || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(wave, "WAVE", 4) != 0) {
        std::fclose(f);
        return false;
    }
    channels = 0;
    uint16_t bits = 0;
    while (true) {
        char id[4];
        uint32_t size = 0;
        if (std::fread(id, 1, 4, f) != 4 || std::fread(&size, 4, 1, f) != 1) break;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmt = 0, ch = 0, block = 0;
            uint32_t rate = 0, byteRate = 0;
            std::fread(&fmt, 2, 1, f);
            std::fread(&ch, 2, 1, f);
            std::fread(&rate, 4, 1, f);
            std::fread(&byteRate, 4, 1, f);
            std::fread(&block, 2, 1, f);
            std::fread(&bits, 2, 1, f);
            channels = ch;
            if (size > 16) std::fseek(f, static_cast<long>(size - 16), SEEK_CUR);
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (bits != 32 || channels == 0) { std::fclose(f); return false; }
            out.resize(size / sizeof(float));
            const size_t read = std::fread(out.data(), sizeof(float), out.size(), f);
            out.resize(read);
            std::fclose(f);
            return !out.empty();
        } else {
            std::fseek(f, static_cast<long>(size + (size & 1u)), SEEK_CUR);
        }
    }
    std::fclose(f);
    return false;
}

/** Peak magnitude of one channel. */
float channelPeak(const std::vector<float>& data, uint32_t channels, uint32_t channel) {
    float peak = 0.0f;
    if (channels == 0) return peak;
    for (size_t f = channel; f < data.size(); f += channels) {
        if (std::isfinite(data[f])) peak = std::max(peak, std::fabs(data[f]));
    }
    return peak;
}

/**
 * Compare a render against a stored baseline and explain any difference.
 *
 * The digest is the gate — it is exact, and an extraction claiming zero audio
 * change may not update it. These diagnostics only make a failure actionable:
 * knowing *where* the first sample diverged and by how much is the difference
 * between "something moved" and a debuggable defect.
 *
 * Baselines live in $AESTRA_CHAR_BASELINE. Absent, the comparison is skipped —
 * the digests are not bit-portable, so they cannot be committed.
 */
bool compareAgainstBaseline(const std::string& baselineDir, const std::string& name,
                            const std::vector<float>& actual, uint32_t channels) {
    namespace fs = std::filesystem;
    const std::string path = (fs::path(baselineDir) / (name + ".wav")).string();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true; // nothing recorded yet; the caller writes it
    }

    std::vector<float> expected;
    uint32_t expectedChannels = 0;
    if (!readFloatWav(path, expected, expectedChannels)) {
        std::printf("  baseline %s is unreadable\n", name.c_str());
        return false;
    }
    if (expectedChannels != channels) {
        std::printf("  %s: channel count changed, baseline %u vs now %u\n", name.c_str(), expectedChannels,
                    channels);
        return false;
    }
    if (expected.size() != actual.size()) {
        std::printf("  %s: length changed, baseline %zu vs now %zu samples (%zd frames)\n", name.c_str(),
                    expected.size(), actual.size(),
                    (static_cast<ptrdiff_t>(actual.size()) - static_cast<ptrdiff_t>(expected.size())) /
                        static_cast<ptrdiff_t>(channels ? channels : 1));
        return false;
    }

    size_t firstDiff = expected.size();
    double maxDelta = 0.0;
    size_t maxDeltaAt = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        const double delta = std::fabs(static_cast<double>(expected[i]) - static_cast<double>(actual[i]));
        if (delta > 0.0 && firstDiff == expected.size()) firstDiff = i;
        if (delta > maxDelta) { maxDelta = delta; maxDeltaAt = i; }
    }
    if (firstDiff == expected.size()) return true;

    const uint32_t ch = channels ? channels : 1;
    std::printf("  %s: first difference at frame %zu channel %zu — expected %.9g, got %.9g\n", name.c_str(),
                firstDiff / ch, firstDiff % ch, static_cast<double>(expected[firstDiff]),
                static_cast<double>(actual[firstDiff]));
    std::printf("  %s: max |delta| %.9g at frame %zu channel %zu\n", name.c_str(), maxDelta, maxDeltaAt / ch,
                maxDeltaAt % ch);
    return false;
}

/** Harness holding one project, so each case renders through the live path. */
struct Harness {
    std::shared_ptr<TrackManager> tm;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MuseService> service;
    PlaylistLaneID lane;
    std::string dir;

    bool init(const std::string& renderDir) {
        dir = renderDir;
        tm = std::make_shared<TrackManager>();
        tm->getUnitManager().setPatternManager(&tm->getPatternManager());
        engine = std::make_unique<AudioEngine>();
        engine->setSampleRate(48000);
        engine->setBufferConfig(4096, 2);
        MuseService::wireHeadlessEngine(tm, *engine);
        if (!engine->initialize()) return false;
        service = std::make_unique<MuseService>(tm.get(), engine.get());
        lane = tm->getPlaylistModel().createLane("chars");
        return lane.isValid();
    }

    /** Place a clip built directly from a buffer, bypassing file import. */
    ClipInstanceID addClip(const std::string& name, std::shared_ptr<AudioBufferData> buffer, double startBeat,
                           const ClipEdits& edits) {
        const double durationSeconds = buffer->durationSeconds();
        const uint64_t frames = buffer->numFrames;
        const ClipSourceID sourceId =
            tm->getSourceManager().createRecordedSource(dir + "/" + name + ".src", name, std::move(buffer));
        if (!sourceId.isValid()) return {};

        Aestra::Audio::AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = durationSeconds;
        Aestra::Audio::AudioSlice slice;
        slice.startSamples = 0.0;
        slice.lengthSamples = static_cast<double>(frames);
        payload.slices.push_back(slice);

        const double durationBeats = tm->getPlaylistModel().secondsToBeats(durationSeconds);
        const PatternID pattern = tm->getPatternManager().createAudioPattern(name, durationBeats, payload);
        if (!pattern.isValid()) return {};

        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.name = name;
        clip.startBeat = startBeat;
        clip.durationBeats = durationBeats;
        clip.durationSeconds = durationSeconds;
        clip.patternId = pattern;
        clip.sourceId = pattern.value;
        clip.edits = edits;
        return tm->getPlaylistModel().addClip(lane, clip);
    }

    /** Render the arranged timeline through the engine's own export path. */
    bool render(const std::string& file, std::vector<float>& out, uint32_t& channels) {
        JSON args = JSON::object();
        args.set("file", JSON(file));
        args.set("tail", JSON(0.0));
        JSON r = JSON::parse(service->handleRequest(request("render_song", args)));
        if (!r.has("status") || r["status"].asString() != "ok") return false;
        return readFloatWav(file, out, channels);
    }
};

/** One characterization case: render it, assert semantics, report its digest. */
struct CaseResult {
    bool rendered{false};
    uint64_t digest{0};
    std::vector<float> data;
    uint32_t channels{0};
};

CaseResult runCase(const std::string& name, const std::string& dir,
                   const std::function<void(Harness&)>& build) {
    CaseResult result;
    Harness h;
    if (!h.init(dir)) {
        check(false, name + ": harness initialises");
        return result;
    }
    build(h);

    const std::string file = dir + "/" + name + ".wav";
    if (!h.render(file, result.data, result.channels)) {
        check(false, name + ": renders");
        return result;
    }
    result.rendered = true;
    result.digest = digestOf(result.data);

    // Recording and verifying are separate, mutually exclusive modes.
    //
    // They used to be one: a missing baseline was silently recorded from the
    // current render. That is fail-open in the worst possible way — pointed at
    // an empty or mistyped directory during the extraction, a *changed*
    // renderer would generate its own expected output and pass. The oracle
    // must never be produced by the implementation under verification.
    namespace fs = std::filesystem;
    std::error_code bec;
    if (!g_recordDir.empty()) {
        const std::string stored = (fs::path(g_recordDir) / (name + ".wav")).string();
        if (fs::exists(stored, bec)) {
            // Refuse to overwrite: re-recording over a baseline is how an
            // unwanted change quietly becomes the new expectation.
            check(false, name + ": baseline already exists, refusing to overwrite");
        } else {
            fs::copy_file(file, stored, bec);
            check(!bec, name + ": baseline recorded");
        }
    } else if (!g_verifyDir.empty()) {
        const std::string stored = (fs::path(g_verifyDir) / (name + ".wav")).string();
        if (!fs::exists(stored, bec)) {
            // A missing baseline is a failure, never an invitation to write one.
            check(false, name + ": baseline file is missing from the verify directory");
        } else {
            const bool matched = compareAgainstBaseline(g_verifyDir, name, result.data, result.channels);
            check(matched, name + ": matches the recorded baseline exactly");
            ++g_verifiedCases;
        }
    }

    // Determinism: the same project rendered twice must be byte-identical.
    // Portable to assert, and it catches accumulation-order drift that a
    // tolerance check would not see.
    //
    // Compared with memcmp, not by digest. Equal digests are overwhelmingly
    // strong evidence of equal bytes but are not the same claim, and both
    // buffers are already in memory, so there is no reason to assert the
    // weaker one.
    std::vector<float> again;
    uint32_t againChannels = 0;
    const std::string file2 = dir + "/" + name + "_2.wav";
    if (h.render(file2, again, againChannels)) {
        const bool identical =
            againChannels == result.channels && again.size() == result.data.size() &&
            std::memcmp(again.data(), result.data.data(), result.data.size() * sizeof(float)) == 0;
        check(identical, name + ": rendering twice is byte-identical");
    } else {
        check(false, name + ": second render succeeds");
    }
    return result;
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }
    CommandRegistry::initialize();

    namespace fs = std::filesystem;

    // Resolve the mode before anything renders, and refuse ambiguity.
    const char* recordEnv = std::getenv("AESTRA_CHAR_RECORD_BASELINE");
    const char* verifyEnv = std::getenv("AESTRA_CHAR_VERIFY_BASELINE");
    if (recordEnv && verifyEnv) {
        std::cout << "FAIL: set only one of AESTRA_CHAR_RECORD_BASELINE / AESTRA_CHAR_VERIFY_BASELINE\n";
        return 1;
    }
    if (recordEnv) {
        g_recordDir = recordEnv;
        std::error_code rec;
        fs::create_directories(g_recordDir, rec);
        if (rec) {
            std::cout << "FAIL: could not create the record directory: " << g_recordDir << "\n";
            return 1;
        }
        std::cout << "MODE: recording baselines into " << g_recordDir << "\n";
    } else if (verifyEnv) {
        g_verifyDir = verifyEnv;
        std::error_code vec;
        // Verification never creates anything. A wrong path must fail loudly
        // rather than quietly become a recording run.
        if (!fs::is_directory(g_verifyDir, vec)) {
            std::cout << "FAIL: verify directory does not exist: " << g_verifyDir << "\n";
            return 1;
        }
        std::cout << "MODE: verifying against baselines in " << g_verifyDir << "\n";
    } else {
        std::cout << "MODE: semantic and determinism checks only "
                     "(set AESTRA_CHAR_VERIFY_BASELINE to gate a refactor)\n";
    }

    const fs::path dir = fs::temp_directory_path() / "aestra_clip_characterization";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        std::cout << "FAIL: could not create the working directory\n";
        return 1;
    }
    const std::string d = dir.string();

    std::cout << "=== Clip render characterization ===\n";
    std::cout << "Digests are printed, not asserted: float output is not bit-portable.\n"
                 "Refactor gate — capture these before extracting the kernel, and require\n"
                 "them unchanged afterwards within the same build and environment.\n\n";

    std::vector<std::pair<std::string, uint64_t>> digests;

    // 1. Unity rate — and it must really BE unity.
    //
    // The runtime has a direct-copy fast path taken when |ratio - 1| < 1e-9,
    // where ratio = (sourceRate / engineRate) * playbackRate. A fixture named
    // "unity-rate" that quietly drifted into the interpolated branch would be
    // characterizing resampling under the wrong name, so the preconditions are
    // asserted rather than assumed. Confirmed empirically: nudging phase only
    // inside the interpolated branch leaves this case byte-identical.
    {
        constexpr uint32_t kEngineRate = 48000;
        ClipEdits unityEdits; // playbackRate 1.0, sourceStart 0, no fades
        auto r = runCase("unity-rate", d, [&](Harness& h) {
            check(static_cast<uint32_t>(h.tm->getPlaylistModel().getProjectSampleRate()) == kEngineRate,
                  "unity-rate: project rate is the engine rate");
            check(unityEdits.playbackRate == 1.0f, "unity-rate: playback rate is exactly 1");
            check(unityEdits.sourceStart == 0.0, "unity-rate: no slip, so the source offset is integral");
            h.addClip("unity", makeStepSource(kEngineRate, 24000), 0.0, unityEdits);
        });
        if (r.rendered) {
            check(channelPeak(r.data, r.channels, 0) > 0.5f, "unity-rate: renders audible signal");
            digests.emplace_back("unity-rate", r.digest);
        }
    }

    // 2. Resampled: source rate differs, so the interpolator runs.
    {
        auto r = runCase("resampled", d, [](Harness& h) {
            h.addClip("resamp", makeStepSource(44100, 22050), 0.0, ClipEdits{});
        });
        if (r.rendered) {
            check(channelPeak(r.data, r.channels, 0) > 0.5f, "resampled: renders audible signal");
            digests.emplace_back("resampled", r.digest);
        }
    }

    // 3. Panned PARTIALLY, BOTH SIGNS.
    //
    // Two things this fixture has to get right, both learned from sabotage.
    //
    // Not hard-panned: at pan = -1 every pan law agrees (linear gives
    // 1 + (-1) = 0, square-root gives sqrt(0) = 0), so an endpoint fixture
    // cannot characterize the curve at all. Swapping the taper left the
    // hard-panned version byte-identical.
    //
    // Both signs: the runtime takes separate branches for positive and
    // negative pan, so one interior negative value leaves the positive branch
    // uncharacterized. A fixture must exercise a *discriminating* point on
    // *every* branch, not merely a valid one.
    {
        auto r = runCase("panned", d, [](Harness& h) {
            ClipEdits left;
            left.pan = -0.5f;
            h.addClip("panL", makeStepSource(48000, 12000), 0.0, left);
            ClipEdits right;
            right.pan = 0.35f;
            h.addClip("panR", makeStepSource(48000, 12000), 4.0, right);
        });
        if (r.rendered) {
            check(channelPeak(r.data, r.channels, 0) > 0.05f && channelPeak(r.data, r.channels, 1) > 0.05f,
                  "panned: both channels carry signal, so neither branch is silent");
            digests.emplace_back("panned", r.digest);
        }
    }

    // 4. Faded: explicit fades longer than the automatic edge fade.
    {
        auto r = runCase("faded", d, [](Harness& h) {
            ClipEdits e;
            e.fadeInBeats = 0.25f;
            e.fadeOutBeats = 0.25f;
            h.addClip("fade", makeStepSource(48000, 24000), 0.0, e);
        });
        if (r.rendered) {
            check(r.data.size() > 2 && std::fabs(r.data[0]) < 0.05f, "faded: starts near silence");
            digests.emplace_back("faded", r.digest);
        }
    }

    // 5. Overlapping: two clips sharing a region must sum.
    {
        auto r = runCase("overlapping", d, [](Harness& h) {
            h.addClip("a", makeStepSource(48000, 24000), 0.0, ClipEdits{});
            h.addClip("b", makeStepSource(48000, 24000), 0.25, ClipEdits{});
        });
        if (r.rendered) {
            check(channelPeak(r.data, r.channels, 0) > 0.5f, "overlapping: renders audible signal");
            digests.emplace_back("overlapping", r.digest);
        }
    }

    // 6. Short clip: shorter than the automatic edge fade, where the fade
    //    clamps against the clip length.
    {
        auto r = runCase("short-clip", d, [](Harness& h) {
            h.addClip("short", makeStepSource(48000, 64), 0.0, ClipEdits{});
        });
        if (r.rendered) {
            digests.emplace_back("short-clip", r.digest);
        }
    }

    std::cout << "\n--- digests (compare before vs after the kernel extraction) ---\n";
    for (const auto& [name, digest] : digests) {
        std::printf("  %-14s %016llx\n", name.c_str(), static_cast<unsigned long long>(digest));
    }
    std::cout << "\n";

    check(digests.size() == 6, "every characterization case rendered");

    // A wrong verify directory must produce six failures, not a silent pass
    // over zero comparisons.
    if (!g_verifyDir.empty()) {
        check(g_verifiedCases == 6,
              "exactly six pre-existing baselines were compared (got " + std::to_string(g_verifiedCases) + ")");
    }

    fs::remove_all(dir, ec);

    if (g_failures != 0) {
        std::cout << "FAILED: " << g_failures << " check(s)\n";
        return 1;
    }
    std::cout << "All clip render characterization checks passed.\n";
    return 0;
}
