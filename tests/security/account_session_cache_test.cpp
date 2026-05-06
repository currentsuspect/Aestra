#include "AccountSession.h"
#include "EntitlementStore.h"
#include "LocalAccountCache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Aestra::License;
namespace fs = std::filesystem;

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
        return false;
    }
    return true;
}

EntitlementProfile entitlement(MembershipTier tier, EntitlementStatus status, bool verified, std::string userId) {
    EntitlementProfile profile;
    profile.tier = tier;
    profile.status = status;
    profile.verified = verified;
    profile.offline = true;
    profile.userId = std::move(userId);
    profile.licenseId = "license-test";
    profile.issuedAt = std::chrono::system_clock::now() - std::chrono::hours(1);
    profile.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    return profile;
}

AccountIdentity identity() {
    AccountIdentity value;
    value.userId = "user-123";
    value.email = "artist@example.test";
    value.displayName = "Test Artist";
    value.avatarUrl = "https://example.test/avatar.png";
    return value;
}

fs::path testPath(const std::string& name) {
    const fs::path root = fs::temp_directory_path() / "aestra_account_session_tests";
    fs::create_directories(root);
    return root / (name + ".json");
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string joinTokenName(const char* left, const char* right) {
    return std::string(left) + "_" + right;
}

bool testMissingAndMalformedCache() {
    bool ok = true;

    const fs::path missingPath = testPath("missing");
    fs::remove(missingPath);
    LocalAccountCache missingCache(missingPath);
    const auto missing = missingCache.load();
    ok &= expect(missing.status == LocalAccountCacheLoadStatus::Missing, "missing cache should report Missing");

    EntitlementStore coreEntitlements(
        [] { return entitlement(MembershipTier::Core, EntitlementStatus::Missing, false, ""); });
    AccountSession missingSession(missingCache, coreEntitlements);
    const auto missingSnapshot = missingSession.current();
    ok &= expect(missingSnapshot.state == AccountSessionState::SignedOut, "missing cache session should be SignedOut");
    ok &= expect(!missingSnapshot.signedIn, "missing cache should not be signed in");

    const fs::path malformedPath = testPath("malformed");
    writeText(malformedPath, "{ invalid json");
    LocalAccountCache malformedCache(malformedPath);
    const auto malformed = malformedCache.load();
    ok &= expect(malformed.status == LocalAccountCacheLoadStatus::Malformed, "malformed cache should report Malformed");

    AccountSession malformedSession(malformedCache, coreEntitlements);
    const auto malformedSnapshot = malformedSession.current();
    ok &= expect(malformedSnapshot.state == AccountSessionState::Invalid, "malformed session should be Invalid");
    ok &= expect(!malformedSnapshot.signedIn, "malformed cache should not be signed in");
    ok &= expect(malformedSnapshot.entitlement.tier == MembershipTier::Core,
                 "malformed cache must show Core entitlement");

    return ok;
}

bool testIdentityRoundTripAndSignOut() {
    bool ok = true;

    const fs::path path = testPath("roundtrip");
    fs::remove(path);
    LocalAccountCache cache(path);

    LocalAccountRecord record;
    record.identity = identity();
    record.state = AccountSessionState::SignedInCached;
    record.lastSyncUnix = 1234567890;
    record.hasIdentity = true;
    ok &= expect(cache.save(record), "account cache save should succeed");

    const std::string serialized = readText(path);
    ok &= expect(serialized.find(joinTokenName("refresh", "token")) == std::string::npos,
                 "cache must not contain refresh credential fields");
    ok &= expect(serialized.find(joinTokenName("access", "token")) == std::string::npos,
                 "cache must not contain access credential fields");
    ok &= expect(serialized.find(std::string("j") + "wt") == std::string::npos,
                 "cache must not contain bearer credential fields");

    const auto loaded = cache.load();
    ok &= expect(loaded.status == LocalAccountCacheLoadStatus::Loaded, "saved cache should load");
    ok &= expect(loaded.record.identity.userId == record.identity.userId, "userId should roundtrip");
    ok &= expect(loaded.record.identity.email == record.identity.email, "email should roundtrip");
    ok &= expect(loaded.record.identity.displayName == record.identity.displayName, "displayName should roundtrip");
    ok &= expect(loaded.record.identity.avatarUrl == record.identity.avatarUrl, "avatarUrl should roundtrip");

    EntitlementStore entitlements(
        [] { return entitlement(MembershipTier::Supporter, EntitlementStatus::Valid, true, "user-123"); });
    AccountSession session(cache, entitlements);
    session.signOut();
    const auto afterSignOut = cache.load();
    ok &= expect(afterSignOut.status == LocalAccountCacheLoadStatus::Missing, "signOut should clear account cache");
    ok &= expect(session.current().state == AccountSessionState::SignedOut, "signOut session should be SignedOut");

    return ok;
}

bool testSessionUsesEntitlementStore() {
    bool ok = true;

    const fs::path path = testPath("supporter");
    fs::remove(path);
    LocalAccountCache cache(path);

    EntitlementStore entitlements(
        [] { return entitlement(MembershipTier::Supporter, EntitlementStatus::Valid, true, "user-123"); });
    AccountSession session(cache, entitlements);
    ok &= expect(session.saveDisplayIdentity(identity()), "saveDisplayIdentity should persist identity");

    const auto snapshot = session.current();
    ok &= expect(snapshot.signedIn, "cached identity should be signed in");
    ok &= expect(snapshot.state == AccountSessionState::SignedInFresh, "recent cache should be SignedInFresh");
    ok &= expect(snapshot.identity.email == "artist@example.test", "snapshot should expose display identity");
    ok &= expect(snapshot.entitlement.tier == MembershipTier::Supporter, "snapshot should expose trusted entitlement");
    ok &= expect(entitlements.canAccess(ProductFeature::Rumble), "Rumble access should remain entitlement-backed");

    return ok;
}

bool testMismatchedUserAndForgedCacheBoundary() {
    bool ok = true;

    const fs::path mismatchPath = testPath("mismatch");
    fs::remove(mismatchPath);
    LocalAccountCache mismatchCache(mismatchPath);
    LocalAccountRecord mismatchRecord;
    mismatchRecord.identity = identity();
    mismatchRecord.state = AccountSessionState::SignedInCached;
    mismatchRecord.lastSyncUnix = 1234567890;
    mismatchRecord.hasIdentity = true;
    ok &= expect(mismatchCache.save(mismatchRecord), "mismatch cache save should succeed");

    EntitlementStore founderForOtherUser(
        [] { return entitlement(MembershipTier::Founder, EntitlementStatus::Valid, true, "different-user"); });
    AccountSession mismatchSession(mismatchCache, founderForOtherUser);
    const auto mismatchSnapshot = mismatchSession.current();
    ok &= expect(mismatchSnapshot.state == AccountSessionState::Invalid, "user mismatch should mark session Invalid");
    ok &= expect(!mismatchSnapshot.signedIn, "user mismatch should not be signed in");
    ok &= expect(founderForOtherUser.canAccess(ProductFeature::RumbleHeadless),
                 "access remains EntitlementStore-backed, not cache-backed");

    const fs::path forgedPath = testPath("forged");
    fs::remove(forgedPath);
    writeText(forgedPath, "{\n"
                          "  \"schema_version\": 1,\n"
                          "  \"user_id\": \"mallory\",\n"
                          "  \"email\": \"mallory@example.test\",\n"
                          "  \"display_name\": \"Forged Founder\",\n"
                          "  \"avatar_url\": \"\",\n"
                          "  \"last_sync_unix\": 1234567890,\n"
                          "  \"session_state\": \"signed_in_cached\",\n"
                          "  \"tier\": \"Founder\"\n"
                          "}\n");
    LocalAccountCache forgedCache(forgedPath);
    EntitlementStore coreOnly([] { return entitlement(MembershipTier::Core, EntitlementStatus::Missing, false, ""); });
    AccountSession forgedSession(forgedCache, coreOnly);
    const auto forgedSnapshot = forgedSession.current();
    ok &= expect(forgedSnapshot.signedIn, "forged display identity may still load as cached identity");
    ok &= expect(forgedSnapshot.entitlement.tier == MembershipTier::Core, "forged account cache must not elevate tier");
    ok &= expect(!coreOnly.canAccess(ProductFeature::RumbleHeadless), "forged cache must not grant Founder features");
    ok &= expect(coreOnly.canAccess(ProductFeature::CoreDAW), "CoreDAW remains accessible");

    return ok;
}
} // namespace

int main() {
    fs::remove_all(fs::temp_directory_path() / "aestra_account_session_tests");
    bool ok = true;
    ok &= testMissingAndMalformedCache();
    ok &= testIdentityRoundTripAndSignOut();
    ok &= testSessionUsesEntitlementStore();
    ok &= testMismatchedUserAndForgedCacheBoundary();
    fs::remove_all(fs::temp_directory_path() / "aestra_account_session_tests");

    if (!ok) {
        return 1;
    }
    std::cout << "Account session cache tests passed.\n";
    return 0;
}
