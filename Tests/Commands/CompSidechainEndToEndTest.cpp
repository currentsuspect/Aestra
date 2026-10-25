// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CompSidechainEndToEndTest — a sidechain-only send reaches the compressor's
// key input through the real engine, and ducks the track the compressor is on.
//
// Two Arsenal units on two mixer tracks: a loud "kick" and a quiet sustained
// "808" that sits below the compressor's threshold. The kick track sends to
// the 808 track as sidechain-only; the 808 track has AestraComp with Sidechain
// on. Rendered through render_song (the exporter path):
//   with the send            -> the 808 is ducked while the kick plays
//   without the send         -> nothing changes (no key)
//   Sidechain off, with send -> nothing changes (the key is ignored)
// The kick's own audio is identical in every render, so the renders differ
// only by the ducking.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Plugin/AestraComp.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::AudioRoute;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MixerChannel;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::Audio::Plugins::AestraComp;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) ++g_failures;
}

JSON call(MuseService& s, const JSON& req) { return JSON::parse(s.handleRequest(req.toString())); }
std::string statusOf(JSON r) { return r.has("status") ? r["status"].asString() : "<missing>"; }
JSON req(const std::string& verb, JSON args) {
    JSON r = JSON::object();
    r.set("id", JSON(1.0));
    r.set("verb", JSON(verb));
    r.set("args", args);
    return r;
}

// 16-bit mono WAV of a sine, with an optional exponential decay.
std::string writeTone(const std::string& name, double hz, double amp, double seconds, double decayPerSec) {
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    const uint32_t sr = 48000, frames = static_cast<uint32_t>(seconds * sr);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + frames * 2); f.write("WAVE", 4); f.write("fmt ", 4);
    u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(frames * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double v = amp * std::exp(-decayPerSec * t) * std::sin(2.0 * 3.14159265358979 * hz * t);
        const auto s = static_cast<int16_t>(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0));
        f.write(reinterpret_cast<const char*>(&s), 2);
    }
    return path;
}

std::vector<float> readFloatWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() <= 44) return {};
    std::vector<float> out((bytes.size() - 44) / sizeof(float));
    std::memcpy(out.data(), bytes.data() + 44, out.size() * sizeof(float));
    return out;
}

