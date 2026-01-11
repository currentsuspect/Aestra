// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "TimelineSummaryCache.h"

#include <algorithm>
#include <cmath>

namespace NomadUI {

namespace {

constexpr double kEpsilonDomain = 1e-9;
constexpr double kEpsilonDt = 1e-12;

inline void clearSummaryKeepCapacity(TimelineSummary& s, uint32_t bucketCount)
{
    s.bucketCount = bucketCount;
    if (s.buckets.size() != bucketCount) {
        s.buckets.assign(bucketCount, TimelineSummaryBucket{});
    } else {
        std::fill(s.buckets.begin(), s.buckets.end(), TimelineSummaryBucket{});
    }

    s.maxAudio = 0;
    s.maxMidi = 0;
    s.maxAutomation = 0;
    s.maxEnergySum = 0.0f;
    s.maxPeakSum = 0.0f;
}

} // namespace

TimelineSummaryCache::TimelineSummaryCache()
{
    buffers_[0].bucketCount = kDefaultBucketCount;
    buffers_[1].bucketCount = kDefaultBucketCount;
    buffers_[0].buckets.assign(kDefaultBucketCount, TimelineSummaryBucket{});
    buffers_[1].buckets.assign(kDefaultBucketCount, TimelineSummaryBucket{});
}

TimelineSummaryCache::~TimelineSummaryCache()
{
    stopWorker_();
}

void TimelineSummaryCache::requestRebuild(std::vector<TimelineMinimapClipSpan> spans, double domainStartBeat,
                                         double domainEndBeat, uint32_t bucketCount)
{
    ensureWorker_();

    Task task;
    task.kind = TaskKind::Rebuild;
    task.domainStartBeat = domainStartBeat;
    task.domainEndBeat = domainEndBeat;
    task.bucketCount = (bucketCount > 0) ? bucketCount : kDefaultBucketCount;
    task.spans = std::move(spans);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.clear(); // rebuild supersedes any pending incremental work
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void TimelineSummaryCache::requestApplyDeltas(std::vector<TimelineMinimapClipDelta> deltas, double expectedDomainStartBeat,
                                             double expectedDomainEndBeat)
{
    if (deltas.empty())
        return;

    ensureWorker_();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If a rebuild is queued, drop deltas (the caller should re-send via rebuild state).
        if (!tasks_.empty() && tasks_.back().kind == TaskKind::Rebuild) {
            return;
        }

        if (!tasks_.empty() && tasks_.back().kind == TaskKind::ApplyDeltas &&
            std::abs(tasks_.back().domainStartBeat - expectedDomainStartBeat) <= kEpsilonDomain &&
            std::abs(tasks_.back().domainEndBeat - expectedDomainEndBeat) <= kEpsilonDomain) {
            auto& existing = tasks_.back().deltas;
            existing.insert(existing.end(), deltas.begin(), deltas.end());
        } else {
            Task task;
            task.kind = TaskKind::ApplyDeltas;
            task.domainStartBeat = expectedDomainStartBeat;
            task.domainEndBeat = expectedDomainEndBeat;
            task.deltas = std::move(deltas);
            tasks_.push_back(std::move(task));
        }
    }

    cv_.notify_one();
}

TimelineSummarySnapshot TimelineSummaryCache::getSnapshot() const noexcept
{
    const uint32_t idx = frontIndex_.load(std::memory_order_acquire) & 1u;
    const TimelineSummary& s = buffers_[idx];
    return TimelineSummarySnapshot{&s, s.version};
}

void TimelineSummaryCache::ensureWorker_()
{
    if (workerStarted_)
        return;

    workerStarted_ = true;
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread([this]() { workerLoop_(); });
}

void TimelineSummaryCache::stopWorker_()
{
    if (!workerStarted_)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true, std::memory_order_release);
        tasks_.clear();
    }
    cv_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }

    workerStarted_ = false;
}

void TimelineSummaryCache::workerLoop_()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_.load(std::memory_order_acquire) || !tasks_.empty(); });

            if (stop_.load(std::memory_order_acquire)) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        const uint32_t front = frontIndex_.load(std::memory_order_acquire) & 1u;
        const uint32_t back = (front ^ 1u) & 1u;

        if (task.kind == TaskKind::Rebuild) {
            rebuild_(buffers_[back], task.spans, task.domainStartBeat, task.domainEndBeat, task.bucketCount);
            buffers_[back].version = buffers_[front].version + 1;

            // Rebuild the clip index from scratch to match the newly published domain.
            clipIndex_.clear();
            clipIndex_.reserve(task.spans.size());
            for (const auto& span : task.spans) {
                ClipIndex idx;
                addSpan_(buffers_[back], span, idx);
                clipIndex_[span.id] = idx;
            }

            recomputeMaxima_(buffers_[back]);
            frontIndex_.store(back, std::memory_order_release);
            continue;
        }

        if (task.kind == TaskKind::ApplyDeltas) {
            const TimelineSummary& src = buffers_[front];
            if (std::abs(src.domainStartBeat - task.domainStartBeat) > kEpsilonDomain ||
                std::abs(src.domainEndBeat - task.domainEndBeat) > kEpsilonDomain) {
                // Domain mismatch; ignore incremental updates (caller should send a rebuild).
                continue;
            }

            applyDeltas_(buffers_[back], src, task.deltas);
            buffers_[back].version = buffers_[front].version + 1;
            recomputeMaxima_(buffers_[back]);
            frontIndex_.store(back, std::memory_order_release);
            continue;
        }
    }
}

