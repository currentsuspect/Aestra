// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H5: the sandbox helper's parameter commands, as reply strings (VST3 here; the CLAP module has its own, same format).
//   PARAMS          -> OK <n> <id>:<flags>:<steps>:<default>:<name>:<short>:<unit> ...   (text fields hex-encoded)
//   GETPARAM <id>   -> OK GETPARAM <normalized> <display hex>
//   EDITS           -> OK EDITS <id>:<normalized> ...   (knobs moved in the plugin's window since the last EDITS)
// flags: 1 automatable, 2 bypass, 4 read-only.

#ifdef AESTRA_HAS_VST3
#include "Plugin/VST3Host.h"
#endif

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace AestraHelper {

inline std::string hexOf(const std::string& text) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (const unsigned char c : text) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0F]);
    }
    return out;
}

/// Edits the plugin's window reported, waiting for the app to ask. Bounded: an app that never asks cannot grow it.
class EditLog {
public:
    void add(uint32_t id, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_edits) {
            if (e.first == id) {
                e.second = value; // only the latest matters
                return;
            }
        }
        if (m_edits.size() < 4096) m_edits.emplace_back(id, value);
    }
    std::string take() {
        std::vector<std::pair<uint32_t, double>> edits;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            edits.swap(m_edits);
        }
        std::string out = "OK EDITS";
        char buf[64];
        for (const auto& e : edits) {
            std::snprintf(buf, sizeof buf, " %u:%.9g", e.first, e.second);
            out += buf;
        }
        return out;
    }

private:
    std::mutex m_mutex;
    std::vector<std::pair<uint32_t, double>> m_edits;
};

#ifdef AESTRA_HAS_VST3
inline std::string paramsReply(Aestra::Audio::VST3PluginInstance& plugin) {
    const auto params = plugin.getParameters();
    std::string out = "OK " + std::to_string(params.size());
    char buf[96];
    for (const auto& p : params) {
        const int flags = (p.isAutomatable ? 1 : 0) | (p.isBypass ? 2 : 0) | (p.isReadOnly ? 4 : 0);
        std::snprintf(buf, sizeof buf, " %u:%d:%u:%.9g:", p.id, flags, p.stepCount, static_cast<double>(p.defaultValue));
        out += buf;
        out += hexOf(p.name) + ":" + hexOf(p.shortName) + ":" + hexOf(p.unit);
    }
    return out;
}

inline std::string getParamReply(Aestra::Audio::VST3PluginInstance& plugin, uint32_t id) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "OK GETPARAM %.9g ", static_cast<double>(plugin.getParameter(id)));
    return std::string(buf) + hexOf(plugin.getParameterDisplay(id));
}

#endif // AESTRA_HAS_VST3

} // namespace AestraHelper
