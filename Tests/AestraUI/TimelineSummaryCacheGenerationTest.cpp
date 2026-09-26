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
//   * a published snapshot reports the generation of the rebuild it came from,
//     together with that rebuild's layout;
//   * incremental deltas keep the generation of the rebuild they apply to.
//
// Reading discipline: the cache is double-buffered, so a reader may only look
// at a snapshot while at most one publish can follow it. Every wait below
// happens with exactly one task outstanding, and the next request takes the
// cache mutex after the reads, which orders them before the worker's next
// write. The burst of requests at the end is never read back for that reason;
// it only checks the returned generations.

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

// Waits (bounded) until the published snapshot satisfies `done`.
template <typename Pred>
AestraUI::TimelineSummarySnapshot waitFor(const AestraUI::TimelineSummaryCache& cache, Pred done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    AestraUI::TimelineSummarySnapshot snap = cache.getSnapshot();
    while (!done(snap) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        snap = cache.getSnapshot();
    }
    return snap;
}

AestraUI::TimelineMinimapClipSpan clipOnLane(uint32_t lane) {
    AestraUI::TimelineMinimapClipSpan span;
    span.id = 1;
    span.startBeat = 0.0;
    span.endBeat = 4.0;
    span.trackIndex = lane;
    return span;
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

    // One rebuild at a time, the clip moving lane each time (a lane reorder): each published
    // snapshot pairs the generation with that request's layout.
    uint64_t last = 0;
    for (uint32_t lane = 0; lane < 4; ++lane) {
        const uint64_t generation = cache.requestRebuild({clipOnLane(lane)}, 0.0, 16.0, 64);
        check(generation > last, "each requestRebuild returns a larger generation");
        last = generation;

        const AestraUI::TimelineSummarySnapshot snap = waitFor(
            cache, [&](const AestraUI::TimelineSummarySnapshot& s) { return s.rebuildGeneration == generation; });
        check(snap.rebuildGeneration == generation, "the snapshot reaches the requested generation");
        check(laneHasPresence(snap, lane), "that generation carries its own request's lane layout");
        check(lane == 0 || !laneHasPresence(snap, lane - 1), "that generation does not carry the previous layout");
    }

    // Deltas on top of that rebuild keep its generation (the lane order has not changed).
    const uint64_t versionBeforeDelta = cache.getSnapshot().version;
    AestraUI::TimelineMinimapClipDelta delta;
    delta.hasBefore = true;
    delta.before = clipOnLane(3);
    cache.requestApplyDeltas({delta}, 0.0, 16.0);
    const AestraUI::TimelineSummarySnapshot afterDelta =
        waitFor(cache, [&](const AestraUI::TimelineSummarySnapshot& s) { return s.version > versionBeforeDelta; });
    check(afterDelta.version > versionBeforeDelta, "the delta publishes a new version");
    check(afterDelta.rebuildGeneration == last, "a delta keeps the generation of the rebuild it applies to");
    check(!laneHasPresence(afterDelta, 3), "the delta removed the clip");

    // A burst of requests (a newer request supersedes queued older ones): generations still
    // increase. Not read back; see the reading discipline above.
    for (uint32_t lane = 0; lane < 8; ++lane) {
        const uint64_t generation = cache.requestRebuild({clipOnLane(lane)}, 0.0, 16.0, 64);
        check(generation > last, "a burst of requests still returns larger generations");
        last = generation;
    }

    if (g_failures == 0) {
        std::cout << "[PASS] TimelineSummaryCacheGenerationTest\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
