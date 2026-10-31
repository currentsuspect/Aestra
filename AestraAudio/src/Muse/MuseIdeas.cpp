// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-MU1 (FD-63): the Muse Quiet idea engine. Rules only; see MuseIdeas.h.
#include "Muse/MuseIdeas.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace Muse {

namespace {

using Harmony::HarmonyNote;

constexpr double kEps = 1e-6;

int clampPitch(int p) { return std::max(0, std::min(127, p)); }
int pcOf(int p) { return ((p % 12) + 12) % 12; }

struct Scale {
    bool in[12] = {};
    explicit Scale(const Harmony::Key& key) {
        static const int major[] = {0, 2, 4, 5, 7, 9, 11};
        static const int minor[] = {0, 2, 3, 5, 7, 8, 10};
        const int* steps = key.mode == Harmony::KeyMode::Minor ? minor : major;
        for (int i = 0; i < 7; ++i) in[(key.tonic + steps[i]) % 12] = true;
    }
    // Next scale tone strictly above / below p (a chromatic p moves to the nearest scale tone that way).
    int stepUp(int p) const {
        for (int q = p + 1; q <= 127; ++q) if (in[pcOf(q)]) return q;
        return clampPitch(p);
    }
    int stepDown(int p) const {
        for (int q = p - 1; q >= 0; --q) if (in[pcOf(q)]) return q;
        return clampPitch(p);
    }
};

// Nearest pitch to `from` whose pitch class satisfies the predicate; ties go down.
template <typename Pred>
int nearest(int from, Pred ok) {
    for (int d = 0; d <= 127; ++d) {
        if (from - d >= 0 && ok(pcOf(from - d))) return from - d;
        if (from + d <= 127 && ok(pcOf(from + d))) return from + d;
    }
    return clampPitch(from);
}

// The melody of the bar [srcStart, srcEnd): the highest note per onset, minus anything an octave or more under the
// median pitch (bass lines and chord roots that sit alone at an onset).
std::vector<HarmonyNote> extractMelody(const std::vector<HarmonyNote>& notes, double srcStart, double srcEnd) {
    std::vector<HarmonyNote> inBar;
    for (const auto& n : notes)
        if (n.startBeat >= srcStart - kEps && n.startBeat < srcEnd - kEps && n.durationBeats > 0.0) inBar.push_back(n);
    std::stable_sort(inBar.begin(), inBar.end(), [](const HarmonyNote& a, const HarmonyNote& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        return a.pitch > b.pitch;
    });
    std::vector<HarmonyNote> top;
    for (const auto& n : inBar)
        if (top.empty() || n.startBeat - top.back().startBeat > kEps) top.push_back(n);
    if (top.empty()) return top;
    std::vector<int> ps;
    for (const auto& n : top) ps.push_back(n.pitch);
    std::sort(ps.begin(), ps.end());
    const int median = ps[ps.size() / 2];
    std::vector<HarmonyNote> mel;
    for (const auto& n : top)
        if (n.pitch >= median - 12) mel.push_back(n);
    return mel;
}

bool sameNotes(const std::vector<IdeaNote>& a, const std::vector<IdeaNote>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].startBeat != b[i].startBeat || a[i].durationBeats != b[i].durationBeats || a[i].pitch != b[i].pitch ||
            a[i].velocity != b[i].velocity)
            return false;
    return true;
}

} // namespace

double nextBarStart(const std::vector<HarmonyNote>& notes, double beatsPerBar) {
    if (notes.empty() || beatsPerBar <= 0.0) return -1.0;
    double end = 0.0;
    for (const auto& n : notes) end = std::max(end, n.startBeat + std::max(0.0, n.durationBeats));
    return std::ceil(end / beatsPerBar - 1e-9) * beatsPerBar;
}

