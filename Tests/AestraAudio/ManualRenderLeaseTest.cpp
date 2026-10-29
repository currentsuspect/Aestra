// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A render driven by hand (export, bounce, Muse) must never run alongside the live device
// callback in the same engine. Under a ManualRenderLease, processBlock on any other thread
// plays silence; the owner keeps rendering; the lease nests; releasing it hands the engine back.

#include "Core/AudioEngine.h"
#include "Core/ManualRenderLease.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kFrames = 256;

bool anyNonZero(const std::vector<float>& v) {
    for (float x : v) {
        if (x != 0.0f) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    AudioEngine engine;
    engine.setSampleRate(48000);
    assert(engine.setBufferConfig(kFrames, 2));
    engine.setTestToneEnabled(true);

    std::atomic<int> phase{0}; // 0: no lease, 1: lease held, 2: released, 3: stop
    std::atomic<int> loudDuringLease{0};
    std::atomic<int> loudAfter{0};
    std::atomic<int> blocksDuringLease{0};
    std::thread live([&] {
        std::vector<float> out(kFrames * 2);
        while (phase.load() != 3) {
            const int p = phase.load();
            engine.processBlock(out.data(), nullptr, kFrames, 0.0);
            if (p == 1 && phase.load() == 1) {
                blocksDuringLease.fetch_add(1);
                if (anyNonZero(out)) {
                    loudDuringLease.fetch_add(1);
                }
            } else if (p == 2 && anyNonZero(out)) {
                loudAfter.fetch_add(1);
            }
        }
    });

    {
        ManualRenderLease lease(engine);
        phase.store(1);
        {
            ManualRenderLease nested(engine); // a render inside a render (Muse -> exporter)
            std::vector<float> out(kFrames * 2);
            for (int i = 0; i < 200; ++i) {
                engine.processBlock(out.data(), nullptr, kFrames, 0.0);
                assert(anyNonZero(out) && "the lease owner keeps rendering");
            }
        }
        std::vector<float> out(kFrames * 2);
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
        assert(anyNonZero(out) && "the outer lease still belongs to this thread after the nested one ends");
        while (blocksDuringLease.load() < 50) {
            std::this_thread::yield();
        }
    }
    phase.store(2);
    while (loudAfter.load() < 10) {
        std::this_thread::yield();
    }
    phase.store(3);
    live.join();

    assert(loudDuringLease.load() == 0 && "the live callback plays silence while another thread renders");
    std::printf("[PASS] live blocks under the lease: %d, all silent; the engine plays again once released\n",
                blocksDuringLease.load());
    return 0;
}
