#include "InfoTab.h"

#include <string>
#include <atomic>
#include <filesystem>

#include "LicenseVerifier.h"
#include "../../NomadCore/include/NomadLog.h"

// NomadUI includes
#include "../../NomadUI/Core/NUILabel.h"
#include "../../NomadUI/Widgets/NUIButton.h"
#include "../../NomadUI/Core/NUIIcon.h"

using namespace NomadUI;

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
	// Tooltip support not implemented on NUILabel yet - keep text only

	NUILabel serialLabel;
	serialLabel.setText("Serial: " + g_profile.serial);

	NUILabel verifyLabel;
	verifyLabel.setText(verificationBadge(g_profile.verified));

	// Card icon (SVG). In a full integration, load and cache the SVG as a texture once.
	NUIIcon cardIcon;
	// Load SVG for the membership card icon (uses NUIIcon loader)
	try {
		cardIcon.loadSVGFile(g_cardSvgPath);
		
		// Simple accent for verified tiers (placeholder: change icon tint)
		if (g_profile.verified && g_profile.tier != "Nomad Core") {
			// No glow API currently; tint the icon as a subtle verified accent
			cardIcon.setColorFromTheme("accentPrimary");
		}
	} catch (const std::exception& e) {
		// If loading fails, continue without the icon - non-fatal
		Log::warning("Failed to load card icon: " + std::string(e.what()));
	} catch (...) {
		Log::warning("Failed to load card icon: unknown error");
	}

	// Note: Actual rendering requires attaching these components to the current UI
	// panel/container. This function prepares components; hook it into the Info panel
	// layout where panels are constructed.
}

} // namespace Nomad
