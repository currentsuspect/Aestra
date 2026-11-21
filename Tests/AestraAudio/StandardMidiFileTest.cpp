// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Standard MIDI File reader/writer. Table-driven over hand-built byte arrays.

#include "Midi/StandardMidiFile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <tuple>
#include <string>
#include <vector>

using namespace Aestra::Audio::Midi;
using Bytes = std::vector<uint8_t>;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

void be16(Bytes& b, unsigned v) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
void be32(Bytes& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back((v >> s) & 0xFF); }
void append(Bytes& b, const Bytes& o) { b.insert(b.end(), o.begin(), o.end()); }

Bytes header(unsigned format, unsigned ntrks, unsigned division) {
    Bytes b{'M', 'T', 'h', 'd'};
    be32(b, 6); be16(b, format); be16(b, ntrks); be16(b, division);
    return b;
}
Bytes track(const Bytes& body) {
    Bytes b{'M', 'T', 'r', 'k'};
    be32(b, static_cast<uint32_t>(body.size()));
    append(b, body);
    return b;
}
const Bytes kEot{0x00, 0xFF, 0x2F, 0x00};
Bytes file(unsigned format, unsigned division, const std::vector<Bytes>& bodies) {
    Bytes b = header(format, static_cast<unsigned>(bodies.size()), division);
    for (const auto& t : bodies) append(b, track(t));
    return b;
}
Bytes cat(std::initializer_list<Bytes> parts) { Bytes b; for (auto& p : parts) append(b, p); return b; }

struct Case {
    const char* name;
    Bytes bytes;
    bool ok;
    std::string errorContains;
};

SmfNote mk(int pitch, double start, double dur, float vel, uint8_t ch = 0) {
    SmfNote n;
    n.note.pitch = pitch; n.note.startBeat = start; n.note.durationBeats = dur; n.note.velocity = vel;
    n.channel = ch;
    return n;
}

void testRejections() {
    std::vector<Case> cases = {
        {"empty", {}, false, ""},
        {"garbage magic", {'X', 'Y', 'Z', 'W', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96}, false, "MThd"},
        {"format 2", file(2, 96, {kEot}), false, "format 2"},
        {"smpte division", file(1, 0xE728, {kEot}), false, "SMPTE"},
        {"zero division", file(1, 0, {kEot}), false, ""},
        {"header length < 6", {'M', 'T', 'h', 'd', 0, 0, 0, 4, 0, 0, 0, 1}, false, ""},
        {"data byte with no running status", file(0, 96, {{0x00, 0x3C, 0x40, 0x00, 0xFF, 0x2F, 0x00}}), false, ""},
        {"track chunk longer than file", cat({header(0, 1, 96), {'M', 'T', 'r', 'k', 0, 0, 1, 0, 0}}), false, ""},
    };
    for (auto& c : cases) {
        auto r = readStandardMidiFile(c.bytes);
        check(!r.ok, std::string("rejects: ") + c.name);
        check(!r.ok && !r.error.empty(), std::string("error message set: ") + c.name);
        if (!c.errorContains.empty())
            check(r.error.find(c.errorContains) != std::string::npos,
                  std::string("error mentions '") + c.errorContains + "' for " + c.name + " got: " + r.error);
    }
}

