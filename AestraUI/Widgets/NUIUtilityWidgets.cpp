// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIUtilityWidgets.h"

#include "NUIRenderer.h"
#include <algorithm>

namespace AestraUI {

DialogBox::DialogBox() = default;

void DialogBox::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void DialogBox::setTitle(const std::string& title)
{
    title_ = title;
    repaint();
}

void DialogBox::setMessage(const std::string& message)
{
    message_ = message;
    repaint();
}

FileBrowser::FileBrowser() = default;

void FileBrowser::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void FileBrowser::setCurrentPath(const std::string& path)
{
    path_ = path;
    repaint();
}

PluginBrowser::PluginBrowser() = default;

void PluginBrowser::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void PluginBrowser::setPlugins(const std::vector<std::string>& plugins)
{
    plugins_ = plugins;
    repaint();
}

SettingsPanel::SettingsPanel() = default;

void SettingsPanel::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void SettingsPanel::setCategories(const std::vector<std::string>& categories)
{
    categories_ = categories;
    repaint();
}

Tooltip::Tooltip() = default;

void Tooltip::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void Tooltip::setText(const std::string& text)
{
    text_ = text;
    repaint();
}

ContextMenu::ContextMenu() = default;

ModalOverlay::ModalOverlay()
    : active_(false)
{
}

void ModalOverlay::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ModalOverlay::setActive(bool active)
{
    active_ = active;
    setVisible(active_);
    repaint();
}

} // namespace AestraUI

