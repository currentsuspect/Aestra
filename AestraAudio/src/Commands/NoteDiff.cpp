// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/NoteDiff.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

/**
 * Equality predicate that compares all stable note fields.
 */
bool notesEqualFull(const Aestra::Audio::MidiNote& a, const Aestra::Audio::MidiNote& b) {
    return a.pitch == b.pitch &&
           std::abs(a.startBeat - b.startBeat) < 1e-6 &&
           std::abs(a.durationBeats - b.durationBeats) < 1e-6 &&
           a.unitId == b.unitId;
}

/**
 * Position equality: same pitch, start, and unitId (duration may differ).
 */
bool notesEqualPosition(const Aestra::Audio::MidiNote& a, const Aestra::Audio::MidiNote& b) {
    return a.pitch == b.pitch &&
           std::abs(a.startBeat - b.startBeat) < 1e-6 &&
           a.unitId == b.unitId;
}

/**
 * Expression inequality: velocity or pan changed on an otherwise-identical note.
 */
bool notesExpressionDiffers(const Aestra::Audio::MidiNote& a, const Aestra::Audio::MidiNote& b) {
    return std::abs(a.velocity - b.velocity) > 1e-4f || std::abs(a.pan - b.pan) > 1e-4f;
}

} // anonymous namespace

