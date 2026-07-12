// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "LocalAccountCache.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include "../Support/TestTempDirectory.h"

using Aestra::License::AccountSessionState;
using Aestra::License::LocalAccountCache;
using Aestra::License::LocalAccountCacheLoadStatus;
using Aestra::License::LocalAccountRecord;

namespace {

std::filesystem::path testCachePath() {
    return Aestra::Tests::makeUniqueTempDirectory("LocalAccountCache") / "account_cache.json";
}

LocalAccountRecord validRecord() {
    LocalAccountRecord record;
    record.identity.userId = "user-123";
    record.identity.email = "user@example.test";
    record.identity.displayName = "Test User";
    record.identity.avatarUrl = "https://example.test/avatar.png";
    record.sessionToken = "session-token";
    record.lastSyncUnix = 123456789;
    record.state = AccountSessionState::SignedInFresh;
    record.hasIdentity = true;
    return record;
}

void testRoundTrip() {
    const std::filesystem::path path = testCachePath();
    LocalAccountCache cache(path);
    assert(cache.save(validRecord()));

    const auto loaded = cache.load();
    assert(loaded.status == LocalAccountCacheLoadStatus::Loaded);
    assert(loaded.record.identity.userId == "user-123");
    assert(cache.clear());
}

void testOversizedCacheLoadIsMalformed() {
    const std::filesystem::path path = testCachePath();
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << std::string(64u * 1024u + 1u, 'x');
    }

    LocalAccountCache cache(path);
    const auto loaded = cache.load();
    assert(loaded.status == LocalAccountCacheLoadStatus::Malformed);
    assert(cache.clear());
}

void testOversizedCacheSaveIsRejected() {
    const std::filesystem::path path = testCachePath();
    LocalAccountCache cache(path);
    LocalAccountRecord record = validRecord();
    record.sessionToken = std::string(64u * 1024u, 'x');

    assert(!cache.save(record));
    assert(!std::filesystem::exists(path));
}

} // namespace

int main() {
    testRoundTrip();
    testOversizedCacheLoadIsMalformed();
    testOversizedCacheSaveIsRejected();
    return 0;
}
