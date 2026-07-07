// © 2026 Aestra Studios — All Rights Reserved.
// ClipPrefilterService — background computation of anti-aliased clip copies
// (Phase 4, F1; design: AestraDocs/clip-prefilter-lifecycle.md).
//
// Threading model (deliberately narrow):
//   * enqueue()/drainCompleted()/waitIdle() are called on the graph-build thread
//     (TrackManager::ensureClipPrefilters) or another non-audio thread.
//   * One lazily-started worker thread runs the pure ClipPrefilter math on job
//     data it OWNS (shared_ptr copies) — it never touches SourceManager/ClipSource
//     (their containers are not thread-safe), and it never runs on or blocks the
//     audio thread.
//   * On completion the worker invokes the (atomic-only) rebuild-request callback
//     so the app's graph pump picks the filtered copy up on its next drain.
#pragma once

#include "ClipSource.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace Aestra {
namespace Audio {

class ClipPrefilterService {
public:
    /// `onJobComplete` is invoked FROM THE WORKER THREAD after each finished job;
    /// it must only touch atomics (TrackManager::requestAudioGraphRebuild is).
    explicit ClipPrefilterService(std::function<void()> onJobComplete);
    ~ClipPrefilterService();

    ClipPrefilterService(const ClipPrefilterService&) = delete;
    ClipPrefilterService& operator=(const ClipPrefilterService&) = delete;

    struct Job {
        uint64_t sourceId{0};
        uint64_t contentRevision{0};
        uint32_t targetRate{0};
        std::shared_ptr<const AudioBufferData> source; // owned copy; worker-safe
    };

    struct Result {
        uint64_t sourceId{0};
        uint64_t contentRevision{0};
        uint32_t targetRate{0};
        uint32_t specVersion{0};
        std::shared_ptr<AudioBufferData> filtered;
        double buildMillis{0.0}; // diagnostic, quoted by the research bench
    };

    /// Queue a job unless the same (sourceId, revision, targetRate) is already
    /// queued, running, or completed-but-undrained. Starts the worker on first use.
    void enqueue(Job job);

    /// Move out all completed results (graph-build thread applies them).
    std::vector<Result> drainCompleted();

    /// True if any job is queued, running, or completed-but-undrained.
    bool hasWork() const;

    /// Block the CALLING (non-audio) thread until queue and worker are idle.
    /// Completed results still need drainCompleted() + a graph rebuild to apply.
    void waitIdle();

private:
    void workerLoop();

    using Key = std::tuple<uint64_t, uint64_t, uint32_t>;

    std::function<void()> m_onJobComplete;

    mutable std::mutex m_mutex;
    std::condition_variable m_wakeWorker;
    std::condition_variable m_idle;
    std::deque<Job> m_queue;
    std::vector<Result> m_completed;
    std::set<Key> m_tracked; // queued + running + undrained keys (dedupe)
    bool m_running{false};   // a job is being computed right now
    bool m_shutdown{false};
    std::thread m_worker;
    bool m_workerStarted{false};
};

} // namespace Audio
} // namespace Aestra
