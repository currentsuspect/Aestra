#include "PlaylistModel.h"
#include "PlaylistRuntimeSnapshot.h"
#include "PatternManager.h"
#include "NomadLog.h"
#include <unordered_map>
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
    
    double projectSampleRate = 48000.0;
    double bpm = 120.0;
    
    std::atomic<uint64_t> modificationCounter{0};
    mutable std::recursive_mutex mutex;
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
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    if (sampleRate > 0) {
        m_impl->projectSampleRate = sampleRate;
        notifyChange();
    }
}

double PlaylistModel::getProjectSampleRate() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    return m_impl->projectSampleRate;
}

void PlaylistModel::setBPM(double bpm) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    if (bpm > 0) {
        m_impl->bpm = bpm;
        notifyChange();
    }
}

double PlaylistModel::getBPM() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    return m_impl->bpm;
}

double PlaylistModel::getBPMAtBeat(double beat) const {
    // v3.0 placeholder for tempo automation
    return getBPM();
}

double PlaylistModel::beatToSeconds(double beat) const {
    double bpm = getBPMAtBeat(beat);
    if (bpm <= 0) return 0;
    return beat * (60.0 / bpm);
}

double PlaylistModel::secondsToBeats(double seconds) const {
    double bpm = getBPMAtBeat(0); // v3.0 simple case
    if (bpm <= 0) return 0;
    return seconds * (bpm / 60.0);
}

// === Lane Management ===

PlaylistLaneID PlaylistModel::createLane(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    
    std::string laneName = name;
    if (laneName.empty()) {
        laneName = "Lane " + std::to_string(m_impl->nextLaneNumber++);
    }
    
    PlaylistLane lane(laneName);
    PlaylistLaneID id = lane.id;
    m_impl->lanes.push_back(std::move(lane));
    
    Log::info("PlaylistModel: Created lane '" + laneName + "' (" + id.toString() + ")");
    notifyChange();
    
    return id;
}

bool PlaylistModel::deleteLane(PlaylistLaneID laneId) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    
    int idx = findLaneIndex(laneId);
    if (idx < 0) return false;
    
    m_impl->lanes.erase(m_impl->lanes.begin() + idx);
    notifyChange();
    return true;
}

PlaylistLane* PlaylistModel::getLane(PlaylistLaneID laneId) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    int idx = findLaneIndex(laneId);
    return idx >= 0 ? &m_impl->lanes[idx] : nullptr;
}

const PlaylistLane* PlaylistModel::getLane(PlaylistLaneID laneId) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    int idx = findLaneIndex(laneId);
    return idx >= 0 ? &m_impl->lanes[idx] : nullptr;
}

size_t PlaylistModel::getLaneCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    return m_impl->lanes.size();
}

std::vector<PlaylistLaneID> PlaylistModel::getLaneIDs() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    std::vector<PlaylistLaneID> ids;
    ids.reserve(m_impl->lanes.size());
    for (const auto& lane : m_impl->lanes) {
        ids.push_back(lane.id);
    }
    return ids;
}

PlaylistLaneID PlaylistModel::getLaneId(size_t index) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    if (index >= m_impl->lanes.size()) return PlaylistLaneID();
    return m_impl->lanes[index].id;
}

bool PlaylistModel::moveLane(PlaylistLaneID laneId, size_t newIndex) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    int oldIdx = findLaneIndex(laneId);
    if (oldIdx < 0) return false;
    if (newIndex >= m_impl->lanes.size()) newIndex = m_impl->lanes.size() - 1;
    if (static_cast<size_t>(oldIdx) == newIndex) return true;
    
    PlaylistLane lane = std::move(m_impl->lanes[oldIdx]);
    m_impl->lanes.erase(m_impl->lanes.begin() + oldIdx);
    m_impl->lanes.insert(m_impl->lanes.begin() + newIndex, std::move(lane));
    notifyChange();
    return true;
}

// === Clip Operations ===

ClipInstanceID PlaylistModel::addClip(PlaylistLaneID laneId, const ClipInstance& clip) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    int laneIdx = findLaneIndex(laneId);
    if (laneIdx < 0) return ClipInstanceID();
    
    ClipInstance newClip = clip;
    if (!newClip.id.isValid()) newClip.id = ClipInstanceID::generate();
    
    m_impl->lanes[laneIdx].clips.push_back(newClip);
    m_impl->lanes[laneIdx].sortClips();
    
    notifyChange();
    return newClip.id;
}

ClipInstanceID PlaylistModel::addClipFromPattern(PlaylistLaneID laneId, PatternID patternId,
                                                   double startBeat, double durationBeats) {
    ClipInstance clip;
    clip.patternId = patternId.value;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    return addClip(laneId, clip);
}

