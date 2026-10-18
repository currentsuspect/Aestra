// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {

/**
 * @brief Bring over (SPEC-017, V81-V1): take chosen tracks and patterns from another version into the open one.
 * Nothing merges by itself. The plan lists what the other version has and how it compares by name; the command
 * adds exactly what was chosen and goes back out in one undo step.
 *
 * Audio and pattern clips travel with the audio they play. A track whose patterns play instruments the open
 * version does not have is listed as blocked rather than imported half-working. Imported tracks arrive routed to
 * Master: mixer channels are not part of what is brought over.
 */
namespace BringOver {

using namespace Audio;

enum class Kind { Track, Pattern };
enum class Status { New, Changed, Same, Blocked };

struct Item {
    Kind kind = Kind::Track;
    uint64_t id = 0;        ///< the track id, or the pattern id, in the other project
    std::string name;
    Status status = Status::New;
    std::string detail;     ///< "new in Alt hook", "same as current", "2 clips", ...
};
struct Plan {
    std::vector<Item> tracks;
    std::vector<Item> patterns;
};

namespace detail {

inline void mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); }
inline uint64_t q(double v) { return static_cast<uint64_t>(std::llround(v * 1000000.0)); }
inline uint64_t strHash(const std::string& s) { return std::hash<std::string>{}(s); }

/// A pattern's content as one number: same notes or same audio slices in the same order give the same value.
inline uint64_t patternSignature(const PatternSource& p, const SourceManager& sources) {
    uint64_t h = 1469598103934665603ULL;
    mix(h, q(p.lengthBeats));
    if (p.isMidi()) {
        for (const auto& n : p.getMidiNotes()) {
            mix(h, static_cast<uint64_t>(n.pitch)); mix(h, q(n.startBeat)); mix(h, q(n.durationBeats)); mix(h, q(n.velocity));
        }
    } else if (p.isAudio()) {
        const auto& a = std::get<AudioSlicePayload>(p.payload);
        if (const auto* source = sources.getSource(a.audioSourceId)) mix(h, strHash(source->getFilePath()));
        mix(h, q(a.startOffsetSeconds)); mix(h, q(a.durationSeconds));
        for (const auto& s : a.slices) { mix(h, q(s.startSamples)); mix(h, q(s.lengthSamples)); }
    }
    return h;
}

/// True when every instrument the pattern's notes play also exists in @p current.
inline bool instrumentsExist(const PatternSource& p, const TrackManager& current) {
    if (!p.isMidi()) return true;
    for (const auto& n : p.getMidiNotes())
        if (n.unitId != 0 && current.getUnitManager().getUnit(n.unitId) == nullptr) return false;
    return true;
}

} // namespace detail

