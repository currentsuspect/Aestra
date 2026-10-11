// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// verify_render tests — the confirmation verb.
//
// The question this file exists to answer is not "does verify_render return
// something" but "does it return the right verdict for the right reason". A
// render verb that reports numbers leaves an agent to decide whether -96 dBFS
// means quiet or broken; this one has to decide, and a verdict that is wrong in
// the convenient direction is worse than no verdict at all.
//
// What is deliberately NOT asserted here: the specific peak of a specific
// project. That is an engine measurement with its own tests. What is asserted is
// the contract — that a silent render fails, that a caller who permits silence
// gets a pass, that a verdict of "fail" still arrives as status "ok" so the
// agent loop does not retry instead of fixing the mix, and that an unrunnable
// render is an error rather than a failed verdict.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

JSON call(MuseService& service, const std::string& request) {
    return JSON::parse(service.handleRequest(request));
}

// const, so a response straight out of call() — which returns by value — binds.
// Every JSON accessor is const, so nothing here needs the mutable overload.
std::string status(const JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

JSON str(std::string value) { return JSON(value); }

// A short, loud, deterministic WAV. A ramp rather than a tone: the point is
// only that the render is unambiguously not silent, and a ramp cannot be
// mistaken for a metering artefact.
std::string writeRampWav(const std::string& path, uint32_t sampleRate = 48000) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return {};
    const uint32_t frames = sampleRate; // one second
    const uint32_t dataBytes = frames * 2 * sizeof(float);
    const uint16_t channels = 2;
    const uint16_t blockAlign = channels * sizeof(float);

    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    u32(36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    u32(16);
    u16(3); // IEEE float
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * blockAlign);
    u16(blockAlign);
    u16(32);
    file.write("data", 4);
    u32(dataBytes);
    for (uint32_t n = 0; n < frames; ++n) {
        const float v = 0.5f * static_cast<float>(n) / static_cast<float>(frames);
        for (int c = 0; c < 2; ++c) {
            file.write(reinterpret_cast<const char*>(&v), sizeof(float));
        }
    }
    file.close();
    return file.fail() ? std::string{} : path;
}

// --- response helpers -------------------------------------------------------

bool verdictIs(JSON& response, const std::string& expected) {
    return response.has("result") && response["result"].has("verdict") &&
           response["result"]["verdict"].asString() == expected;
}

bool checkPassed(JSON& response, const std::string& name, bool expectedPass) {
    if (!response.has("result") || !response["result"].has("checks")) return false;
    // Bound through a named local: JSON::operator[] yields a reference into the
    // left-hand object, so a chained subscript on a temporary hands back an
    // rvalue that a JSON& cannot bind to.
    JSON result = response["result"];
    JSON& checks = result["checks"];
    for (size_t i = 0; i < checks.size(); ++i) {
        if (checks[i]["name"].asString() == name) {
            return checks[i]["pass"].asBool() == expectedPass;
        }
    }
    return false;
}

bool hasCheck(JSON& response, const std::string& name) {
    if (!response.has("result") || !response["result"].has("checks")) return false;
    JSON result = response["result"];
    JSON& checks = result["checks"];
    for (size_t i = 0; i < checks.size(); ++i) {
        if (checks[i]["name"].asString() == name) return true;
    }
    return false;
}

// The path is JSON-encoded rather than interpolated. MuseService::handleRequest
// parses this with JSON::parse, and a raw Windows path carries backslashes that are
// invalid escapes — \t and \n would silently rewrite the path before the verb ever
// sees it. MuseServiceTest says the same thing about its own fileRequest().
std::string verifyRequest(double id, const std::string& file, const std::string& extraArgs) {
    return "{\"id\": " + std::to_string(id) + ", \"verb\": \"verify_render\", \"args\": {\"file\": " +
           JSON(file).toString() + extraArgs + "}}";
}

std::string jsonString(const std::string& value) { return JSON(value).toString(); }

} // namespace

