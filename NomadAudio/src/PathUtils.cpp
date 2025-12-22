// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PathUtils.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace Nomad {
namespace Audio {

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    
#ifdef _WIN32
    int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (utf8Len <= 0) return {};
    
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.c_str(), static_cast<int>(wide.size()),
        utf8.data(), utf8Len, nullptr, nullptr
    );
    return utf8;
#else
    // On Unix-like systems, wstring is typically UTF-32; do simple narrowing
    // (Real code should use proper iconv or similar)
    std::string result;
    result.reserve(wide.size());
    for (wchar_t wc : wide) {
        if (wc < 0x80) {
            result.push_back(static_cast<char>(wc));
        } else {
            result.push_back('?'); // Placeholder for non-ASCII
        }
    }
    return result;
#endif
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.c_str(), static_cast<int>(utf8.size()),
        nullptr, 0
    );
    if (wideLen <= 0) return {};
    
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.c_str(), static_cast<int>(utf8.size()),
        wide.data(), wideLen
    );
    return wide;
#else
    std::wstring result;
    result.reserve(utf8.size());
    for (char c : utf8) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return result;
#endif
}

std::string pathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    return wideToUtf8(p.wstring());
#else
    return p.string();
#endif
}

std::wstring pathStringToWide(const std::string& path) {
    if (path.empty()) return {};
    
#ifdef _WIN32
    // First, try interpreting as UTF-8
    int wideLen = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,  // Fail on invalid UTF-8
        path.c_str(), static_cast<int>(path.size()),
        nullptr, 0
    );
    
    if (wideLen > 0) {
        std::wstring wide(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0,
            path.c_str(), static_cast<int>(path.size()),
            wide.data(), wideLen
        );
        return wide;
    }
    
    // UTF-8 failed - input is likely ANSI (system code page)
    // This handles paths from legacy Windows APIs or certain drag-drop scenarios
    wideLen = MultiByteToWideChar(
        CP_ACP, 0,  // ANSI code page
        path.c_str(), static_cast<int>(path.size()),
        nullptr, 0
    );
    
    if (wideLen > 0) {
        std::wstring wide(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(
            CP_ACP, 0,
            path.c_str(), static_cast<int>(path.size()),
            wide.data(), wideLen
        );
        return wide;
    }
    
    // Last resort: assume ASCII-compatible and widen directly
    return std::wstring(path.begin(), path.end());
#else
    return utf8ToWide(path);
#endif
}

std::filesystem::path makeUnicodePath(const std::string& pathStr) {
#ifdef _WIN32
    return std::filesystem::path(pathStringToWide(pathStr));
#else
    return std::filesystem::path(pathStr);
#endif
}

} // namespace Audio
} // namespace Nomad
