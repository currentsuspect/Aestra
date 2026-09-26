// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// TimelineSummaryCache rebuild generations (#978).
//
// The minimap holds its lane colours back until the summary it pairs them with
// has the same lane order. The summary rebuild runs on the cache's worker, so
// "some newer summary" is not enough: an older rebuild already taken by the
// worker can publish after a newer request. The contract pinned here is what
// the minimap relies on instead:
//
//   * every requestRebuild() returns a strictly larger generation;
//   * the snapshot eventually reports the generation of the LATEST request
//     (a newer request supersedes queued older ones, never the other way);
//   * that snapshot carries the latest request's layout;
//   * incremental deltas keep the generation of the rebuild they apply to.

#include "TimelineSummaryCache.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

// Waits (bounded) for the worker to publish the given rebuild generation.
AestraUI::TimelineSummarySnapshot waitForGeneration(const AestraUI::TimelineSummaryCache& cache, uint64_t generation) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    AestraUI::TimelineSummarySnapshot snap = cache.getSnapshot();
    while (snap.rebuildGeneration != generation && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        snap = cache.getSnapshot();
    }
    return snap;
}

std::vector<AestraUI::TimelineMinimapClipSpan> oneClipOnLane(uint32_t lane) {
    AestraUI::TimelineMinimapClipSpan span;
    span.id = 1;
    span.startBeat = 0.0;
    span.endBeat = 4.0;
    span.trackIndex = lane;
    return {span};
}

bool laneHasPresence(const AestraUI::TimelineSummarySnapshot& snap, uint32_t lane) {
    if (!snap.summary || snap.summary->buckets.empty()) {
        return false;
    }
    return snap.summary->buckets.front().trackCounts[lane] > 0;
}

} // namespace

int main() {
    AestraUI::TimelineSummaryCache cache;

    check(cache.getSnapshot().rebuildGeneration == 0, "a fresh cache reports generation 0");

    // Several rebuilds back to back: the lane moves each time, as in a lane reorder.
    uint64_t last = 0;
    for (uint32_t lane = 0; lane < 8; ++lane) {
        const uint64_t generation = cache.requestRebuild(oneClipOnLane(lane), 0.0, 16.0, 64);
        check(generation > last, "each requestRebuild returns a larger generation");
        last = generation;
    }

    const AestraUI::TimelineSummarySnapshot latest = waitForGeneration(cache, last);
    check(latest.rebuildGeneration == last, "the snapshot reaches the latest requested generation");
    check(laneHasPresence(latest, 7), "the latest generation carries the latest request's lane layout");
    check(!laneHasPresence(latest, 0), "the latest generation does not carry an older request's layout");

    // Deltas on top of that rebuild keep its generation (the lane order has not changed).
    AestraUI::TimelineMinimapClipDelta delta;
    delta.hasBefore = true;
    delta.before = oneClipOnLane(7).front();
    cache.requestApplyDeltas({delta}, 0.0, 16.0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    AestraUI::TimelineSummarySnapshot afterDelta = cache.getSnapshot();
    while (afterDelta.version == latest.version && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        afterDelta = cache.getSnapshot();
    }
    check(afterDelta.version > latest.version, "the delta publishes a new version");
    check(afterDelta.rebuildGeneration == last, "a delta keeps the generation of the rebuild it applies to");

    if (g_failures == 0) {
        std::cout << "[PASS] TimelineSummaryCacheGenerationTest\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
