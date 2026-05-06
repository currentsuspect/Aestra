#pragma once

#include "AccountSession.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Aestra {
namespace License {

struct LocalAccountRecord {
    AccountIdentity identity;
    AccountSessionState state = AccountSessionState::SignedOut;
    int64_t lastSyncUnix = 0;
    bool hasIdentity = false;
};

enum class LocalAccountCacheLoadStatus {
    Missing = 0,
    Loaded,
    Malformed,
};

struct LocalAccountCacheLoadResult {
    LocalAccountCacheLoadStatus status = LocalAccountCacheLoadStatus::Missing;
    LocalAccountRecord record;
};

class LocalAccountCache {
public:
    LocalAccountCache();
    explicit LocalAccountCache(std::filesystem::path cachePath);

    LocalAccountCacheLoadResult load() const;
    bool save(const LocalAccountRecord& record) const;
    bool clear() const;
    const std::filesystem::path& path() const { return m_cachePath; }

private:
    std::filesystem::path m_cachePath;
};

std::string accountSessionStateToString(AccountSessionState state);
bool accountSessionStateFromString(const std::string& value, AccountSessionState& state);

} // namespace License
} // namespace Aestra
