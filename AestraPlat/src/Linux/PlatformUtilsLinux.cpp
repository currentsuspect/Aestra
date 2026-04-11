#include "PlatformUtilsLinux.h"

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

/**
 * @brief Removes trailing newline and carriage-return characters from a string.
 *
 * @param value Input string from which trailing '\n' and '\r' characters will be removed.
 * @return std::string The input string with all trailing '\n' and '\r' characters removed.
 */
std::string trimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

} // namespace

namespace Aestra {

/**
 * @brief Produce a shell-safe single-quoted string from arbitrary input.
 *
 * Wraps the input in single quotes and escapes any embedded single quotes so
 * the result can be used safely in a POSIX shell command.
 *
 * @param input The original string to quote.
 * @return std::string The input wrapped in single quotes with embedded `'` characters escaped as `'\''`.
 */
std::string shellEscape(const std::string& input) {
    std::string escaped = "'";
    for (char c : input) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

namespace {

/**
 * @brief Executes a shell command and returns its standard output with trailing newlines removed.
 *
 * @param command Shell command to execute.
 * @return std::string Captured standard output with trailing `\n` and `\r` removed; returns an empty string if the command could not be started or exited with a non-zero status.
 */
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

} /**
 * @brief Retrieves a monotonic high-resolution time value.
 *
 * The value is suitable for measuring elapsed time but is not tied to any wall-clock epoch.
 *
 * @return double Time in seconds since an unspecified starting point. 
 */

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
    std::cerr << "Linux Folder Dialog not fully implemented. Returning empty string." << std::endl;
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
