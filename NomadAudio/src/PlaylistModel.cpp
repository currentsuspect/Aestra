// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PlaylistModel.h"
#include "PlaylistRuntimeSnapshot.h"
#include "NomadLog.h"
#include <algorithm>
#include <mutex>

namespace Nomad {
namespace Audio {

// =============================================================================
// PlaylistModel Implementation
// =============================================================================

struct PlaylistModel::Impl {
    std::vector<PlaylistLane> lanes;
    std::vector<ChangeCallback> observers;
    
    double projectSampleRate = DEFAULT_SAMPLE_RATE;
    double bpm = DEFAULT_BPM;
    GridSubdivision gridSubdivision = GridSubdivision::Beat;
    bool snapEnabled = true;
    
    std::atomic<uint64_t> modificationCounter{0};
    mutable std::mutex mutex;
    uint32_t nextLaneNumber = 1;
};

PlaylistModel::PlaylistModel()
    : m_impl(std::make_unique<Impl>())
{
    Log::info("PlaylistModel created");
}

PlaylistModel::~PlaylistModel() {
    Log::info("PlaylistModel destroyed");
}

// === Project Settings ===

void PlaylistModel::setProjectSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (sampleRate > 0) {
        m_impl->projectSampleRate = sampleRate;
        notifyChange();
    }
}

double PlaylistModel::getProjectSampleRate() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->projectSampleRate;
}

void PlaylistModel::setBPM(double bpm) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (bpm > 0) {
        m_impl->bpm = bpm;
        notifyChange();
    }
}

double PlaylistModel::getBPM() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->bpm;
}

void PlaylistModel::setGridSubdivision(GridSubdivision grid) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->gridSubdivision = grid;
    notifyChange();
}

GridSubdivision PlaylistModel::getGridSubdivision() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->gridSubdivision;
}

void PlaylistModel::setSnapEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->snapEnabled = enabled;
}

bool PlaylistModel::isSnapEnabled() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->snapEnabled;
}

// === Lane Management ===

PlaylistLaneID PlaylistModel::createLane(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::string laneName = name;
    if (laneName.empty()) {
        laneName = "Track " + std::to_string(m_impl->nextLaneNumber++);
    }
    
    PlaylistLane lane(laneName);
    PlaylistLaneID id = lane.id;
    m_impl->lanes.push_back(std::move(lane));
    
    Log::info("PlaylistModel: Created lane '" + laneName + "' (" + id.toString() + ")");
    notifyChange();
    
    return id;
}

bool PlaylistModel::deleteLane(PlaylistLaneID laneId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int idx = findLaneIndex(laneId);
    if (idx < 0) return false;
    
    std::string name = m_impl->lanes[idx].name;
    m_impl->lanes.erase(m_impl->lanes.begin() + idx);
    
    Log::info("PlaylistModel: Deleted lane '" + name + "'");
    notifyChange();
    
    return true;
}

PlaylistLane* PlaylistModel::getLane(PlaylistLaneID laneId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    int idx = findLaneIndex(laneId);
    return idx >= 0 ? &m_impl->lanes[idx] : nullptr;
}

const PlaylistLane* PlaylistModel::getLane(PlaylistLaneID laneId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    int idx = findLaneIndex(laneId);
    return idx >= 0 ? &m_impl->lanes[idx] : nullptr;
}

PlaylistLane* PlaylistModel::getLaneByIndex(size_t index) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return index < m_impl->lanes.size() ? &m_impl->lanes[index] : nullptr;
}

const PlaylistLane* PlaylistModel::getLaneByIndex(size_t index) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return index < m_impl->lanes.size() ? &m_impl->lanes[index] : nullptr;
}

size_t PlaylistModel::getLaneCount() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->lanes.size();
}

std::vector<PlaylistLaneID> PlaylistModel::getLaneIDs() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<PlaylistLaneID> ids;
    ids.reserve(m_impl->lanes.size());
    for (const auto& lane : m_impl->lanes) {
        ids.push_back(lane.id);
    }
    return ids;
}

