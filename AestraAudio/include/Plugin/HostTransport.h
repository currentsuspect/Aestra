// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// What a hosted plugin is told about the song: tempo, metre, playing or stopped, and where the playhead is.
//
// VST3 (ProcessContext) and CLAP (clap_event_transport) plugins used to get nothing, so a tempo-synced delay or LFO
// had no tempo, an arpeggiator never knew the song was playing, and Vital reset its own BPM. The engine publishes
// one snapshot per render block (AudioEngine::renderGraph); every plugin host reads it in process(). In the sandbox
// the app sends the snapshot with each block and the helper publishes it in its own process, so the same hosts
// read it there too.
//
// One writer (the render thread), any number of readers, no locks: a sequence counter brackets each write, and a
// reader that sees it move retries a few times, then reports nothing rather than a torn snapshot.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Aestra {
namespace Audio {

struct HostTransport {
    bool valid = false;   ///< false: no engine has published (a plugin then gets no transport, as before)
    bool playing = false;
    bool looping = false;
    double tempo = 120.0; ///< beats per minute
    int32_t timeSigNumerator = 4;
    int32_t timeSigDenominator = 4;
    int64_t samplePosition = 0; ///< the block's first sample on the song's timeline
    double sampleRate = 48000.0;
    double ppqPosition = 0.0;   ///< the same point in quarter notes
    double barStartPpq = 0.0;   ///< where the bar holding it starts, in quarter notes
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;

    /// The snapshot for a block starting at samplePosition. Beats are quarter notes: the engine's metre is n/4.
    static HostTransport at(int64_t samplePosition, double sampleRate, double tempo, int32_t beatsPerBar, bool playing,
                            bool looping, double loopStartBeat, double loopEndBeat) noexcept {
        HostTransport t;
        t.valid = sampleRate > 0.0 && tempo > 0.0;
        t.playing = playing;
        t.looping = looping;
        t.tempo = tempo;
        t.timeSigNumerator = beatsPerBar > 0 ? beatsPerBar : 4;
        t.timeSigDenominator = 4;
        t.samplePosition = samplePosition;
        t.sampleRate = sampleRate;
        if (t.valid) {
            t.ppqPosition = static_cast<double>(samplePosition) / sampleRate * tempo / 60.0;
            t.barStartPpq = std::floor(t.ppqPosition / t.timeSigNumerator) * t.timeSigNumerator;
        }
        t.loopStartPpq = loopStartBeat;
        t.loopEndPpq = loopEndBeat;
        return t;
    }

    /// The snapshot as trailing tokens on a sandbox PROCESS line:
    /// " T <flags> <num> <den> <sample> <tempo> <rate> <ppq> <bar ppq> <loop start> <loop end>". Flags: 1 valid,
    /// 2 playing, 4 looping. Doubles go as the hex of their bits, so no locale (a plugin may call setlocale) can turn
    /// a decimal point into a comma between the app and the helper. Empty when the snapshot is not valid.
    std::string toWire() const {
        if (!valid)
            return {};
        char buf[256];
        const int n = std::snprintf(buf, sizeof buf, " T %u %d %d %lld", 1u | (playing ? 2u : 0u) | (looping ? 4u : 0u),
                                    timeSigNumerator, timeSigDenominator, static_cast<long long>(samplePosition));
        std::string out(buf, n > 0 ? static_cast<size_t>(n) : 0);
        for (double d : {tempo, sampleRate, ppqPosition, barStartPpq, loopStartPpq, loopEndPpq}) {
            uint64_t bits = 0;
            std::memcpy(&bits, &d, sizeof bits);
            std::snprintf(buf, sizeof buf, " %016llx", static_cast<unsigned long long>(bits));
            out += buf;
        }
        return out;
    }

