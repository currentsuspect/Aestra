// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82: Muse sets a unit sampler's envelope and play modes (set_sampler) and reads
// them back (list_units.sampler).
//
// The release is proven in audio, not only in the model: a held note on a long
// sample keeps ringing after its note-off with a long release, and is near silent
// a short moment after it with a short one. That is the "808 rings into the next
// kick" problem from the boom bap session, which had no Muse verb to fix.
#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"
#include "Plugin/SamplerPlugin.h"

#include "AestraJSON.h"

#include <algorithm>
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

JSON call(MuseService& service, const JSON& request) { return JSON::parse(service.handleRequest(request.toString())); }

std::string status(JSON response) { return response.has("status") ? response["status"].asString() : "<missing>"; }

JSON req(double id, const std::string& verb, JSON args) {
    JSON r = JSON::object();
    r.set("id", JSON(id));
    r.set("verb", JSON(verb));
    r.set("args", args);
    return r;
}

JSON args() { return JSON::object(); }

void writeU32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void writeU16(std::ofstream& f, uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); }

// Two seconds of a steady 55 Hz sine: long enough that only the envelope, not the
// sample running out, decides when a note goes quiet.
std::string writeLongSine() {
    const std::string path = (std::filesystem::temp_directory_path() / "muse_sampler_verb_sine.wav").string();
    const uint32_t sampleRate = 48000, frames = 96000;
    constexpr double kPi = 3.14159265358979323846;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    writeU32(f, 36 + frames * 2);
    f.write("WAVEfmt ", 8);
    writeU32(f, 16);
    writeU16(f, 1);
    writeU16(f, 1);
    writeU32(f, sampleRate);
    writeU32(f, sampleRate * 2);
    writeU16(f, 2);
    writeU16(f, 16);
    f.write("data", 4);
    writeU32(f, frames * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const auto s = static_cast<int16_t>(20000.0 * std::sin(2.0 * kPi * 55.0 * i / sampleRate));
        writeU16(f, static_cast<uint16_t>(s));
    }
    return path;
}

