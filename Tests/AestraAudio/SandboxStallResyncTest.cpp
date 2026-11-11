// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// #1229: a sandboxed plugin that misses one deadline must come back to exactly one chunk of delay.
//
// The sandbox runs the plugin one chunk behind the engine, and PDC compensates exactly that chunk.
// When the helper was late once, the late result still played (a chunk later than PDC expects) and
// the audio that played dry stayed queued, so the track sat one chunk late for good. Here the test
// stands in for the helper (an identity plugin) and decides when each result comes back. Every input
// sample is its own position + 1, so each output sample says where it came from: 0 is silence, the
// current input is dry, anything else is wet and its delay can be read off.

#include "OutOfProcessPluginInstance.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kChannels = 2;
constexpr uint32_t kCapacity = 4096; // what EffectChain::prepare passes

enum Kind : int64_t { kSilence = -1, kDry = -2, kMixed = -3 };

struct Helper {
    std::vector<float> held;
    uint32_t frames = 0;
    bool holding = false;
    int lateCallbacks = 0; // how many more callbacks the held chunk waits before it is posted
    int lost = 0;
};

class Rig {
public:
    Rig() : m_plugin(PluginInfo{}, "") { m_plugin.prepareHandOffForTest(48000.0, kCapacity); }

    // One callback, then the helper's turn. Returns the delay heard in this callback (or a Kind).
    int64_t callback(uint32_t frames, int stallFor = 0) {
        std::vector<float> in[kChannels], out[kChannels];
        const float* inPtr[kChannels];
        float* outPtr[kChannels];
        for (uint32_t c = 0; c < kChannels; ++c) {
            in[c].resize(frames);
            out[c].assign(frames, 0.0f);
            for (uint32_t i = 0; i < frames; ++i) {
                in[c][i] = static_cast<float>(m_position + i + 1);
            }
            inPtr[c] = in[c].data();
            outPtr[c] = out[c].data();
        }
        m_plugin.process(inPtr, outPtr, kChannels, kChannels, frames, nullptr, nullptr);

        int64_t heard = kSilence;
        bool first = true;
        for (uint32_t i = 0; i < frames; ++i) {
            const float v = out[0][i];
            assert(out[1][i] == v && "both channels carry the same source");
            int64_t kind;
            if (v == 0.0f) {
                kind = kSilence;
            } else if (v == in[0][i]) {
                kind = kDry;
            } else {
                kind = static_cast<int64_t>(m_position + i) - (static_cast<int64_t>(v) - 1);
            }
            // Silence may lead into wet audio (start-up, a chunk that grew); anything else must be uniform.
            if (first) {
                heard = kind;
                first = false;
            } else if (kind != heard && !(heard == kSilence && kind >= 0)) {
                heard = kMixed;
            } else if (heard == kSilence && kind >= 0) {
                heard = kind;
            }
        }
        m_position += frames;
        helperTurn(stallFor);
        return heard;
    }

    int lost() const { return m_helper.lost; }

private:
    void post() {
        if (!m_plugin.postResultForTest(m_helper.held, m_helper.frames)) {
            ++m_helper.lost;
        }
        m_helper.holding = false;
    }

    // A real helper finishes one chunk, then takes the next; it is only ever late, never early.
    void helperTurn(int stallFor) {
        if (m_helper.holding) {
            if (m_helper.lateCallbacks > 0) {
                --m_helper.lateCallbacks;
                return;
            }
            post();
        }
        if (m_plugin.takeChunkForTest(m_helper.held, m_helper.frames)) {
            m_helper.holding = true;
            m_helper.lateCallbacks = stallFor;
            if (stallFor == 0) {
                post();
            }
        }
    }

    OutOfProcessPluginInstance m_plugin;
    Helper m_helper;
    uint64_t m_position = 0;
};

const char* describe(int64_t heard) {
    static char text[32];
    if (heard == kSilence)
        return "silence";
    if (heard == kDry)
        return "dry";
    if (heard == kMixed)
        return "mixed";
    std::snprintf(text, sizeof text, "delay %lld", static_cast<long long>(heard));
    return text;
}

// Steady callbacks: silence for the first chunk, then exactly one chunk of delay.
void testSteadyDelayIsOneChunk() {
    Rig rig;
    assert(rig.callback(256) == kSilence);
    for (int i = 0; i < 20; ++i) {
        const int64_t heard = rig.callback(256);
        assert(heard == 256 && "steady state: one chunk behind");
    }
    std::printf("[PASS] steady: one chunk of silence, then exactly 256 frames of delay\n");
}

// The helper misses one deadline (or a few). After a short recovery the delay is one chunk again,
// and wet audio is never heard at any other delay.
void testRecoversAfterStall(int lateBy) {
    Rig rig;
    const uint32_t chunk = 256;
    for (int i = 0; i < 10; ++i) {
        rig.callback(chunk);
    }
    rig.callback(chunk, lateBy); // the helper takes this callback's chunk and returns it late
    int recovery = 0;
    bool back = false;
    for (int i = 0; i < 40; ++i) {
        const int64_t heard = rig.callback(chunk);
        std::printf("    after a %d-callback stall, callback %2d: %s\n", lateBy, i, describe(heard));
        assert(heard != kMixed);
        assert((heard == kDry || heard == kSilence || heard == chunk) && "wet audio only ever at one chunk of delay");
        if (heard == chunk) {
            back = true;
        } else {
            assert(!back && "once recovered it stays recovered");
            ++recovery;
        }
    }
    assert(back && "the delay returns to one chunk");
    assert(recovery <= lateBy + 2 && "within a few callbacks");
    std::printf("[PASS] %d-callback stall: back to exactly one chunk after %d callbacks (%d results lost)\n", lateBy,
                recovery, rig.lost());
}

// A loop wrap splits a callback in two: the delay stays one chunk with no dry or silent gap.
void testSplitCallbacksStayExact() {
    Rig rig;
    rig.callback(256);
    for (int i = 0; i < 20; ++i) {
        const int64_t a = rig.callback(256);
        assert(a == 256);
        const int64_t b = rig.callback(100);
        const int64_t c = rig.callback(156);
        assert(b == 256 && c == 256 && "a split callback keeps the delay");
    }
    std::printf("[PASS] split callbacks (256 / 100+156) keep exactly 256 frames of delay\n");
}

// The callback grows: the chunk (and the reported latency) grows with it, and the delay follows.
void testGrowingCallbackFollows() {
    Rig rig;
    for (int i = 0; i < 6; ++i) {
        rig.callback(256);
    }
    const int64_t growing = rig.callback(512);
    assert((growing == 512 || growing == kSilence) && "the grown chunk's delay, with silence where it grew");
    for (int i = 0; i < 10; ++i) {
        assert(rig.callback(512) == 512);
    }
    std::printf("[PASS] callback grows 256 -> 512: delay follows to exactly 512\n");
}

// A callback with no frames before any audio (chunk still 0) must return, not spin trimming a 0-frame backlog.
void testEmptyFirstCallback() {
    Rig rig;
    assert(rig.callback(0) == kSilence);
    assert(rig.callback(256) == kSilence);
    for (int i = 0; i < 5; ++i) {
        assert(rig.callback(256) == 256);
    }
    std::printf("[PASS] a 0-frame first callback returns, and the delay starts normally after it\n");
}

} // namespace

int main() {
    testSteadyDelayIsOneChunk();
    testRecoversAfterStall(1);
    testRecoversAfterStall(3);
    testSplitCallbacksStayExact();
    testGrowingCallbackFollows();
    testEmptyFirstCallback();
    return 0;
}
