// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseParamUnitsTest — effect parameters read and write in their own units (#1405).
//
// Driven from the socket, an agent asked for 3.5 kHz on a bell band. `"3.5k"` was
// refused ("requires a number"), `3500` was refused ("maximum is 1.000000"), and
// 0.5 came back as "800Hz" — so the only way to reach a frequency was to bisect.
// get_effects now publishes each parameter's unit, range, curve and step labels,
// set_effect_param accepts the value written in that unit, and the reply reports
// what the plugin now displays.
//
// The plugin-level half is the important one: the table that declares those
// ranges is checked against the strings each plugin prints, so a range can never
// drift from the display it claims to describe.
#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Plugin/AestraComp.h"
#include "Plugin/AestraEQ.h"
#include "Plugin/AestraLimit.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) ++g_failures;
}

JSON call(MuseService& s, const std::string& verb, JSON args) {
    JSON r = JSON::object();
    r.set("id", JSON(1.0));
    r.set("verb", JSON(verb));
    r.set("args", args);
    return JSON::parse(s.handleRequest(r.toString()));
}

std::string statusOf(JSON& r) { return r.has("status") ? r["status"].asString() : "<missing>"; }
std::string messageOf(JSON& r) { return r.has("message") ? r["message"].asString() : "<missing>"; }

JSON effectArgs(double track, double slot, const std::string& effect) {
    JSON a = JSON::object();
    a.set("track", JSON(track));
    if (slot >= 0.0) a.set("slot", JSON(slot));
    a.set("effect", JSON(effect));
    return a;
}

JSON setParamArgs(const std::string& param, JSON value) {
    JSON a = JSON::object();
    a.set("track", JSON(0.0));
    a.set("slot", JSON(0.0));
    a.set("param", JSON(param));
    a.set("value", value);
    return a;
}

// The entry get_effects publishes for one parameter, or an empty object.
JSON findParam(MuseService& service, double track, double slot, const std::string& name) {
    JSON a = JSON::object();
    a.set("track", JSON(track));
    JSON r = call(service, "get_effects", a);
    if (statusOf(r) != "ok" || !r["result"].has("effects")) return JSON::object();
    for (size_t e = 0; e < r["result"]["effects"].size(); ++e) {
        JSON effect = r["result"]["effects"][e];
        if (!effect.has("slot") || effect["slot"].asNumber() != slot) continue;
        for (size_t p = 0; p < effect["params"].size(); ++p) {
            JSON param = effect["params"][p];
            if (param["name"].asString() == name) return param;
        }
    }
    return JSON::object();
}

// One plugin's declarations against the strings it prints: every value it can
// display must read back as the value that produced it.
template <typename Plugin>
void checkRoundTrip(Plugin& plugin, const char* name, int& failures) {
    plugin.initialize(48000.0, 512);
    for (const auto& param : plugin.getParameters()) {
        Aestra::Audio::ParameterTextInfo info;
        if (!plugin.describeParameterText(param.id, info)) continue;
        std::vector<float> positions;
        if (info.curve == Aestra::Audio::ParamCurve::Stepped) {
            // Only a step's own position prints as that step; 0.5 between two
            // steps is not a value the display can name.
            for (size_t i = 0; i < info.labels.size(); ++i)
                positions.push_back(static_cast<float>(i) / static_cast<float>(info.labels.size() - 1));
        } else {
            positions = {0.0f, 0.37f, 1.0f};
        }
        for (float norm : positions) {
            plugin.setParameter(param.id, norm);
            const std::string shown = plugin.getParameterDisplay(param.id);
            float back = -1.0f;
            std::string error;
            const bool parsed = plugin.parseParameterText(param.id, shown, back, error);
            const bool close = parsed && std::abs(back - norm) <= 0.02f;
            check(close, std::string(name) + " " + param.name + " " + shown + " round-trips");
            if (!close) ++failures;
        }
    }
}

} // namespace

