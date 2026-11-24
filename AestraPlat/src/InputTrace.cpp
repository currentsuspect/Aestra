#include "AestraInputTrace.h"

#include "AestraLog.h"

#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>

namespace Aestra::InputTrace {

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("AESTRA_INPUT_TRACE");
        return v && *v && std::string(v) != "0";
    }();
    return on;
}

void log(const std::string& line) {
    Log::info("[InputTrace] " + line);
}

bool allow(const char* key, int perSecond) {
    using Clock = std::chrono::steady_clock;
    struct Window {
        Clock::time_point start{};
        int count = 0;
    };
    static std::mutex mutex;
    static std::map<const char*, Window> windows;
    std::lock_guard<std::mutex> lock(mutex);
    auto& w = windows[key];
    const auto now = Clock::now();
    if (w.start == Clock::time_point{} || now - w.start >= std::chrono::seconds(1)) {
        w.start = now;
        w.count = 0;
    }
    return ++w.count <= perSecond;
}

} // namespace Aestra::InputTrace