void testBasicNote() {
    // 96 PPQ; note C4 at tick 96 (beat 1), length 48 ticks (0.5 beat), velocity 100
    Bytes body = {0x00, 0xFF, 0x03, 0x04, 'L', 'e', 'a', 'd',
                  0x60, 0x90, 60, 100,
                  0x30, 0x80, 60, 0,
                  0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    check(r.ok, "basic: parses");
    if (!r.ok || r.data.tracks.size() != 1) { check(false, "basic: one track"); return; }
    check(r.data.tracks[0].name == "Lead", "basic: track name");
    check(r.data.tracks[0].notes.size() == 1, "basic: one note");
    if (r.data.tracks[0].notes.size() != 1) return;
    const auto& n = r.data.tracks[0].notes[0];
    check(n.note.pitch == 60 && near(n.note.startBeat, 1.0) && near(n.note.durationBeats, 0.5), "basic: pitch/start/duration");
    check(near(n.note.velocity, 100.0 / 127.0, 1e-6), "basic: velocity scaled to 0..1");
}

void testNoteOnVelocityZeroIsNoteOff() {
    Bytes body = {0x00, 0x90, 64, 80, 0x60, 0x90, 64, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    check(r.ok && r.data.tracks[0].notes.size() == 1 && near(r.data.tracks[0].notes[0].note.durationBeats, 1.0),
          "vel-0 note-on closes the note");
}

void testRunningStatus() {
    // one status byte, three note-on/off pairs via running status; channel 3
    Bytes body = {0x00, 0x93, 60, 90, 0x18, 60, 0, 0x00, 62, 90, 0x18, 62, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    check(r.ok, "running status: parses");
    if (!r.ok) return;
    auto& ns = r.data.tracks[0].notes;
    check(ns.size() == 2, "running status: two notes");
    if (ns.size() == 2) {
        check(ns[0].channel == 3 && ns[1].channel == 3, "running status: channel kept");
        check(ns[1].note.pitch == 62 && near(ns[1].note.startBeat, 0.25), "running status: second note position");
    }
}

void testMetaBreaksNothingButSysexSkipped() {
    Bytes body = {0x00, 0xF0, 0x03, 0x01, 0x02, 0xF7,        // sysex skipped
                  0x00, 0xFF, 0x7F, 0x02, 0xAA, 0xBB,        // sequencer-specific meta skipped
                  0x00, 0xFF, 0x01, 0x03, 'a', 'b', 'c',     // text meta skipped
                  0x00, 0x90, 60, 90, 0x60, 0x80, 60, 0x40, 0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    check(r.ok && r.data.tracks[0].notes.size() == 1, "unknown meta/sysex skipped, note kept");
    // other channel events are ignored without disturbing parsing
    Bytes b2 = {0x00, 0xB0, 7, 100, 0x00, 0xC0, 5, 0x00, 0xE0, 0, 0x40, 0x00, 0xD0, 9,
                0x00, 0x90, 60, 90, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r2 = readStandardMidiFile(file(0, 96, {b2}));
    check(r2.ok && r2.data.tracks[0].notes.size() == 1, "CC/PC/bend/aftertouch ignored");
}

void testOverlappingSamePitch() {
    // on 0, on 48, off 96, off 144 (96 ppq): pairs FIFO -> [0,1) and [0.5,1.5)
    Bytes body = {0x00, 0x90, 60, 90, 0x30, 0x90, 60, 90, 0x30, 0x80, 60, 0, 0x30, 0x80, 60, 0,
                  0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    auto& ns = r.data.tracks[0].notes;
    check(r.ok && ns.size() == 2, "overlap: two notes");
    if (ns.size() == 2) {
        check(near(ns[0].note.startBeat, 0.0) && near(ns[0].note.durationBeats, 1.0), "overlap: first");
        check(near(ns[1].note.startBeat, 0.5) && near(ns[1].note.durationBeats, 1.0), "overlap: second");
    }
}

void testDanglingNoteClosedAtEndOfTrack() {
    Bytes body = {0x00, 0x90, 60, 90, 0x60, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(0, 96, {body}));
    check(r.ok && r.data.tracks[0].notes.size() == 1 && near(r.data.tracks[0].notes[0].note.durationBeats, 1.0),
          "unterminated note ends at end-of-track");
    Bytes orphan = {0x00, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r2 = readStandardMidiFile(file(0, 96, {orphan}));
    check(r2.ok && r2.data.tracks[0].notes.empty(), "orphan note-off ignored");
}

void testTempoAndTimeSignatureIndependentBeats() {
    // tempo 120 at 0, tempo 60 at beat 2 (tick 192), time sig 3/4 at 0, 6/8 at beat 4.
    Bytes tempoTrack = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
                        0x81, 0x40, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,   // delta 192
                        0x00, 0xFF, 0x58, 0x04, 3, 2, 24, 8,
                        0x81, 0x40, 0xFF, 0x58, 0x04, 6, 3, 24, 8,       // delta 192 -> beat 4
                        0x00, 0xFF, 0x2F, 0x00};
    // a note at beat 3 for 1 beat: its beat numbers must not depend on the tempo change before it
    Bytes noteTrack = {0x00, 0xFF, 0x03, 0x01, 'N', 0x82, 0x20, 0x90, 60, 90, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(file(1, 96, {tempoTrack, noteTrack}));
    check(r.ok, "tempo map: parses");
    if (!r.ok) return;
    check(r.data.tempos.size() == 2, "tempo map: two tempo changes");
    if (r.data.tempos.size() == 2) {
        check(near(r.data.tempos[0].bpm, 120.0, 1e-6) && near(r.data.tempos[0].beat, 0.0), "tempo 1");
        check(near(r.data.tempos[1].bpm, 60.0, 1e-6) && near(r.data.tempos[1].beat, 2.0), "tempo 2 at beat 2");
    }
    check(r.data.timeSignatures.size() == 2, "time sigs: two");
    if (r.data.timeSignatures.size() == 2) {
        check(r.data.timeSignatures[0].numerator == 3 && r.data.timeSignatures[0].denominator == 4, "3/4");
        check(r.data.timeSignatures[1].numerator == 6 && r.data.timeSignatures[1].denominator == 8 &&
                  near(r.data.timeSignatures[1].beat, 4.0), "6/8 at beat 4");
    }
    check(r.data.tracks.size() == 2 && r.data.tracks[1].notes.size() == 1, "tempo map: note track present");
    if (r.data.tracks.size() == 2 && r.data.tracks[1].notes.size() == 1)
        check(near(r.data.tracks[1].notes[0].note.startBeat, 3.0) && near(r.data.tracks[1].notes[0].note.durationBeats, 1.0),
              "note beats unaffected by tempo changes");
    // same note, same bytes, but with a different tempo track: identical beats
    Bytes otherTempo = {0x00, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90, 0x00, 0xFF, 0x2F, 0x00};
    auto r2 = readStandardMidiFile(file(1, 96, {otherTempo, noteTrack}));
    check(r2.ok && r2.data.tracks[1].notes.size() == 1 && near(r2.data.tracks[1].notes[0].note.startBeat, 3.0),
          "beats identical under a different tempo");
}

void testPpqScaling() {
    Bytes body = {0x00, 0x90, 60, 90, 0x83, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00}; // 480 ticks
    auto r = readStandardMidiFile(file(0, 480, {body}));
    check(r.ok && near(r.data.tracks[0].notes[0].note.durationBeats, 1.0), "PPQ 480: 480 ticks = 1 beat");
    auto r2 = readStandardMidiFile(file(0, 960, {body}));
    check(r2.ok && near(r2.data.tracks[0].notes[0].note.durationBeats, 0.5), "PPQ 960: 480 ticks = 0.5 beat");
}

void testUnknownChunkSkippedAndExtraHeader() {
    Bytes h = {'M', 'T', 'h', 'd', 0, 0, 0, 8, 0, 0, 0, 1, 0, 96, 0xDE, 0xAD};
    Bytes unk = {'X', 'F', 'I', 'H', 0, 0, 0, 2, 1, 2};
    Bytes body = {0x00, 0x90, 60, 90, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00};
    auto r = readStandardMidiFile(cat({h, unk, track(body)}));
    check(r.ok && r.data.tracks.size() == 1 && r.data.tracks[0].notes.size() == 1, "unknown chunk and long header skipped");
}

void testWriterShape() {
    SmfData d;
    d.tempos = {{0.0, 120.0}};
    SmfTrack t; t.name = "T";
    t.notes = {mk(60, 0.0, 1.0, 1.0f)};
    d.tracks = {t};
    Bytes b = writeStandardMidiFile(d);
    check(b.size() > 14 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd', "writer: MThd");
    check(b.size() > 13 && b[9] == 1, "writer: format 1");
    check(b.size() > 13 && b[10] == 0 && b[11] == 2, "writer: tempo track + one track");
    check(b.size() > 13 && b[12] == 0x01 && b[13] == 0xE0, "writer: PPQ 480");
}

void testRoundTrip() {
    SmfData d;
    d.tempos = {{0.0, 120.0}, {8.0, 90.0}};
    d.timeSignatures = {{0.0, 7, 8}, {7.0, 4, 4}};
    SmfTrack a; a.name = "Bass";
    a.notes = {mk(36, 0.0, 0.5, 100.0f / 127.0f, 0), mk(36, 1.0, 1.0, 1.0f, 0),
               mk(40, 1.0, 1.0, 64.0f / 127.0f, 2),
               // overlapping same pitch
               mk(60, 2.0, 2.0, 80.0f / 127.0f), mk(60, 3.0, 2.0, 81.0f / 127.0f),
               // back to back same pitch: off must precede the next on at the same tick
               mk(62, 6.0, 1.0, 90.0f / 127.0f), mk(62, 7.0, 1.0, 91.0f / 127.0f),
               // 1/480 grid positions
               mk(70, 1.0 / 480.0, 3.0 / 480.0, 50.0f / 127.0f), mk(71, 479.0 / 480.0, 1.0 / 480.0, 50.0f / 127.0f),
               // zero length
               mk(72, 9.0, 0.0, 70.0f / 127.0f)};
    SmfTrack b; b.name = "Keys";
    b.notes = {mk(48, 0.0, 4.0, 1.0f / 127.0f, 15)};
    d.tracks = {a, b};
    Bytes bytes = writeStandardMidiFile(d);
    auto r = readStandardMidiFile(bytes);
    check(r.ok, "round trip: parses own output: " + r.error);
    if (!r.ok) return;
    check(r.data.tracks.size() >= 2, "round trip: tracks");
    // find tracks by name (tempo track comes first and has no notes)
    const SmfTrack *ra = nullptr, *rb = nullptr;
    for (auto& t : r.data.tracks) { if (t.name == "Bass") ra = &t; if (t.name == "Keys") rb = &t; }
    check(ra && rb, "round trip: named tracks");
    if (!ra || !rb) return;
    check(ra->notes.size() == a.notes.size(), "round trip: note count");
    auto key = [](const SmfNote& n) { return std::make_tuple(n.note.startBeat, n.note.pitch); };
    auto sortedIn = a.notes;
    std::sort(sortedIn.begin(), sortedIn.end(), [&](auto& x, auto& y) { return key(x) < key(y); });
    auto sortedOut = ra->notes;
    std::sort(sortedOut.begin(), sortedOut.end(), [&](auto& x, auto& y) { return key(x) < key(y); });
    for (size_t i = 0; i < std::min(sortedIn.size(), sortedOut.size()); ++i) {
        const auto& x = sortedIn[i]; const auto& y = sortedOut[i];
        std::string at = " (note " + std::to_string(i) + ")";
        check(x.note.pitch == y.note.pitch, "rt pitch" + at);
        check(near(x.note.startBeat, y.note.startBeat), "rt start exact" + at);
        check(near(x.note.durationBeats, y.note.durationBeats), "rt duration exact" + at);
        check(near(x.note.velocity, y.note.velocity, 1e-6), "rt velocity" + at);
        check(x.channel == y.channel, "rt channel" + at);
    }
    check(rb->notes.size() == 1 && rb->notes[0].channel == 15 && near(rb->notes[0].note.velocity, 1.0 / 127.0, 1e-6),
          "round trip: second track, channel 15, min velocity");
    check(r.data.tempos.size() == 2 && near(r.data.tempos[1].beat, 8.0) && near(r.data.tempos[1].bpm, 90.0, 1e-3),
          "round trip: tempo map");
    check(r.data.timeSignatures.size() == 2 && r.data.timeSignatures[0].numerator == 7 &&
              r.data.timeSignatures[0].denominator == 8 && near(r.data.timeSignatures[1].beat, 7.0),
          "round trip: time signatures");
}

void testWriterClamps() {
    SmfData d;
    SmfTrack t;
    t.notes = {mk(200, -1.0, 1.0, 5.0f), mk(-4, 0.0, -2.0, 0.0f)};
    d.tracks = {t};
    auto r = readStandardMidiFile(writeStandardMidiFile(d));
    check(r.ok, "clamps: output parses");
    if (!r.ok) return;
    size_t total = 0;
    for (auto& tr : r.data.tracks) for (auto& n : tr.notes) {
        ++total;
        check(n.note.pitch >= 0 && n.note.pitch <= 127 && n.note.startBeat >= 0.0 && n.note.durationBeats >= 0.0 &&
                  n.note.velocity > 0.0f && n.note.velocity <= 1.0f, "clamps: values in range");
    }
    check(total == 2, "clamps: both notes kept");
    // empty data still writes a readable file
    auto e = readStandardMidiFile(writeStandardMidiFile(SmfData{}));
    check(e.ok, "empty data round-trips");
}

void testFuzz() {
    // valid base file with several features, then every truncation and random corruption
    SmfData d;
    d.tempos = {{0.0, 100.0}};
    SmfTrack t; t.name = "X";
    for (int i = 0; i < 12; ++i) t.notes.push_back(mk(40 + i, i * 0.25, 0.5, 0.7f, i % 4));
    d.tracks = {t};
    Bytes base = writeStandardMidiFile(d);
    check(readStandardMidiFile(base).ok, "fuzz: base parses");
    size_t rejected = 0;
    for (size_t n = 0; n < base.size(); ++n) {
        Bytes cut(base.begin(), base.begin() + n);
        auto r = readStandardMidiFile(cut);   // must not crash; truncation inside a chunk must fail
        if (!r.ok) ++rejected;
    }
    check(rejected >= base.size() - 14 - 1, "fuzz: truncations are rejected (chunk lengths are checked)");
    std::mt19937 rng(12345);
    for (int i = 0; i < 4000; ++i) {
        Bytes m = base;
        int flips = 1 + static_cast<int>(rng() % 6);
        for (int f = 0; f < flips; ++f) m[rng() % m.size()] = static_cast<uint8_t>(rng());
        auto r = readStandardMidiFile(m);
        if (r.ok) for (auto& tr : r.data.tracks) for (auto& n : tr.notes)
            check(n.note.pitch >= 0 && n.note.pitch <= 127 && n.note.durationBeats >= 0.0, "fuzz: ok result sane");
    }
    for (int i = 0; i < 4000; ++i) {
        Bytes g(rng() % 200);
        for (auto& v : g) v = static_cast<uint8_t>(rng());
        if (i % 2 == 0 && g.size() > 14) { g[0] = 'M'; g[1] = 'T'; g[2] = 'h'; g[3] = 'd'; g[4] = g[5] = g[6] = 0; g[7] = 6; }
        (void)readStandardMidiFile(g);
    }
    // huge declared lengths must not allocate or overrun
    Bytes huge = cat({header(0, 1, 96), {'M', 'T', 'r', 'k', 0xFF, 0xFF, 0xFF, 0xFF}});
    check(!readStandardMidiFile(huge).ok, "fuzz: 4 GB chunk length rejected");
    Bytes vlq = cat({header(0, 1, 96), track({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x90, 60, 90})});
    check(!readStandardMidiFile(vlq).ok, "fuzz: 5-byte delta rejected");
}

} // namespace

int main() {
    testRejections();
    testBasicNote();
    testNoteOnVelocityZeroIsNoteOff();
    testRunningStatus();
    testMetaBreaksNothingButSysexSkipped();
    testOverlappingSamePitch();
    testDanglingNoteClosedAtEndOfTrack();
    testTempoAndTimeSignatureIndependentBeats();
    testPpqScaling();
    testUnknownChunkSkippedAndExtraHeader();
    testWriterShape();
    testRoundTrip();
    testWriterClamps();
    testFuzz();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "StandardMidiFileTest passed\n";
    return 0;
}
