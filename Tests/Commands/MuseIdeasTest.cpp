// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseIdeasTest — V83-MU1 (FD-63): the Muse Quiet idea engine writes next-bar ideas from rules.
//   1. nextBarStart: the first whole bar after the last note ends;
//   2. the board's D minor phrase: "Answer bar 1, a step up" keeps the rhythm, moves each pitch one diatonic step,
//      and lands the last note on a tone of the bar-2 chord;
//   3. "Walk down to the tonic" is diatonic, descending, inside the bar, and ends on D;
//   4. edge cases: nothing to answer, no key, determinism, range, maxIdeas.

#include "Harmony/HarmonyAnalysis.h"
#include "Muse/MuseIdeas.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio::Harmony;
using namespace Aestra::Audio::Muse;

static int g_failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond << "\n";                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static HarmonyNote hn(double s, double d, int p, float v = 0.8f) { return HarmonyNote{s, d, p, v}; }

// A4 C5 Bb4 | A4 G4 F4, with a Dm stab under it: the board's phrase in D minor.
static std::vector<HarmonyNote> boardPhrase() {
    return {hn(0, 0.5, 69, 0.9f), hn(0.5, 0.5, 72, 0.8f), hn(1, 1, 70, 0.7f), hn(2, 0.5, 69, 0.6f),
            hn(2.5, 0.5, 67, 0.5f), hn(3, 1, 65, 0.4f),
            hn(0, 1.75, 50), hn(0, 1.75, 53), hn(0, 1.75, 57)};
}

static HarmonyTimeline dMinorWithBbAtBar2() {
    HarmonyTimeline t;
    t.key.tonic = 2;
    t.key.mode = KeyMode::Minor;
    t.key.confidence = 0.9;
    ChordSpan dm;
    dm.startBeat = 0; dm.endBeat = 4; dm.root = 2; dm.quality = ChordQuality::Minor;
    ChordSpan bb;
    bb.startBeat = 4; bb.endBeat = 8; bb.root = 10; bb.quality = ChordQuality::Major;
    t.chords = {dm, bb};
    return t;
}

static bool inDMinor(int pitch) {
    static const int pcs[] = {2, 4, 5, 7, 9, 10, 0};
    int pc = ((pitch % 12) + 12) % 12;
    return std::find(std::begin(pcs), std::end(pcs), pc) != std::end(pcs);
}

static const Idea* find(const std::vector<Idea>& v, const std::string& label) {
    for (const auto& i : v) if (i.label == label) return &i;
    return nullptr;
}

static bool sameIdeas(const std::vector<Idea>& a, const std::vector<Idea>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].label != b[i].label || a[i].notes.size() != b[i].notes.size()) return false;
        for (size_t n = 0; n < a[i].notes.size(); ++n) {
            const auto &x = a[i].notes[n], &y = b[i].notes[n];
            if (x.startBeat != y.startBeat || x.durationBeats != y.durationBeats || x.pitch != y.pitch ||
                x.velocity != y.velocity) return false;
        }
    }
    return true;
}

static void testNextBarStart() {
    CHECK(nextBarStart({hn(3, 0.5, 60)}, 4.0) == 4.0);
    CHECK(nextBarStart({hn(7, 1, 60)}, 4.0) == 8.0);
    CHECK(nextBarStart({}, 4.0) == -1.0);
    CHECK(nextBarStart(boardPhrase(), 4.0) == 4.0);
}

static void testAnswerStepUp() {
    auto ideas = nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0);
    CHECK(!ideas.empty());
    CHECK(ideas.size() <= 3);
    CHECK(!ideas.empty() && ideas[0].label == "Answer bar 1, a step up");
    const Idea* a = find(ideas, "Answer bar 1, a step up");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->notes.size() == 6);
    const double starts[] = {4, 4.5, 5, 6, 6.5, 7};
    const int ups[] = {70, 74, 72, 70, 69};
    const float vels[] = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f};
    for (size_t i = 0; i < a->notes.size() && i < 6; ++i) {
        CHECK(a->notes[i].startBeat == starts[i]);
        CHECK(a->notes[i].velocity == vels[i]);
        if (i < 5) CHECK(a->notes[i].pitch == ups[i]);
    }
    // Bar-2 chord is Bb (Bb D F): the last note lands on one of its tones, nearest to the stepped G4 (F4).
    int lastPc = a->notes.back().pitch % 12;
    CHECK(lastPc == 10 || lastPc == 2 || lastPc == 5);
    CHECK(a->notes.back().pitch == 65);
    // Same rhythm lengths as the source melody.
    CHECK(a->notes[0].durationBeats == 0.5);
    CHECK(a->notes[2].durationBeats == 1.0);
}