std::vector<Idea> nextBarIdeas(const std::vector<HarmonyNote>& notes, const Harmony::HarmonyTimeline& harmony,
                               double beatsPerBar, double barStartBeat, int maxIdeas) {
    std::vector<Idea> out;
    if (maxIdeas <= 0 || beatsPerBar <= 0.0 || !harmony.key.valid()) return out;

    const double srcStart = barStartBeat - beatsPerBar;
    const auto melody = extractMelody(notes, srcStart, barStartBeat);
    if (melody.empty()) return out;

    const Scale scale(harmony.key);
    const int tonic = harmony.key.tonic;
    const int barNumber = static_cast<int>(std::floor(srcStart / beatsPerBar + 0.5)) + 1;

    // Target chord: the new bar's chord, else the one the source bar ends on, else the tonic triad.
    int root = -1;
    Harmony::ChordQuality quality = Harmony::ChordQuality::Major;
    const Harmony::ChordSpan& here = harmony.chordAt(barStartBeat);
    if (!here.isNoChord()) {
        root = here.root;
        quality = here.quality;
    } else {
        const Harmony::ChordSpan& before = harmony.chordAt(barStartBeat - 0.01);
        if (!before.isNoChord()) {
            root = before.root;
            quality = before.quality;
        }
    }
    if (root < 0) {
        root = tonic;
        quality = harmony.key.mode == Harmony::KeyMode::Minor ? Harmony::ChordQuality::Minor
                                                              : Harmony::ChordQuality::Major;
    }
    const std::vector<int> tones = Harmony::chordTones(root, quality);
    auto isTone = [&](int pc) { return std::find(tones.begin(), tones.end(), pc) != tones.end(); };

    auto shifted = [&](const HarmonyNote& n) {
        IdeaNote o;
        o.startBeat = n.startBeat + beatsPerBar;
        o.durationBeats = std::min(n.durationBeats, barStartBeat + beatsPerBar - o.startBeat);
        o.pitch = clampPitch(n.pitch);
        o.velocity = n.velocity;
        return o;
    };

    std::vector<Idea> all;

    // 1. Answer: same rhythm, one scale step up, last note onto a chord tone.
    {
        Idea idea;
        idea.label = "Answer bar " + std::to_string(barNumber) + ", a step up";
        for (const auto& n : melody) {
            IdeaNote o = shifted(n);
            o.pitch = scale.stepUp(o.pitch);
            idea.notes.push_back(o);
        }
        idea.notes.back().pitch = nearest(idea.notes.back().pitch, isTone);
        all.push_back(std::move(idea));
    }

    // 2. Repeat: same pitches, last note onto the chord root.
    {
        Idea idea;
        idea.label = "Repeat bar " + std::to_string(barNumber) + ", land on the root";
        for (const auto& n : melody) idea.notes.push_back(shifted(n));
        idea.notes.back().pitch = nearest(idea.notes.back().pitch, [&](int pc) { return pc == root; });
        all.push_back(std::move(idea));
    }

    // 3. Walk: one note a beat, stepping down from just under the melody's last note to the tonic.
    {
        Idea idea;
        idea.label = "Walk down to the tonic";
        const int count = std::max(1, static_cast<int>(std::floor(beatsPerBar + kEps)));
        int p = scale.stepDown(clampPitch(melody.back().pitch));
        for (int i = 0; i < count; ++i) {
            const double start = barStartBeat + i;
            if (pcOf(p) == tonic) { // reached the tonic early: hold it for the rest of the bar
                idea.notes.push_back({start, barStartBeat + beatsPerBar - start, p, 0.8f});
                break;
            }
            if (i == count - 1) { // last beat: the tonic nearest the walk, kept at or below the step before it
                int t = nearest(p, [&](int pc) { return pc == tonic; });
                if (!idea.notes.empty() && t >= idea.notes.back().pitch) t = nearest(idea.notes.back().pitch - 1,
                    [&](int pc) { return pc == tonic; });
                idea.notes.push_back({start, 1.0, t, 0.8f});
                break;
            }
            idea.notes.push_back({start, 1.0, p, 0.8f});
            p = scale.stepDown(p);
        }
        all.push_back(std::move(idea));
    }

    for (auto& idea : all) {
        if (idea.notes.empty()) continue;
        bool dup = false;
        for (const auto& kept : out)
            if (sameNotes(kept.notes, idea.notes)) { dup = true; break; }
        if (dup) continue;
        out.push_back(std::move(idea));
        if (static_cast<int>(out.size()) >= maxIdeas) break;
    }
    return out;
}

} // namespace Muse
} // namespace Audio
} // namespace Aestra
