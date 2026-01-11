// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"

#include <string>

namespace NomadUI {

/**
 * @brief Mixer strip footer: track number label.
 */
class UIMixerFooter : public NUIComponent {
public:
    UIMixerFooter();

    void onRender(NUIRenderer& renderer) override;

    void setTrackNumber(int number);

    std::function<void()> onInvalidateRequested;

protected:
    int getTrackNumber() const { return m_trackNumber; }

private:
    int m_trackNumber{0};
    std::string m_cachedText;

    // Cached theme colors
    NUIColor m_textSecondary;

    void cacheThemeColors();
};

} // namespace NomadUI