int main() {
    using namespace Aestra::Audio;

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "aestra_muse_verify_render";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::cout << "FAIL: could not create " << dir.string() << " (" << ec.message() << ")\n";
        return 1;
    }

    const std::string sample = writeRampWav((dir / "ramp.wav").string());
    if (sample.empty()) {
        std::cout << "FAIL: could not write the test sample\n";
        return 1;
    }
    const std::string renderA = (dir / "a.wav").string();
    const std::string renderB = (dir / "b.wav").string();
    const std::string renderC = (dir / "c.wav").string();

    if (!PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager did not initialise\n";
        return 1;
    }

    auto trackManager = std::make_shared<TrackManager>();
    const uint32_t sampleRate = 48000;
    trackManager->setOutputSampleRate(static_cast<double>(sampleRate));
    trackManager->setInputSampleRate(static_cast<double>(sampleRate));
    trackManager->setInputChannelCount(0);
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());

    AudioEngine engine;
    engine.setSampleRate(sampleRate);
    // Must cover AudioExporter's 4096-frame render blocks.
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    if (!engine.initialize()) {
        std::cout << "FAIL: audio engine did not initialise\n";
        return 1;
    }
    CommandRegistry::initialize();

    MuseService service(trackManager.get(), &engine);

    // --- the verb exists and is discoverable --------------------------------
    {
        JSON r = call(service, "{\"id\": 1, \"verb\": \"get_schema\"}");
        check(status(r) == "ok", "get_schema answers");
        bool listed = false;
        JSON result = r["result"];
        JSON& actions = result["actions"];
        for (size_t i = 0; i < actions.size(); ++i) {
            if (actions[i]["verb"].asString() == "verify_render") listed = true;
        }
        check(listed, "verify_render is in the manifest an agent is handed");
    }

    // --- nothing to render is an error, not a failed verdict ----------------
    {
        JSON r = call(service, verifyRequest(2, renderA, ""));
        check(status(r) == "execution_error",
              "an empty timeline is an execution_error, not verdict fail");
        check(!r.has("result") || !r["result"].has("verdict"),
              "a render that never ran carries no verdict to misread");
    }

    // --- a real session -----------------------------------------------------
    //
    // A sampler unit playing a note, arranged onto the timeline. That shape
    // rather than an audio clip, because per-UNIT gain reaches the offline
    // render (mixer channel volume and mute do not) and the silence case below
    // needs a gain that works. The audio-clip path gets its own check further
    // down so both sources stay covered.
    check(status(call(service, "{\"id\": 3, \"verb\": \"add_track\", \"args\": {\"name\": \"T\"}}")) ==
              "ok",
          "add_track");
    {
        JSON req = JSON::object();
        req.set("id", JSON(4.0));
        req.set("verb", str("add_unit"));
        JSON args = JSON::object();
        args.set("name", str("Sampler"));
        req.set("args", args);
        check(status(call(service, req.toString())) == "ok", "add_unit");
    }
    {
        JSON req = JSON::object();
        req.set("id", JSON(5.0));
        req.set("verb", str("load_sample"));
        JSON args = JSON::object();
        args.set("unit", JSON(1.0));
        args.set("file", str(sample));
        req.set("args", args);
        check(status(call(service, req.toString())) == "ok", "load_sample");
    }
    {
        JSON req = JSON::object();
        req.set("id", JSON(6.0));
        req.set("verb", str("add_note"));
        JSON args = JSON::object();
        args.set("pattern", JSON(1.0));
        args.set("unit", JSON(1.0));
        args.set("pitch", JSON(60.0));
        args.set("start", JSON(0.0));
        args.set("duration", JSON(1.0));
        req.set("args", args);
        check(status(call(service, req.toString())) == "ok", "add_note at the sampler root");
    }
    {
        JSON req = JSON::object();
        req.set("id", JSON(7.0));
        req.set("verb", str("arrange_pattern"));
        JSON args = JSON::object();
        args.set("pattern", JSON(1.0));
        args.set("track", JSON(0.0));
        args.set("start", JSON(0.0));
        req.set("args", args);
        check(status(call(service, req.toString())) == "ok", "arrange_pattern onto the timeline");
    }

    // --- a healthy render passes -------------------------------------------
    {
        JSON r = call(service, verifyRequest(10, renderA, ""));
        check(status(r) == "ok", "verify_render answers ok on a healthy render");
        check(verdictIs(r, "pass"), "a non-silent, non-clipping render passes");
        check(r["result"]["peakDb"].asNumber() > -89.0, "the reported peak is a real level");
        check(checkPassed(r, "not_silent", true), "not_silent passes");
        check(checkPassed(r, "no_clipping", true), "no_clipping passes");
        check(r["result"].has("maxTruePeakDbTp"), "the true peak is reported alongside peakDb");
        check(r["result"]["frames"].asNumber() > 0, "frames were rendered");
    }

    // --- reproducibility ----------------------------------------------------
    {
        JSON r = call(service, verifyRequest(11, renderB, ""));
        check(status(r) == "ok" && verdictIs(r, "pass"), "a second render of the same session passes");
        check(!r["result"].has("digest"),
              "no digest is reported when nothing was asked to compare against");

        std::string against;
        for (char c : renderB) {
            if (c == '\\') { against += '\\'; against += '\\'; }
            else if (c == '"') { against += '\\'; against += '"'; }
            else against += c;
        }
        JSON same = call(service, verifyRequest(12, renderC, ", \"against\": \"" + against + "\""));
        check(status(same) == "ok", "comparing against an identical render answers ok");
        check(same["result"].has("digest"), "a digest is reported when comparing");
        check(checkPassed(same, "matches_previous_render", true),
              "an unchanged session re-renders byte for byte");

        JSON missing =
            call(service, verifyRequest(13, (dir / "d.wav").string(),
                                        ", \"against\": " +
                                            jsonString((dir / "never.wav").string())));
        check(status(missing) != "ok",
              "comparing against a file that is not there is an error, not a pass");
    }

    // --- comparing a render against itself is not a pass --------------------
    {
        // Regression. The reference digest used to be taken AFTER the render, so
        // naming the same path in both 'file' and 'against' meant the render
        // overwrote the reference and the check then compared the new file with
        // itself — matching unconditionally.
        //
        // Detecting it needs the two things at once: the same path for both keys,
        // AND a session that changed since that path was written. With an unchanged
        // session the comparison passes legitimately, before and after the fix, so
        // such a test proves nothing.
        const std::string reused = (dir / "reused.wav").string();
        JSON first = call(service, verifyRequest(15, reused, ""));
        check(status(first) == "ok" && verdictIs(first, "pass"),
              "the reused path renders once and passes");

        // Change the session so the next render of the same path cannot match.
        {
            JSON req = JSON::object();
            req.set("id", JSON(16.0));
            req.set("verb", str("add_note"));
            JSON args = JSON::object();
            args.set("pattern", JSON(1.0));
            args.set("unit", JSON(1.0));
            args.set("pitch", JSON(72.0));
            args.set("start", JSON(0.5));
            args.set("duration", JSON(0.25));
            args.set("velocity", JSON(1.0));
            req.set("args", args);
            check(status(call(service, req.toString())) == "ok",
                  "a note is added so the next render must differ");
        }

        JSON again = call(service, verifyRequest(17, reused,
                                                 ", \"against\": " + jsonString(reused)));
        check(status(again) == "ok", "same path for file and against is still allowed");
        check(checkPassed(again, "matches_previous_render", false),
              "a changed session re-rendered over its own reference FAILS the comparison");
        check(verdictIs(again, "fail"), "and the verdict reflects that");

        // And a genuinely unchanged re-render of the same path still passes, so the
        // check above is not passing merely because same-path is always a failure.
        JSON unchanged = call(service, verifyRequest(18, reused, ""));
        check(status(unchanged) == "ok" && verdictIs(unchanged, "pass"),
              "an unchanged re-render of the same path still passes");
    }

    // --- a failed check arrives as ok/fail, not as an error -----------------
    {
        // No solo needed: a peak ceiling nothing can meet is the cleanest way to
        // make exactly one check fail, so this does not depend on solo behaviour.
        JSON r = call(service, verifyRequest(20, renderA, ", \"expect_peak_max_db\": -80"));
        check(status(r) == "ok",
              "a failing check still answers status ok: the verb worked, the audio did not");
        check(verdictIs(r, "fail"), "an unmet expectation yields verdict fail");
        check(checkPassed(r, "peak_in_range", false), "peak_in_range is the check that failed");
        check(checkPassed(r, "not_silent", true), "the other checks are unaffected");
    }

    // --- every check reports its expectation and its actual ----------------
    {
        JSON r = call(service, verifyRequest(21, renderA, ", \"expect_peak_max_db\": -80"));
        JSON result = r["result"];
        JSON& checks = result["checks"];
        bool allExplained = checks.size() > 0;
        for (size_t i = 0; i < checks.size(); ++i) {
            if (!checks[i].has("expected") || !checks[i].has("actual") ||
                checks[i]["expected"].asString().empty() ||
                checks[i]["actual"].asString().empty()) {
                allExplained = false;
            }
        }
        check(allExplained,
              "each check carries the expectation and the actual value, so a fail is actionable");
    }

    // --- silence is a failure by default, and permitted on request ----------
    {
        JSON r = call(service, verifyRequest(30, renderA, ", \"expect_not_silent\": false"));
        check(status(r) == "ok" && verdictIs(r, "pass"),
              "a caller who permits silence gets a pass rather than a nagging fail");
        check(!hasCheck(r, "not_silent"),
              "the check it turned off is absent, not present-and-passing");
    }

    // --- silence that was NOT permitted is reported as such ----------------
    {
        // Silence via UNIT gain, not the mixer. Per-source gain is known to
        // reach the offline render; mixer channel volume and mute are not (see
        // the mute/volume-does-not-affect-export finding). Using the mixer here
        // would have made this test assert the bug, and using it as a comment
        // instead of a fixture would have made it depend on the bug being fixed.
        check(status(call(service, "{\"id\": 31, \"verb\": \"set_unit_gain\", \"args\": "
                                  "{\"unit\": 1, \"value\": 0}}")) == "ok",
              "set_unit_gain 0 on the clip's unit");
        JSON r = call(service, verifyRequest(32, renderA, ""));
        check(status(r) == "ok", "a silent render still answers status ok");
        check(verdictIs(r, "fail"), "a silent render fails the verdict");
        check(checkPassed(r, "not_silent", false), "not_silent is the check that failed");
        check(r["result"]["peakDb"].asNumber() <= -89.0,
              "the silent render's peak is at the floor, so the fail had a real cause");
    }

    // --- bad arguments are refused, never silently ignored ------------------
    {
        JSON r = call(service, verifyRequest(40, renderA, ", \"expect_loudness\": true"));
        check(status(r) == "validation_error",
              "an unknown expectation is refused rather than dropped");
        check(r["message"].asString().find("expect_loudness") != std::string::npos,
              "the refusal names the offending key");

        JSON noFile = call(service, "{\"id\": 41, \"verb\": \"verify_render\", \"args\": {}}");
        check(status(noFile) == "validation_error", "a missing file is a validation_error");

        JSON emptyFile = call(service, verifyRequest(42, "", ""));
        check(status(emptyFile) == "validation_error", "an empty file path is refused");

        JSON badTail = call(service, verifyRequest(43, renderA, ", \"tail\": 999"));
        check(status(badTail) == "validation_error", "an out-of-range tail is refused");

        JSON badBool = call(service, verifyRequest(44, renderA, ", \"expect_not_silent\": 1"));
        check(status(badBool) == "validation_error",
              "a non-boolean expectation is refused rather than coerced");
    }

    // --- it is an action, so it is undo-free by construction ----------------
    {
        JSON r = call(service, verifyRequest(50, renderA, ""));
        check(status(r) == "ok", "verify_render runs after all that history churn");
        check(!r.has("undoable") || r["undoable"].asBool() == false,
              "a render is not an undoable project edit");
    }

    // --- the audio-clip path is covered too ---------------------------------
    {
        // A second track with an audio clip, so both source kinds are exercised:
        // the sampler note above goes through the pattern engine, this one
        // through the playlist lane.
        check(status(call(service, "{\"id\": 60, \"verb\": \"add_track\", \"args\": {\"name\": \"A\"}}")) ==
                  "ok",
              "add_track for the audio clip");
        check(status(call(service, "{\"id\": 61, \"verb\": \"add_lane\", \"args\": {\"name\": \"L\"}}")) ==
                  "ok",
              "add_lane");
        JSON req = JSON::object();
        req.set("id", JSON(62.0));
        req.set("verb", str("add_clip"));
        JSON args = JSON::object();
        args.set("track", JSON(1.0));
        args.set("file", str(sample));
        args.set("bar", JSON(1.0));
        req.set("args", args);
        check(status(call(service, req.toString())) == "ok", "add_clip");

        JSON r = call(service, verifyRequest(63, (dir / "clip.wav").string(), ""));
        check(status(r) == "ok", "a session with an audio clip verifies");
        check(verdictIs(r, "pass"), "the audio clip renders audibly");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(dir, cleanup);

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
