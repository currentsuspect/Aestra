// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-HS1 (FD-63): key and chord detection from MIDI. See HarmonyAnalysis.h for the contract.
//
// Pipeline: notes -> key (Krumhansl-Schmuckler over the duration x velocity pitch-class histogram)
//   -> half-bar windows, each a weighted pitch-class vector with extra weight on the lowest sounding note
//   -> every root x quality template scored (cosine similarity, bass-is-root bonus, small key prior)
//   -> Viterbi over the windows with a change penalty that is lower on bar lines
//   -> merge equal neighbours, name them, then carve in the user's overrides.
// Not real-time code: it allocates and is meant for a worker/main thread, on demand.
#include "Harmony/HarmonyAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Aestra {
namespace Audio {
namespace Harmony {

namespace {

constexpr int kQualityCount = static_cast<int>(ChordQuality::Count);

struct QualityInfo {
    std::array<int, 5> intervals; // semitones above the root; -1 ends the list
    const char* suffix;
    double complexity; // subtracted from the score, so a plain triad wins a tie with a coloured chord
    bool minorish;     // lower-case roman numeral
};

// Order must match enum ChordQuality.
const QualityInfo kQualities[kQualityCount] = {
    {{0, 4, 7, -1, -1}, "", 0.00, false},      // Major
    {{0, 3, 7, -1, -1}, "m", 0.00, true},      // Minor
    {{0, 3, 6, -1, -1}, "dim", 0.01, true},    // Diminished
    {{0, 4, 8, -1, -1}, "aug", 0.02, false},   // Augmented
    {{0, 2, 7, -1, -1}, "sus2", 0.02, false},  // Sus2
    {{0, 5, 7, -1, -1}, "sus4", 0.02, false},  // Sus4
    {{0, 4, 7, 10, -1}, "7", 0.03, false},     // Dominant7
    {{0, 4, 7, 11, -1}, "maj7", 0.03, false},  // Major7
    {{0, 3, 7, 10, -1}, "m7", 0.03, true},     // Minor7
    {{0, 3, 6, 10, -1}, "m7b5", 0.04, true},   // HalfDiminished7
    {{0, 3, 6, 9, -1}, "dim7", 0.04, true},    // Diminished7
    {{0, 4, 7, 9, -1}, "6", 0.05, false},      // Major6
    {{0, 3, 7, 9, -1}, "m6", 0.07, true},      // Minor6
    {{0, 2, 4, 7, -1}, "add9", 0.05, false},   // Add9
};

// Template weights: the root and the characteristic tones count fully, the fifth a little less (it is often omitted).
std::array<std::array<double, 12>, kQualityCount> buildTemplates() {
    std::array<std::array<double, 12>, kQualityCount> t{};
    for (int q = 0; q < kQualityCount; ++q) {
        double norm = 0.0;
        for (int i : kQualities[q].intervals) {
            if (i < 0)
                break;
            const double w = (i == 7) ? 0.75 : 1.0;
            t[q][i] = w;
            norm += w * w;
        }
        norm = std::sqrt(norm);
        for (double& v : t[q])
            v /= norm;
    }
    return t;
}
const std::array<std::array<double, 12>, kQualityCount>& templates() {
    static const auto t = buildTemplates();
    return t;
}

constexpr double kMajorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double velocityWeight(float v) { return 0.3 + 0.7 * std::min(1.0f, std::max(0.0f, v)); }

double correlation(const double* a, const double* b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= 12;
    mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da <= 0 || db <= 0) ? 0.0 : num / std::sqrt(da * db);
}

const char* kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
const char* noteName(int pc, bool flats) { return (flats ? kFlatNames : kSharpNames)[((pc % 12) + 12) % 12]; }

bool inScale(const Key& key, int pc) {
    static const bool major[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    // Minor allows the raised seventh, so the dominant chord counts as in key.
    static const bool minor[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1};
    const int d = ((pc - key.tonic) % 12 + 12) % 12;
    return (key.mode == KeyMode::Major ? major : minor)[d];
}

struct Window {
    double start = 0.0;
    bool empty = true;
    std::array<double, 12> pcv{};
    int bass = -1;
};

constexpr double kBassBoost = 0.18;       // added to the bass pitch class (the pcv sums to 1 before it)
constexpr double kBassIsRootBonus = 0.06; // a chord rooted on the bass note is the likelier reading
constexpr double kDiatonicBonus = 0.02;   // every chord tone is in the detected key
constexpr double kBarChangePenalty = 0.12;
constexpr double kMidBarChangePenalty = 0.30;
constexpr double kBassMinShare = 0.30;    // a bass note must sound for this share of the window to count

double scoreChord(const Window& w, int root, int q, const Key& key) {
    const auto& tpl = templates()[q];
    double dot = 0, pn = 0;
    for (int i = 0; i < 12; ++i) {
        dot += w.pcv[(root + i) % 12] * tpl[i];
    }
    for (double v : w.pcv)
        pn += v * v;
    pn = std::sqrt(pn);
    double s = pn > 0 ? dot / pn : 0.0;
    if (w.bass == root)
        s += kBassIsRootBonus;
    s -= kQualities[q].complexity;
    if (key.valid()) {
        bool all = true;
        for (int i : kQualities[q].intervals) {
            if (i < 0)
                break;
            all = all && inScale(key, root + i);
        }
        if (all)
            s += kDiatonicBonus;
    }
    return s;
}

std::string slashedName(int root, ChordQuality q, int bass, bool flats) {
    return chordName(root, q, bass, flats);
}

} // namespace

// ---- Key ---------------------------------------------------------------------------------------------------

std::string Key::name() const {
    if (!valid())
        return "";
    return std::string(noteName(tonic, keyUsesFlats(*this))) + (mode == KeyMode::Major ? " major" : " minor");
}

bool keyUsesFlats(const Key& key) {
    if (!key.valid())
        return false;
    const int majorTonic = key.mode == KeyMode::Major ? key.tonic : (key.tonic + 3) % 12;
    // F, Bb, Eb, Ab, Db, Gb majors (and their relative minors) are spelled with flats.
    return majorTonic == 5 || majorTonic == 10 || majorTonic == 3 || majorTonic == 8 || majorTonic == 1 ||
           majorTonic == 6;
}

namespace {
// Histogram of the notes: duration x velocity per pitch class.
std::array<double, 12> pitchClassHistogram(const std::vector<HarmonyNote>& notes) {
    std::array<double, 12> h{};
    for (const auto& n : notes) {
        if (n.durationBeats <= 0 || n.pitch < 0 || n.pitch > 127)
            continue;
        h[n.pitch % 12] += n.durationBeats * velocityWeight(n.velocity);
    }
    return h;
}

Key keyFromHistogram(const std::array<double, 12>& h) {
    double total = 0;
    for (double v : h)
        total += v;
    if (total <= 0)
        return {};
    double best = -2, second = -2;
    int bestTonic = 0;
    KeyMode bestMode = KeyMode::Major;
    for (int mode = 0; mode < 2; ++mode) {
        for (int tonic = 0; tonic < 12; ++tonic) {
            double rotated[12];
            for (int i = 0; i < 12; ++i)
                rotated[i] = h[(tonic + i) % 12];
            const double r = correlation(rotated, mode == 0 ? kMajorProfile : kMinorProfile);
            if (r > best) {
                second = best;
                best = r;
                bestTonic = tonic;
                bestMode = mode == 0 ? KeyMode::Major : KeyMode::Minor;
            } else if (r > second) {
                second = r;
            }
        }
    }
    Key k;
    k.tonic = bestTonic;
    k.mode = bestMode;
    const double margin = std::min(1.0, std::max(0.0, (best - second) / 0.1));
    k.confidence = std::min(1.0, std::max(0.0, best)) * (0.5 + 0.5 * margin);
    if (k.confidence <= 0.0)
        k.confidence = 1e-3; // a key was chosen; "no key" is tonic -1
    return k;
}
} // namespace

Key detectKey(const std::vector<HarmonyNote>& notes) { return keyFromHistogram(pitchClassHistogram(notes)); }

// ---- Names -------------------------------------------------------------------------------------------------

std::string chordName(int root, ChordQuality quality, int bass, bool useFlats) {
    if (root < 0)
        return "N.C.";
    std::string s = noteName(root, useFlats);
    s += kQualities[static_cast<int>(quality)].suffix;
    if (bass >= 0 && bass != root) {
        s += "/";
        s += noteName(bass, useFlats);
    }
    return s;
}

std::string romanNumeral(const Key& key, int root, ChordQuality quality) {
    if (!key.valid() || root < 0)
        return "";
    static const char* major[12] = {"I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"};
    static const char* minor[12] = {"I", "bII", "II", "III", "#III", "IV", "bV", "V", "VI", "#VI", "VII", "#VII"};
    const int d = ((root - key.tonic) % 12 + 12) % 12;
    std::string r = (key.mode == KeyMode::Major ? major : minor)[d];
    const QualityInfo& qi = kQualities[static_cast<int>(quality)];
    if (qi.minorish) {
        // lower-case the numeral but keep a leading accidental
        for (char& c : r)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
    }
    switch (quality) {
    case ChordQuality::Diminished: r += "\xC2\xB0"; break;           // degree sign
    case ChordQuality::Diminished7: r += "\xC2\xB0" "7"; break;
    case ChordQuality::HalfDiminished7: r += "\xC3\xB8" "7"; break;  // o-slash
    case ChordQuality::Augmented: r += "+"; break;
    case ChordQuality::Dominant7: case ChordQuality::Minor7: r += "7"; break;
    case ChordQuality::Major7: r += "maj7"; break;
    case ChordQuality::Major6: case ChordQuality::Minor6: r += "6"; break;
    case ChordQuality::Sus2: r += "sus2"; break;
    case ChordQuality::Sus4: r += "sus4"; break;
    case ChordQuality::Add9: r += "add9"; break;
    default: break;
    }
    return r;
}

bool parseChordName(const std::string& text, int& root, ChordQuality& quality, int& bass) {
    size_t i = 0;
    while (i < text.size() && text[i] == ' ')
        ++i;
    auto readPc = [&](int& pc) {
        if (i >= text.size() || text[i] < 'A' || text[i] > 'G')
            return false;
        static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
        pc = base[text[i] - 'A'];
        ++i;
        while (i < text.size() && (text[i] == '#' || text[i] == 'b')) {
            // 'b' directly after the letter is a flat; "Bb" is B flat, "Bbmaj7" B flat maj7.
            pc += text[i] == '#' ? 1 : -1;
            ++i;
            break;
        }
        pc = ((pc % 12) + 12) % 12;
        return true;
    };
    int r = 0;
    if (!readPc(r))
        return false;
    size_t slash = text.find('/', i);
    std::string suffix = text.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
    while (!suffix.empty() && suffix.back() == ' ')
        suffix.pop_back();
    struct Alias { const char* text; ChordQuality q; };
    static const Alias aliases[] = {
        {"", ChordQuality::Major}, {"maj", ChordQuality::Major}, {"M", ChordQuality::Major},
        {"m", ChordQuality::Minor}, {"min", ChordQuality::Minor}, {"-", ChordQuality::Minor},
        {"dim", ChordQuality::Diminished}, {"o", ChordQuality::Diminished},
        {"aug", ChordQuality::Augmented}, {"+", ChordQuality::Augmented},
        {"sus2", ChordQuality::Sus2}, {"sus4", ChordQuality::Sus4}, {"sus", ChordQuality::Sus4},
        {"7", ChordQuality::Dominant7}, {"maj7", ChordQuality::Major7}, {"M7", ChordQuality::Major7},
        {"m7", ChordQuality::Minor7}, {"min7", ChordQuality::Minor7}, {"-7", ChordQuality::Minor7},
        {"m7b5", ChordQuality::HalfDiminished7}, {"\xC3\xB8", ChordQuality::HalfDiminished7},
        {"\xC3\xB8" "7", ChordQuality::HalfDiminished7},
        {"dim7", ChordQuality::Diminished7}, {"o7", ChordQuality::Diminished7},
        {"6", ChordQuality::Major6}, {"m6", ChordQuality::Minor6}, {"add9", ChordQuality::Add9},
    };
    bool found = false;
    ChordQuality q = ChordQuality::Major;
    for (const Alias& a : aliases) {
        if (suffix == a.text) {
            q = a.q;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    int b = -1;
    if (slash != std::string::npos) {
        i = slash + 1;
        if (!readPc(b))
            return false;
        while (i < text.size() && text[i] == ' ')
            ++i;
        if (i != text.size())
            return false;
    }
    root = r;
    quality = q;
    bass = (b == r) ? -1 : b;
    return true;
}

// ---- Timeline lookup -----------------------------------------------------------------------------------------

const ChordSpan& noChordSpan() noexcept {
    static const ChordSpan kNone = [] {
        ChordSpan s;
        s.name = "N.C.";
        return s;
    }();
    return kNone;
}

const ChordSpan& HarmonyTimeline::chordAt(double beat) const noexcept {
    // First span starting after the beat; the one before it is the only candidate.
    auto it = std::upper_bound(chords.begin(), chords.end(), beat,
                               [](double b, const ChordSpan& c) { return b < c.startBeat; });
    if (it == chords.begin())
        return noChordSpan();
    --it;
    return beat < it->endBeat ? *it : noChordSpan();
}

// ---- Analysis ----------------------------------------------------------------------------------------------------

namespace {

std::vector<Window> buildWindows(const std::vector<HarmonyNote>& notes, double windowBeats, size_t& firstIdx) {
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (const auto& n : notes) {
        if (n.durationBeats <= 0 || n.pitch < 0 || n.pitch > 127)
            continue;
        lo = std::min(lo, n.startBeat);
        hi = std::max(hi, n.startBeat + n.durationBeats);
    }
    std::vector<Window> windows;
    if (lo > hi)
        return windows;
    const long first = static_cast<long>(std::floor(lo / windowBeats + 1e-9));
    const long last = static_cast<long>(std::ceil(hi / windowBeats - 1e-9));
    const long count = std::max(1L, last - first);
    firstIdx = static_cast<size_t>(std::max(0L, first));
    windows.resize(static_cast<size_t>(count));
    for (long k = 0; k < count; ++k)
        windows[static_cast<size_t>(k)].start = (first + k) * windowBeats;

    struct Overlap { double amount; int pitch; };
    std::vector<std::vector<Overlap>> perWindow(windows.size());
    for (const auto& n : notes) {
        if (n.durationBeats <= 0 || n.pitch < 0 || n.pitch > 127)
            continue;
        const double ns = n.startBeat, ne = n.startBeat + n.durationBeats;
        long k0 = static_cast<long>(std::floor(ns / windowBeats + 1e-9)) - first;
        long k1 = static_cast<long>(std::ceil(ne / windowBeats - 1e-9)) - first;
        k0 = std::max(0L, k0);
        k1 = std::min(count, k1);
        for (long k = k0; k < k1; ++k) {
            const double ws = windows[static_cast<size_t>(k)].start;
            const double o = std::min(ne, ws + windowBeats) - std::max(ns, ws);
            if (o <= 1e-9)
                continue;
            Window& w = windows[static_cast<size_t>(k)];
            w.pcv[n.pitch % 12] += o * velocityWeight(n.velocity);
            w.empty = false;
            perWindow[static_cast<size_t>(k)].push_back({o, n.pitch});
        }
    }
    for (size_t k = 0; k < windows.size(); ++k) {
        Window& w = windows[k];
        if (w.empty)
            continue;
        // Bass: the lowest pitch that sounds for a real share of the window; failing that, the lowest of the
        // longest-sounding notes.
        int lowest = 128;
        double longest = 0;
        for (const auto& o : perWindow[k]) {
            longest = std::max(longest, o.amount);
            if (o.amount >= kBassMinShare * windowBeats)
                lowest = std::min(lowest, o.pitch);
        }
        if (lowest == 128)
            for (const auto& o : perWindow[k])
                if (o.amount >= longest - 1e-9)
                    lowest = std::min(lowest, o.pitch);
        w.bass = lowest % 12;
        // Compress, so a doubled root (octaves are everywhere) does not outweigh the colour tones.
        double sum = 0;
        for (double& v : w.pcv) {
            v = std::sqrt(v);
            sum += v;
        }
        for (double& v : w.pcv)
            v /= sum;
        w.pcv[w.bass] += kBassBoost;
    }
    return windows;
}

struct Picked {
    int root, quality;
    double score;
};

} // namespace

HarmonyTimeline analyzeHarmony(const std::vector<HarmonyNote>& notes, double beatsPerBar,
                               const std::vector<ChordOverride>& overrides) {
    HarmonyTimeline out;
    if (!(beatsPerBar > 0))
        beatsPerBar = 4.0;
    const double windowBeats = beatsPerBar / 2.0;

    size_t firstIdx = 0;
    std::vector<Window> windows = buildWindows(notes, windowBeats, firstIdx);

    // Key: the whole-input histogram, plus a nudge toward the tonic the music starts on (bass of the first
    // sounding window). Without it, vi-IV-I-V reads as the relative major.
    std::array<double, 12> hist = pitchClassHistogram(notes);
    double total = 0;
    for (double v : hist)
        total += v;
    for (const Window& w : windows) {
        if (!w.empty) {
            hist[w.bass] += 0.12 * total;
            break;
        }
    }
    out.key = keyFromHistogram(hist);
    const bool flats = keyUsesFlats(out.key);

    // Viterbi over the non-empty windows.
    std::vector<size_t> live;
    for (size_t i = 0; i < windows.size(); ++i)
        if (!windows[i].empty)
            live.push_back(i);

    if (!live.empty()) {
        constexpr int S = 12 * kQualityCount;
        const size_t T = live.size();
        std::vector<float> emission(T * S);
        for (size_t t = 0; t < T; ++t)
            for (int s = 0; s < S; ++s)
                emission[t * S + s] = static_cast<float>(scoreChord(windows[live[t]], s / kQualityCount, s % kQualityCount, out.key));
        std::vector<float> score(S), next(S);
        std::vector<int16_t> back(T * S, -1);
        for (int s = 0; s < S; ++s)
            score[s] = emission[s];
        for (size_t t = 1; t < T; ++t) {
            const double start = windows[live[t]].start;
            const double bars = start / beatsPerBar;
            const bool barLine = std::fabs(bars - std::round(bars)) < 1e-6;
            const float penalty = static_cast<float>(barLine ? kBarChangePenalty : kMidBarChangePenalty);
            int bestPrev = 0;
            for (int s = 1; s < S; ++s)
                if (score[s] > score[bestPrev])
                    bestPrev = s;
            for (int s = 0; s < S; ++s) {
                const float stay = score[s];
                const float move = score[bestPrev] - penalty;
                if (stay >= move) {
                    next[s] = stay + emission[t * S + s];
                    back[t * S + s] = static_cast<int16_t>(s);
                } else {
                    next[s] = move + emission[t * S + s];
                    back[t * S + s] = static_cast<int16_t>(bestPrev);
                }
            }
            score.swap(next);
        }
        std::vector<int> path(T);
        int cur = 0;
        for (int s = 1; s < S; ++s)
            if (score[s] > score[cur])
                cur = s;
        for (size_t t = T; t-- > 0;) {
            path[t] = cur;
            if (t > 0)
                cur = back[t * S + cur];
        }

        // Spans: a chord runs from its first window to the start of the next different chord (silent windows
        // in between continue it); the last one ends at the end of the last sounding window.
        struct Raw {
            int state;
            double start, end;
            double confSum;
            int n;
            std::array<int, 12> bassVotes;
        };
        std::vector<Raw> raws;
        for (size_t t = 0; t < T; ++t) {
            const Window& w = windows[live[t]];
            const double cos = std::min(1.0, std::max(0.0, static_cast<double>(emission[t * S + path[t]])));
            const double winEnd = w.start + windowBeats;
            if (raws.empty() || raws.back().state != path[t]) {
                if (!raws.empty())
                    raws.back().end = w.start; // silence in between belongs to the previous chord
                Raw r{path[t], w.start, winEnd, 0.0, 0, {}};
                raws.push_back(r);
            }
            Raw& r = raws.back();
            r.end = winEnd;
            r.confSum += cos;
            r.n += 1;
            if (w.bass >= 0)
                r.bassVotes[w.bass] += 1;
        }
        // A run of silence between two equal states: extend through it (end was set per window; patch gaps).
        for (size_t i = 0; i + 1 < raws.size(); ++i)
            raws[i].end = raws[i + 1].start;

        for (const Raw& r : raws) {
            ChordSpan c;
            c.startBeat = r.start;
            c.endBeat = r.end;
            c.root = r.state / kQualityCount;
            c.quality = static_cast<ChordQuality>(r.state % kQualityCount);
            // Slash chord: the most-voted bass, when it is a chord tone other than the root.
            int bestBass = -1, bestVotes = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (r.bassVotes[pc] > bestVotes) {
                    bestVotes = r.bassVotes[pc];
                    bestBass = pc;
                }
            if (bestBass >= 0 && bestBass != c.root) {
                const int d = (bestBass - c.root + 12) % 12;
                for (int i : kQualities[static_cast<int>(c.quality)].intervals) {
                    if (i < 0)
                        break;
                    if (i == d) {
                        c.bass = bestBass;
                        break;
                    }
                }
            }
            c.name = slashedName(c.root, c.quality, c.bass, flats);
            c.roman = romanNumeral(out.key, c.root, c.quality);
            c.confidence = static_cast<float>(r.n ? r.confSum / r.n : 0.0);
            out.chords.push_back(std::move(c));
        }
    }

    // Overrides: carve each one out of what is there and put the user's chord in its place.
    for (const ChordOverride& ov : overrides) {
        if (!(ov.endBeat > ov.startBeat))
            continue;
        int root = -1, bass = -1;
        ChordQuality q = ChordQuality::Major;
        const bool parsed = parseChordName(ov.chordName, root, q, bass);
        std::vector<ChordSpan> carved;
        for (const ChordSpan& c : out.chords) {
            if (c.endBeat <= ov.startBeat || c.startBeat >= ov.endBeat) {
                carved.push_back(c);
                continue;
            }
            if (c.startBeat < ov.startBeat) {
                ChordSpan left = c;
                left.endBeat = ov.startBeat;
                carved.push_back(left);
            }
            if (c.endBeat > ov.endBeat) {
                ChordSpan right = c;
                right.startBeat = ov.endBeat;
                carved.push_back(right);
            }
        }
        ChordSpan u;
        u.startBeat = ov.startBeat;
        u.endBeat = ov.endBeat;
        u.userSet = true;
        u.confidence = 1.0f;
        u.name = ov.chordName;
        if (parsed) {
            u.root = root;
            u.quality = q;
            u.bass = bass;
            u.roman = romanNumeral(out.key, root, q);
        }
        carved.push_back(std::move(u));
        std::sort(carved.begin(), carved.end(),
                  [](const ChordSpan& a, const ChordSpan& b) { return a.startBeat < b.startBeat; });
        out.chords = std::move(carved);
    }
    return out;
}

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