// Peak (dBFS) of a rendered file between two times, read straight from the WAV
// the render verb writes (float32 stereo).
double windowPeakDb(const std::string& file, double fromSeconds, double toSeconds) {
    std::ifstream f(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 12;
    uint16_t channels = 2;
    uint32_t sampleRate = 48000;
    while (pos + 8 <= bytes.size()) {
        const std::string id(bytes.data() + pos, 4);
        uint32_t size = 0;
        std::memcpy(&size, bytes.data() + pos + 4, 4);
        if (id == "fmt ") {
            std::memcpy(&channels, bytes.data() + pos + 10, 2);
            std::memcpy(&sampleRate, bytes.data() + pos + 12, 4);
        } else if (id == "data") {
            const float* data = reinterpret_cast<const float*>(bytes.data() + pos + 8);
            const size_t frames = size / (4 * channels);
            const size_t a = static_cast<size_t>(fromSeconds * sampleRate);
            const size_t b = std::min(frames, static_cast<size_t>(toSeconds * sampleRate));
            float peak = 0.0f;
            for (size_t i = a; i < b; ++i)
                for (uint16_t c = 0; c < channels; ++c) peak = std::max(peak, std::fabs(data[i * channels + c]));
            return peak > 0.0f ? 20.0 * std::log10(peak) : -200.0;
        }
        pos += 8 + size + (size & 1);
    }
    return 999.0;
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(trackManager, engine);
    if (!engine.initialize()) {
        std::cout << "FAIL: engine initialize\n";
        return 1;
    }
    CommandRegistry::initialize();
    MuseService service(trackManager.get(), &engine);
    const std::string sample = writeLongSine();
    const std::string out = (std::filesystem::temp_directory_path() / "muse_sampler_verb_out.wav").string();

    JSON a = args();
    a.set("name", JSON("Bass"));
    check(status(call(service, req(1, "add_track", a))) == "ok", "add a track");
    a = args();
    a.set("name", JSON("808"));
    check(status(call(service, req(2, "add_unit", a))) == "ok", "add a unit");
    a = args();
    a.set("unit", JSON(1.0));
    a.set("file", JSON(sample));
    check(status(call(service, req(3, "load_sample", a))) == "ok", "load the long sine");
    // One half-beat note at beat 0; at 120 BPM its note-off lands at 0.25 s.
    a = args();
    a.set("pattern", JSON(1.0));
    a.set("unit", JSON(1.0));
    a.set("pitch", JSON(60.0));
    a.set("start", JSON(0.0));
    a.set("duration", JSON(0.5));
    check(status(call(service, req(4, "add_note", a))) == "ok", "one held note");
    a = args();
    a.set("pattern", JSON(1.0));
    a.set("track", JSON(0.0));
    a.set("start", JSON(0.0));
    check(status(call(service, req(5, "arrange_pattern", a))) == "ok", "arrange it");
    a = args();
    a.set("value", JSON(120.0));
    call(service, req(6, "set_bpm", a));

    auto render = [&]() {
        JSON r = args();
        r.set("file", JSON(out));
        r.set("tail", JSON(1.5));
        return status(call(service, req(7, "render_song", r))) == "ok";
    };
    auto setSampler = [&](JSON fields) {
        fields.set("unit", JSON(1.0));
        return call(service, req(8, "set_sampler", fields));
    };

    // --- long release: still ringing 0.3 s after the note-off -------------------
    a = args();
    a.set("release", JSON(2.0));
    check(status(setSampler(a)) == "ok", "set a 2 s release");
    check(render(), "render with the long release");
    const double longTail = windowPeakDb(out, 0.55, 0.65);

    // --- short release: near silent by then ----------------------------------------
    a = args();
    a.set("release", JSON(0.03));
    a.set("attack", JSON(0.0));
    check(status(setSampler(a)) == "ok", "set a 30 ms release");
    check(render(), "render with the short release");
    const double shortTail = windowPeakDb(out, 0.55, 0.65);
    const double body = windowPeakDb(out, 0.05, 0.2);
    std::cout << "body " << body << " dB, tail long " << longTail << " dB, short " << shortTail << " dB\n";
    check(body > -30.0, "the held note itself is audible");
    check(longTail > body - 12.0, "a 2 s release keeps ringing after the note ends");
    check(shortTail < body - 60.0, "a 30 ms release is silent 0.3 s after the note ends");

    // --- read back through list_units ---------------------------------------------
    JSON units = call(service, req(9, "list_units", args()));
    JSON s = units["result"]["units"][0]["sampler"];
    check(std::fabs(s["release"].asNumber() - 0.03) < 1e-4, "list_units shows the release");
    check(s.has("mono") && s.has("cutSelf") && s.has("voices"), "list_units shows the play modes");

    // --- only the given fields change; play modes too --------------------------------
    a = args();
    a.set("mono", JSON(true));
    a.set("voices", JSON(1.0));
    check(status(setSampler(a)) == "ok", "set mono and one voice");
    auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(
        trackManager->getUnitManager().getUnitPlugin(1));
    check(sampler && sampler->isMonoMode() && sampler->getMaxVoices() == 1, "mono and voices applied");
    check(sampler && std::fabs(sampler->getRelease() - 0.03f) < 1e-6, "the release was left alone");

    // --- one undo step restores the previous state ----------------------------------
    check(status(call(service, req(10, "undo", args()))) == "ok", "undo");
    check(sampler && !sampler->isMonoMode() && sampler->getMaxVoices() != 1, "undo restores the play modes");
    check(status(call(service, req(11, "undo", args()))) == "ok", "undo again");
    check(sampler && std::fabs(sampler->getRelease() - 2.0f) < 1e-6, "undo restores the long release");

    // --- refusals -----------------------------------------------------------------
    check(status(setSampler(args())) != "ok", "refuses a call that changes nothing");
    a = args();
    a.set("release", JSON(0.25));
    check(status(setSampler(a)) == "ok", "set a release the unit does not have yet");
    a = args();
    a.set("release", JSON(0.25));
    check(status(setSampler(a)) != "ok", "refuses values the unit already has");
    a = args();
    a.set("release", JSON(9.0));
    check(status(setSampler(a)) != "ok", "refuses a release over 5 s");
    a = args();
    a.set("voices", JSON(0.0));
    check(status(setSampler(a)) != "ok", "refuses zero voices");
    a = args();
    a.set("unit", JSON(42.0));
    a.set("release", JSON(0.1));
    check(status(call(service, req(12, "set_sampler", a))) != "ok", "refuses a unit that does not exist");

    std::filesystem::remove(out);
    std::filesystem::remove(sample);
    if (g_failures) {
        std::cout << "MuseSamplerVerbTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "MuseSamplerVerbTest: all checks passed\n";
    return 0;
}