/// What @p other has that @p current could take, compared by name.
inline Plan plan(TrackManager& other, TrackManager& current, const std::string& otherName) {
    Plan out;
    auto& op = other.getPatternManager();
    auto& cp = current.getPatternManager();
    std::unordered_map<std::string, uint64_t> currentPatterns;
    cp.forEachPattern([&](const PatternSource& p) { currentPatterns[p.name] = detail::patternSignature(p, current.getSourceManager()); });

    std::unordered_map<std::string, uint64_t> currentTracks;
    for (Track* t : current.getTracks()) {
        uint64_t h = 7;
        for (const auto& laneId : t->laneIds) {
            const auto* lane = current.getPlaylistModel().getLane(laneId);
            if (!lane) continue;
            for (const auto& c : lane->clips) {
                detail::mix(h, detail::q(c.startBeat)); detail::mix(h, detail::q(c.durationBeats));
                if (const auto* p = cp.getPattern(c.patternId)) detail::mix(h, detail::patternSignature(*p, current.getSourceManager()));
            }
        }
        currentTracks[t->name] = h;
    }

    for (Track* t : other.getTracks()) {
        Item item;
        item.kind = Kind::Track;
        item.id = t->trackId;
        item.name = t->name;
        uint64_t h = 7;
        size_t clips = 0;
        bool blocked = false;
        for (const auto& laneId : t->laneIds) {
            const auto* lane = other.getPlaylistModel().getLane(laneId);
            if (!lane) continue;
            for (const auto& c : lane->clips) {
                ++clips;
                detail::mix(h, detail::q(c.startBeat)); detail::mix(h, detail::q(c.durationBeats));
                if (const auto* p = op.getPattern(c.patternId)) {
                    detail::mix(h, detail::patternSignature(*p, other.getSourceManager()));
                    blocked = blocked || !detail::instrumentsExist(*p, current);
                }
            }
        }
        const auto it = currentTracks.find(t->name);
        if (blocked) { item.status = Status::Blocked; item.detail = "needs an instrument this version does not have"; }
        else if (it == currentTracks.end()) { item.status = Status::New; item.detail = "new in " + otherName; }
        else if (it->second == h) { item.status = Status::Same; item.detail = "same as current"; }
        else { item.status = Status::Changed; item.detail = std::to_string(clips) + (clips == 1 ? " clip" : " clips") + " \xc2\xb7 differs from current"; }
        out.tracks.push_back(std::move(item));
    }

    op.forEachPattern([&](const PatternSource& p) {
        Item item;
        item.kind = Kind::Pattern;
        item.id = p.id.value;
        item.name = p.name.empty() ? "Pattern " + std::to_string(p.id.value) : p.name;
        const auto it = currentPatterns.find(p.name);
        const uint64_t sig = detail::patternSignature(p, other.getSourceManager());
        if (!detail::instrumentsExist(p, current)) { item.status = Status::Blocked; item.detail = "needs an instrument this version does not have"; }
        else if (it == currentPatterns.end()) { item.status = Status::New; item.detail = "new in " + otherName; }
        else if (it->second == sig) { item.status = Status::Same; item.detail = "same as current"; }
        else { item.status = Status::Changed; item.detail = "differs from current"; }
        out.patterns.push_back(std::move(item));
    });
    std::sort(out.patterns.begin(), out.patterns.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
    return out;
}

/// What the dialog's checkboxes start as: what is new or changed, never what is the same or blocked.
inline bool defaultChecked(const Item& item) { return item.status == Status::New || item.status == Status::Changed; }

/// The chosen tracks and patterns, copied out of the other project so it can be closed again.
class Command : public ICommand {
public:
    Command(TrackManager& current, TrackManager& other, const std::vector<uint64_t>& trackIds, const std::vector<uint64_t>& patternIds)
        : m_current(current) {
        auto& op = other.getPatternManager();
        std::unordered_map<uint64_t, size_t> patternIndex;  // other's pattern id -> index into m_patterns
        const auto takePattern = [&](PatternID id) -> int {
            const auto found = patternIndex.find(id.value);
            if (found != patternIndex.end()) return static_cast<int>(found->second);
            const PatternSource* src = op.getPattern(id);
            if (!src) return -1;
            PatternCopy copy;
            copy.pattern = *src;
            if (src->isAudio()) {
                const auto& a = std::get<AudioSlicePayload>(src->payload);
                if (const auto* source = other.getSourceManager().getSource(a.audioSourceId)) {
                    copy.audioPath = source->getFilePath();
                    copy.audioName = source->getName();
                    copy.buffer = source->getBuffer();
                }
            }
            m_patterns.push_back(std::move(copy));
            patternIndex[id.value] = m_patterns.size() - 1;
            return static_cast<int>(m_patterns.size() - 1);
        };
        for (const uint64_t trackId : trackIds) {
            const Track* t = other.getTrack(trackId);
            if (!t) continue;
            TrackCopy tc;
            tc.name = t->name;
            for (const auto& laneId : t->laneIds) {
                const auto* lane = other.getPlaylistModel().getLane(laneId);
                if (!lane) continue;
                LaneCopy lc;
                lc.name = lane->name; lc.volume = lane->volume; lc.pan = lane->pan; lc.muted = lane->muted; lc.color = lane->colorRGBA;
                for (const auto& clip : lane->clips) {
                    ClipCopy cc;
                    cc.clip = clip;
                    cc.clip.id = ClipInstanceID{};
                    cc.pattern = clip.patternId.value ? takePattern(clip.patternId) : -1;
                    lc.clips.push_back(std::move(cc));
                }
                tc.lanes.push_back(std::move(lc));
            }
            m_tracks.push_back(std::move(tc));
        }
        for (const uint64_t id : patternIds) takePattern(PatternID{id});
    }

    void execute() override {
        auto& patterns = m_current.getPatternManager();
        auto& sources = m_current.getSourceManager();
        auto& playlist = m_current.getPlaylistModel();
        m_patternIds.clear();
        for (auto& copy : m_patterns) {
            PatternSource pattern = copy.pattern;
            if (pattern.isAudio()) {
                auto& payload = std::get<AudioSlicePayload>(pattern.payload);
                const ClipSourceID sourceId = copy.audioPath.empty() ? ClipSourceID{} : sources.getOrCreateSource(copy.audioPath, copy.audioName);
                if (auto* source = sources.getSource(sourceId); source && !source->isReady() && copy.buffer) source->setBuffer(copy.buffer);
                payload.audioSourceId = sourceId;
                pattern.setMixerChannelId(0);  // Master: mixer channels are not brought over
            }
            m_patternIds.push_back(patterns.adoptPattern(pattern));
        }
        m_made.clear();
        for (const auto& tc : m_tracks) {
            Made made;
            for (size_t i = 0; i < tc.lanes.size(); ++i) {
                const auto& lc = tc.lanes[i];
                const PlaylistLaneID laneId = playlist.createLane(lc.name);
                if (auto* lane = playlist.getLane(laneId)) { lane->volume = lc.volume; lane->pan = lc.pan; lane->muted = lc.muted; lane->colorRGBA = lc.color; }
                if (i == 0) made.trackId = m_current.createTrack(laneId, tc.name);
                else if (auto* track = m_current.getTrack(made.trackId)) { track->laneIds.push_back(laneId); if (auto* lane = playlist.getLane(laneId)) lane->trackId = made.trackId; }
                made.lanes.push_back(laneId);
                for (const auto& cc : lc.clips) {
                    ClipInstance clip = cc.clip;
                    if (cc.pattern >= 0) {
                        clip.patternId = m_patternIds[static_cast<size_t>(cc.pattern)];
                        if (const auto* p = patterns.getPattern(clip.patternId); p && p->isAudio())
                            clip.sourceId = std::get<AudioSlicePayload>(p->payload).audioSourceId.value;
                    }
                    made.clips.push_back(playlist.addClip(laneId, clip));
                }
            }
            m_made.push_back(std::move(made));
        }
        m_current.markModified();
    }

    void undo() override {
        auto& playlist = m_current.getPlaylistModel();
        for (auto it = m_made.rbegin(); it != m_made.rend(); ++it) {
            for (const auto& clipId : it->clips) playlist.removeClip(clipId);
            if (it->trackId) m_current.removeTrack(it->trackId);
            for (const auto& laneId : it->lanes) playlist.removeLane(laneId);
        }
        m_made.clear();
        for (const auto& id : m_patternIds) m_current.getPatternManager().removePattern(id);
        m_patternIds.clear();
        m_current.markModified();
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Bring over"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_patterns.size() * 256 + m_tracks.size() * 256; }
    bool changesProjectState() const override { return true; }
    std::string type() const override { return "bring_over"; }

    size_t trackCount() const { return m_tracks.size(); }
    size_t patternCount() const { return m_patterns.size(); }

private:
    struct PatternCopy {
        PatternSource pattern;
        std::string audioPath, audioName;
        std::shared_ptr<AudioBufferData> buffer;
    };
    struct ClipCopy { ClipInstance clip; int pattern = -1; };
    struct LaneCopy { std::string name; float volume = 1.0f, pan = 0.0f; bool muted = false; uint32_t color = 0xFFFFFFFF; std::vector<ClipCopy> clips; };
    struct TrackCopy { std::string name; std::vector<LaneCopy> lanes; };
    struct Made { uint64_t trackId = 0; std::vector<PlaylistLaneID> lanes; std::vector<ClipInstanceID> clips; };

    TrackManager& m_current;
    std::vector<PatternCopy> m_patterns;
    std::vector<TrackCopy> m_tracks;
    std::vector<PatternID> m_patternIds;
    std::vector<Made> m_made;
};

} // namespace BringOver
} // namespace Aestra
