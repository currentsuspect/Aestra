#include "LocalAccountCache.h"

#include "AestraJSON.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace Aestra {
namespace License {

namespace {
constexpr int kSchemaVersion = 1;

std::filesystem::path defaultDataDir() {
    if (const char* envDir = std::getenv("AESTRA_DATA_DIR")) {
        if (*envDir != '\0') {
            return std::filesystem::path(envDir);
        }
    }

#ifdef _WIN32
    if (const char* appData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(appData) / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return std::filesystem::path(xdg) / "AESTRA";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#endif
}

std::filesystem::path defaultCachePath() {
    if (const char* envPath = std::getenv("AESTRA_ACCOUNT_CACHE_PATH")) {
        if (*envPath != '\0') {
            return std::filesystem::path(envPath);
        }
    }
    return defaultDataDir() / "account" / "session.json";
}

bool readString(const Aestra::JSON& json, const std::string& key, std::string& out) {
    if (!json.has(key) || !json[key].isString()) {
        return false;
    }
    out = json[key].asString();
    return true;
}

bool readOptionalString(const Aestra::JSON& json, const std::string& key, std::string& out) {
    if (!json.has(key)) {
        out.clear();
        return true;
    }
    if (!json[key].isString()) {
        return false;
    }
    out = json[key].asString();
    return true;
}

std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}
} // namespace

std::string accountSessionStateToString(AccountSessionState state) {
    switch (state) {
    case AccountSessionState::SignedInCached:
        return "signed_in_cached";
    case AccountSessionState::SignedInFresh:
        return "signed_in_fresh";
    case AccountSessionState::Expired:
        return "expired";
    case AccountSessionState::Invalid:
        return "invalid";
    case AccountSessionState::SyncUnavailable:
        return "sync_unavailable";
    case AccountSessionState::SignedOut:
    default:
        return "signed_out";
    }
}

bool accountSessionStateFromString(const std::string& value, AccountSessionState& state) {
    if (value == "signed_out") {
        state = AccountSessionState::SignedOut;
        return true;
    }
    if (value == "signed_in_cached") {
        state = AccountSessionState::SignedInCached;
        return true;
    }
    if (value == "signed_in_fresh") {
        state = AccountSessionState::SignedInFresh;
        return true;
    }
    if (value == "expired") {
        state = AccountSessionState::Expired;
        return true;
    }
    if (value == "invalid") {
        state = AccountSessionState::Invalid;
        return true;
    }
    if (value == "sync_unavailable") {
        state = AccountSessionState::SyncUnavailable;
        return true;
    }
    return false;
}

LocalAccountCache::LocalAccountCache() : m_cachePath(defaultCachePath()) {}

LocalAccountCache::LocalAccountCache(std::filesystem::path cachePath) : m_cachePath(std::move(cachePath)) {}

LocalAccountCacheLoadResult LocalAccountCache::load() const {
    LocalAccountCacheLoadResult result;
    std::ifstream file(m_cachePath, std::ios::binary);
    if (!file.good()) {
        result.status = LocalAccountCacheLoadStatus::Missing;
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    try {
        Aestra::JSON json = Aestra::JSON::parse(buffer.str());
        if (!json.isObject()) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }
        if (!json.has("schema_version") || !json["schema_version"].isNumber() ||
            static_cast<int>(json["schema_version"].asNumber()) != kSchemaVersion) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }

        LocalAccountRecord record;
        if (!readString(json, "user_id", record.identity.userId) || record.identity.userId.empty()) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }
        if (!readOptionalString(json, "email", record.identity.email) ||
            !readOptionalString(json, "display_name", record.identity.displayName) ||
            !readOptionalString(json, "avatar_url", record.identity.avatarUrl) ||
            !readOptionalString(json, "session_token", record.sessionToken)) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }
        if (!json.has("last_sync_unix") || !json["last_sync_unix"].isNumber()) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }
        record.lastSyncUnix = static_cast<int64_t>(json["last_sync_unix"].asNumber());

        if (!json.has("session_state") || !json["session_state"].isString() ||
            !accountSessionStateFromString(json["session_state"].asString(), record.state)) {
            result.status = LocalAccountCacheLoadStatus::Malformed;
            return result;
        }

        record.hasIdentity = true;
        result.status = LocalAccountCacheLoadStatus::Loaded;
        result.record = std::move(record);
        return result;
    } catch (const std::exception&) {
        result.status = LocalAccountCacheLoadStatus::Malformed;
        return result;
    }
}

bool LocalAccountCache::save(const LocalAccountRecord& record) const {
    if (!record.hasIdentity || record.identity.userId.empty()) {
        return false;
    }

    try {
        std::filesystem::create_directories(m_cachePath.parent_path());

        std::ofstream file(m_cachePath, std::ios::binary | std::ios::trunc);
        if (!file.good()) {
            return false;
        }
        file << "{\n"
             << "  \"schema_version\": " << kSchemaVersion << ",\n"
             << "  \"user_id\": \"" << escapeJsonString(record.identity.userId) << "\",\n"
             << "  \"email\": \"" << escapeJsonString(record.identity.email) << "\",\n"
             << "  \"display_name\": \"" << escapeJsonString(record.identity.displayName) << "\",\n"
             << "  \"avatar_url\": \"" << escapeJsonString(record.identity.avatarUrl) << "\",\n"
             << "  \"session_token\": \"" << escapeJsonString(record.sessionToken) << "\",\n"
             << "  \"last_sync_unix\": " << record.lastSyncUnix << ",\n"
             << "  \"session_state\": \"" << accountSessionStateToString(record.state) << "\"\n"
             << "}\n";
        return file.good();
    } catch (const std::exception&) {
        return false;
    }
}

bool LocalAccountCache::clear() const {
    std::error_code ec;
    if (!std::filesystem::exists(m_cachePath, ec)) {
        return true;
    }
    return std::filesystem::remove(m_cachePath, ec) && !ec;
}

} // namespace License
} // namespace Aestra