void TimelineSummaryCache::rebuild_(TimelineSummary& dst, const std::vector<TimelineMinimapClipSpan>& spans,
                                   double domainStartBeat, double domainEndBeat, uint32_t bucketCount)
{
    dst.domainStartBeat = domainStartBeat;
    dst.domainEndBeat = domainEndBeat;
    clearSummaryKeepCapacity(dst, bucketCount);

    // Actual span accumulation is done by the caller when rebuilding clipIndex_ as well,
    // so keep this method minimal and data-structure focused.
}

void TimelineSummaryCache::applyDeltas_(TimelineSummary& dst, const TimelineSummary& src,
                                       const std::vector<TimelineMinimapClipDelta>& deltas)
{
    dst.domainStartBeat = src.domainStartBeat;
    dst.domainEndBeat = src.domainEndBeat;
    clearSummaryKeepCapacity(dst, src.bucketCount);

    if (dst.buckets.size() == src.buckets.size()) {
        std::copy(src.buckets.begin(), src.buckets.end(), dst.buckets.begin());
    }

    for (const auto& d : deltas) {
        if (d.hasBefore) {
            auto it = clipIndex_.find(d.before.id);
            if (it != clipIndex_.end()) {
                removeSpan_(dst, it->second);
                clipIndex_.erase(it);
            }
        }

        if (d.hasAfter) {
            ClipIndex idx;
            addSpan_(dst, d.after, idx);
            clipIndex_[d.after.id] = idx;
        }
    }
}

void TimelineSummaryCache::recomputeMaxima_(TimelineSummary& s)
{
    uint32_t maxAudio = 0;
    uint32_t maxMidi = 0;
    uint32_t maxAutomation = 0;
    float maxEnergy = 0.0f;
    float maxPeak = 0.0f;

    for (const auto& b : s.buckets) {
        maxAudio = std::max(maxAudio, static_cast<uint32_t>(std::max(0, b.audioCount)));
        maxMidi = std::max(maxMidi, static_cast<uint32_t>(std::max(0, b.midiCount)));
        maxAutomation = std::max(maxAutomation, static_cast<uint32_t>(std::max(0, b.automationCount)));
        maxEnergy = std::max(maxEnergy, std::max(0.0f, b.energySum));
        maxPeak = std::max(maxPeak, std::max(0.0f, b.peakSum));
    }

    s.maxAudio = maxAudio;
    s.maxMidi = maxMidi;
    s.maxAutomation = maxAutomation;
    s.maxEnergySum = maxEnergy;
    s.maxPeakSum = maxPeak;
}

int TimelineSummaryCache::clampBucketIndex_(int idx, int n)
{
    if (n <= 0)
        return 0;
    return std::max(0, std::min(idx, n - 1));
}

bool TimelineSummaryCache::computeBucketRange_(int& outI0, int& outI1, double startBeat, double endBeat,
                                              double domainStartBeat, double domainEndBeat, int n)
{
    const double denom = (domainEndBeat - domainStartBeat);
    if (denom <= kEpsilonDomain || n <= 0)
        return false;

    if (!(endBeat > startBeat))
        return false;

    const double dt = denom / static_cast<double>(n);
    if (dt <= kEpsilonDt)
        return false;

    const double s0 = std::max(startBeat, domainStartBeat);
    const double s1 = std::min(endBeat, domainEndBeat);
    if (!(s1 > s0))
        return false;

    const int i0 = static_cast<int>(std::floor((s0 - domainStartBeat) / dt));
    const int i1 = static_cast<int>(std::ceil((s1 - domainStartBeat) / dt)) - 1;

    outI0 = clampBucketIndex_(i0, n);
    outI1 = clampBucketIndex_(i1, n);
    if (outI1 < outI0)
        std::swap(outI0, outI1);
    return true;
}

void TimelineSummaryCache::addSpan_(TimelineSummary& s, const TimelineMinimapClipSpan& span, ClipIndex& outIndex)
{
    outIndex.type = span.type;
    outIndex.i0 = 0;
    outIndex.i1 = -1;

    int i0 = 0, i1 = -1;
    if (!computeBucketRange_(i0, i1, span.startBeat, span.endBeat, s.domainStartBeat, s.domainEndBeat,
                             static_cast<int>(s.bucketCount))) {
        return;
    }

    outIndex.i0 = i0;
    outIndex.i1 = i1;

    for (int i = i0; i <= i1; ++i) {
        auto& b = s.buckets[static_cast<size_t>(i)];
        switch (span.type) {
            case TimelineMinimapClipType::Audio:
                b.audioCount += 1;
                break;
            case TimelineMinimapClipType::Midi:
                b.midiCount += 1;
                break;
            case TimelineMinimapClipType::Automation:
                b.automationCount += 1;
                break;
        }

        b.energySum += span.energyApprox;
        b.peakSum += span.peakApprox;
    }
}

void TimelineSummaryCache::removeSpan_(TimelineSummary& s, const ClipIndex& idx)
{
    if (idx.i1 < idx.i0)
        return;

    const int n = static_cast<int>(s.bucketCount);
    const int i0 = clampBucketIndex_(idx.i0, n);
    const int i1 = clampBucketIndex_(idx.i1, n);

    for (int i = i0; i <= i1; ++i) {
        auto& b = s.buckets[static_cast<size_t>(i)];
        switch (idx.type) {
            case TimelineMinimapClipType::Audio:
                b.audioCount -= 1;
                break;
            case TimelineMinimapClipType::Midi:
                b.midiCount -= 1;
                break;
            case TimelineMinimapClipType::Automation:
                b.automationCount -= 1;
                break;
        }

        // Energy/peak contribution removal is not yet tracked per-clip.
        // We keep this additive-only until Energy mode is implemented with per-clip proxies.
    }
}

} // namespace NomadUI

