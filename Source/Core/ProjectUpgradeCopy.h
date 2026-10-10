// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraCore/include/AestraJSON.h"
#include "../AestraCore/include/AestraLog.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace Aestra {
namespace ProjectUpgradeCopy {

/// Where a project's last pre-upgrade file is kept: "Song.aes" from schema v3
/// is kept as "Song.v3.aes" beside it.
inline std::string pathFor(const std::string& projectPath, int schemaVersion) {
    const std::filesystem::path p(projectPath);
    return (p.parent_path() / (p.stem().string() + ".v" + std::to_string(schemaVersion) + p.extension().string()))
        .string();
}

/**
 * @brief Before a save overwrites @p projectPath, keep it if an older schema wrote it.
 *
 * .bak rotates on every save and history snapshots are pruned, so without this
 * the last file an older Aestra can open is gone after two saves. The copy is
 * made once and never replaced: it is the file as the older version last wrote
 * it. An unreadable file is left alone and the save goes ahead.
 *
 * @return the copy's path when one was made, otherwise empty.
 */
inline std::string keepBeforeSave(const std::string& projectPath, int currentVersion) {
    namespace fs = std::filesystem;
    std::ifstream in(projectPath, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    JSON root;
    try {
        root = JSON::parse(text.str());
    } catch (...) {
        return {};
    }
    if (!root.isObject() || !root.has("version") || !root["version"].isNumber() ||
        !std::isfinite(root["version"].asNumber())) {
        return {};
    }
    const int version = static_cast<int>(root["version"].asNumber());
    if (version <= 0 || version >= currentVersion) return {};

    const std::string copyPath = pathFor(projectPath, version);
    std::error_code ec;
    if (fs::exists(copyPath, ec)) return {};
    fs::copy_file(projectPath, copyPath, fs::copy_options::none, ec);
    if (ec) {
        Log::warning("Pre-upgrade copy failed (non-fatal): " + ec.message());
        return {};
    }
    Log::info("[ProjectSave] Kept the schema v" + std::to_string(version) + " file as " + copyPath +
              " before saving as v" + std::to_string(currentVersion));
    return copyPath;
}

} // namespace ProjectUpgradeCopy
} // namespace Aestra