bool PlaylistModel::removeClip(ClipInstanceID clipId) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    
    m_impl->lanes[laneIdx].clips.erase(m_impl->lanes[laneIdx].clips.begin() + clipIdx);
    notifyChange();
    return true;
}

ClipInstance* PlaylistModel::getClip(ClipInstanceID clipId) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    return (laneIdx >= 0 && clipIdx >= 0) ? &m_impl->lanes[laneIdx].clips[clipIdx] : nullptr;
}

const ClipInstance* PlaylistModel::getClip(ClipInstanceID clipId) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    return (laneIdx >= 0 && clipIdx >= 0) ? &m_impl->lanes[laneIdx].clips[clipIdx] : nullptr;
}

PlaylistLaneID PlaylistModel::findClipLane(ClipInstanceID clipId) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    return laneIdx >= 0 ? m_impl->lanes[laneIdx].id : PlaylistLaneID();
}

bool PlaylistModel::moveClip(ClipInstanceID clipId, PlaylistLaneID targetLaneId, double newStartBeat) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [srcLaneIdx, clipIdx] = findClipLocation(clipId);
    if (srcLaneIdx < 0 || clipIdx < 0) return false;
    int dstLaneIdx = findLaneIndex(targetLaneId);
    if (dstLaneIdx < 0) return false;
    
    if (srcLaneIdx == dstLaneIdx) {
        m_impl->lanes[srcLaneIdx].clips[clipIdx].startBeat = newStartBeat;
        m_impl->lanes[srcLaneIdx].sortClips();
    } else {
        ClipInstance clip = std::move(m_impl->lanes[srcLaneIdx].clips[clipIdx]);
        clip.startBeat = newStartBeat;
        m_impl->lanes[srcLaneIdx].clips.erase(m_impl->lanes[srcLaneIdx].clips.begin() + clipIdx);
        m_impl->lanes[dstLaneIdx].clips.push_back(std::move(clip));
        m_impl->lanes[dstLaneIdx].sortClips();
    }
    notifyChange();
    return true;
}

bool PlaylistModel::setClipDuration(ClipInstanceID clipId, double newDurationBeats) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return false;
    m_impl->lanes[laneIdx].clips[clipIdx].durationBeats = newDurationBeats;
    notifyChange();
    return true;
}

ClipInstanceID PlaylistModel::splitClip(ClipInstanceID clipId, double splitBeat) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return ClipInstanceID();
    
    ClipInstance& clip = m_impl->lanes[laneIdx].clips[clipIdx];
    if (splitBeat <= clip.startBeat || splitBeat >= clip.startBeat + clip.durationBeats) return ClipInstanceID();
    
    double firstPartDur = splitBeat - clip.startBeat;
    ClipInstance nextPart = clip;
    nextPart.id = ClipInstanceID::generate();
    nextPart.startBeat = splitBeat;
    nextPart.durationBeats = clip.durationBeats - firstPartDur;
    
    clip.durationBeats = firstPartDur;
    m_impl->lanes[laneIdx].clips.push_back(nextPart);
    m_impl->lanes[laneIdx].sortClips();
    notifyChange();
    return nextPart.id;
}

ClipInstanceID PlaylistModel::duplicateClip(ClipInstanceID clipId) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    auto [laneIdx, clipIdx] = findClipLocation(clipId);
    if (laneIdx < 0 || clipIdx < 0) return ClipInstanceID();
    
    ClipInstance newClip = m_impl->lanes[laneIdx].clips[clipIdx];
    newClip.id = ClipInstanceID::generate();
    newClip.startBeat += newClip.durationBeats;
    m_impl->lanes[laneIdx].clips.push_back(newClip);
    m_impl->lanes[laneIdx].sortClips();
    notifyChange();
    return newClip.id;
}

std::vector<const ClipInstance*> PlaylistModel::getClipsInRange(PlaylistLaneID laneId,
                                                                 double startBeat,
                                                                 double endBeat) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    int idx = findLaneIndex(laneId);
    if (idx < 0) return {};
    
    std::vector<const ClipInstance*> result;
    for (const auto& clip : m_impl->lanes[idx].clips) {
        if (clip.startBeat < endBeat && clip.startBeat + clip.durationBeats > startBeat) {
            result.push_back(&clip);
        }
    }
    return result;
}

double PlaylistModel::getTotalDurationBeats() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    double maxEnd = 0.0;
    for (const auto& lane : m_impl->lanes) {
        for (const auto& clip : lane.clips) {
            maxEnd = std::max(maxEnd, clip.startBeat + clip.durationBeats);
        }
    }
    return maxEnd;
}

void PlaylistModel::addChangeObserver(ChangeCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    m_impl->observers.push_back(std::move(callback));
}

void PlaylistModel::clearChangeObservers() {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    m_impl->observers.clear();
}

