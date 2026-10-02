// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPerfProbe.h"

#include "NUIComponent.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <typeinfo>
#include <utility>
#include <vector>

namespace AestraUI::PerfProbe {
namespace {

constexpr size_t kTopN = 8;       // entries per detail line
constexpr size_t kLineBuffer = 160; // one formatted entry

struct Counters {
    std::map<std::string, int> presentReasons;
    std::map<std::string, int> dirtyOrigins;
    std::map<std::string, double> renderSelfMs;
    std::array<double, 3> phaseMs{};
};

Counters& counters() {
    static Counters c;
    return c;
}

// Mangled RTTI name, trimmed of the Itanium "N8AestraUI...E" noise for reading.
std::string typeName(const NUIComponent& component) {
    std::string name = typeid(component).name();
    for (const char* prefix : {"N8AestraUI", "N6Aestra5Audio", "N6Aestra"}) {
        if (name.rfind(prefix, 0) == 0) name = name.substr(std::char_traits<char>::length(prefix));
    }
    size_t i = 0;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') ++i; // length prefix
    name = name.substr(i);
    if (!name.empty() && name.back() == 'E') name.pop_back();
    return name;
}

template <typename T>
std::string topN(const std::map<std::string, T>& values, size_t n, double divisor, const char* format) {
    std::vector<std::pair<double, std::string>> ranked;
    ranked.reserve(values.size());
    for (const auto& [key, value] : values) ranked.push_back({static_cast<double>(value) / divisor, key});
    std::sort(ranked.rbegin(), ranked.rend());
    std::string out;
    std::array<char, kLineBuffer> buf{};
    for (size_t i = 0; i < ranked.size() && i < n; ++i) {
        std::snprintf(buf.data(), buf.size(), format, ranked[i].second.c_str(), ranked[i].first);
        out += buf.data();
    }
    return out;
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("AESTRA_FRAME_STATS");
        return v && std::atoi(v) >= 2;
    }();
    return on;
}

void recordDirtyOrigin(const NUIComponent& component, const NUIComponent* parent) {
    counters().dirtyOrigins[typeName(component) + "<" + (parent ? typeName(*parent) : std::string("root")) + ">"]++;
}

void recordRenderSelf(const NUIComponent& component, double ms) { counters().renderSelfMs[typeName(component)] += ms; }

void recordPhase(Phase phase, double ms) { counters().phaseMs.at(static_cast<size_t>(phase)) += ms; }

void recordPresentReason(const char* reason) { counters().presentReasons[reason]++; }

std::string takeReport(size_t presentedFrames) {
    auto& c = counters();
    const double frames = static_cast<double>(std::max<size_t>(1, presentedFrames));
    std::string report = "[FrameWhy]" + topN(c.presentReasons, kTopN, 1.0, " %s=%.0f");
    report += "\n[DirtyWho] per second:" + topN(c.dirtyOrigins, kTopN, 1.0, " %s=%.0f");
    report += "\n[RenderSelf] ms/frame:" + topN(c.renderSelfMs, kTopN, frames, " %s=%.2f");
    std::array<char, kLineBuffer> phase{};
    std::snprintf(phase.data(), phase.size(), "\n[Phase] ms/frame: tree=%.2f submit=%.2f swap=%.2f", c.phaseMs[0] / frames,
                  c.phaseMs[1] / frames, c.phaseMs[2] / frames);
    report += phase.data();
    c = Counters{};
    return report;
}

} // namespace AestraUI::PerfProbe
