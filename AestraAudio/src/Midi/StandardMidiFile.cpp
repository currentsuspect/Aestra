// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Midi/StandardMidiFile.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace Midi {

namespace {

// Bounds-checked byte cursor. Every read says whether it succeeded; nothing indexes past `end`.
struct Cursor {
    const uint8_t* p;
    const uint8_t* end;
    size_t left() const { return static_cast<size_t>(end - p); }
    bool u8(uint8_t& v) {
        if (p >= end) return false;
        v = *p++;
        return true;
    }
    bool u16(uint32_t& v) {
        if (left() < 2) return false;
        v = (uint32_t(p[0]) << 8) | p[1];
        p += 2;
        return true;
    }
    bool u32(uint32_t& v) {
        if (left() < 4) return false;
        v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        p += 4;
        return true;
    }
    bool skip(size_t n) {
        if (left() < n) return false;
        p += n;
        return true;
    }
    // SMF variable-length quantity: at most 4 bytes.
    bool vlq(uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b;
            if (!u8(b)) return false;
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return true;
        }
        return false;
    }
};

struct OpenNote {
    uint64_t startTick;
    uint8_t velocity;
};

bool parseTrack(Cursor c, uint32_t ppq, SmfData& out, SmfTrack& track, std::string& err) {
    uint64_t tick = 0;
    uint8_t running = 0;
    // One FIFO of open notes per (channel, pitch); note-offs close the oldest.
    std::vector<std::vector<OpenNote>> open(16 * 128);
    auto beat = [&](uint64_t t) { return static_cast<double>(t) / static_cast<double>(ppq); };
    auto emit = [&](int ch, int pitch, const OpenNote& on, uint64_t endTick) {
        SmfNote n;
        n.note.pitch = pitch;
        n.note.startBeat = beat(on.startTick);
        n.note.durationBeats = beat(endTick - on.startTick);
        n.note.velocity = static_cast<float>(on.velocity) / 127.0f;
        n.channel = static_cast<uint8_t>(ch);
        track.notes.push_back(n);
    };
    bool sawEnd = false;
    while (c.left() > 0 && !sawEnd) {
        uint32_t delta;
        if (!c.vlq(delta)) { err = "truncated or oversized delta time"; return false; }
        tick += delta;
        uint8_t status;
        if (!c.u8(status)) { err = "truncated event"; return false; }
        if (status < 0x80) {
            if (running == 0) { err = "data byte without a running status"; return false; }
            --c.p; // reuse this byte as the first data byte
            status = running;
        } else if (status < 0xF0) {
            running = status;
        }
        if (status >= 0x80 && status < 0xF0) {
            const int type = status & 0xF0, ch = status & 0x0F;
            uint8_t d1, d2 = 0;
            if (!c.u8(d1)) { err = "truncated channel event"; return false; }
            if (type != 0xC0 && type != 0xD0 && !c.u8(d2)) { err = "truncated channel event"; return false; }
            if ((d1 & 0x80) || (d2 & 0x80)) { err = "channel event data byte out of range"; return false; }
            if (type == 0x90 && d2 != 0) {
                open[ch * 128 + d1].push_back({tick, d2});
            } else if (type == 0x80 || type == 0x90) {
                auto& q = open[ch * 128 + d1];
                if (!q.empty()) {
                    emit(ch, d1, q.front(), tick);
                    q.erase(q.begin());
                }
            }
            continue;
        }
        if (status == 0xF0 || status == 0xF7) {
            running = 0;
            uint32_t len;
            if (!c.vlq(len) || !c.skip(len)) { err = "truncated sysex"; return false; }
            continue;
        }
        if (status != 0xFF) { err = "unsupported system message in track"; return false; }
        running = 0;
        uint8_t type;
        uint32_t len;
        if (!c.u8(type) || !c.vlq(len) || len > c.left()) { err = "truncated meta event"; return false; }
        const uint8_t* d = c.p;
        c.p += len;
        if (type == 0x2F) {
            sawEnd = true;
        } else if (type == 0x03) {
            if (track.name.empty()) track.name.assign(reinterpret_cast<const char*>(d), len);
        } else if (type == 0x51 && len == 3) {
            const uint32_t us = (uint32_t(d[0]) << 16) | (uint32_t(d[1]) << 8) | d[2];
            if (us > 0) out.tempos.push_back({beat(tick), 60000000.0 / us});
        } else if (type == 0x58 && len >= 2) {
            if (d[1] < 16) out.timeSignatures.push_back({beat(tick), d[0], 1 << d[1]});
        }
    }
    // Notes still sounding when the track ends are closed at its end.
    for (int ch = 0; ch < 16; ++ch)
        for (int p = 0; p < 128; ++p)
            for (const auto& on : open[ch * 128 + p]) emit(ch, p, on, std::max(tick, on.startTick));
    std::stable_sort(track.notes.begin(), track.notes.end(), [](const SmfNote& a, const SmfNote& b) {
        return a.note.startBeat < b.note.startBeat;
    });
    return true;
}

