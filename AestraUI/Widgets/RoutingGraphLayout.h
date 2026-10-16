// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The routing graph's layout rules (V81-R1, FD-25), kept free of the renderer so they can be tested.
//
// The graph draws exceptions only. A channel that goes straight to Master, sends nowhere and is fed by nothing
// says nothing a producer could not assume, so it leaves the canvas for the "Direct to Master" list. Everything
// else sits in a column by signal depth: sources (fed by nobody) first, then each channel one column right of the
// deepest channel feeding it. Rows follow the feeders: a bus sits level with the middle of the channels routed
// into it, so wires run short and flat. Master is not a node; it is the rail every last output runs into.

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AestraUI::RoutingGraph {

constexpr uint32_t kMaster = 0;

struct Send {
    uint32_t target{kMaster};
    bool sidechain{false};
};

enum class Role { Track, Bus, Return };

struct Channel {
    uint32_t id{0};
    uint32_t output{kMaster}; ///< main output; kMaster when it goes to Master
    bool outputAudible{true}; ///< false when the main path is switched off (no output wire)
    Role role{Role::Track};
    std::vector<Send> sends;
};

struct Placed {
    uint32_t id{0};
    int column{0};
    float row{0.0f}; ///< in row units; may be fractional (a bus centred between its feeders)
};

struct Plan {
    std::vector<uint32_t> direct; ///< straight to Master, nothing else: listed, not drawn
    std::vector<Placed> nodes;    ///< drawn, in input order
    int columns{0};
    int customRoutes{0}; ///< main outputs that go somewhere other than Master
    int sends{0};        ///< audio sends
    int sidechains{0};   ///< sidechain-only sends
};

/// True when the channel says nothing beyond "goes to Master": no custom output, no sends, nobody feeds it.
inline bool isPlain(const Channel& ch, const std::unordered_set<uint32_t>& fed) {
    return ch.output == kMaster && ch.sends.empty() && fed.find(ch.id) == fed.end();
}

/// Lays the graph out. With allTracks, plain channels are drawn as sources instead of listed.
inline Plan plan(const std::vector<Channel>& channels, bool allTracks = false) {
    Plan out;
    std::unordered_map<uint32_t, size_t> index;
    for (size_t i = 0; i < channels.size(); ++i)
        index[channels[i].id] = i;

    // feeders[n] = every channel whose output or send lands on n (main-output feeders kept apart for rows).
    std::unordered_map<uint32_t, std::vector<uint32_t>> feeders, mainFeeders;
    std::unordered_set<uint32_t> fed;
    for (const auto& ch : channels) {
        if (ch.output != kMaster && ch.outputAudible && index.count(ch.output)) {
            feeders[ch.output].push_back(ch.id);
            mainFeeders[ch.output].push_back(ch.id);
            fed.insert(ch.output);
            ++out.customRoutes;
        }
        for (const auto& s : ch.sends) {
            if (s.sidechain)
                ++out.sidechains;
            else
                ++out.sends;
            if (s.target != kMaster && index.count(s.target)) {
                feeders[s.target].push_back(ch.id);
                fed.insert(s.target);
            }
        }
    }

    std::vector<size_t> drawn;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (!allTracks && isPlain(channels[i], fed))
            out.direct.push_back(channels[i].id);
        else
            drawn.push_back(i);
    }

    // Depth = one past the deepest feeder. The engine refuses cycles, but a corrupt project must not hang the
    // UI, so relaxation stops after as many passes as there are channels.
    std::unordered_map<uint32_t, int> depth;
    for (size_t i : drawn)
        depth[channels[i].id] = 0;
    for (size_t pass = 0; pass < drawn.size(); ++pass) {
        bool changed = false;
        for (size_t i : drawn) {
            const uint32_t id = channels[i].id;
            auto it = feeders.find(id);
            if (it == feeders.end())
                continue;
            int d = 0;
            for (uint32_t f : it->second) {
                auto fd = depth.find(f);
                if (fd != depth.end())
                    d = std::max(d, fd->second + 1);
            }
            if (d != depth[id]) {
                depth[id] = d;
                changed = true;
            }
        }
        if (!changed)
            break;
    }

    int maxDepth = -1;
    for (size_t i : drawn)
        maxDepth = std::max(maxDepth, depth[channels[i].id]);
    out.columns = maxDepth + 1;

    // Sources: grouped by where they go (all of a bus's feeders together), a blank row between groups.
    std::unordered_map<uint32_t, float> row;
    {
        std::vector<size_t> sources;
        for (size_t i : drawn)
            if (depth[channels[i].id] == 0)
                sources.push_back(i);
        // Group key = the first destination drawn (main output, else first send target, else Master).
        auto groupOf = [&](const Channel& ch) -> uint32_t {
            if (ch.output != kMaster && ch.outputAudible)
                return ch.output;
            for (const auto& s : ch.sends)
                if (s.target != kMaster)
                    return s.target;
            return kMaster;
        };
        std::vector<uint32_t> groupOrder;
        std::unordered_map<uint32_t, std::vector<size_t>> groups;
        for (size_t i : sources) {
            const uint32_t g = groupOf(channels[i]);
            if (!groups.count(g))
                groupOrder.push_back(g);
            groups[g].push_back(i);
        }
        float r = 0.0f;
        for (size_t gi = 0; gi < groupOrder.size(); ++gi) {
            const auto& members = groups[groupOrder[gi]];
            if (gi > 0 && (members.size() > 1 || groups[groupOrder[gi - 1]].size() > 1))
                r += 1.0f; // breathing room around a bus's feeders
            for (size_t i : members)
                row[channels[i].id] = r++;
        }
    }

    // Deeper columns: level with the middle of the main-output feeders (else all feeders), then pushed apart so
    // no two nodes in a column overlap.
    for (int d = 1; d <= maxDepth; ++d) {
        struct Want {
            uint32_t id;
            float y;
            size_t order;
        };
        std::vector<Want> wants;
        for (size_t i : drawn) {
            const uint32_t id = channels[i].id;
            if (depth[id] != d)
                continue;
            const auto& from = mainFeeders.count(id) ? mainFeeders[id] : feeders[id];
            float sum = 0.0f;
            int n = 0;
            for (uint32_t f : from) {
                auto rf = row.find(f);
                if (rf != row.end()) {
                    sum += rf->second;
                    ++n;
                }
            }
            wants.push_back({id, n ? sum / static_cast<float>(n) : 0.0f, i});
        }
        std::stable_sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) {
            return a.y != b.y ? a.y < b.y : a.order < b.order;
        });
        // Channels wanting the same row (two returns fed by one bus) spread around it, not below it.
        for (size_t i = 0; i < wants.size();) {
            size_t j = i;
            while (j + 1 < wants.size() && wants[j + 1].y == wants[i].y)
                ++j;
            const float start = wants[i].y - static_cast<float>(j - i) * 0.5f;
            for (size_t k = i; k <= j; ++k)
                wants[k].y = start + static_cast<float>(k - i);
            i = j + 1;
        }
        float floor = -1e9f;
        for (auto& w : wants) {
            const float y = std::max({w.y, floor, 0.0f});
            row[w.id] = y;
            floor = y + 1.0f;
        }
    }

    for (size_t i : drawn) {
        const uint32_t id = channels[i].id;
        out.nodes.push_back({id, depth[id], row[id]});
    }
    return out;
}

