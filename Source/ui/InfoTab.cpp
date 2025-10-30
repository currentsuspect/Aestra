#include "InfoTab.h"

#include <string>
#include <atomic>
#include <filesystem>

#include "LicenseVerifier.h"

// NomadUI includes
#include "NUILabel.h"
#include "NUIButton.h"
#include "NUIIcon.h"

namespace Nomad {

static std::atomic<bool> g_profileLoaded{false};
static UserProfile g_profile;
static std::string g_cardSvgPath;

static std::string getAssetsCardsDir() {
	// Use mock assets location in public builds
	// cards live in nomad-core/assets_mock/cards by default
	return std::string("nomad-core/assets_mock/cards");
}

static std::string svgForTier(const std::string& tier) {
	if (tier == "Nomad Founder") return getAssetsCardsDir() + "/founder_card.svg";
	if (tier == "Nomad Studio+") return getAssetsCardsDir() + "/studio_card.svg";
	if (tier == "Nomad Campus")  return getAssetsCardsDir() + "/campus_card.svg";
	return getAssetsCardsDir() + "/core_card.svg";
}

static const char* tooltipForTier(const std::string& tier) {
	if (tier == "Nomad Founder") return "Founding Access — Where it all began.";
	if (tier == "Nomad Studio+") return "Full Suite Access — Muse Integration Active.";
	if (tier == "Nomad Campus")  return "Educational / Enterprise Edition.";
	return "Essential Access — Free Tier.";
}

static const char* verificationBadge(bool verified) {
	return verified ? "✅ Verified" : "⚪ Offline / ❌ Unverified";
}

static void ensureProfileLoadedOnce() {
	if (g_profileLoaded.load()) return;
	g_profile = loadProfile();
	verifyLicense(g_profile);
	g_cardSvgPath = svgForTier(g_profile.tier);
	g_profileLoaded.store(true);
}

void RenderInfoTab() {
	ensureProfileLoadedOnce();

	// Minimal placeholder UI wiring using NomadUI components. Real layout integration
	// should add these elements to the active UI tree/panel.
	NUILabel usernameLabel;
	usernameLabel.setText("User: " + g_profile.username);

	NUILabel tierLabel;
	tierLabel.setText("Access: " + g_profile.tier);
	tierLabel.setTooltip(tooltipForTier(g_profile.tier));

	NUILabel serialLabel;
	serialLabel.setText("Serial: " + g_profile.serial);

	NUILabel verifyLabel;
	verifyLabel.setText(verificationBadge(g_profile.verified));

	// Card icon (SVG). In a full integration, load and cache the SVG as a texture once.
	NUIIcon cardIcon;
	cardIcon.setSVGPath(g_cardSvgPath);

	// Simple accent for verified tiers (placeholder: change icon tint)
	if (g_profile.verified && g_profile.tier != "Nomad Core") {
		cardIcon.setGlow(true); // assume this toggles a subtle glow animation if available
	}

	// Note: Actual rendering requires attaching these components to the current UI
	// panel/container. This function prepares components; hook it into the Info panel
	// layout where panels are constructed.
}

} // namespace Nomad