bool PlaylistModel::moveLane(PlaylistLaneID laneId, size_t newIndex) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int oldIdx = findLaneIndex(laneId);
    if (oldIdx < 0) return false;
    
    if (newIndex >= m_impl->lanes.size()) {
        newIndex = m_impl->lanes.size() - 1;
    }
    
    if (static_cast<size_t>(oldIdx) == newIndex) return true;
    
    PlaylistLane lane = std::move(m_impl->lanes[oldIdx]);
    m_impl->lanes.erase(m_impl->lanes.begin() + oldIdx);
    m_impl->lanes.insert(m_impl->lanes.begin() + newIndex, std::move(lane));
    
    notifyChange();
    return true;
}

// === Clip Operations ===

PlaylistClipID PlaylistModel::addClip(PlaylistLaneID laneId, const PlaylistClip& clip) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int laneIdx = findLaneIndex(laneId);
    if (laneIdx < 0) return PlaylistClipID();
    
    PlaylistClip newClip = clip;
    if (!newClip.id.isValid()) {
        newClip.id = PlaylistClipID::generate();
    }
    
    m_impl->lanes[laneIdx].clips.push_back(newClip);
    m_impl->lanes[laneIdx].sortClips();
    
    Log::info("PlaylistModel: Added clip '" + newClip.name + "' to lane '" + 
              m_impl->lanes[laneIdx].name + "' at " + 
              std::to_string(samplesToSeconds(newClip.startTime, m_impl->projectSampleRate)) + "s");
    notifyChange();
    
    return newClip.id;
}

PlaylistClipID PlaylistModel::addClipFromSource(PlaylistLaneID laneId, ClipSourceID sourceId,
                                                  SampleIndex startTime, SampleIndex length,
                                                  SampleIndex sourceStart) {
    PlaylistClip clip(sourceId);
    clip.startTime = startTime;
    clip.length = length;
    clip.sourceStart = sourceStart;
    
    return addClip(laneId, clip);
}

bool PlaylistModel::removeClip(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    
    std::string clipName = m_impl->lanes[laneIdx].clips[clipIdx].name;
    m_impl->lanes[laneIdx].clips.erase(m_impl->lanes[laneIdx].clips.begin() + clipIdx);
    
    Log::info("PlaylistModel: Removed clip '" + clipName + "'");
    notifyChange();
    
    return true;
}

PlaylistClip* PlaylistModel::getClip(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return nullptr;
    return &m_impl->lanes[laneIdx].clips[clipIdx];
}

const PlaylistClip* PlaylistModel::getClip(PlaylistClipID clipId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return nullptr;
    return &m_impl->lanes[laneIdx].clips[clipIdx];
}

PlaylistLaneID PlaylistModel::findClipLane(PlaylistClipID clipId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0) return PlaylistLaneID();
    return m_impl->lanes[laneIdx].id;
}

bool PlaylistModel::moveClip(PlaylistClipID clipId, PlaylistLaneID targetLaneId, SampleIndex newStartTime) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [srcLaneIdx, clipIdx] = findClipLocation(clipId);
    if (srcLaneIdx < 0 || clipIdx < 0) return false;
    
    int dstLaneIdx = findLaneIndex(targetLaneId);
    if (dstLaneIdx < 0) return false;
    
    // Apply snapping
    if (m_impl->snapEnabled) {
        newStartTime = Audio::snapToGrid(newStartTime, m_impl->gridSubdivision, 
                                          m_impl->bpm, m_impl->projectSampleRate);
    }
    
    if (srcLaneIdx == dstLaneIdx) {
        // Same lane - just update position
        m_impl->lanes[srcLaneIdx].clips[clipIdx].startTime = newStartTime;
        m_impl->lanes[srcLaneIdx].sortClips();
    } else {
        // Different lane - move clip
        PlaylistClip clip = std::move(m_impl->lanes[srcLaneIdx].clips[clipIdx]);
        clip.startTime = newStartTime;
        m_impl->lanes[srcLaneIdx].clips.erase(m_impl->lanes[srcLaneIdx].clips.begin() + clipIdx);
        m_impl->lanes[dstLaneIdx].clips.push_back(std::move(clip));
        m_impl->lanes[dstLaneIdx].sortClips();
    }
    
    notifyChange();
    return true;
}

bool PlaylistModel::moveClip(PlaylistClipID clipId, SampleIndex newStartTime) {
    PlaylistLaneID laneId = findClipLane(clipId);
    if (!laneId.isValid()) return false;
    return moveClip(clipId, laneId, newStartTime);
}