/// The heading over a column: sources first, then what the column holds.
inline std::string columnHeading(int column, const std::vector<Channel>& channels, const Plan& p) {
    if (column == 0)
        return "SOURCES";
    bool bus = false, ret = false;
    std::unordered_map<uint32_t, Role> roles;
    for (const auto& ch : channels)
        roles[ch.id] = ch.role;
    for (const auto& n : p.nodes) {
        if (n.column != column)
            continue;
        const Role r = roles[n.id];
        bus |= r != Role::Return;
        ret |= r == Role::Return;
    }
    const std::string what = bus && ret ? "BUSES & RETURNS" : (ret ? "RETURNS" : "BUSES");
    return what + " \xC2\xB7 DEPTH " + std::to_string(column);
}

/// Every channel upstream (feeding into) and downstream (fed from) of `id`, including itself. Master is
/// downstream of anything that reaches it but is not listed. A sidechain carries no audio onward (it only feeds
/// a detector), so the walk takes the channel at its far end and stops there.
struct Trace {
    std::unordered_set<uint32_t> upstream, downstream;
    std::unordered_set<uint32_t> listening; ///< reached downstream only through a sidechain: their outputs are not in the chain
    bool contains(uint32_t id) const { return upstream.count(id) || downstream.count(id); }
};

inline Trace trace(const std::vector<Channel>& channels, uint32_t id) {
    Trace t;
    struct Link {
        uint32_t to;
        bool sidechain;
    };
    std::unordered_map<uint32_t, std::vector<Link>> out, in;
    for (const auto& ch : channels) {
        if (ch.output != kMaster && ch.outputAudible) {
            out[ch.id].push_back({ch.output, false});
            in[ch.output].push_back({ch.id, false});
        }
        for (const auto& s : ch.sends)
            if (s.target != kMaster) {
                out[ch.id].push_back({s.target, s.sidechain});
                in[s.target].push_back({ch.id, s.sidechain});
            }
    }
    auto walk = [](uint32_t start, std::unordered_map<uint32_t, std::vector<Link>>& next,
                   std::unordered_set<uint32_t>& seen, std::unordered_set<uint32_t>& terminal) {
        std::vector<uint32_t> stack{start};
        seen.insert(start);
        while (!stack.empty()) {
            const uint32_t cur = stack.back();
            stack.pop_back();
            for (const Link& l : next[cur]) {
                if (l.sidechain) {
                    if (seen.insert(l.to).second)
                        terminal.insert(l.to);
                } else if (seen.insert(l.to).second || terminal.erase(l.to)) {
                    stack.push_back(l.to); // reached by audio after all
                }
            }
        }
    };
    std::unordered_set<uint32_t> upTerminal;
    walk(id, out, t.downstream, t.listening);
    walk(id, in, t.upstream, upTerminal);
    return t;
}

/// Channels that would loop if `source` were routed into them: `source` itself and everything whose audio
/// reaches it. (The engine's routing authority has the final word; this is for explaining a refusal.)
inline std::unordered_set<uint32_t> wouldLoop(const std::vector<Channel>& channels, uint32_t source) {
    return trace(channels, source).upstream;
}


} // namespace AestraUI::RoutingGraph
