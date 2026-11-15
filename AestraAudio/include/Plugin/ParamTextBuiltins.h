// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The display side of the built-in effects' parameters, declared (#1405).
//
// Each plugin already prints these values — getParameterDisplay() turns 0.5 into
// "800Hz". What was missing was the same mapping stated so that a caller can go
// the other way: "800Hz" or "3.5k" back to 0.5. That used to cost an agent a
// bisection per parameter, because the mapping (log between band-specific
// limits, gains centred on 0 dB, authored in normalised units) was private.
//
// The tables are templates over the plugin type so they can name its parameter
// ids without including the plugin header: EQ.h includes this, not the other way
// round. Every range below is the one the plugin's own display mapping uses, and
// ParamTextBuiltinsTest checks the two agree by writing the limit in and reading
// the display back.
//
// A parameter that is not listed here stays normalized-only, and get_effects
// says so rather than publishing a 0..1 "range" dressed as a unit.

#include "Plugin/ParamText.h"

#include <initializer_list>

namespace Aestra {
namespace Audio {

namespace ParamTextDetail {

inline bool stepped(std::initializer_list<const char*> labels, ParameterTextInfo& out) {
    out.curve = ParamCurve::Stepped;
    out.labels.clear();
    for (const char* label : labels) out.labels.emplace_back(label);
    out.min = 0.0;
    out.max = 1.0;
    out.unit.clear();
    return out.labels.size() >= 2;
}

inline bool linear(double minValue, double maxValue, const char* unit, ParameterTextInfo& out) {
    out.curve = ParamCurve::Linear;
    out.min = minValue;
    out.max = maxValue;
    out.unit = unit ? unit : "";
    out.labels.clear();
    return true;
}

inline bool logCurve(double minValue, double maxValue, const char* unit, ParameterTextInfo& out) {
    out.curve = ParamCurve::Log;
    out.min = minValue;
    out.max = maxValue;
    out.unit = unit ? unit : "";
    out.labels.clear();
    return minValue > 0.0 && maxValue > minValue;
}

template <typename EQ>
inline bool describeEQ(uint32_t id, ParameterTextInfo& out) {
    out = ParameterTextInfo{};
    switch (id) {
    // Frequencies are exponential between band-specific limits: norm 0 is the
    // first value here and norm 1 the second, which is what makes "3.5k" land.
    case EQ::kParamHPFFreq: return logCurve(20.0, 500.0, "Hz", out);
    case EQ::kParamLShFreq: return logCurve(40.0, 1000.0, "Hz", out);
    case EQ::kParamBell1Freq: return logCurve(80.0, 8000.0, "Hz", out);
    case EQ::kParamBell2Freq: return logCurve(200.0, 16000.0, "Hz", out);
    case EQ::kParamHShFreq: return logCurve(2000.0, 20000.0, "Hz", out);
    case EQ::kParamLPFFreq: return logCurve(1000.0, 20000.0, "Hz", out);
    // Gains are linear through 0 dB, ±18.
    case EQ::kParamLShGain:
    case EQ::kParamBell1Gain:
    case EQ::kParamBell2Gain:
    case EQ::kParamHShGain:
    case EQ::kParamOutputGain: return linear(-18.0, 18.0, "dB", out);
    // Q is linear 0.1..10.
    case EQ::kParamLShQ:
    case EQ::kParamBell1Q:
    case EQ::kParamBell2Q:
    case EQ::kParamHShQ: return linear(0.1, 10.0, "", out);
    // Slopes pick from a fixed list; the numbers are the labels the display prints.
    case EQ::kParamHPFSlope:
    case EQ::kParamLPFSlope:
        return stepped({"6 dB/oct", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct"}, out);
    case EQ::kParamHPFEnable:
    case EQ::kParamLShEnable:
    case EQ::kParamBell1Enable:
    case EQ::kParamBell2Enable:
    case EQ::kParamHShEnable:
    case EQ::kParamLPFEnable:
    case EQ::kParamBypass: return stepped({"OFF", "ON"}, out);
    case EQ::kParamPolarityInvert: return stepped({"NORMAL", "INV"}, out);
    case EQ::kParamBell1Type:
    case EQ::kParamBell2Type: return stepped({"Bell", "Notch", "Band Pass", "Tilt"}, out);
    case EQ::kParamHPFStereoMode:
    case EQ::kParamLShStereoMode:
    case EQ::kParamBell1StereoMode:
    case EQ::kParamBell2StereoMode:
    case EQ::kParamHShStereoMode:
    case EQ::kParamLPFStereoMode: return stepped({"Stereo", "Left", "Right", "Mid", "Side"}, out);
    default: return false;
    }
}

template <typename Comp>
inline bool describeComp(uint32_t id, ParameterTextInfo& out) {
    out = ParameterTextInfo{};
    switch (id) {
    case Comp::kThreshold: return linear(-60.0, 0.0, "dB", out);
    case Comp::kRatio: return linear(1.0, 20.0, ":1", out);
    case Comp::kAttack: {
        // The mapping's floor is 0.1 ms, but formatMs() rounds to whole
        // milliseconds, so the display prints "0ms" at norm 0. Declaring 0 makes
        // the printed value readable back — the 0.1 ms difference is below the
        // display's own resolution.
        const bool ok = linear(0.0, 100.0, "ms", out);
        out.note = "floor is 0.1 ms; the display rounds to whole milliseconds";
        return ok;
    }
    case Comp::kRelease: return linear(10.0, 1000.0, "ms", out);
    case Comp::kMakeup: return linear(0.0, 24.0, "dB", out);
    case Comp::kKnee: return linear(0.0, 24.0, "dB", out);
    case Comp::kMix: return linear(0.0, 100.0, "%", out);
    case Comp::kInputGain:
    case Comp::kOutputGain: return linear(-24.0, 24.0, "dB", out);
    case Comp::kCompMode: return stepped({"Clean", "Classic", "Optical"}, out);
    case Comp::kOversampling: return stepped({"Off", "2x", "4x"}, out);
    case Comp::kSidechain:
    case Comp::kBypass: return stepped({"OFF", "ON"}, out);
    default:
        // Detector HPF stays normalized-only on purpose: norm 0 prints "Off",
        // so a declared Hz minimum would describe a value the display denies.
        return false;
    }
}

template <typename Limit>
inline bool describeLimit(uint32_t id, ParameterTextInfo& out) {
    out = ParameterTextInfo{};
    switch (id) {
    case Limit::kCeiling: {
        // The display prints "dB", so that is the unit accepted; dBTP is what it
        // means, and the note says so rather than making "-1.0 dB" fail.
        const bool ok = linear(-24.0, 0.0, "dB", out);
        out.note = "true-peak ceiling (dBTP)";
        return ok;
    }
    case Limit::kRelease: return linear(10.0, 500.0, "ms", out);
    case Limit::kReleaseMode: return stepped({"Auto", "Manual"}, out);
    case Limit::kBypass: return stepped({"OFF", "ON"}, out);
    default: return false;
    }
}

/// Describe, then read the text through the same declaration. One path, so a
/// parameter can never accept a value it would not advertise.
template <typename Describe>
inline bool parseWith(const Describe& describe, uint32_t id, const std::string& text, float& normalized,
                      std::string& error) {
    ParameterTextInfo info;
    if (!describe(id, info)) {
        error = "this parameter takes its normalized value 0..1";
        return false;
    }
    return ParamText::resolve(text, info, normalized, error);
}

} // namespace ParamTextDetail
} // namespace Audio
} // namespace Aestra
