#include "AccountSession.h"
#include "EntitlementStore.h"
#include "LocalAccountCache.h"
#include "MembershipViewModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

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

AccountIdentity identity(std::string userId = "user-123") {
    AccountIdentity value;
    value.userId = std::move(userId);
    value.email = "artist@example.test";
    value.displayName = "Test Artist";
    value.avatarUrl = "";
    return value;
}

fs::path testPath(const std::string& name) {
    const fs::path root = fs::temp_directory_path() / "aestra_membership_view_model_tests";
    fs::create_directories(root);
    return root / (name + ".json");
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

bool featureEnabled(const MembershipViewState& state, const std::string& label) {
    for (const MembershipFeatureRow& row : state.features) {
        if (row.label == label) {
            return row.enabled;
        }
    }
    return false;
}

std::string featureReason(const MembershipViewState& state, const std::string& label) {
    for (const MembershipFeatureRow& row : state.features) {
        if (row.label == label) {
            return row.reason;
        }
    }
    return "";
}

MembershipViewState viewStateFor(const fs::path& path, const EntitlementProfile& profile) {
    LocalAccountCache cache(path);
    EntitlementStore entitlements([profile] { return profile; });
    AccountSession session(cache, entitlements);
    MembershipViewModel viewModel(session, entitlements);
    return viewModel.current();
}

bool testCoreSignedOut() {
    const fs::path path = testPath("core_signed_out");
    fs::remove(path);
    const MembershipViewState state =
        viewStateFor(path, entitlement(MembershipTier::Core, EntitlementStatus::Missing, false, ""));

    bool ok = true;
    ok &= expect(state.tierLabel == "Aestra Core", "Core signed-out tier label");
    ok &= expect(state.statusLabel == "Signed out", "Core signed-out status label");
    ok &= expect(!state.signedIn, "Core signed-out should not be signed in");
    ok &= expect(featureEnabled(state, "Core DAW"), "Core DAW must remain enabled");
    ok &= expect(!featureEnabled(state, "Aestra Rumble"), "Rumble disabled without entitlement");
    ok &= expect(state.detailMessage.find("Core access remains available") != std::string::npos,
                 "Core fallback message should be calm and clear");
    return ok;
}

bool testSupporterDisplay() {
    const fs::path path = testPath("supporter");
    fs::remove(path);
    LocalAccountCache cache(path);
    EntitlementStore entitlements(
        [] { return entitlement(MembershipTier::Supporter, EntitlementStatus::Valid, true, "user-123"); });
    AccountSession session(cache, entitlements);
    session.saveDisplayIdentity(identity());
    MembershipViewModel viewModel(session, entitlements);
    const MembershipViewState state = viewModel.current();

    bool ok = true;
    ok &= expect(state.tierLabel == "Aestra Supporter", "Supporter tier label");
    ok &= expect(state.statusLabel == "Verified", "Supporter status label");
    ok &= expect(state.signedIn, "Supporter cached identity should be signed in");
    ok &= expect(featureEnabled(state, "Aestra Rumble"), "Supporter enables Rumble");
    ok &= expect(!featureEnabled(state, "Rumble Headless"), "Supporter does not enable Rumble Headless");
    ok &= expect(featureEnabled(state, "Supporter Badge"), "Supporter badge enabled");
    return ok;
}

bool testFounderDisplay() {
    const fs::path path = testPath("founder");
    fs::remove(path);
    LocalAccountCache cache(path);
    EntitlementStore entitlements(
        [] { return entitlement(MembershipTier::Founder, EntitlementStatus::Valid, true, "user-123"); });
    AccountSession session(cache, entitlements);
    session.saveDisplayIdentity(identity());
    MembershipViewModel viewModel(session, entitlements);
    const MembershipViewState state = viewModel.current();

    bool ok = true;
    ok &= expect(state.tierLabel == "Aestra Founder", "Founder tier label");
    ok &= expect(featureEnabled(state, "Aestra Rumble"), "Founder enables Rumble");
    ok &= expect(featureEnabled(state, "Rumble Headless"), "Founder enables Rumble Headless");
    ok &= expect(featureEnabled(state, "Founder Badge"), "Founder badge enabled");
    return ok;
}

bool testForgedLocalAccountCacheDoesNotGrantAccess() {
    const fs::path path = testPath("forged");
    fs::remove(path);
    writeText(path, "{\n"
                    "  \"schema_version\": 1,\n"
                    "  \"user_id\": \"mallory\",\n"
                    "  \"email\": \"mallory@example.test\",\n"
                    "  \"display_name\": \"Forged Founder\",\n"
                    "  \"avatar_url\": \"\",\n"
                    "  \"last_sync_unix\": 1234567890,\n"
                    "  \"session_state\": \"signed_in_cached\",\n"
                    "  \"tier\": \"Founder\"\n"
                    "}\n");

    const MembershipViewState state =
        viewStateFor(path, entitlement(MembershipTier::Core, EntitlementStatus::Missing, false, ""));

    bool ok = true;
    ok &= expect(state.tierLabel == "Aestra Core", "forged cache must still display Core");
    ok &= expect(!featureEnabled(state, "Rumble Headless"), "forged cache must not enable Founder features");
    ok &= expect(featureReason(state, "Rumble Headless").find("verified signed membership") != std::string::npos,
                 "forged cache should explain missing signed entitlement");
    return ok;
}

bool testInvalidState(EntitlementStatus status, const std::string& expectedStatusLabel) {
    const fs::path path = testPath("invalid_" + expectedStatusLabel);
    fs::remove(path);
    LocalAccountCache cache(path);
    EntitlementStore entitlements([status] { return entitlement(MembershipTier::Founder, status, false, "user-123"); });
    AccountSession session(cache, entitlements);
    session.saveDisplayIdentity(identity());
    MembershipViewModel viewModel(session, entitlements);
    const MembershipViewState state = viewModel.current();

    bool ok = true;
    ok &= expect(state.tierLabel == "Aestra Core", expectedStatusLabel + " falls back to Core");
    ok &= expect(state.statusLabel == expectedStatusLabel, expectedStatusLabel + " status label");
    ok &= expect(!featureEnabled(state, "Aestra Rumble"), expectedStatusLabel + " does not enable Rumble");
    ok &= expect(featureEnabled(state, "Core DAW"), expectedStatusLabel + " keeps Core DAW enabled");
    return ok;
}

bool testRefreshUnavailable() {
    const fs::path path = testPath("refresh");
    fs::remove(path);
    LocalAccountCache cache(path);
    EntitlementStore entitlements(
        [] { return entitlement(MembershipTier::Supporter, EntitlementStatus::Valid, true, "user-123"); });
    AccountSession session(cache, entitlements);
    session.saveDisplayIdentity(identity());
    session.refreshAsync();

    MembershipViewModel viewModel(session, entitlements);
    const MembershipViewState state = viewModel.current();

    bool ok = true;
    ok &= expect(state.statusLabel == "Sync unavailable", "refresh unavailable status");
    ok &= expect(state.tierLabel == "Aestra Supporter", "sync unavailable preserves signed local tier");
    ok &= expect(featureEnabled(state, "Aestra Rumble"), "sync unavailable does not remove verified local access");
    ok &= expect(state.detailMessage.find("Sync is unavailable") != std::string::npos,
                 "sync unavailable detail message");
    return ok;
}

bool testMembershipDisplaySummary() {
    MembershipViewState state;
    state.accountLabel = "artist@example.test";
    state.tierLabel = "Aestra Founder";
    state.statusLabel = "Verified";
    state.detailMessage = "Membership is verified locally.";
    state.verified = true;
    state.offline = true;
    state.features.push_back({"Core DAW", true, ""});
    state.features.push_back({"Rumble Headless", true, ""});

    const std::string summary = membershipDisplaySummary(state, "Synced now.");
    bool ok = true;
    ok &= expect(summary.find("Account: artist@example.test") != std::string::npos,
                 "summary includes account label");
    ok &= expect(summary.find("Tier: Aestra Founder") != std::string::npos, "summary includes tier");
    ok &= expect(summary.find("Status: Verified") != std::string::npos, "summary includes status");
    ok &= expect(summary.find("Last refresh: Synced now.") != std::string::npos, "summary includes refresh state");
    ok &= expect(summary.find("Available: Rumble Headless") != std::string::npos,
                 "summary includes feature rows");
    return ok;
}

bool testMembershipBadgeMapping() {
    bool ok = true;

    MembershipViewState founder;
    founder.tierLabel = "Aestra Founder";
    founder.statusLabel = "Verified";
    founder.verified = true;
    ok &= expect(membershipBadgeTierText(founder) == "Founder", "badge maps Founder tier");
    ok &= expect(membershipBadgeStatusText(founder) == "Verified", "badge maps verified status");

    MembershipViewState supporterOffline;
    supporterOffline.tierLabel = "Aestra Supporter";
    supporterOffline.statusLabel = "Cached offline";
    supporterOffline.verified = true;
    ok &= expect(membershipBadgeTierText(supporterOffline) == "Supporter", "badge maps Supporter tier");
    ok &= expect(membershipBadgeStatusText(supporterOffline) == "Offline", "badge compacts cached offline status");

    MembershipViewState signedOut;
    signedOut.tierLabel = "Aestra Core";
    signedOut.statusLabel = "Signed out";
    ok &= expect(membershipBadgeTierText(signedOut) == "Core", "badge maps Core tier");
    ok &= expect(membershipBadgeStatusText(signedOut) == "Signed out", "badge maps signed-out status");
    return ok;
}
} // namespace

int main() {
    fs::remove_all(fs::temp_directory_path() / "aestra_membership_view_model_tests");
    bool ok = true;
    ok &= testCoreSignedOut();
    ok &= testSupporterDisplay();
    ok &= testFounderDisplay();
    ok &= testForgedLocalAccountCacheDoesNotGrantAccess();
    ok &= testInvalidState(EntitlementStatus::Expired, "Expired");
    ok &= testInvalidState(EntitlementStatus::InvalidSignature, "Invalid");
    ok &= testInvalidState(EntitlementStatus::Revoked, "Revoked");
    ok &= testInvalidState(EntitlementStatus::WrongDevice, "Wrong device");
    ok &= testInvalidState(EntitlementStatus::ParseError, "Unreadable cache");
    ok &= testRefreshUnavailable();
    ok &= testMembershipDisplaySummary();
    ok &= testMembershipBadgeMapping();
    fs::remove_all(fs::temp_directory_path() / "aestra_membership_view_model_tests");

    if (!ok) {
        return 1;
    }
    std::cout << "MembershipViewModel tests passed.\n";
    return 0;
}
