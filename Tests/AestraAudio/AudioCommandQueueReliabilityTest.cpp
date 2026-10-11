// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioCommandQueueReliabilityTest — regression for #913.
//
// The queue is an unreliable bounded UI→RT transport with per-class delivery
// semantics: state commands keep best-effort drop-newest, while edge commands
// (transport, count-in) deliver via pushReliable() — a bounded producer-side
// wait that preserves every accepted edge in FIFO order. These tests pin that
// contract: edges survive a flood in order, and any edge that cannot be
// delivered fails loudly (false + edgeDroppedCount) instead of vanishing.

#include "Core/AudioCommandQueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Aestra::Audio::AudioCommandQueue;
using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

AudioQueueCommand makeCmd(AudioQueueCommandType type, float v1 = 0.0f, uint64_t pos = 0) {
    AudioQueueCommand cmd{};
    cmd.type = type;
    cmd.value1 = v1;
    cmd.samplePos = pos;
    return cmd;
}

void edgeClassification() {
    check(AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetTransportState), "transport is an edge");
    check(AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::MetronomeCountInStart), "count-in start is an edge");
    check(AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::MetronomeCountInStop), "count-in stop is an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::None), "none is not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetTrackVolume), "volume is state, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetTrackPan), "pan is state, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetTrackMute), "mute is state, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetTrackSolo), "solo is state, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::SetMetronomeEnabled),
          "metronome-enabled is state, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::AuditionUnit),
          "audition is best-effort, not an edge");
    check(!AudioCommandQueue::isEdgeCommand(AudioQueueCommandType::LoadProjectState), "unhandled types are not edges");
}

// PLAY/STOP/PLAY pushed through a flooded queue must all land, in order —
// never collapsed into a single "playing = false".
void reliableEdgeSurvivesFloodInOrder() {
    AudioCommandQueue q;
    const size_t cap = AudioCommandQueue::capacity();
    for (size_t i = 0; i < cap; ++i) {
        check(q.push(makeCmd(AudioQueueCommandType::SetTrackVolume, 0.5f)), "flood fill push succeeds");
    }
    check(!q.push(makeCmd(AudioQueueCommandType::SetTrackVolume, 0.5f)), "queue is full after capacity pushes");

    // Hold the drainer shut until the producer is provably inside the reliable
    // wait: with a full queue and no consumer, pushReliable cannot succeed
    // without spinning, so this pins the retry contract instead of racing it.
    std::atomic<bool> releaseDrain{false};
    std::atomic<bool> producerWaiting{false};
    std::vector<AudioQueueCommand> popped;
    std::mutex poppedMutex;
    std::atomic<size_t> poppedCount{0};
    std::atomic<bool> stopDrain{false};
    std::thread drainer([&] {
        while (!releaseDrain.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        AudioQueueCommand cmd{};
        while (!stopDrain.load(std::memory_order_relaxed)) {
            if (q.pop(cmd)) {
                std::lock_guard<std::mutex> lock(poppedMutex);
                popped.push_back(cmd);
                poppedCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
        AudioQueueCommand cmd2{};
        while (q.pop(cmd2)) {
            std::lock_guard<std::mutex> lock(poppedMutex);
            popped.push_back(cmd2);
            poppedCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;
    std::thread producer([&] {
        producerWaiting.store(true, std::memory_order_release);
        ok1 = q.pushReliable(makeCmd(AudioQueueCommandType::SetTransportState, 1.0f, 100));
        ok2 = q.pushReliable(makeCmd(AudioQueueCommandType::SetTransportState, 0.0f, 200));
        ok3 = q.pushReliable(makeCmd(AudioQueueCommandType::SetTransportState, 1.0f, 300));
    });
    while (!producerWaiting.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // The producer has entered the reliable wait against a full, undrained
    // queue; only the release below can let it succeed.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    releaseDrain.store(true, std::memory_order_release);
    producer.join();
    check(ok1 && ok2 && ok3, "edges accepted despite flood");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (poppedCount.load(std::memory_order_relaxed) < cap + 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stopDrain.store(true, std::memory_order_relaxed);
    drainer.join();

    check(popped.size() == cap + 3, "every flooded command plus all three edges drained");
    if (popped.size() == cap + 3) {
        const AudioQueueCommand& e1 = popped[cap];
        const AudioQueueCommand& e2 = popped[cap + 1];
        const AudioQueueCommand& e3 = popped[cap + 2];
        check(e1.type == AudioQueueCommandType::SetTransportState && e1.value1 == 1.0f && e1.samplePos == 100,
              "first edge intact (PLAY@100)");
        check(e2.type == AudioQueueCommandType::SetTransportState && e2.value1 == 0.0f && e2.samplePos == 200,
              "second edge intact (STOP@200)");
        check(e3.type == AudioQueueCommandType::SetTransportState && e3.value1 == 1.0f && e3.samplePos == 300,
              "third edge intact (PLAY@300)");
    }
    check(q.edgeDroppedCount() == 0, "no edge drops counted when delivery succeeds");
}

void reliableEdgeFailsLoudWithoutConsumer() {
    AudioCommandQueue q;
    const size_t cap = AudioCommandQueue::capacity();
    for (size_t i = 0; i < cap; ++i) {
        q.push(makeCmd(AudioQueueCommandType::SetTrackVolume, 0.5f));
    }
    const bool ok = q.pushReliable(makeCmd(AudioQueueCommandType::SetTransportState, 0.0f, 0), 5);
    check(!ok, "edge push fails when no consumer drains within the deadline");
    check(q.edgeDroppedCount() == 1, "failed edge counted separately");
    check(q.droppedCount() == 1, "failed edge still counted in total drops");
}

void stateDropDoesNotTouchEdgeCounter() {
    AudioCommandQueue q;
    const size_t cap = AudioCommandQueue::capacity();
    for (size_t i = 0; i < cap; ++i) {
        q.push(makeCmd(AudioQueueCommandType::SetTrackVolume, 0.5f));
    }
    check(!q.push(makeCmd(AudioQueueCommandType::SetTrackPan, 0.1f)), "state push fails when full");
    check(q.droppedCount() == 1, "state drop counted in total drops");
    check(q.edgeDroppedCount() == 0, "state drop leaves the edge counter at zero");
}

} // namespace

int main() {
    std::cout << "=== AudioCommandQueue reliability (#913) ===\n";
    edgeClassification();
    reliableEdgeSurvivesFloodInOrder();
    reliableEdgeFailsLoudWithoutConsumer();
    stateDropDoesNotTouchEdgeCounter();

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
