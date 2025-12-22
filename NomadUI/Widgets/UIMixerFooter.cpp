// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerFooter.h"

#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"

namespace NomadUI {

UIMixerFooter::UIMixerFooter()
{
    cacheThemeColors();
}

void UIMixerFooter::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_textSecondary = theme.getColor("textSecondary");
}

void UIMixerFooter::setTrackNumber(int number)
{
    if (m_trackNumber == number) return;
    m_trackNumber = number;
    m_cachedText = (m_trackNumber > 0) ? std::to_string(m_trackNumber) : std::string();
    repaint();
}

void UIMixerFooter::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;

    if (m_cachedText.empty()) return;

    renderer.drawTextCentered(m_cachedText, b, 9.0f, m_textSecondary);
}

} // namespace NomadUI