namespace Aestra {
namespace Audio {

NoteDiffResult diffNotes(const std::vector<MidiNote>& before,
                        const std::vector<MidiNote>& after) {
    NoteDiffResult result;

    // Track which notes have been matched to avoid double-assignment.
    std::unordered_set<size_t> matchedBefore;
    std::unordered_set<size_t> matchedAfter;

    // -------------------------------------------------------------------------
    // Pass 1: Full-field exact matching using ALL stable fields.
    // This resolves the "same pitch/start/unit, different duration" ambiguity
    // because notes with different duration are never considered equal here.
    // -------------------------------------------------------------------------
    for (size_t bi = 0; bi < before.size(); ++bi) {
        const auto& bnote = before[bi];
        for (size_t ai = 0; ai < after.size(); ++ai) {
            if (matchedAfter.count(ai)) continue;
            if (notesEqualFull(bnote, after[ai])) {
                matchedBefore.insert(bi);
                matchedAfter.insert(ai);
                // Full-field equality ignores expression, so a velocity/pan
                // change on an otherwise-untouched note surfaces here.
                if (notesExpressionDiffers(bnote, after[ai])) {
                    result.modified.push_back({bnote, after[ai]});
                }
                break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Pass 2: Position-dur matching for resize detection.
    // Notes at the same position (pitch+start+unitId) but different duration → resize.
    // -------------------------------------------------------------------------
    struct PosKey {
        int pitch;
        double startBeat;
        uint64_t unitId;
        bool operator==(const PosKey& o) const {
            return pitch == o.pitch && std::abs(startBeat - o.startBeat) < 1e-6 && unitId == o.unitId;
        }
    };
    struct PosKeyHash {
        size_t operator()(const PosKey& k) const {
            size_t h = std::hash<int>{}(k.pitch);
            h ^= std::hash<uint64_t>{}(k.unitId);
            uint64_t bits;
            static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
            std::memcpy(&bits, &k.startBeat, sizeof(double));
            h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<PosKey, std::vector<std::pair<size_t, MidiNote>>, PosKeyHash> byPosBefore;
    std::unordered_map<PosKey, std::vector<std::pair<size_t, MidiNote>>, PosKeyHash> byPosAfter;

    for (size_t bi = 0; bi < before.size(); ++bi) {
        if (matchedBefore.count(bi)) continue;
        const auto& n = before[bi];
        PosKey key{n.pitch, n.startBeat, n.unitId};
        byPosBefore[key].push_back({bi, n});
    }

    for (size_t ai = 0; ai < after.size(); ++ai) {
        if (matchedAfter.count(ai)) continue;
        const auto& n = after[ai];
        PosKey key{n.pitch, n.startBeat, n.unitId};
        byPosAfter[key].push_back({ai, n});
    }

    // For each position group, match by duration for resize detection.
    for (auto& [key, beforeVec] : byPosBefore) {
        auto it = byPosAfter.find(key);
        if (it == byPosAfter.end()) {
            // No notes at this position in 'after' — skip for now.
            // These may be moves (if there's a matching duration elsewhere).
            continue;
        }
        auto& afterVec = it->second;

        // Match notes at this position.
        // All notes are at the SAME position (same pitch, start, unitId).
        // We match by duration, preferring exact matches and minimizing total distance.
        std::vector<bool> afterUsed(afterVec.size(), false);
        std::vector<bool> beforeUsed(beforeVec.size(), false);

        // Build all possible pairs with distances, sort by distance.
        std::vector<std::tuple<double, size_t, size_t>> pairs;
        for (size_t bi = 0; bi < beforeVec.size(); ++bi) {
            double bDur = beforeVec[bi].second.durationBeats;
            for (size_t ai = 0; ai < afterVec.size(); ++ai) {
                double dist = std::abs(bDur - afterVec[ai].second.durationBeats);
                pairs.push_back({dist, bi, ai});
            }
        }
        std::sort(pairs.begin(), pairs.end());

        // Greedy matching by increasing distance - this ensures optimal pairing.
        for (auto& [dist, bi, ai] : pairs) {
            if (beforeUsed[bi] || afterUsed[ai]) continue;
            beforeUsed[bi] = true;
            afterUsed[ai] = true;
            const auto& bnote = beforeVec[bi].second;
            const auto& anote = afterVec[ai].second;
            if (dist >= 1e-6) {
                // Non-exact match = resize
                result.resized.push_back({bnote, anote});
            } else if (notesExpressionDiffers(bnote, anote)) {
                // Same position and duration but new velocity/pan.
                result.modified.push_back({bnote, anote});
            }
        }

        // Mark matched notes (both used and unused).
        // Unmatched notes remain unmarked so they can be collected at the end.
        for (size_t bi = 0; bi < beforeVec.size(); ++bi) {
            if (beforeUsed[bi]) {
                matchedBefore.insert(beforeVec[bi].first);
            }
        }
        for (size_t ai = 0; ai < afterVec.size(); ++ai) {
            if (afterUsed[ai]) {
                matchedAfter.insert(afterVec[ai].first);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Pass 3: Duration-based matching for move detection.
    // Notes with the same duration but different positions → move.
    // -------------------------------------------------------------------------
    struct DurKey {
        double duration;
        uint64_t unitId;
        bool operator==(const DurKey& o) const {
            return std::abs(duration - o.duration) < 1e-6 && unitId == o.unitId;
        }
    };
    struct DurKeyHash {
        size_t operator()(const DurKey& k) const {
            uint64_t bits;
            static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
            std::memcpy(&bits, &k.duration, sizeof(double));
            return std::hash<uint64_t>{}(bits) ^ std::hash<uint64_t>{}(k.unitId);
        }
    };

    std::unordered_map<DurKey, std::vector<std::pair<size_t, MidiNote>>, DurKeyHash> byDurBefore;
    std::unordered_map<DurKey, std::vector<std::pair<size_t, MidiNote>>, DurKeyHash> byDurAfter;

    for (size_t bi = 0; bi < before.size(); ++bi) {
        if (matchedBefore.count(bi)) continue;
        const auto& n = before[bi];
        DurKey key{n.durationBeats, n.unitId};
        byDurBefore[key].push_back({bi, n});
    }

    for (size_t ai = 0; ai < after.size(); ++ai) {
        if (matchedAfter.count(ai)) continue;
        const auto& n = after[ai];
        DurKey key{n.durationBeats, n.unitId};
        byDurAfter[key].push_back({ai, n});
    }

    // Match by duration. Take the first matching pair from each duration group.
    for (auto& [key, beforeVec] : byDurBefore) {
        auto it = byDurAfter.find(key);
        if (it == byDurAfter.end()) continue;
        auto& afterVec = it->second;

        if (beforeVec.size() == 1 && afterVec.size() == 1) {
            const auto& bnote = beforeVec[0].second;
            const auto& anote = afterVec[0].second;
            // Verify positions differ (otherwise already handled as resize).
            if (!notesEqualPosition(bnote, anote)) {
                matchedBefore.insert(beforeVec[0].first);
                matchedAfter.insert(afterVec[0].first);
                result.moved.push_back({bnote, anote});
            }
        }
    }

    // Collect remaining unmatched notes.
    for (size_t bi = 0; bi < before.size(); ++bi) {
        if (!matchedBefore.count(bi)) {
            result.removed.push_back(before[bi]);
        }
    }
    for (size_t ai = 0; ai < after.size(); ++ai) {
        if (!matchedAfter.count(ai)) {
            result.added.push_back(after[ai]);
        }
    }

    // Sort for deterministic output order.
    auto sortNotes = [](std::vector<MidiNote>& v) {
        std::sort(v.begin(), v.end(), [](const MidiNote& a, const MidiNote& b) {
            if (a.pitch != b.pitch) return a.pitch < b.pitch;
            return a.startBeat < b.startBeat;
        });
    };
    auto sortPairs = [](std::vector<std::pair<MidiNote, MidiNote>>& v) {
        std::sort(v.begin(), v.end(), [](const std::pair<MidiNote, MidiNote>& a,
                                         const std::pair<MidiNote, MidiNote>& b) {
            if (a.first.pitch != b.first.pitch) return a.first.pitch < b.first.pitch;
            return a.first.startBeat < b.first.startBeat;
        });
    };

    sortNotes(result.removed);
    sortNotes(result.added);
    sortPairs(result.resized);
    sortPairs(result.moved);
    sortPairs(result.modified);

    return result;
}

} // namespace Audio
} // namespace Aestra