// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The records the plugin helper (AestraPluginHost --stdio) returns for `SCANALL vst3 <hex path>`, and how the app reads
// them. Scanning a VST3 bundle means loading it, and a loaded plugin stays mapped in the process that scanned it: on
// 15 Nov 2026 Surge XT and Surge XT Effects were 40 MB of Aestra's 477 MB at idle for no reason but a scan at startup.
// The helper loads the bundle and exits; the app only ever sees the text.
//
// One record per plugin class, ';' apart; fields hex-encoded, ',' apart:
//   id, name, vendor, version, category, type (PluginType as an integer), hasMidiInput (0 or 1)

#include "Plugin/PluginHost.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace ScanProtocol {

inline std::string hexEncode(const std::string& in) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(in.size() * 2);
    for (unsigned char c : in) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

inline std::string hexDecode(const std::string& in) {
    std::string out;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        const int hi = nibble(in[i]);
        const int lo = nibble(in[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out += static_cast<char>((hi << 4) | lo);
    }
    return out;
}

inline std::string encodeRecord(const PluginInfo& info) {
    return hexEncode(info.id) + "," + hexEncode(info.name) + "," + hexEncode(info.vendor) + "," + hexEncode(info.version) +
           "," + hexEncode(info.category) + "," + hexEncode(std::to_string(static_cast<int>(info.type))) + "," +
           hexEncode(info.hasMidiInput ? "1" : "0");
}

inline std::string encodeRecords(const std::vector<PluginInfo>& plugins) {
    std::string out;
    for (const auto& info : plugins) {
        if (!out.empty()) out += ";";
        out += encodeRecord(info);
    }
    return out;
}

/// One VST3 record back into a PluginInfo (path supplied by the caller). Nothing for a malformed or invalid one.
inline std::optional<PluginInfo> decodeRecord(const std::string& record, const std::filesystem::path& path) {
    std::vector<std::string> field;
    std::istringstream parts(record);
    std::string part;
    while (std::getline(parts, part, ',')) field.push_back(hexDecode(part));
    if (field.size() < 7) return std::nullopt;
    PluginInfo info;
    info.id = field[0];
    info.name = field[1];
    info.vendor = field[2];
    info.version = field[3];
    info.category = field[4];
    info.format = PluginFormat::VST3;
    int type = 0;
    try {
        type = std::stoi(field[5]);
    } catch (...) {
        return std::nullopt;
    }
    if (type < static_cast<int>(PluginType::Effect) || type > static_cast<int>(PluginType::Analyzer)) return std::nullopt;
    info.type = static_cast<PluginType>(type);
    info.hasMidiInput = field[6] == "1";
    info.path = path;
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    info.hasEditor = true; // as VST3PluginFactory::scanPlugin assumes: verified on load
    return info.isValid() ? std::optional<PluginInfo>(info) : std::nullopt;
}

/// The whole "OK ..." payload (after "OK ") into plugins.
inline std::vector<PluginInfo> decodeRecords(const std::string& payload, const std::filesystem::path& path) {
    std::vector<PluginInfo> out;
    std::istringstream records(payload);
    std::string record;
    while (std::getline(records, record, ';')) {
        if (auto info = decodeRecord(record, path)) out.push_back(std::move(*info));
    }
    return out;
}

} // namespace ScanProtocol
} // namespace Audio
} // namespace Aestra
