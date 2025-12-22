// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace NomadUI {

using TimelineMinimapClipId = uint64_t;

enum class TimelineMinimapClipType
{
    Audio = 0,
    Midi = 1,
    Automation = 2,
};

struct TimelineMinimapClipSpan
{
    TimelineMinimapClipId id = 0;
    TimelineMinimapClipType type = TimelineMinimapClipType::Audio;
    double startBeat = 0.0;
    double endBeat = 0.0; // exclusive

    float energyApprox = 0.0f;
    float peakApprox = 0.0f;
};

struct TimelineMinimapClipDelta
{
    bool hasBefore = false;
    bool hasAfter = false;
    TimelineMinimapClipSpan before;
    TimelineMinimapClipSpan after;
};

struct TimelineSummaryBucket
{
    // Signed counts to make incremental +/- safe even under odd event ordering.
    int32_t audioCount = 0;
    int32_t midiCount = 0;
    int32_t automationCount = 0;

    float energySum = 0.0f;
    float peakSum = 0.0f;
};

struct TimelineSummary
{
    double domainStartBeat = 0.0;
    double domainEndBeat = 0.0;
    uint32_t bucketCount = 0;

    std::vector<TimelineSummaryBucket> buckets;

    uint32_t maxAudio = 0;
    uint32_t maxMidi = 0;
    uint32_t maxAutomation = 0;
    float maxEnergySum = 0.0f;
    float maxPeakSum = 0.0f;

    uint64_t version = 0;
};

struct TimelineSummarySnapshot
{
    const TimelineSummary* summary = nullptr;
    uint64_t version = 0;
};

class TimelineSummaryCache final
{
public:
    static constexpr uint32_t kDefaultBucketCount = 2048;

    TimelineSummaryCache();
    ~TimelineSummaryCache();

    TimelineSummaryCache(const TimelineSummaryCache&) = delete;
    TimelineSummaryCache& operator=(const TimelineSummaryCache&) = delete;

    void requestRebuild(std::vector<TimelineMinimapClipSpan> spans, double domainStartBeat, double domainEndBeat,
                        uint32_t bucketCount = kDefaultBucketCount);
    void requestApplyDeltas(std::vector<TimelineMinimapClipDelta> deltas, double expectedDomainStartBeat,
                            double expectedDomainEndBeat);

    TimelineSummarySnapshot getSnapshot() const noexcept;

private:
    struct ClipIndex
    {
        int i0 = 0;
        int i1 = 0;
        TimelineMinimapClipType type = TimelineMinimapClipType::Audio;
    };

    enum class TaskKind
    {
        Rebuild,
        ApplyDeltas,
    };

    struct Task
    {
        TaskKind kind = TaskKind::ApplyDeltas;
        double domainStartBeat = 0.0;
        double domainEndBeat = 0.0;
        uint32_t bucketCount = kDefaultBucketCount;
        std::vector<TimelineMinimapClipSpan> spans;
        std::vector<TimelineMinimapClipDelta> deltas;
    };

    void ensureWorker_();
    void stopWorker_();
    void workerLoop_();

    void rebuild_(TimelineSummary& dst, const std::vector<TimelineMinimapClipSpan>& spans, double domainStartBeat,
                 double domainEndBeat, uint32_t bucketCount);
    void applyDeltas_(TimelineSummary& dst, const TimelineSummary& src, const std::vector<TimelineMinimapClipDelta>& deltas);
    void recomputeMaxima_(TimelineSummary& s);

    static int clampBucketIndex_(int idx, int n);
    static bool computeBucketRange_(int& outI0, int& outI1, double startBeat, double endBeat, double domainStartBeat,
                                   double domainEndBeat, int n);

    static void addSpan_(TimelineSummary& s, const TimelineMinimapClipSpan& span, ClipIndex& outIndex);
    static void removeSpan_(TimelineSummary& s, const ClipIndex& idx);

    // Double-buffered summaries. Front buffer is immutable for the renderer.
    TimelineSummary buffers_[2];
    std::atomic<uint32_t> frontIndex_{0};

    // Worker thread state
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::atomic<bool> stop_{false};
    bool workerStarted_ = false;

    // Worker-owned clip bucket-range index (O(1) removal).
    std::unordered_map<TimelineMinimapClipId, ClipIndex> clipIndex_;
};

} // namespace NomadUI

