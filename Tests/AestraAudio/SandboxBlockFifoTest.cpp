// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The sandbox hand-over FIFOs (SandboxBlockFifo.h): audio survives wrap-around and arbitrary
// callback cuts unchanged, and MIDI offsets re-base from callback time to chunk time.

#include "Plugin/SandboxBlockFifo.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Aestra::Audio;

namespace {

void testAudioSurvivesCutsAndWraps() {
    InterleavedFifo in;
    InterleavedFifo out;
    in.reset(300, 2);
    out.reset(300, 2);
    constexpr size_t kChunk = 128;
    out.pushSilence(kChunk); // primed one chunk behind, as the sandbox does
    const size_t cuts[] = {128, 40, 88, 128, 1, 127, 64, 64, 100, 28};
    std::vector<float> l(256), r(256), chunkBuf(kChunk * 2);
    float next = 1.0f;
    float expect = 1.0f;
    size_t played = 0;
    for (int round = 0; round < 50; ++round) {
        for (size_t cut : cuts) {
            for (size_t i = 0; i < cut; ++i) {
                l[i] = next;
                r[i] = -next;
                next += 1.0f;
            }
            const float* chans[2] = {l.data(), r.data()};
            assert(in.pushPlanar(chans, 2, cut) == cut);
            if (in.frames() >= kChunk) {
                in.popInterleaved(chunkBuf.data(), kChunk);      // to the "helper"
                assert(out.pushInterleaved(chunkBuf.data(), kChunk) == kChunk); // back from it, unchanged
            }
            std::vector<float> ol(cut), orr(cut), o3(cut, 9.0f);
            float* outs[3] = {ol.data(), orr.data(), o3.data()};
            assert(out.frames() >= cut);
            out.popPlanar(outs, 3, cut);
            for (size_t i = 0; i < cut; ++i, ++played) {
                if (played < kChunk) {
                    assert(ol[i] == 0.0f && orr[i] == 0.0f); // the primed chunk
                } else {
                    assert(ol[i] == expect && orr[i] == -expect);
                    expect += 1.0f;
                }
                assert(o3[i] == 0.0f && "outputs past the FIFO's channels are silenced");
            }
        }
    }
    std::printf("[PASS] audio plays exactly one chunk behind through any callback cuts and wraps\n");
}

void testMidiRebasesToChunkTime() {
    ChunkMidiQueue q;
    const uint8_t on[3] = {0x90, 60, 100};
    const uint8_t off[3] = {0x80, 60, 0};
    // Callback A covers FIFO frames [0, 40): note-on at offset 10. Callback B covers [40, 128): off at offset 50.
    assert(q.push(0 + 10, on));
    assert(q.push(40 + 50, off));
    assert(q.push(130, on)); // belongs to the next chunk
    uint8_t buf[64];
    const size_t bytes = q.takeChunk(0, 128, buf, sizeof(buf));
    assert(bytes == 16);
    uint32_t o0 = 0, o1 = 0;
    std::memcpy(&o0, buf, 4);
    std::memcpy(&o1, buf + 8, 4);
    assert(o0 == 10 && buf[4] == 3 && buf[5] == 0x90);
    assert(o1 == 90 && buf[13] == 0x80);
    const size_t rest = q.takeChunk(128, 256, buf, sizeof(buf));
    assert(rest == 8);
    std::memcpy(&o0, buf, 4);
    assert(o0 == 2 && "an event in the next chunk is offset from that chunk's start");
    std::printf("[PASS] MIDI offsets re-base from callback time to chunk time\n");
}

} // namespace

int main() {
    testAudioSurvivesCutsAndWraps();
    testMidiRebasesToChunkTime();
    return 0;
}
