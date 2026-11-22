// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: the theme. Every colour the browser keeps (its own members, the search
// field, its icons, the collection tags) is read in applyThemeColors(), which the constructor and a theme change both run.
// The rendered list cache is redrawn by the render cache itself when the theme generation moves on (RenderCacheTheme.h);
// the item text caches are dropped here.
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

void FileBrowser::applyThemeColors() {
    auto& themeManager = NUIThemeManager::getInstance();
    backgroundColor_ = themeManager.getColor("backgroundPrimary");
    textColor_ = themeManager.getColor("textPrimary");
    selectedColor_ = themeManager.getColor("accentText"); // the current crumb is accent text
    hoverColor_ = themeManager.getColor("buttonBgHover").withAlpha(0.72f);

    if (searchInput_) {
        searchInput_->setTextColor(textColor_);
        searchInput_->setPlaceholderColor(themeManager.getColor("textSecondary").withAlpha(0.56f));
        searchInput_->setBackgroundColor(themeManager.getColor("backgroundPrimary"));
        searchInput_->setBorderColor(themeManager.getColor("borderSubtle").withAlpha(0.62f));
        searchInput_->setFocusedBorderColor(themeManager.getColor("focusRing"));
    }
    if (m_searchIcon) m_searchIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.72f));
    if (chevronIcon_) chevronIcon_->setColor(themeManager.getColor("textSecondary"));
}

void FileBrowser::onThemeChanged(const NUIThemeProperties& theme) {
    applyThemeColors();
    invalidateAllItemCaches(); // cached row text was laid out with the old colours
    invalidateCache();         // the list is redrawn on the next frame (the render cache would also see the new generation)
    NUIComponent::onThemeChanged(theme);
}

// The first collection ("Purple") takes the theme's accent text colour, so it follows the theme instead of a violet
// literal; the rest are fixed identity colours.
NUIColor FileBrowser::collectionColor(const std::string& name) const {
    static constexpr uint32_t kPalette[] = {0x000000, 0xf97316, 0x22c55e, 0x3b82f6,
                                            0xec4899, 0x14b8a6, 0xeab308, 0xef4444};
    const auto it = std::find(collections_.begin(), collections_.end(), name);
    if (it == collections_.end()) return NUIColor(0.42f, 0.42f, 0.42f, 1.0f); // a plain tag
    const size_t index = static_cast<size_t>(it - collections_.begin());
    const size_t slot = index % (sizeof(kPalette) / sizeof(kPalette[0]));
    if (slot == 0) return NUIThemeManager::getInstance().getColor("accentText");
    return NUIColor::fromHex(kPalette[slot]);
}

} // namespace AestraUI
