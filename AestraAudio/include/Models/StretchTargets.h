// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Stretch-to-Target: which clip lengths are worth offering, and in what order (SPEC-001, V8-W1).
//
// Two independent decisions that used to be one fixed 1 / 2 / 4 / 8 menu:
//   1. the GENERATOR: which target lengths make musical sense for a clip of L bars
//      (engine-agnostic musical truth);
//   2. the RANKER: which of those is the best trade given what the engine can do
//      (engine-aware cost).
// The generator never changes when the engine does; only distanceCost() and the label wording do.
//
// Pure functions of the clip length (and, for the "match arrangement" row, the lengths of other
// clips). No model, no UI, no engine: callable and testable anywhere.
//
// Where SPEC-001 disagrees with itself, the choice made here is written next to the code:
//   - window: the spec writes [L/4, L*2] ∩ [4, 256], which leaves a 1-bar clip with no target
//     at all, while §5 says the menu is "the only affordance that works for a 1-bar clip".
//     The floor here is 1 bar.
//   - distance cost: §4's prose says the varispeed era is quadratic, but its formula and every
//     worked example are linear. The formula as written is implemented (see distanceCost()).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/// Why a length is on the list. A length in more than one family earns its largest bonus.
enum class StretchTargetClass : uint8_t {
    Integer,    ///< the nearest whole bar: the honest floor
    Echo,       ///< the length of a clip elsewhere in the arrangement (never ranked)
    Fractional, ///< L/2, L/3, L/4, L*2, L*3/2 when they land on a whole bar
    Structural, ///< 6, 10, 20, 30: odd but real phrase lengths
    Phrase3,    ///< 12, 24, 48, 96: the 12-bar family
    Phrase4,    ///< 4, 8, 16, 32, 64, 128: the primary phrase grid
};

/// The engine the labels and the cost describe. Only varispeed exists today; the generator
/// does not depend on this, which is the point of splitting it from the ranker.
enum class StretchEngine : uint8_t { Varispeed };

struct StretchTarget {
    int bars{0};
    double score{0.0};      ///< cost - bonus, in bar units; lower is better; negative = worth a nudge
    double speed{1.0};      ///< L / bars: the playback rate that fills the target
    StretchTargetClass cls{StretchTargetClass::Integer};
    bool approximate{false}; ///< the ratio falls outside the engine's 0.25x-4x envelope
};

struct StretchTargetList {
    std::vector<StretchTarget> ranked;      ///< best first, at most `maxRanked`
    std::vector<StretchTarget> arrangement; ///< the "match arrangement" row: context, never auto-ranked
};

namespace stretch_detail {

constexpr int kPhrase4[] = {4, 8, 16, 32, 64, 128};
constexpr int kPhrase3[] = {12, 24, 48, 96};
constexpr int kStructural[] = {6, 10, 20, 30};
constexpr double kMinBars = 1.0;   // a target is at least one bar
constexpr double kMaxBars = 256.0; // and no more than the kernel's practical ceiling
constexpr double kMinRatio = 0.25; // the engine's varispeed envelope, from #747
constexpr double kMaxRatio = 4.0;

/// "this target is effectively N bars closer than it is, because it lands on the phrase grid."
constexpr double bonus(StretchTargetClass c) {
    switch (c) {
    case StretchTargetClass::Phrase4: return 2.5;
    case StretchTargetClass::Phrase3: return 2.0;
    case StretchTargetClass::Structural: return 1.5;
    case StretchTargetClass::Fractional: return 1.0;
    case StretchTargetClass::Echo: return 0.5;
    case StretchTargetClass::Integer: return 0.0;
    }
    return 0.0;
}

inline bool isWholeBar(double v) { return v >= 0.5 && std::fabs(v - std::round(v)) < 1e-9; }

} // namespace stretch_detail

/// The engine-selected distance layer: how much a move of `bars` costs. This is what swaps when
/// the engine ships. SPEC-001 §4's prose calls the varispeed era quadratic, but its own formula
/// (score = |L - T| - bonus) and all of its worked examples are linear; the formula as written
/// is what is implemented, and the disagreement is recorded in the spec.
inline double distanceCost(StretchEngine /*engine*/, double bars) { return std::fabs(bars); }