SmfReadResult fail(const std::string& msg) {
    SmfReadResult r;
    r.error = msg;
    return r;
}

// ---- writer helpers ----

void putVlq(std::vector<uint8_t>& b, uint32_t v) {
    if (v > 0x0FFFFFFFu) v = 0x0FFFFFFFu;
    uint8_t tmp[4];
    int n = 0;
    tmp[n++] = v & 0x7F;
    while ((v >>= 7) != 0) tmp[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
    while (n > 0) b.push_back(tmp[--n]);
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

uint64_t toTick(double beat) {
    if (!(beat > 0.0)) return 0;
    const double t = std::llround(beat * kWriterPpq);
    return t > 0x0FFFFFFF ? 0x0FFFFFFF : static_cast<uint64_t>(t);
}

struct Ev {
    uint64_t tick;
    int order; // 0 = note-off, 1 = meta/other, 2 = note-on (offs first so same-pitch neighbours stay apart)
    std::vector<uint8_t> bytes;
};

void emitTrack(std::vector<uint8_t>& file, std::vector<Ev>& evs, const std::string& name) {
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<uint8_t> body;
    if (!name.empty()) {
        putVlq(body, 0);
        body.push_back(0xFF);
        body.push_back(0x03);
        const size_t len = std::min<size_t>(name.size(), 127);
        putVlq(body, static_cast<uint32_t>(len));
        body.insert(body.end(), name.begin(), name.begin() + len);
    }
    uint64_t last = 0;
    for (const auto& e : evs) {
        putVlq(body, static_cast<uint32_t>(e.tick - last));
        body.insert(body.end(), e.bytes.begin(), e.bytes.end());
        last = e.tick;
    }
    body.insert(body.end(), {0x00, 0xFF, 0x2F, 0x00});
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    putU32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

} // namespace

SmfReadResult readStandardMidiFile(const uint8_t* bytes, size_t size) {
    if (!bytes && size) return fail("no data");
    Cursor c{bytes, bytes + size};
    if (c.left() < 8 || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'h' || bytes[3] != 'd')
        return fail("not a Standard MIDI File (missing MThd header)");
    c.skip(4);
    uint32_t hlen, format, ntrks, division;
    if (!c.u32(hlen) || hlen < 6 || !c.u16(format) || !c.u16(ntrks) || !c.u16(division))
        return fail("truncated or invalid MIDI header");
    if (!c.skip(hlen - 6)) return fail("truncated MIDI header");
    if (format > 2) return fail("unknown MIDI file format " + std::to_string(format));
    if (format == 2) return fail("MIDI format 2 (independent sequences) is not supported");
    if (division & 0x8000) return fail("SMPTE time division is not supported (only PPQ)");
    if (division == 0) return fail("invalid time division (0 ticks per quarter note)");

    SmfReadResult r;
    r.data.format = static_cast<int>(format);
    uint32_t tracksRead = 0;
    while (c.left() > 0 && tracksRead < ntrks) {
        if (c.left() < 8) return fail("truncated chunk header");
        const bool isTrk = c.p[0] == 'M' && c.p[1] == 'T' && c.p[2] == 'r' && c.p[3] == 'k';
        c.skip(4);
        uint32_t len;
        c.u32(len);
        if (len > c.left()) return fail("chunk length runs past the end of the file");
        if (isTrk) {
            SmfTrack t;
            std::string err;
            if (!parseTrack(Cursor{c.p, c.p + len}, division, r.data, t, err)) return fail("track " + std::to_string(tracksRead) + ": " + err);
            r.data.tracks.push_back(std::move(t));
            ++tracksRead;
        }
        c.skip(len);
    }
    if (r.data.tracks.empty()) return fail("file contains no tracks");
    std::stable_sort(r.data.tempos.begin(), r.data.tempos.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.beat < b.beat; });
    std::stable_sort(r.data.timeSignatures.begin(), r.data.timeSignatures.end(),
                     [](const TimeSignatureChange& a, const TimeSignatureChange& b) { return a.beat < b.beat; });
    r.ok = true;
    return r;
}

std::vector<uint8_t> writeStandardMidiFile(const SmfData& data) {
    std::vector<uint8_t> f{'M', 'T', 'h', 'd'};
    putU32(f, 6);
    const size_t ntrks = data.tracks.size() + 1;
    f.insert(f.end(), {0x00, 0x01, static_cast<uint8_t>(ntrks >> 8), static_cast<uint8_t>(ntrks & 0xFF),
                       static_cast<uint8_t>(kWriterPpq >> 8), static_cast<uint8_t>(kWriterPpq & 0xFF)});

    std::vector<Ev> conductor;
    for (const auto& t : data.tempos) {
        if (!(t.bpm > 0.0)) continue;
        const uint32_t us = static_cast<uint32_t>(std::min(std::llround(60000000.0 / t.bpm), 0xFFFFFFLL));
        conductor.push_back({toTick(t.beat), 1, {0xFF, 0x51, 0x03, uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us)}});
    }
    for (const auto& s : data.timeSignatures) {
        int dd = 0;
        while ((1 << dd) < s.denominator && dd < 7) ++dd;
        conductor.push_back({toTick(s.beat), 1,
                             {0xFF, 0x58, 0x04, uint8_t(std::clamp(s.numerator, 1, 255)), uint8_t(dd), 24, 8}});
    }
    emitTrack(f, conductor, "Tempo");

    for (const auto& t : data.tracks) {
        std::vector<Ev> evs;
        for (const auto& n : t.notes) {
            const int pitch = std::clamp(n.note.pitch, 0, 127);
            const int vel = std::clamp(static_cast<int>(std::lround(n.note.velocity * 127.0f)), 1, 127);
            const uint8_t ch = n.channel & 0x0F;
            const uint64_t on = toTick(n.note.startBeat);
            const uint64_t off = on + toTick(n.note.durationBeats);
            if (off == on) {
                // Zero length: on and off back to back, kept together by sorting them as one event.
                evs.push_back({on, 2, {uint8_t(0x90 | ch), uint8_t(pitch), uint8_t(vel), 0x00, uint8_t(0x80 | ch), uint8_t(pitch), 0}});
            } else {
                evs.push_back({on, 2, {uint8_t(0x90 | ch), uint8_t(pitch), uint8_t(vel)}});
                evs.push_back({off, 0, {uint8_t(0x80 | ch), uint8_t(pitch), 0}});
            }
        }
        emitTrack(f, evs, t.name);
    }
    return f;
}

} // namespace Midi
} // namespace Audio
} // namespace Aestra