// === Trim Operations ===

bool PlaylistModel::trimClipStart(PlaylistClipID clipId, SampleIndex deltaSamples) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    
    PlaylistClip& clip = m_impl->lanes[laneIdx].clips[clipIdx];
    
    // Clamp delta to valid range
    // Can't trim beyond start of source (sourceStart can't go negative)
    if (deltaSamples < 0 && -deltaSamples > static_cast<SampleIndex>(clip.sourceStart)) {
        deltaSamples = -static_cast<SampleIndex>(clip.sourceStart);
    }
    // Can't trim beyond end of clip
    if (deltaSamples > 0 && deltaSamples >= clip.length) {
        deltaSamples = clip.length - 1;
    }
    
    clip.startTime += deltaSamples;
    clip.sourceStart += deltaSamples;
    clip.length -= deltaSamples;
    
    m_impl->lanes[laneIdx].sortClips();
    notifyChange();
    
    return true;
}

bool PlaylistModel::trimClipEnd(PlaylistClipID clipId, SampleIndex deltaSamples) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    
    PlaylistClip& clip = m_impl->lanes[laneIdx].clips[clipIdx];
    
    // Clamp delta - can't make length <= 0
    SampleIndex newLength = clip.length - deltaSamples;
    if (newLength < 1) {
        newLength = 1;
    }
    
    clip.length = newLength;
    notifyChange();
    
    return true;
}

bool PlaylistModel::setClipLength(PlaylistClipID clipId, SampleIndex newLength) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    
    if (newLength < 1) newLength = 1;
    
    m_impl->lanes[laneIdx].clips[clipIdx].length = newLength;
    notifyChange();
    
    return true;
}

// === Split & Duplicate ===

PlaylistClipID PlaylistModel::splitClip(PlaylistClipID clipId, SampleIndex splitTime) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return PlaylistClipID();
    
    PlaylistClip& clip = m_impl->lanes[laneIdx].clips[clipIdx];
    
    // Validate split time
    if (splitTime <= clip.startTime || splitTime >= clip.getEndTime()) {
        Log::warning("PlaylistModel: Invalid split time " + 
                     std::to_string(splitTime) + " for clip");
        return PlaylistClipID();
    }
    
    // Calculate split positions
    SampleIndex splitOffset = splitTime - clip.startTime;
    
    // Create second clip (right half)
    PlaylistClip newClip = clip;
    newClip.id = PlaylistClipID::generate();
    newClip.startTime = splitTime;
    newClip.length = clip.length - splitOffset;
    newClip.sourceStart = clip.sourceStart + splitOffset;
    
    // Truncate original clip (left half)
    clip.length = splitOffset;
    
    // Add new clip
    m_impl->lanes[laneIdx].clips.push_back(newClip);
    m_impl->lanes[laneIdx].sortClips();
    
    Log::info("PlaylistModel: Split clip at " + 
              std::to_string(samplesToSeconds(splitTime, m_impl->projectSampleRate)) + "s");
    notifyChange();
    
    return newClip.id;
}

PlaylistClipID PlaylistModel::duplicateClip(PlaylistClipID clipId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return PlaylistClipID();
    
    PlaylistClip newClip = m_impl->lanes[laneIdx].clips[clipIdx];
    newClip.id = PlaylistClipID::generate();
    newClip.startTime = newClip.getEndTime(); // Place after original
    
    m_impl->lanes[laneIdx].clips.push_back(newClip);
    m_impl->lanes[laneIdx].sortClips();
    
    Log::info("PlaylistModel: Duplicated clip '" + newClip.name + "'");
    notifyChange();
    
    return newClip.id;
}

// === Queries ===

std::vector<const PlaylistClip*> PlaylistModel::getClipsInRange(PlaylistLaneID laneId,
                                                                   SampleIndex rangeStart,
                                                                   SampleIndex rangeEnd) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    int idx = findLaneIndex(laneId);
    if (idx < 0) return {};
    
    return m_impl->lanes[idx].getClipsInRange(rangeStart, rangeEnd);
}