std::unique_ptr<PlaylistRuntimeSnapshot> PlaylistModel::buildRuntimeSnapshot(
    const PatternManager& patternManager,
    const SourceManager& sourceManager) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    
    auto snapshot = std::make_unique<PlaylistRuntimeSnapshot>();
    snapshot->projectSampleRate = m_impl->projectSampleRate;
    snapshot->bpm = m_impl->bpm;
    snapshot->modificationId = m_impl->modificationCounter.load();
    
    snapshot->lanes.reserve(m_impl->lanes.size());
    
    // Get a thread-safe snapshot of all patterns
    auto allPatterns = patternManager.getAllPatterns();
    std::unordered_map<uint64_t, std::shared_ptr<PatternSource>> patternSnapshot;
    for (const auto& p : allPatterns) {
        patternSnapshot[p->id.value] = p;
    }
    
    for (const auto& lane : m_impl->lanes) {
        LaneRuntimeInfo laneInfo;
        laneInfo.volume = lane.volume;
        laneInfo.pan = lane.pan;
        laneInfo.muted = lane.muted;
        laneInfo.solo = lane.solo;
        laneInfo.automationCurves = lane.automationCurves;
        
        laneInfo.clips.reserve(lane.clips.size());
        
        for (const auto& clip : lane.clips) {
            // 1. Resolve Pattern
            if (patternSnapshot.find(clip.patternId.value) == patternSnapshot.end()) continue;
            auto patternPtr = patternSnapshot[clip.patternId.value];
            if (!patternPtr) continue;
            
            const PatternSource& pattern = *patternPtr;

            
            ClipRuntimeInfo clipInfo;
            clipInfo.patternId = clip.patternId.value;
            clipInfo.patternVersion = pattern.version;
            
            // 2. Convert Time (Beats -> Samples)
            clipInfo.startTime = beatsToSamples(clip.startBeat, m_impl->bpm, m_impl->projectSampleRate);
            clipInfo.length = beatsToSamples(clip.durationBeats, m_impl->bpm, m_impl->projectSampleRate);
            
            // 3. Populate Local Edits
            clipInfo.gainLinear = clip.edits.gainLinear;
            clipInfo.pan = clip.edits.pan;
            clipInfo.muted = clip.muted || clip.edits.muted; // Clip muting is OR of instance and override
            clipInfo.playbackRate = clip.edits.playbackRate;
            clipInfo.fadeInLength = beatsToSamples(clip.edits.fadeInBeats, m_impl->bpm, m_impl->projectSampleRate);
            clipInfo.fadeOutLength = beatsToSamples(clip.edits.fadeOutBeats, m_impl->bpm, m_impl->projectSampleRate);
            clipInfo.sourceStart = clip.edits.sourceStart;

            
            // 4. Resolve Payload
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, MidiPayload>) {
                    clipInfo.midiData = &arg.notes; // Pass pointer to notes vector
                    clipInfo.midiNoteCount = static_cast<uint32_t>(arg.notes.size());
                } else if constexpr (std::is_same_v<T, AudioSlicePayload>) {
                    const ClipSource* source = sourceManager.getSource(arg.audioSourceId);
                    if (source && source->isReady()) {
                        clipInfo.audioData = source->getRawBuffer(); 
                        clipInfo.sourceSampleRate = source->getSampleRate();
                        clipInfo.sourceChannels = source->getNumChannels();
                        clipInfo.sourceStart = clip.edits.sourceStart;
                    }
                }
            }, pattern.payload);

            
            laneInfo.clips.push_back(std::move(clipInfo));
        }
        
        snapshot->lanes.push_back(std::move(laneInfo));
    }
    
    return snapshot;
}

void PlaylistModel::clear() {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
    m_impl->lanes.clear();
    m_impl->nextLaneNumber = 1;
    notifyChange();
}

uint64_t PlaylistModel::getModificationCounter() const {
    return m_impl->modificationCounter.load();
}

void PlaylistModel::notifyChange() {
    m_impl->modificationCounter.fetch_add(1);
    for (const auto& callback : m_impl->observers) {
        if (callback) callback();
    }
}

int PlaylistModel::findLaneIndex(PlaylistLaneID laneId) const {
    for (size_t i = 0; i < m_impl->lanes.size(); ++i) {
        if (m_impl->lanes[i].id == laneId) return static_cast<int>(i);
    }
    return -1;
}

std::pair<int, int> PlaylistModel::findClipLocation(ClipInstanceID clipId) const {
    for (size_t i = 0; i < m_impl->lanes.size(); ++i) {
        int clipIdx = m_impl->lanes[i].findClipIndex(clipId);
        if (clipIdx >= 0) return {static_cast<int>(i), clipIdx};
    }
    return {-1, -1};
}

} // namespace Audio
} // namespace Nomad
