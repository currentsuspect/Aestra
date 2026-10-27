// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Opening a plugin's module in the sandbox helper: dlopen on Linux and macOS, LoadLibrary on Windows (V82-H8). The
// helper's CLAP host was POSIX-only because it called dlopen directly, so on Windows no CLAP plugin could load at all.

#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace AestraHelper {

/// Opens the module at @p utf8Path (the app sends paths as UTF-8). Null when it cannot be loaded.
inline void* openLibrary(const std::string& utf8Path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wide.data(), n);
    // The module's own folder first for its dependencies: a plugin's DLLs sit next to it, not next to the helper.
    return reinterpret_cast<void*>(LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
#else
    return dlopen(utf8Path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

inline void* librarySymbol(void* library, const char* name) {
    if (!library)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

inline void closeLibrary(void* library) {
    if (!library)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

} // namespace AestraHelper