std::vector<std::pair<PlaylistLaneID, const PlaylistClip*>> 
    PlaylistModel::getAllClipsInRange(SampleIndex rangeStart, SampleIndex rangeEnd) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<std::pair<PlaylistLaneID, const PlaylistClip*>> result;
    
    for (const auto& lane : m_impl->lanes) {
        auto clips = lane.getClipsInRange(rangeStart, rangeEnd);
        for (const auto* clip : clips) {
            result.emplace_back(lane.id, clip);
        }
    }
    
    return result;
}

SampleIndex PlaylistModel::getTotalDuration() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    SampleIndex maxEnd = 0;
    for (const auto& lane : m_impl->lanes) {
        maxEnd = std::max(maxEnd, lane.getTotalDuration());
    }
    return maxEnd;
}

// === Snapping ===

SampleIndex PlaylistModel::snapToGrid(SampleIndex position) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (!m_impl->snapEnabled) return position;
    
    return Audio::snapToGrid(position, m_impl->gridSubdivision, 
                              m_impl->bpm, m_impl->projectSampleRate);
}

// === Observers ===

void PlaylistModel::addChangeObserver(ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->observers.push_back(std::move(callback));
}

void PlaylistModel::clearChangeObservers() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->observers.clear();
}

// === Snapshot ===

std::unique_ptr<PlaylistRuntimeSnapshot> PlaylistModel::buildRuntimeSnapshot(
    const SourceManager& sourceManager) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto snapshot = std::make_unique<PlaylistRuntimeSnapshot>();
    snapshot->projectSampleRate = m_impl->projectSampleRate;
    snapshot->modificationId = m_impl->modificationCounter.load();
    
    snapshot->lanes.reserve(m_impl->lanes.size());
    
    for (const auto& lane : m_impl->lanes) {
        LaneRuntimeInfo laneInfo;
        laneInfo.volume = lane.volume;
        laneInfo.pan = lane.pan;
        laneInfo.muted = lane.muted;
        laneInfo.solo = lane.solo;
        
        laneInfo.clips.reserve(lane.clips.size());
        
        for (const auto& clip : lane.clips) {
            ClipRuntimeInfo clipInfo;
            clipInfo.startTime = clip.startTime;
            clipInfo.length = clip.length;
            clipInfo.sourceStart = clip.sourceStart;
            clipInfo.gainLinear = clip.gainLinear;
            clipInfo.pan = clip.pan;
            clipInfo.muted = clip.muted;
            clipInfo.playbackRate = clip.playbackRate;
            clipInfo.fadeInLength = clip.fadeInLength;
            clipInfo.fadeOutLength = clip.fadeOutLength;
            clipInfo.flags = clip.flags;
            
            // Resolve source to raw buffer pointer
            const ClipSource* source = sourceManager.getSource(clip.sourceId);
            if (source && source->isReady()) {
                clipInfo.audioData = source->getRawBuffer();
                clipInfo.sourceSampleRate = source->getSampleRate();
                clipInfo.sourceChannels = source->getNumChannels();
            } else {
                clipInfo.audioData = nullptr;
                clipInfo.sourceSampleRate = 0;
                clipInfo.sourceChannels = 0;
            }
            
            laneInfo.clips.push_back(clipInfo);
        }
        
        snapshot->lanes.push_back(std::move(laneInfo));
    }
    
    return snapshot;
}

// === Clear ===

void PlaylistModel::clear() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    m_impl->lanes.clear();
    m_impl->nextLaneNumber = 1;
    
    Log::info("PlaylistModel: Cleared all data");
    notifyChange();
}

uint64_t PlaylistModel::getModificationCounter() const {
    return m_impl->modificationCounter.load();
}

// === Internal Helpers ===

void PlaylistModel::notifyChange() {
    m_impl->modificationCounter.fetch_add(1);
    for (const auto& callback : m_impl->observers) {
        if (callback) callback();
    }
}

int PlaylistModel::findLaneIndex(PlaylistLaneID laneId) const {
    for (size_t i = 0; i < m_impl->lanes.size(); ++i) {
        if (m_impl->lanes[i].id == laneId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::pair<int, int> PlaylistModel::findClipLocation(PlaylistClipID clipId) const {
    for (size_t i = 0; i < m_impl->lanes.size(); ++i) {
        int clipIdx = m_impl->lanes[i].findClipIndex(clipId);
        if (clipIdx >= 0) {
            return {static_cast<int>(i), clipIdx};
        }
    }
    return {-1, -1};
}

} // namespace Audio
} // namespace Nomad