    /// Reads what toWire() wrote, starting at the token after "T". Anything malformed gives !valid: the plugin then
    /// gets no transport for the block, never a wrong one.
    static HostTransport fromWire(const std::string* tokens, size_t count) noexcept {
        HostTransport t;
        if (count != 10)
            return t;
        const auto integer = [](const std::string& s, long long& out) {
            char* end = nullptr;
            out = std::strtoll(s.c_str(), &end, 10);
            return !s.empty() && end && *end == '\0';
        };
        const auto real = [](const std::string& s, double& out) {
            char* end = nullptr;
            const unsigned long long bits = std::strtoull(s.c_str(), &end, 16);
            if (s.size() != 16 || !end || *end != '\0')
                return false;
            const uint64_t b = bits;
            std::memcpy(&out, &b, sizeof out);
            return std::isfinite(out);
        };
        long long flags = 0, num = 0, den = 0, sample = 0;
        if (!integer(tokens[0], flags) || !integer(tokens[1], num) || !integer(tokens[2], den) ||
            !integer(tokens[3], sample))
            return t;
        double d[6] = {};
        for (size_t i = 0; i < 6; ++i)
            if (!real(tokens[4 + i], d[i]))
                return t;
        if ((flags & 1) == 0 || num < 1 || num > 64 || den < 1 || den > 64 || d[0] <= 0.0 || d[0] > 1000.0 ||
            d[1] <= 0.0)
            return t;
        t.playing = (flags & 2) != 0;
        t.looping = (flags & 4) != 0;
        t.timeSigNumerator = static_cast<int32_t>(num);
        t.timeSigDenominator = static_cast<int32_t>(den);
        t.samplePosition = sample;
        t.tempo = d[0];
        t.sampleRate = d[1];
        t.ppqPosition = d[2];
        t.barStartPpq = d[3];
        t.loopStartPpq = d[4];
        t.loopEndPpq = d[5];
        t.valid = true;
        return t;
    }
};

/// The numbers a CLAP clap_event_transport carries for a snapshot, worked out once for the in-process host (real SDK
/// types) and the helper (its ABI mirror), which each copy them into their own struct.
struct ClapTransportValues {
    static constexpr double kFactor = 2147483648.0; // CLAP_BEATTIME_FACTOR and CLAP_SECTIME_FACTOR, 1 << 31
    uint32_t flags = 0;
    int64_t songPosBeats = 0;
    int64_t songPosSeconds = 0;
    double tempo = 0.0;
    int64_t loopStartBeats = 0;
    int64_t loopEndBeats = 0;
    int64_t loopStartSeconds = 0;
    int64_t loopEndSeconds = 0;
    int64_t barStart = 0;
    int32_t barNumber = 0;
    uint16_t tsigNum = 4;
    uint16_t tsigDenom = 4;