static void testRepeatLandsOnRoot() {
    auto ideas = nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0);
    const Idea* r = find(ideas, "Repeat bar 1, land on the root");
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->notes.size() == 6);
    CHECK(r->notes[0].pitch == 69);
    CHECK(r->notes[4].pitch == 67);
    CHECK(r->notes.back().pitch == 70); // Bb root nearest F4 (65): Bb4 is 5 away, Bb3 (58) is 7 away
}

static void testWalkToTonic() {
    auto ideas = nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0);
    const Idea* w = find(ideas, "Walk down to the tonic");
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(!w->notes.empty());
    CHECK(w->notes.back().pitch % 12 == 2);
    double total = 0.0;
    for (size_t i = 0; i < w->notes.size(); ++i) {
        CHECK(inDMinor(w->notes[i].pitch));
        CHECK(w->notes[i].startBeat >= 4.0 && w->notes[i].startBeat + w->notes[i].durationBeats <= 8.0 + 1e-9);
        if (i > 0) CHECK(w->notes[i].pitch < w->notes[i - 1].pitch);
        total += w->notes[i].durationBeats;
    }
    CHECK(std::fabs(total - 4.0) < 1e-9);
    // Source ends on F4: E4, then D4 reached early and held for the rest of the bar.
    CHECK(w->notes.size() == 2);
    CHECK(w->notes[0].pitch == 64 && w->notes[1].pitch == 62);
    CHECK(w->notes[1].durationBeats == 3.0);
}

static void testWalkFullBar() {
    // Source ends on Bb4: A G F E, the final E4 replaced by the nearest tonic D4.
    std::vector<HarmonyNote> n = {hn(0, 1, 72), hn(1, 1, 70)};
    auto ideas = nextBarIdeas(n, dMinorWithBbAtBar2(), 4.0, 4.0);
    const Idea* w = find(ideas, "Walk down to the tonic");
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(w->notes.size() == 4);
    if (w->notes.size() == 4) {
        CHECK(w->notes[0].pitch == 69 && w->notes[1].pitch == 67 && w->notes[2].pitch == 65);
        CHECK(w->notes[3].pitch == 62);
    }
}

static void testChordFallbackFromAnalysis() {
    // Harmony built from the source alone: nothing sounds at beat 4, so the chord the bar ends on is used.
    auto notes = boardPhrase();
    HarmonyTimeline h = analyzeHarmony(notes, 4.0);
    auto ideas = nextBarIdeas(notes, h, 4.0, 4.0);
    CHECK(!ideas.empty());
    for (const auto& i : ideas)
        for (const auto& nn : i.notes) CHECK(nn.startBeat >= 4.0 && nn.startBeat < 8.0);
}

static void testEdges() {
    HarmonyTimeline noKey;
    CHECK(nextBarIdeas({}, dMinorWithBbAtBar2(), 4.0, 4.0).empty());
    CHECK(nextBarIdeas(boardPhrase(), noKey, 4.0, 4.0).empty());
    CHECK(nextBarIdeas({}, noKey, 4.0, 0.0).empty());
    // Nothing before the target bar.
    CHECK(nextBarIdeas({hn(8, 1, 60)}, dMinorWithBbAtBar2(), 4.0, 4.0).empty());

    auto a = nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0);
    auto b = nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0);
    CHECK(sameIdeas(a, b));

    for (const auto& i : a) {
        CHECK(!i.notes.empty());
        for (const auto& nn : i.notes) {
            CHECK(nn.startBeat >= 4.0 && nn.startBeat < 8.0);
            CHECK(nn.startBeat + nn.durationBeats <= 8.0 + 1e-9);
            CHECK(nn.pitch >= 0 && nn.pitch <= 127);
        }
    }
    CHECK(nextBarIdeas(boardPhrase(), dMinorWithBbAtBar2(), 4.0, 4.0, 1).size() == 1);

    // Extreme registers stay in range.
    auto hi = nextBarIdeas({hn(0, 1, 127), hn(1, 1, 126)}, dMinorWithBbAtBar2(), 4.0, 4.0);
    auto lo = nextBarIdeas({hn(0, 1, 0), hn(1, 1, 1)}, dMinorWithBbAtBar2(), 4.0, 4.0);
    for (const auto* v : {&hi, &lo})
        for (const auto& i : *v)
            for (const auto& nn : i.notes) CHECK(nn.pitch >= 0 && nn.pitch <= 127);
}

int main() {
    testNextBarStart();
    testAnswerStepUp();
    testRepeatLandsOnRoot();
    testWalkToTonic();
    testWalkFullBar();
    testChordFallbackFromAnalysis();
    testEdges();
    if (g_failures == 0) std::cout << "MuseIdeasTest OK\n";
    return g_failures == 0 ? 0 : 1;
}
