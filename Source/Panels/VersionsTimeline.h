// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/TakeManager.h"
#include "NUITypes.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief Where every version sits on the History timeline (SPEC-017, V81-V1): one lane per branch in the order the
 * branches first appear, time running left to right, a link from each version to its parent. Pure: it takes the
 * manifest's versions and a rectangle and returns positions, so the layout is testable without a window.
 */
struct VersionsTimeline {
    static constexpr float kLabelColumn = 130.0f;  ///< room at the left for the branch names
    static constexpr float kTailRoom = 180.0f;     ///< room at the right for the dashed tail and its chip

    struct Node {
        int version = -1;       ///< index into the versions passed in
        int lane = 0;
        float x = 0.0f, y = 0.0f;
        bool autosave = false;
        bool open = false;
    };
    struct Link { int from = -1, to = -1; };  ///< node indices: parent to child
    struct Result {
        std::vector<std::string> lanes;       ///< branch names, lane order
        std::vector<float> laneY;
        std::vector<Node> nodes;              ///< same order as the versions given
        std::vector<Link> links;
    };

    static Result layout(const std::vector<TakeManager::TakeEntry>& versions, const AestraUI::NUIRect& area) {
        Result r;
        for (const auto& v : versions) {
            const std::string lane = v.branch.empty() ? "Main" : v.branch;
            if (std::find(r.lanes.begin(), r.lanes.end(), lane) == r.lanes.end()) r.lanes.push_back(lane);
        }
        const float top = area.y + 84.0f;
        const float step = r.lanes.size() > 1
                               ? std::clamp((area.height - 84.0f - 90.0f) / static_cast<float>(r.lanes.size() - 1), 56.0f, 120.0f)
                               : 0.0f;
        for (size_t i = 0; i < r.lanes.size(); ++i) r.laneY.push_back(top + step * static_cast<float>(i));

        // Time order decides the left-to-right rank; ties keep the manifest's order.
        std::vector<int> order(versions.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return versions[static_cast<size_t>(a)].createdAtMs < versions[static_cast<size_t>(b)].createdAtMs; });
        const float left = area.x + kLabelColumn + 40.0f, right = std::max(left + 1.0f, area.right() - kTailRoom);
        // A saved version takes a full slot along the axis; an autosave takes half, so the labels stay readable.
        float total = 0.0f;
        std::vector<float> slotAt(order.size(), 0.0f);
        for (size_t rank = 0; rank < order.size(); ++rank) {
            slotAt[rank] = total;
            total += versions[static_cast<size_t>(order[rank])].kind == TakeManager::TakeEntry::Kind::Autosave ? 0.5f : 1.0f;
        }
        const float lastSlot = order.empty() ? 0.0f : slotAt.back();
        const float stepX = lastSlot > 0.0f ? (right - left) / lastSlot : 0.0f;

        r.nodes.resize(versions.size());
        for (size_t rank = 0; rank < order.size(); ++rank) {
            const int i = order[rank];
            const auto& v = versions[static_cast<size_t>(i)];
            auto& n = r.nodes[static_cast<size_t>(i)];
            n.version = i;
            const std::string lane = v.branch.empty() ? "Main" : v.branch;
            n.lane = static_cast<int>(std::find(r.lanes.begin(), r.lanes.end(), lane) - r.lanes.begin());
            n.x = left + stepX * slotAt[rank];
            n.y = r.laneY[static_cast<size_t>(n.lane)];
            n.autosave = v.kind == TakeManager::TakeEntry::Kind::Autosave;
            n.open = v.active;
        }
        for (size_t i = 0; i < versions.size(); ++i) {
            if (versions[i].parentId.empty()) continue;
            for (size_t j = 0; j < versions.size(); ++j)
                if (versions[j].id == versions[i].parentId) r.links.push_back({static_cast<int>(j), static_cast<int>(i)});
        }
        return r;
    }

    /// The node under @p p (within @p radius), or -1. Later nodes win a tie, as they are drawn on top.
    static int nodeAt(const Result& r, const AestraUI::NUIPoint& p, float radius = 12.0f) {
        for (int i = static_cast<int>(r.nodes.size()) - 1; i >= 0; --i) {
            const auto& n = r.nodes[static_cast<size_t>(i)];
            if (std::fabs(n.x - p.x) <= radius && std::fabs(n.y - p.y) <= radius) return i;
        }
        return -1;
    }

    /// "Today · 14:30", or "Oct 2 · 19:15" for earlier days; empty for a time that was never set.
    static std::string timeLabel(uint64_t epochMs, uint64_t nowMs) {
        if (epochMs == 0) return {};
        const auto toTm = [](uint64_t ms) {
            std::time_t t = static_cast<std::time_t>(ms / 1000);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            return tm;
        };
        const std::tm then = toTm(epochMs), now = toTm(nowMs);
        char buf[48];
        if (then.tm_year == now.tm_year && then.tm_yday == now.tm_yday)
            std::snprintf(buf, sizeof buf, "Today \xc2\xb7 %02d:%02d", then.tm_hour, then.tm_min);
        else {
            static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            std::snprintf(buf, sizeof buf, "%s %d \xc2\xb7 %02d:%02d", months[std::clamp(then.tm_mon, 0, 11)], then.tm_mday, then.tm_hour, then.tm_min);
        }
        return buf;
    }
};

} // namespace Aestra
