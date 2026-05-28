#include "PlatformUtilsLinux.h"

#include "../../../AestraCore/include/AestraLog.h"
#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>
#include <array>
#include <cstdio>

namespace {

std::string trimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

} // namespace

namespace Aestra {

namespace {

std::string runDialogCommand(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status != 0) {
        return "";
    }

    return trimTrailingNewlines(output);
}

} // namespace

double PlatformUtilsLinux::getTime() const {
    return (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
}

void PlatformUtilsLinux::sleep(int milliseconds) const {
    SDL_Delay(milliseconds);
}

std::string PlatformUtilsLinux::openFileDialog(const std::string& title, const std::string& filter) const {
    const std::string escapedTitle = shellEscape(title.empty() ? "Open File" : title);

    std::string path = runDialogCommand(
        "command -v zenity >/dev/null 2>&1 && zenity --file-selection --title=" + escapedTitle);
    if (!path.empty()) return path;

    path = runDialogCommand(
        "command -v qarma >/dev/null 2>&1 && qarma --file-selection --title=" + escapedTitle);
    if (!path.empty()) return path;

    path = runDialogCommand("command -v kdialog >/dev/null 2>&1 && kdialog --getopenfilename ~");
    if (!path.empty()) return path;

    return "";
}

std::string PlatformUtilsLinux::saveFileDialog(const SaveFileDialogOptions& options) const {
    const std::string escapedTitle = shellEscape(options.title.empty() ? "Save File" : options.title);
    const std::string filenameArg = options.defaultPath.empty() ? "" : (" --filename=" + shellEscape(options.defaultPath));

    std::string path = runDialogCommand("command -v zenity >/dev/null 2>&1 && zenity --file-selection --save --confirm-overwrite --title=" +
                                        escapedTitle + filenameArg);
    if (!path.empty()) return path;

    path = runDialogCommand("command -v qarma >/dev/null 2>&1 && qarma --file-selection --save --confirm-overwrite --title=" +
                            escapedTitle + filenameArg);
    if (!path.empty()) return path;

    const std::string kdialogDefault = options.defaultPath.empty() ? "~" : shellEscape(options.defaultPath);
    path = runDialogCommand("command -v kdialog >/dev/null 2>&1 && kdialog --getsavefilename " + kdialogDefault);
    if (!path.empty()) return path;

    return "";
}

std::string PlatformUtilsLinux::selectFolderDialog(const std::string& title) const {
    AESTRA_LOG_WARNING("Linux Folder Dialog not fully implemented. Returning empty string.");
    return "";
}

void PlatformUtilsLinux::setClipboardText(const std::string& text) const {
    SDL_SetClipboardText(text.c_str());
}

std::string PlatformUtilsLinux::getClipboardText() const {
    if (SDL_HasClipboardText()) {
        char* text = SDL_GetClipboardText();
        std::string result(text);
        SDL_free(text);
        return result;
    }
    return "";
}

std::string PlatformUtilsLinux::getPlatformName() const {
    return "Linux";
}

int PlatformUtilsLinux::getProcessorCount() const {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

size_t PlatformUtilsLinux::getSystemMemory() const {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
    return 0;
}

std::string PlatformUtilsLinux::getAppDataPath(const std::string& appName) const {
    // Sanitize appName to prevent path traversal
    if (appName.empty() || appName.find("..") != std::string::npos || appName.find('/') != std::string::npos ||
        appName.find('\\') != std::string::npos) {
        AESTRA_LOG_ERROR("Invalid app name for getAppDataPath: " + appName);
        return "";
    }

    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path path;
    if (xdg && *xdg) {
        path = std::filesystem::path(xdg);
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            path = std::filesystem::path(home) / ".local" / "share";
        } else {
            AESTRA_LOG_WARNING("Could not determine app data path - HOME and XDG_DATA_HOME not set");
            return "";
        }
    }

    path /= appName;

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        AESTRA_LOG_ERROR("Failed to create app data directory: " + ec.message());
        return "";
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

    return path.string();
}

} // namespace Aestra
