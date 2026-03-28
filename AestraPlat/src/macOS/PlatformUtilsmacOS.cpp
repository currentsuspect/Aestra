#include "PlatformUtilsmacOS.h"

#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace Aestra {

// Static initialization for mach timebase
static mach_timebase_info_data_t s_timebaseInfo;
static bool s_timebaseInitialized = false;

static void initTimebase() {
    if (!s_timebaseInitialized) {
        mach_timebase_info(&s_timebaseInfo);
        s_timebaseInitialized = true;
    }
}

double PlatformUtilsmacOS::getTime() const {
    initTimebase();
    uint64_t time = mach_absolute_time();
    // Convert to nanoseconds, then to seconds
    double nanoseconds = static_cast<double>(time * s_timebaseInfo.numer) / s_timebaseInfo.denom;
    return nanoseconds / 1e9;
}

void PlatformUtilsmacOS::sleep(int milliseconds) const {
    SDL_Delay(milliseconds);
}

std::string PlatformUtilsmacOS::openFileDialog(const std::string& title, const std::string& filter) const {
    // TODO: Implement native macOS file dialog using Cocoa
    // For now, return empty string (SDL doesn't provide file dialogs)
    std::cerr << "macOS File Dialog not fully implemented. Returning empty string." << std::endl;
    return "";
}

std::string PlatformUtilsmacOS::saveFileDialog(const std::string& title, const std::string& filter) const {
    // TODO: Implement native macOS file dialog using Cocoa
    std::cerr << "macOS Save Dialog not fully implemented. Returning empty string." << std::endl;
    return "";
}

std::string PlatformUtilsmacOS::selectFolderDialog(const std::string& title) const {
    // TODO: Implement native macOS folder dialog using Cocoa
    std::cerr << "macOS Folder Dialog not fully implemented. Returning empty string." << std::endl;
    return "";
}

void PlatformUtilsmacOS::setClipboardText(const std::string& text) const {
    SDL_SetClipboardText(text.c_str());
}

std::string PlatformUtilsmacOS::getClipboardText() const {
    if (SDL_HasClipboardText()) {
        char* text = SDL_GetClipboardText();
        std::string result(text);
        SDL_free(text);
        return result;
    }
    return "";
}

std::string PlatformUtilsmacOS::getPlatformName() const {
    return "macOS";
}

int PlatformUtilsmacOS::getProcessorCount() const {
    int count;
    size_t size = sizeof(count);
    sysctlbyname("hw.logicalcpu", &count, &size, nullptr, 0);
    return count > 0 ? count : 1;
}

size_t PlatformUtilsmacOS::getSystemMemory() const {
    int64_t memSize;
    size_t size = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &size, nullptr, 0) == 0) {
        return static_cast<size_t>(memSize);
    }
    return 0;
}

std::string PlatformUtilsmacOS::getAppDataPath(const std::string& appName) const {
    // macOS: ~/Library/Application Support/<appName>
    const char* home = std::getenv("HOME");
    std::filesystem::path path;
    
    if (home && *home) {
        path = std::filesystem::path(home) / "Library" / "Application Support" / appName;
    } else {
        // Fallback to temp
        path = std::filesystem::path("/tmp") / appName;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path, ec);
        // Set appropriate permissions
        std::filesystem::permissions(path, 
            std::filesystem::perms::owner_read | 
            std::filesystem::perms::owner_write | 
            std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace, ec);
    }

    return path.string();
}

} // namespace Aestra
