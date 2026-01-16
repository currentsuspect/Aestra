// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file PathUtils.h
 * @brief Cross-platform Unicode path handling utilities
 * 
 * This header provides robust UTF-8 <-> UTF-16 path conversion for Windows,
 * avoiding the deprecated and unreliable std::wstring_convert/codecvt approach.
 * 
 * ============================================================================
 * THE UNICODE PATH LAW FOR NOMAD:
 * ============================================================================
 * 
 * 1. INTERNAL STORAGE: Always use std::filesystem::path for file paths
 * 2. FOR LOGGING/UI: Convert to UTF-8 using pathToUtf8()
 * 3. FOR WIN32 APIs: Use path.native() or path.wstring() directly
 * 4. FOR DECODERS: Use pathToWide() for APIs expecting wchar_t*, 
 *                  or path.string() only for ASCII-only codecs
 * 
 * NEVER use raw std::string for file paths in core audio/file code on Windows!
 * ============================================================================
 */

#include <string>
#include <filesystem>

namespace Nomad {
namespace Audio {

/**
 * @brief Convert a wide string (UTF-16 on Windows) to UTF-8
 * 
 * Uses Win32 WideCharToMultiByte for reliable conversion.
 * On non-Windows platforms, assumes input is already UTF-8 compatible.
 * 
 * @param wide Wide string to convert
 * @return UTF-8 encoded string
 */
std::string wideToUtf8(const std::wstring& wide);

/**
 * @brief Convert a UTF-8 string to wide string (UTF-16 on Windows)
 * 
 * Uses Win32 MultiByteToWideChar for reliable conversion.
 * 
 * @param utf8 UTF-8 encoded string
 * @return Wide string (UTF-16 on Windows)
 */
std::wstring utf8ToWide(const std::string& utf8);

/**
 * @brief Convert a std::filesystem::path to UTF-8 for logging/display
 * 
 * This is the SAFE way to get a string from a path for logging purposes.
 * On Windows, this properly handles Unicode characters like "Beyoncé", "日本語", etc.
 * 
 * @param p Filesystem path
 * @return UTF-8 encoded string representation
 */
std::string pathToUtf8(const std::filesystem::path& p);

/**
 * @brief Convert a raw string path to wide string for Windows APIs
 * 
 * This handles the case where you receive a path as std::string from external
 * sources (drag-drop, command line, etc.). On Windows, if the string contains
 * non-ASCII characters, it's likely in the system's ANSI code page or already UTF-8.
 * 
 * We try UTF-8 first; if that fails or produces invalid results, fall back to
 * treating it as ANSI (system code page).
 * 
 * @param path String path (may be UTF-8 or ANSI on Windows)
 * @return Wide string suitable for Windows APIs
 */
std::wstring pathStringToWide(const std::string& path);

/**
 * @brief Get a std::filesystem::path from a raw string, handling Unicode properly
 * 
 * On Windows, this creates a path that internally stores UTF-16, ensuring
 * proper handling of non-ASCII characters regardless of the input encoding.
 * 
 * @param pathStr String path (may be UTF-8 or ANSI on Windows)
 * @return std::filesystem::path with proper Unicode handling
 */
std::filesystem::path makeUnicodePath(const std::string& pathStr);

} // namespace Audio
} // namespace Nomad
