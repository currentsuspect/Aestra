// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPerfProbe.h"

#include "NUIComponent.h"
#include "../../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
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

int level() {
    static const int value = [] {
        const char* v = std::getenv("AESTRA_FRAME_STATS");
        return v ? std::max(0, std::atoi(v)) : 0;
    }();
    return value;
}

bool enabled() { return level() >= 2; }

namespace {
struct FrameWork {
    std::vector<double> ms;
    std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point workStart = windowStart;
};
FrameWork& frameWork() {
    static FrameWork f;
    return f;
}
} // namespace

void frameWorkBegin() {
    if (level() > 0) frameWork().workStart = std::chrono::steady_clock::now();
}

void frameWorkEnd(bool presented) {
    if (level() <= 0) return;
    auto& f = frameWork();
    const auto now = std::chrono::steady_clock::now();
    if (presented) f.ms.push_back(std::chrono::duration<double, std::milli>(now - f.workStart).count());
    if (now - f.windowStart < std::chrono::seconds(1)) return;
    f.windowStart = now;
    if (f.ms.empty()) return;

    std::sort(f.ms.begin(), f.ms.end());
    double sum = 0.0;
    for (const double ms : f.ms) sum += ms;
    const auto over = [&](double budget) {
        return static_cast<long>(std::count_if(f.ms.begin(), f.ms.end(), [budget](double ms) { return ms > budget; }));
    };
    const std::size_t p95 = (f.ms.size() * 95 + 99) / 100 - 1; // nearest rank: ceil(0.95 n) - 1
    std::array<char, 256> line{};
    std::snprintf(line.data(), line.size(),
                  "[FrameStats] frames=%zu work avg=%.2fms p95=%.2fms max=%.2fms over16.7=%ld over33=%ld",
                  f.ms.size(), sum / static_cast<double>(f.ms.size()), f.ms[p95], f.ms.back(), over(16.7), over(33.3));
    Aestra::Log::info(line.data());
    if (enabled()) {
        std::istringstream report(takeReport(f.ms.size()));
        for (std::string detail; std::getline(report, detail);) Aestra::Log::info(detail);
    }
    f.ms.clear();
}

bool presentBecause(const char* reason) {
    if (enabled()) recordPresentReason(reason);
    return true;
}

void recordDirtyOrigin(const NUIComponent& component, const NUIComponent* parent) {
    counters().dirtyOrigins[typeName(component) + "<" + (parent ? typeName(*parent) : std::string("root")) + ">"]++;
}

void recordRenderSelf(const NUIComponent& component, double ms) { counters().renderSelfMs[typeName(component)] += ms; }

void recordPhase(Phase phase, double ms) { counters().phaseMs.at(static_cast<size_t>(phase)) += ms; }

void recordPresentReason(const char* reason) { counters().presentReasons[reason]++; }

std::string takeReport(std::size_t presentedFrames) {
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
