// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RoutingModel: the read-only routing view (V8-C15). Built once from
// TrackManager; see the header for what it deliberately does not do.

#include "Models/RoutingModel.h"

#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

namespace {
constexpr uint32_t kEngineMaster = 0xFFFFFFFFu;

uint32_t toModelSpace(uint32_t id) { return id == kEngineMaster ? RoutingModel::kMaster : id; }
} // namespace

RoutingModel RoutingModel::capture(const TrackManager& manager) {
    RoutingModel model;
    const size_t count = manager.getChannelCount();
    model.m_channels.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const MixerChannel* ch = manager.getChannel(i);
        if (!ch) continue;

        Channel info;
        info.id = ch->getChannelId();
        info.name = ch->getName();
        info.mainOutputId = toModelSpace(ch->getMainOutputId());
        model.m_channels.push_back(info);

        Edge main;
        main.sourceId = info.id;
        main.targetId = info.mainOutputId;
        model.m_edges.push_back(main);

        for (const AudioRoute& send : ch->getSends()) {
            Edge e;
            e.sourceId = info.id;
            e.targetId = toModelSpace(send.targetChannelId);
            e.kind = EdgeKind::Send;
            e.sendId = send.sendId;
            e.gain = send.gain;
            e.postFader = send.postFader;
            e.muted = send.mute;
            e.sidechainOnly = send.sidechainOnly;
            model.m_edges.push_back(e);
        }
    }
    return model;
}

std::string RoutingModel::nameOf(uint32_t id) const {
    if (id == kMaster) return "Master";
    for (const Channel& ch : m_channels) {
        if (ch.id == id) return ch.name;
    }
    return {};
}

std::vector<RoutingModel::Edge> RoutingModel::feeds(uint32_t id) const {
    std::vector<Edge> out;
    for (const Edge& e : m_edges) {
        if (e.targetId == id) out.push_back(e);
    }
    std::stable_partition(out.begin(), out.end(), [](const Edge& e) { return e.kind == EdgeKind::Main; });
    return out;
}

std::vector<RoutingModel::Edge> RoutingModel::outputsOf(uint32_t id) const {
    std::vector<Edge> out;
    for (const Edge& e : m_edges) {
        if (e.sourceId == id) out.push_back(e);
    }
    return out;
}

std::vector<uint32_t> RoutingModel::idsNamed(const std::string& name) const {
    std::vector<uint32_t> ids;
    if (name == "Master") ids.push_back(kMaster);
    for (const Channel& ch : m_channels) {
        if (ch.name == name) ids.push_back(ch.id);
    }
    return ids;
}

std::vector<RoutingModel::OutputGroup> RoutingModel::groupByOutput() const {
    std::vector<OutputGroup> groups;
    auto groupFor = [&](uint32_t target) -> OutputGroup& {
        for (OutputGroup& g : groups) {
            if (g.targetId == target) return g;
        }
        OutputGroup g;
        g.targetId = target;
        g.targetName = nameOf(target);
        groups.push_back(std::move(g));
        return groups.back();
    };
    for (const Channel& ch : m_channels) {
        groupFor(ch.mainOutputId).sourceIds.push_back(ch.id);
    }
    std::stable_sort(groups.begin(), groups.end(), [](const OutputGroup& a, const OutputGroup& b) {
        return (a.targetId == kMaster) > (b.targetId == kMaster);
    });
    return groups;
}

} // namespace Audio
} // namespace Aestra
