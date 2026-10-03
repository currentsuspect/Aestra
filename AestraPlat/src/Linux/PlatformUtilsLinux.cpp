#include "PlatformUtilsLinux.h"
#include "XdgPortalFileChooser.h"

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

// Zenity 4 can otherwise negotiate its minimum file-chooser size on some
// Wayland compositors, clipping the file list and action row into a tiny
// top-left window. These remain hints: the compositor may constrain them on
// smaller displays.
constexpr const char* ZENITY_FILE_DIALOG_PRESENTATION = " --modal --width=760 --height=560";

// True when @p tool is on PATH. Checked separately from running it, because a
// picker's non-zero exit means "cancelled", and must not read as "missing".
bool toolAvailable(const char* tool) {
    const std::string probe = std::string("command -v ") + tool + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

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

// Every picker below BLOCKS until the user answers; callers run them off the
// UI thread (see AestraPlat/include/AestraFileDialog.h).
//
// Order: the XDG Desktop Portal (the desktop's own dialog, no Aestra-side
// knowledge of which one), then the first installed legacy picker. A cancel
// is final at every stage: only "this picker does not exist here" moves on.
std::string PlatformUtilsLinux::openFileDialog(const std::string& title, const std::string& filter) const {
    const std::string dialogTitle = title.empty() ? "Open File" : title;
    if (auto picked = XdgPortal::openFile(dialogTitle, XdgPortal::parseWin32Filter(filter))) {
        return *picked;
    }

    const std::string escapedTitle = shellEscape(dialogTitle);
    if (toolAvailable("zenity")) {
        return runDialogCommand("zenity --file-selection --title=" + escapedTitle + ZENITY_FILE_DIALOG_PRESENTATION);
    }
    if (toolAvailable("qarma")) {
        return runDialogCommand("qarma --file-selection --title=" + escapedTitle);
    }
    if (toolAvailable("kdialog")) {
        return runDialogCommand("kdialog --getopenfilename ~");
    }
    std::cerr << "[Platform] No file chooser available (no XDG portal, zenity, qarma or kdialog)" << std::endl;
    return "";
}

std::string PlatformUtilsLinux::saveFileDialog(const SaveFileDialogOptions& options) const {
    const std::string dialogTitle = options.title.empty() ? "Save File" : options.title;
    if (auto picked =
            XdgPortal::saveFile(dialogTitle, XdgPortal::parseWin32Filter(options.filter), options.defaultPath)) {
        return *picked;
    }

    const std::string escapedTitle = shellEscape(dialogTitle);
    const std::string filenameArg = options.defaultPath.empty() ? "" : (" --filename=" + shellEscape(options.defaultPath));
    if (toolAvailable("zenity")) {
        return runDialogCommand("zenity --file-selection --save --confirm-overwrite --title=" + escapedTitle +
                                filenameArg + ZENITY_FILE_DIALOG_PRESENTATION);
    }
    if (toolAvailable("qarma")) {
        return runDialogCommand("qarma --file-selection --save --confirm-overwrite --title=" + escapedTitle +
                                filenameArg);
    }
    if (toolAvailable("kdialog")) {
        const std::string kdialogDefault = options.defaultPath.empty() ? "~" : shellEscape(options.defaultPath);
        return runDialogCommand("kdialog --getsavefilename " + kdialogDefault);
    }
    std::cerr << "[Platform] No file chooser available (no XDG portal, zenity, qarma or kdialog)" << std::endl;
    return "";
}

std::string PlatformUtilsLinux::selectFolderDialog(const std::string& title) const {
    const std::string dialogTitle = title.empty() ? "Choose Folder" : title;
    if (auto picked = XdgPortal::selectFolder(dialogTitle)) {
        return *picked;
    }

    const std::string escapedTitle = shellEscape(dialogTitle);
    if (toolAvailable("zenity")) {
        return runDialogCommand("zenity --file-selection --directory --title=" + escapedTitle +
                                ZENITY_FILE_DIALOG_PRESENTATION);
    }
    if (toolAvailable("qarma")) {
        return runDialogCommand("qarma --file-selection --directory --title=" + escapedTitle);
    }
    if (toolAvailable("kdialog")) {
        return runDialogCommand("kdialog --getexistingdirectory ~");
    }
    std::cerr << "[Platform] No folder chooser available (no XDG portal, zenity, qarma or kdialog)" << std::endl;
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
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path path;
    if (xdg && *xdg) {
        path = std::filesystem::path(xdg);
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            path = std::filesystem::path(home) / ".local" / "share";
        } else {
            return "/tmp/" + appName;
        }
    }

    path /= appName;

    std::error_code ec;
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path, ec);
        std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                     ec);
    }

    return path.string();
}

} // namespace Aestra
