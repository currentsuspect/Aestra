// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §3.1: follow-playhead is the USER's choice, and focus changes must not overwrite it.
//
// Owner report: "Every time I turn it off, still come back to it being on." AestraContent
// forced it on at every Timeline focus change and at startup (setFollowPlayhead(true) in
// setViewFocus), so no choice survived. It now only SUSPENDS following on Arsenal/Audition
// focus; the choice is persisted through FD-23's store by the app.
//
// This pins TrackManagerUI's side of that contract. (The focus transitions live in
// AestraContent, which a headless test cannot construct; it no longer calls
// setFollowPlayhead at all, so they cannot reach the choice.)

#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

struct Fixture {
    std::shared_ptr<TrackManager> trackManager = std::make_shared<TrackManager>();
    std::shared_ptr<TrackManagerUI> ui = std::make_shared<TrackManagerUI>(trackManager);
    int notifications = 0;
    bool lastEnabled = false;
    TrackManagerUI::FollowMode lastMode = TrackManagerUI::FollowMode::Page;

    Fixture() {
        ui->setOnFollowPreferenceChanged([this](bool enabled, TrackManagerUI::FollowMode mode) {
            ++notifications;
            lastEnabled = enabled;
            lastMode = mode;
        });
    }
};

void testDefaultIsOff() {
    Fixture f;
    check(!f.ui->isFollowPlayhead(), "follow-playhead defaults to off");
    check(!f.ui->isFollowActive(), "and so nothing follows");
}

// The report: switching focus must never turn following back on.
void testSuspendingNeverChangesTheChoice() {
    Fixture f;
    f.ui->setFollowSuspended(true);  // e.g. entering Arsenal
    f.ui->setFollowSuspended(false); // back to Timeline
    check(!f.ui->isFollowPlayhead(), "a focus round-trip leaves an OFF choice off");
    check(f.notifications == 0, "and is not a preference change");

    f.ui->setFollowPlayhead(true);
    f.ui->setFollowSuspended(true);
    check(f.ui->isFollowPlayhead(), "suspending keeps an ON choice");
    check(!f.ui->isFollowActive(), "while suspended, nothing follows");
    f.ui->setFollowSuspended(false);
    check(f.ui->isFollowActive(), "resuming restores following");
}

void testUserChangesNotifyOnce() {
    Fixture f;
    f.ui->setFollowPlayhead(true);
    check(f.notifications == 1 && f.lastEnabled, "turning it on is one persisted change");
    f.ui->setFollowPlayhead(true);
    check(f.notifications == 1, "re-asserting the same value is not a change");
    f.ui->setFollowMode(TrackManagerUI::FollowMode::Continuous);
    check(f.notifications == 2 && f.lastMode == TrackManagerUI::FollowMode::Continuous,
          "choosing Continuous is persisted too");
}

void testApplyingAStoredPreferenceDoesNotEcho() {
    Fixture f;
    f.ui->applyFollowPreference(true, TrackManagerUI::FollowMode::Continuous);
    check(f.ui->isFollowPlayhead() && f.ui->getFollowMode() == TrackManagerUI::FollowMode::Continuous,
          "a stored preference is restored at startup");
    check(f.notifications == 0, "and restoring it is not written straight back");
}

} // namespace

int main() {
    testDefaultIsOff();
    testSuspendingNeverChangesTheChoice();
    testUserChangesNotifyOnce();
    testApplyingAStoredPreferenceDoesNotEcho();

    if (g_failures == 0) {
        std::cout << "Timeline follow preference tests passed\n";
        return 0;
    }
    std::cout << g_failures << " timeline follow preference test(s) failed\n";
    return 1;
}