double diffDb(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += (static_cast<double>(a[i]) - b[i]) * (static_cast<double>(a[i]) - b[i]);
    return 10.0 * std::log10(std::max(1.0e-30, acc / std::max<size_t>(1, n)));
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) {
        std::cout << "FAIL: plugin manager initialize\n";
        return 1;
    }
    auto tm = std::make_shared<TrackManager>();
    tm->getUnitManager().setPatternManager(&tm->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(tm, engine);
    if (!engine.initialize()) {
        std::cout << "FAIL: engine initialize\n";
        return 1;
    }
    CommandRegistry::initialize();
    MuseService service(tm.get(), &engine);

    const std::string kick = writeTone("cse2e_kick.wav", 90.0, 0.9, 0.35, 6.0);
    const std::string sub = writeTone("cse2e_808.wav", 55.0, 0.05, 2.2, 0.0);
    const std::string out = (std::filesystem::temp_directory_path() / "cse2e_out.wav").string();

    for (const char* name : {"Kick", "808"}) {
        JSON a = JSON::object();
        a.set("name", JSON(name));
        check(statusOf(call(service, req("add_track", a))) == "ok", std::string("add track ") + name);
        check(statusOf(call(service, req("add_unit", a))) == "ok", std::string("add unit ") + name);
    }
    MixerChannel* kickCh = tm->getChannel(0);
    MixerChannel* subCh = tm->getChannel(1);
    if (!kickCh || !subCh) return 1;
    const std::string files[2] = {kick, sub};
    for (int u = 1; u <= 2; ++u) {
        JSON a = JSON::object();
        a.set("unit", JSON(static_cast<double>(u)));
        a.set("file", JSON(files[u - 1]));
        check(statusOf(call(service, req("load_sample", a))) == "ok", "load sample into unit " + std::to_string(u));
    }
    tm->getUnitManager().setUnitMixerChannel(1, kickCh->getChannelId());
    tm->getUnitManager().setUnitMixerChannel(2, subCh->getChannelId());

    // Both units in unit 1's pattern: kick on every beat, one long 808 note.
    JSON steps = JSON::object();
    steps.set("pattern", JSON(1.0));
    steps.set("unit", JSON(1.0));
    steps.set("pitch", JSON(60.0));
    steps.set("steps", JSON("x---x---x---x---"));
    check(statusOf(call(service, req("set_steps", steps))) == "ok", "kick on every beat");
    JSON note = JSON::object();
    note.set("pattern", JSON(1.0));
    note.set("unit", JSON(2.0));
    note.set("pitch", JSON(60.0));
    note.set("start", JSON(0.0));
    note.set("duration", JSON(4.0));
    note.set("velocity", JSON(1.0));
    note.set("pan", JSON(0.0));
    check(statusOf(call(service, req("add_note", note))) == "ok", "one long 808 note");
    JSON arrange = JSON::object();
    arrange.set("pattern", JSON(1.0));
    arrange.set("track", JSON(0.0));
    arrange.set("start", JSON(0.0));
    check(statusOf(call(service, req("arrange_pattern", arrange))) == "ok", "arrange");

    // AestraComp on the 808 track, sidechain on, threshold above the 808 alone.
    JSON fx = JSON::object();
    fx.set("track", JSON(1.0));
    fx.set("effect", JSON("com.Aestrastudios.comp"));
    check(statusOf(call(service, req("add_effect", fx))) == "ok", "insert the compressor on the 808 track");
    auto comp = subCh->getEffectChain().getPlugin(0);
    if (!comp) return 1;
    // Both units pass through the sampler's output trim, so the levels at the
    // compressor are far below the files: the 808 sits near -42 dB RMS, the
    // kick key peaks near -19 dB. -38 dB lies between them.
    comp->setParameter(AestraComp::kThreshold, 22.0f / 60.0f);
    comp->setParameter(AestraComp::kRatio, 1.0f);
    comp->setParameter(AestraComp::kAttack, 0.0f);
    comp->setParameter(AestraComp::kRelease, 0.05f);
    comp->setParameter(AestraComp::kSidechain, 1.0f);

    const auto render = [&](const std::string& label) {
        JSON a = JSON::object();
        a.set("file", JSON(out));
        a.set("tail", JSON(0.0));
        JSON r = call(service, req("render_song", a));
        check(statusOf(r) == "ok", "render " + label);
        return readFloatWav(out);
    };

    const auto noSend = render("without the send");

    AudioRoute key;
    key.targetChannelId = subCh->getChannelId();
    key.sidechainOnly = true;
    key.postFader = false;
    kickCh->addSend(key);
    const auto withSend = render("with the sidechain send");

    comp->setParameter(AestraComp::kSidechain, 0.0f);
    const auto sidechainOff = render("with the send, Sidechain off");

    const double ducking = diffDb(withSend, noSend);
    const double ignored = diffDb(sidechainOff, noSend);
    const auto energy = [](const std::vector<float>& x) {
        double acc = 0.0;
        for (float v : x) acc += static_cast<double>(v) * v;
        return acc;
    };
    check(energy(withSend) < energy(noSend), "and the difference is a reduction: the render with the key is quieter");
    check(ducking > -70.0, "the sidechain send changes the render: the 808 is ducked (difference " +
                               std::to_string(ducking) + " dB)");
    check(ignored < -100.0, "with Sidechain off the send changes nothing (difference " + std::to_string(ignored) +
                                " dB)");

    std::filesystem::remove(out);
    std::filesystem::remove(kick);
    std::filesystem::remove(sub);
    if (g_failures == 0) {
        std::cout << "CompSidechainEndToEndTest: all checks passed\n";
        return 0;
    }
    std::cout << "CompSidechainEndToEndTest: " << g_failures << " failure(s)\n";
    return 1;
}