int main() {
    if (!Aestra::Audio::PluginManager::getInstance().initialize()) return 1;

    // ---- The declaration and the display agree (all three built-ins) ----
    {
        int failures = 0;
        Aestra::Audio::Plugins::AestraEQ eq;
        Aestra::Audio::Plugins::AestraComp comp;
        Aestra::Audio::Plugins::AestraLimit limit;
        checkRoundTrip(eq, "EQ", failures);
        checkRoundTrip(comp, "Comp", failures);
        checkRoundTrip(limit, "Limit", failures);

        // The user's own case, at the plugin level: the exponential bell range.
        float norm = -1.0f;
        std::string error;
        eq.setParameter(Aestra::Audio::Plugins::AestraEQ::kParamBell1Freq, 0.0f);
        check(eq.parseParameterText(Aestra::Audio::Plugins::AestraEQ::kParamBell1Freq, "3.5k", norm, error),
              "EQ bell frequency accepts 3.5k");
        check(std::abs(norm - 0.8205f) < 1.0e-3f, "3.5k lands on the log-curve position");
        // 80 * 100^0.8205 = 3500 Hz
        float normHz = -1.0f;
        check(eq.parseParameterText(Aestra::Audio::Plugins::AestraEQ::kParamBell1Freq, "3500 Hz", normHz, error),
              "EQ bell frequency accepts '3500 Hz'");
        check(std::abs(normHz - norm) < 1.0e-4f, "3.5k and 3500 Hz are the same value");
    }

    auto tm = std::make_shared<TrackManager>();
    tm->getUnitManager().setPatternManager(&tm->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(4096, 2);
    MuseService::wireHeadlessEngine(tm, engine);
    if (!engine.initialize()) return 1;
    CommandRegistry::initialize();
    MuseService service(tm.get(), &engine);

    // ---- get_effects publishes the units an agent needs ----
    {
        JSON track = JSON::object();
        track.set("name", JSON("Bass"));
        JSON created = call(service, "add_track", track);
        check(statusOf(created) == "ok", "add a track to put an effect on");

        JSON added = call(service, "add_effect", effectArgs(0.0, -1.0, "com.Aestrastudios.eq"));
        check(statusOf(added) == "ok", "insert an EQ on track 0");

        JSON freq = findParam(service, 0.0, 0.0, "Bell 1 Frequency");
        check(!freq.has("acceptsText") ? false : freq["acceptsText"].asBool(), "bell frequency accepts text values");
        check(freq.has("min") && std::abs(freq["min"].asNumber() - 80.0) < 1e-6, "bell frequency min is 80 Hz");
        check(freq.has("max") && std::abs(freq["max"].asNumber() - 8000.0) < 1e-6, "bell frequency max is 8000 Hz");
        check(freq.has("curve") && freq["curve"].asString() == "log", "bell frequency curve is log");
        check(freq.has("unit") && freq["unit"].asString() == "Hz", "bell frequency unit is Hz");

        JSON slope = findParam(service, 0.0, 0.0, "High-Pass Slope");
        check(slope.has("labels") && slope["labels"].size() == 7, "high-pass slope publishes its steps");
        check(slope.has("labels") && slope["labels"][3].asString() == "36 dB/oct", "slope step 3 is 36 dB/oct");
    }

    // ---- The value written in a unit lands (the bug from #1405) ----
    {
        JSON r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("3.5k")));
        check(statusOf(r) == "ok", "'3.5k' is accepted");
        check(r.has("result") && r["result"].has("display") && r["result"]["display"].asString() == "3.5kHz",
              "reply reports the value the plugin displays");

        JSON freq = findParam(service, 0.0, 0.0, "Bell 1 Frequency");
        check(std::abs(freq["value"].asNumber() - 0.8205) < 1e-3, "the bell lands at 3.5 kHz, not at 1.0");
        check(freq["display"].asString() == "3.5kHz", "get_effects agrees with the reply");

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("3500 Hz")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "3.5kHz",
              "the same value written with a space and a unit lands too");

        // Stepped parameters take their own label, or the number in it.
        r = call(service, "set_effect_param", setParamArgs("High-Pass Slope", JSON("36")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "36 dB/oct",
              "a stepped parameter takes the number from its label");
        r = call(service, "set_effect_param", setParamArgs("Bell 1 Type", JSON("Notch")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "Notch",
              "a stepped parameter takes its label");
    }

    // ---- Normalized values keep working: nothing existing may change meaning ----
    {
        JSON r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON(0.5)));
        check(statusOf(r) == "ok", "a plain 0.5 is still normalized");
        check(r["result"]["display"].asString() == "800Hz", "0.5 is the middle of the log range: 800Hz");
    }

    // ---- Refusals name the range, the unit, or the steps ----
    {
        JSON r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("40 kHz")));
        const std::string msg = messageOf(r);
        check(statusOf(r) != "ok", "40 kHz is refused");
        check(msg.find("out of range") != std::string::npos && msg.find("80 Hz .. 8000 Hz") != std::string::npos,
              "the refusal quotes the range: " + msg);

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("3.5 dB")));
        check(statusOf(r) != "ok" && messageOf(r).find("wrong unit") != std::string::npos,
              "a decibel value on a frequency is refused as a wrong unit");

        r = call(service, "set_effect_param", setParamArgs("High-Pass Slope", JSON("30 dB/oct")));
        check(statusOf(r) != "ok" && messageOf(r).find("expected one of") != std::string::npos,
              "an unknown slope lists the steps it accepts");

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("wobble")));
        check(statusOf(r) != "ok" && messageOf(r).find("cannot read 'wobble'") != std::string::npos,
              "unparseable text is refused by name");
    }

    // ---- A parameter with no declared units says so instead of pretending ----
    {
        JSON added = call(service, "add_effect", effectArgs(0.0, -1.0, "com.Aestrastudios.comp"));
        check(statusOf(added) == "ok", "insert a compressor in slot 1");
        JSON hpf = findParam(service, 0.0, 1.0, "Detector HPF");
        check(hpf.has("acceptsText") && !hpf["acceptsText"].asBool(),
              "a parameter whose display has an Off position publishes no unit");
        check(!hpf.has("min"), "no fake range is published for it");

        JSON args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("slot", JSON(1.0));
        args.set("param", JSON("Detector HPF"));
        args.set("value", JSON("200Hz"));
        JSON r = call(service, "set_effect_param", args);
        check(statusOf(r) != "ok" && messageOf(r).find("normalized value 0..1") != std::string::npos,
              "it refuses text and says only 0..1 is accepted");

        args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("slot", JSON(1.0));
        args.set("param", JSON("Attack"));
        args.set("value", JSON("0ms"));
        r = call(service, "set_effect_param", args);
        check(statusOf(r) == "ok", "the compressor's own '0ms' display reads back");

        args = JSON::object();
        args.set("track", JSON(0.0));
        args.set("slot", JSON(1.0));
        args.set("param", JSON("Ratio"));
        args.set("value", JSON("4:1"));
        r = call(service, "set_effect_param", args);
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "4:1", "a ratio reads as '4:1'");
    }

    // ---- A stepped parameter publishes its labels, not a 0..1 range dressed in a unit ----
    {
        JSON slope = findParam(service, 0.0, 0.0, "High-Pass Slope");
        check(!slope.has("min") && !slope.has("max"),
              "a stepped parameter publishes no numeric bounds (its labels are the range)");
        check(slope.has("labels") && slope["labels"].size() == 7, "and still publishes its labels");
    }

    // ---- "=0.5" is the display value, for a unitless parameter whose own range covers 0..1 ----
    {
        JSON q = findParam(service, 0.0, 0.0, "Bell 1 Q");
        check(q.has("min") && std::abs(q["min"].asNumber() - 0.1) < 1e-6, "bell Q min is 0.1");
        check(q.has("max") && std::abs(q["max"].asNumber() - 10.0) < 1e-6, "bell Q max is 10");
        check(q.has("note") && q["note"].asString().find("=0.5") != std::string::npos,
              "bell Q says how to write its own 0.5");

        JSON r = call(service, "set_effect_param", setParamArgs("Bell 1 Q", JSON("=0.50")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "0.50",
              "'=0.50' sets Q 0.50, not the middle of the range");

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Q", JSON("0.5")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "5.05",
              "a bare 0.5 is still normalized: the middle of 0.1..10");

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Q", JSON("=")));
        check(statusOf(r) != "ok" && messageOf(r).find("'=' needs a value") != std::string::npos,
              "'=' with nothing after it is refused by name");
    }

    // ---- A mistyped unit is refused, not read as "some Hz" ----
    {
        JSON r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("3500 xyzHz")));
        check(statusOf(r) != "ok" && messageOf(r).find("wrong unit") != std::string::npos,
              "'3500 xyzHz' is refused as a wrong unit: " + messageOf(r));

        r = call(service, "set_effect_param", setParamArgs("Bell 1 Frequency", JSON("3.5kHz")));
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "3.5kHz",
              "'3.5kHz' still reads as 3.5 kHz");
    }

    // ---- The limiter's ceiling, the way a mastering move is spoken ----
    {
        JSON added = call(service, "add_effect", effectArgs(-1.0, -1.0, "com.Aestrastudios.limiter"));
        check(statusOf(added) == "ok", "insert a limiter on the master");
        JSON args = JSON::object();
        args.set("track", JSON(-1.0));
        args.set("slot", JSON(0.0));
        args.set("param", JSON("Ceiling"));
        args.set("value", JSON("-1.0 dB"));
        JSON r = call(service, "set_effect_param", args);
        check(statusOf(r) == "ok" && r["result"]["display"].asString() == "-1.0dB",
              "a master ceiling accepts '-1.0 dB'");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
