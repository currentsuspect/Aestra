// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraJSONFile.h"

#include "AestraFile.h"
#include "AestraPlatform.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace Aestra {

std::string resolveAppDataFilePath(const std::string& appName, const std::string& fileName) {
    IPlatformUtils* utils = Platform::getUtils();
    if (utils == nullptr) {
        return {};
    }

    const std::string appDataDir = utils->getAppDataPath(appName);
    if (appDataDir.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(appDataDir, ec);
    if (ec && !std::filesystem::exists(appDataDir)) {
        return {};
    }

    return (std::filesystem::path(appDataDir) / fileName).string();
}

std::optional<JSON> readJSONStrict(const std::string& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    std::ifstream in(path);
    if (!in) {
        return std::nullopt; // Missing or unreadable: not an error, just "no file yet".
    }

    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (contents.empty()) {
        return std::nullopt;
    }

    bool consumedAll = false;
    JSON root = JSON::parseStrict(contents, consumedAll);
    if (!consumedAll) {
        return std::nullopt; // Malformed or trailing garbage: same "unusable" bucket as missing.
    }
    return root;
}

bool writeJSONAtomic(const std::string& path, const JSON& root) {
    if (path.empty()) {
        return false;
    }

    const std::string jsonStr = root.toString(2);
    const std::string tmpPath = path + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
        if (!Aestra::syncOfstream(out, tmpPath)) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    std::error_code ec;
#ifdef _WIN32
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
    }
    ec.clear();
#endif
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

#ifndef _WIN32
    if (!Aestra::fsyncParentDirectory(path)) {
        return false;
    }
#endif

    return true;
}

} // namespace Aestra
