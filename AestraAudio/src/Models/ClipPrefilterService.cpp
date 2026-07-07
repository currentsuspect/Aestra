// © 2026 Aestra Studios — All Rights Reserved.
#include "Models/ClipPrefilterService.h"

#include "DSP/ClipPrefilter.h"

#include <chrono>
#include <utility>

namespace Aestra {
namespace Audio {

ClipPrefilterService::ClipPrefilterService(std::function<void()> onJobComplete)
    : m_onJobComplete(std::move(onJobComplete)) {}

ClipPrefilterService::~ClipPrefilterService() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }
    m_wakeWorker.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void ClipPrefilterService::enqueue(Job job) {
    if (!job.source || !job.source->isValid() ||
        !ClipPrefilter::isNeeded(job.source->sampleRate, job.targetRate)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown) {
            return;
        }
        const Key key{job.sourceId, job.contentRevision, job.targetRate};
        if (m_tracked.count(key) != 0) {
            return; // already queued, running, or awaiting drain
        }
        m_tracked.insert(key);
        m_queue.push_back(std::move(job));
        if (!m_workerStarted) {
            m_worker = std::thread(&ClipPrefilterService::workerLoop, this);
            m_workerStarted = true;
        }
    }
    m_wakeWorker.notify_one();
}

std::vector<ClipPrefilterService::Result> ClipPrefilterService::drainCompleted() {
    std::vector<Result> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    out.swap(m_completed);
    for (const Result& r : out) {
        m_tracked.erase(Key{r.sourceId, r.contentRevision, r.targetRate});
    }
    return out;
}

bool ClipPrefilterService::hasWork() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty() || m_running || !m_completed.empty();
}

void ClipPrefilterService::waitIdle() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return (m_queue.empty() && !m_running) || m_shutdown; });
}

void ClipPrefilterService::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wakeWorker.wait(lock, [this] { return m_shutdown || !m_queue.empty(); });
            if (m_shutdown) {
                m_idle.notify_all();
                return;
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
            m_running = true;
        }

        // Pure math on worker-owned data; no model access, no audio-thread contact.
        const auto t0 = std::chrono::steady_clock::now();
        Result result;
        result.sourceId = job.sourceId;
        result.contentRevision = job.contentRevision;
        result.targetRate = job.targetRate;
        result.specVersion = ClipPrefilter::kSpecVersion;
        const std::vector<double> h = ClipPrefilter::design(job.source->sampleRate, job.targetRate);
        if (!h.empty()) {
            auto filtered = std::make_shared<AudioBufferData>();
            filtered->sampleRate = job.source->sampleRate; // filtering does NOT resample
            filtered->numChannels = job.source->numChannels;
            filtered->numFrames = job.source->numFrames;
            filtered->interleavedData.resize(job.source->interleavedData.size());
            ClipPrefilter::apply(job.source->interleavedData.data(), filtered->interleavedData.data(),
                                 job.source->numFrames, job.source->numChannels, h);
            result.filtered = std::move(filtered);
        }
        result.buildMillis =
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                std::chrono::steady_clock::now() - t0)
                .count();

        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            if (result.filtered) {
                m_completed.push_back(std::move(result));
                notify = true;
            } else {
                // Nothing produced (spec said not needed): stop tracking the key.
                m_tracked.erase(Key{job.sourceId, job.contentRevision, job.targetRate});
            }
        }
        m_idle.notify_all();
        if (notify && m_onJobComplete) {
            m_onJobComplete(); // atomic-only by contract (graph dirty request)
        }
    }
}

} // namespace Audio
} // namespace Aestra