    static ClapTransportValues from(const HostTransport& t) noexcept {
        // CLAP_TRANSPORT_HAS_TEMPO | HAS_BEATS_TIMELINE | HAS_SECONDS_TIMELINE | HAS_TIME_SIGNATURE, then IS_PLAYING
        // (1 << 4) and IS_LOOP_ACTIVE (1 << 6).
        ClapTransportValues v;
        if (!t.valid)
            return v;
        v.flags = 1u | 2u | 4u | 8u;
        if (t.playing)
            v.flags |= 1u << 4;
        const double secondsPerBeat = 60.0 / t.tempo;
        const auto beats = [](double ppq) { return static_cast<int64_t>(std::llround(ppq * kFactor)); };
        const auto seconds = [&](double ppq) { return static_cast<int64_t>(std::llround(ppq * secondsPerBeat * kFactor)); };
        v.songPosBeats = beats(t.ppqPosition);
        v.songPosSeconds = static_cast<int64_t>(std::llround(static_cast<double>(t.samplePosition) / t.sampleRate * kFactor));
        v.tempo = t.tempo;
        if (t.looping && t.loopEndPpq > t.loopStartPpq) {
            v.flags |= 1u << 6;
            v.loopStartBeats = beats(t.loopStartPpq);
            v.loopEndBeats = beats(t.loopEndPpq);
            v.loopStartSeconds = seconds(t.loopStartPpq);
            v.loopEndSeconds = seconds(t.loopEndPpq);
        }
        v.barStart = beats(t.barStartPpq);
        v.barNumber = static_cast<int32_t>(t.barStartPpq / t.timeSigNumerator);
        v.tsigNum = static_cast<uint16_t>(t.timeSigNumerator);
        v.tsigDenom = static_cast<uint16_t>(t.timeSigDenominator);
        return v;
    }
};

class HostTransportClock {
public:
    /// Render thread only.
    static void publish(const HostTransport& t) noexcept {
        Slot& s = slot();
        const uint32_t seq = s.seq.load(std::memory_order_relaxed);
        s.seq.store(seq + 1, std::memory_order_relaxed); // odd: a write is under way
        std::atomic_thread_fence(std::memory_order_release);
        s.flags.store((t.valid ? 1u : 0u) | (t.playing ? 2u : 0u) | (t.looping ? 4u : 0u), std::memory_order_relaxed);
        s.tempo.store(t.tempo, std::memory_order_relaxed);
        s.timeSig.store((static_cast<uint32_t>(t.timeSigNumerator) << 16) |
                            static_cast<uint32_t>(t.timeSigDenominator & 0xFFFF),
                        std::memory_order_relaxed);
        s.samplePosition.store(t.samplePosition, std::memory_order_relaxed);
        s.sampleRate.store(t.sampleRate, std::memory_order_relaxed);
        s.ppqPosition.store(t.ppqPosition, std::memory_order_relaxed);
        s.barStartPpq.store(t.barStartPpq, std::memory_order_relaxed);
        s.loopStartPpq.store(t.loopStartPpq, std::memory_order_relaxed);
        s.loopEndPpq.store(t.loopEndPpq, std::memory_order_relaxed);
        s.seq.store(seq + 2, std::memory_order_release);
    }

    /// Any thread. A snapshot that kept moving under the reader comes back !valid.
    static HostTransport current() noexcept {
        const Slot& s = slot();
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t before = s.seq.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            HostTransport t;
            const uint32_t flags = s.flags.load(std::memory_order_relaxed);
            t.valid = (flags & 1u) != 0;
            t.playing = (flags & 2u) != 0;
            t.looping = (flags & 4u) != 0;
            t.tempo = s.tempo.load(std::memory_order_relaxed);
            const uint32_t sig = s.timeSig.load(std::memory_order_relaxed);
            t.timeSigNumerator = static_cast<int32_t>(sig >> 16);
            t.timeSigDenominator = static_cast<int32_t>(sig & 0xFFFF);
            t.samplePosition = s.samplePosition.load(std::memory_order_relaxed);
            t.sampleRate = s.sampleRate.load(std::memory_order_relaxed);
            t.ppqPosition = s.ppqPosition.load(std::memory_order_relaxed);
            t.barStartPpq = s.barStartPpq.load(std::memory_order_relaxed);
            t.loopStartPpq = s.loopStartPpq.load(std::memory_order_relaxed);
            t.loopEndPpq = s.loopEndPpq.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s.seq.load(std::memory_order_relaxed) == before)
                return t;
        }
        return {};
    }

    /// Tests and a fresh helper: back to "nothing published".
    static void reset() noexcept { publish(HostTransport{}); }

private:
    struct Slot {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint32_t> flags{0};
        std::atomic<double> tempo{120.0};
        std::atomic<uint32_t> timeSig{(4u << 16) | 4u};
        std::atomic<int64_t> samplePosition{0};
        std::atomic<double> sampleRate{48000.0};
        std::atomic<double> ppqPosition{0.0};
        std::atomic<double> barStartPpq{0.0};
        std::atomic<double> loopStartPpq{0.0};
        std::atomic<double> loopEndPpq{0.0};
    };
    static Slot& slot() noexcept {
        static Slot s;
        return s;
    }
};

} // namespace Audio
} // namespace Aestra
