// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "NUICoreWidgets.h"
#include <functional>
#include <string>
#include <vector>

namespace NomadUI {

class DialogBox : public NUIComponent {
public:
    DialogBox();

    void onRender(NUIRenderer& renderer) override;

    void setTitle(const std::string& title);
    void setMessage(const std::string& message);
    const std::string& getTitle() const { return title_; }
    const std::string& getMessage() const { return message_; }

private:
    std::string title_;
    std::string message_;
};

class FileBrowser : public NUIComponent {
public:
    FileBrowser();

    void onRender(NUIRenderer& renderer) override;

    void setCurrentPath(const std::string& path);
    const std::string& getCurrentPath() const { return path_; }

private:
    std::string path_;
};

class PluginBrowser : public NUIComponent {
public:
    PluginBrowser();

    void onRender(NUIRenderer& renderer) override;

    void setPlugins(const std::vector<std::string>& plugins);
    const std::vector<std::string>& getPlugins() const { return plugins_; }

private:
    std::vector<std::string> plugins_;
};

class SettingsPanel : public NUIComponent {
public:
    SettingsPanel();

    void onRender(NUIRenderer& renderer) override;

    void setCategories(const std::vector<std::string>& categories);
    const std::vector<std::string>& getCategories() const { return categories_; }

private:
    std::vector<std::string> categories_;
};

class Tooltip : public NUIComponent {
public:
    Tooltip();

    void onRender(NUIRenderer& renderer) override;

    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

private:
    std::string text_;
};

class NotificationToast : public NUIComponent {
public:
    NotificationToast();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;

    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void setDuration(double duration);

private:
    std::string text_;
    double duration_;
    double elapsed_;
};

class ContextMenu : public NUIPopupMenu {
public:
    ContextMenu();
};

class ModalOverlay : public NUIComponent {
public:
    ModalOverlay();

    void onRender(NUIRenderer& renderer) override;

    void setActive(bool active);
    bool isActive() const { return active_; }

private:
    bool active_;
};

} // namespace NomadUI