/// Every length worth offering for a clip of `lengthBars`, with its class. Engine-agnostic:
/// no cost, no order, no echo (echo is context, handled by the ranker's second row).
inline std::vector<StretchTarget> generateStretchTargets(double lengthBars) {
    using namespace stretch_detail;
    std::vector<StretchTarget> out;
    if (!(lengthBars > 0.0) || !std::isfinite(lengthBars)) {
        return out;
    }
    const double lo = std::max(lengthBars / 4.0, kMinBars);
    const double hi = std::min(lengthBars * 2.0, kMaxBars);

    auto add = [&](double bars, StretchTargetClass cls) {
        if (!isWholeBar(bars) || bars < lo - 1e-9 || bars > hi + 1e-9) {
            return;
        }
        const int whole = static_cast<int>(std::lround(bars));
        if (std::fabs(static_cast<double>(whole) - lengthBars) < 1e-9) {
            return; // T == L: a no-op suggestion is noise
        }
        for (auto& existing : out) {
            if (existing.bars == whole) {
                if (bonus(cls) > bonus(existing.cls)) {
                    existing.cls = cls; // the strongest family it belongs to
                }
                return;
            }
        }
        StretchTarget t;
        t.bars = whole;
        t.cls = cls;
        t.speed = lengthBars / static_cast<double>(whole);
        const double ratio = static_cast<double>(whole) / lengthBars;
        t.approximate = ratio < kMinRatio - 1e-9 || ratio > kMaxRatio + 1e-9;
        out.push_back(t);
    };

    for (int b : kPhrase4) add(b, StretchTargetClass::Phrase4);
    for (int b : kPhrase3) add(b, StretchTargetClass::Phrase3);
    for (int b : kStructural) add(b, StretchTargetClass::Structural);
    add(lengthBars / 2.0, StretchTargetClass::Fractional);
    add(lengthBars / 3.0, StretchTargetClass::Fractional);
    add(lengthBars / 4.0, StretchTargetClass::Fractional);
    add(lengthBars * 2.0, StretchTargetClass::Fractional);
    add(lengthBars * 1.5, StretchTargetClass::Fractional);
    add(std::round(lengthBars), StretchTargetClass::Integer); // the honest floor
    return out;
}

/// The ranked top `maxRanked` plus the "match arrangement" row.
/// `echoLengthsBars`: whole-bar lengths of clips on other lanes. They are shown as context in
/// their own row; they are never mixed into the ranking ("a random 27-bar clip must never
/// present itself as a musical suggestion").
inline StretchTargetList rankStretchTargets(double lengthBars, const std::vector<int>& echoLengthsBars = {},
                                            StretchEngine engine = StretchEngine::Varispeed,
                                            size_t maxRanked = 5) {
    using namespace stretch_detail;
    StretchTargetList list;
    list.ranked = generateStretchTargets(lengthBars);
    if (list.ranked.empty()) {
        return list;
    }
    for (auto& t : list.ranked) {
        t.score = distanceCost(engine, lengthBars - static_cast<double>(t.bars)) - bonus(t.cls);
    }
    std::sort(list.ranked.begin(), list.ranked.end(), [&](const StretchTarget& a, const StretchTarget& b) {
        if (a.score != b.score) return a.score < b.score;
        const double da = std::fabs(lengthBars - a.bars), db = std::fabs(lengthBars - b.bars);
        if (da != db) return da < db;
        return a.bars < b.bars;
    });
    if (list.ranked.size() > maxRanked) {
        list.ranked.resize(maxRanked);
    }

    std::vector<int> echoes;
    for (int e : echoLengthsBars) {
        if (e >= 1 && static_cast<double>(e) >= std::max(lengthBars / 4.0, kMinBars) - 1e-9 &&
            static_cast<double>(e) <= std::min(lengthBars * 2.0, kMaxBars) + 1e-9 &&
            std::fabs(static_cast<double>(e) - lengthBars) > 1e-9 && std::find(echoes.begin(), echoes.end(), e) == echoes.end()) {
            echoes.push_back(e);
        }
    }
    std::sort(echoes.begin(), echoes.end(), [&](int a, int b) {
        const double da = std::fabs(lengthBars - a), db = std::fabs(lengthBars - b);
        return da != db ? da < db : a < b;
    });
    for (int e : echoes) {
        StretchTarget t;
        t.bars = e;
        t.cls = StretchTargetClass::Echo;
        t.speed = lengthBars / static_cast<double>(e);
        t.score = distanceCost(engine, lengthBars - e) - bonus(StretchTargetClass::Echo);
        const double ratio = static_cast<double>(e) / lengthBars;
        t.approximate = ratio < kMinRatio - 1e-9 || ratio > kMaxRatio + 1e-9;
        list.arrangement.push_back(t);
    }
    return list;
}

/// The one formatter the menu (and, later, the edge-drag label) uses. The interface must always
/// describe what the engine is actually doing: a varispeed fit is `x1.25 speed · pitch follows`,
/// never "stretch". Only a real stretch engine earns the word.
inline std::string formatStretchTarget(const StretchTarget& t, StretchEngine engine = StretchEngine::Varispeed) {
    (void)engine; // varispeed is the only engine; its wording is the only wording
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%.2f", t.speed);
    std::string s = std::to_string(t.bars) + (t.bars == 1 ? " bar" : " bars") + " \xC2\xB7 \xC3\x97" + speed +
                    " speed \xC2\xB7 pitch follows";
    if (t.approximate) {
        s += " \xC2\xB7 approximate";
    }
    return s;
}

} // namespace Audio
} // namespace Aestra
