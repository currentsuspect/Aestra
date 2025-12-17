#include "PlatformUtilsLinux.h"
#include <unistd.h>
#include <sys/sysinfo.h>
#include <pwd.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <time.h>
#include <SDL2/SDL.h>

namespace Nomad {

double PlatformUtilsLinux::getTime() const {
    return (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
}

void PlatformUtilsLinux::sleep(int milliseconds) const {
    SDL_Delay(milliseconds);
}

std::string PlatformUtilsLinux::openFileDialog(const std::string& title, const std::string& filter) const {
    std::cerr << "Linux File Dialog not fully implemented. Returning empty string." << std::endl;
    return ""; 
}

std::string PlatformUtilsLinux::saveFileDialog(const std::string& title, const std::string& filter) const {
    std::cerr << "Linux File Dialog not fully implemented. Returning empty string." << std::endl;
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
        std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    }
    
    return path.string();
}

} // namespace Nomad
