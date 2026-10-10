// © 2025 Aestra Studios â€" All Rights Reserved. Licensed for personal & educational use only.
#include "PlatformUtilsWin32.h"

#include "../../../AestraCore/include/AestraLog.h"
#include "PlatformWindowWin32.h"

#include <shobjidl.h>

#include <cstdio>
#include <thread>

namespace Aestra {

PlatformUtilsWin32::PlatformUtilsWin32() {
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_startTime);
}

PlatformUtilsWin32::~PlatformUtilsWin32() {
    // Clean up window class and icon resources during platform shutdown
    // This is called after all windows have been destroyed (in Platform::shutdown())
    PlatformWindowWin32::unregisterWindowClass();
}

// =============================================================================
// Time
// =============================================================================

double PlatformUtilsWin32::getTime() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - m_startTime.QuadPart) / (double)m_frequency.QuadPart;
}

void PlatformUtilsWin32::sleep(int milliseconds) const {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// =============================================================================
// File Dialogs
// =============================================================================

std::string PlatformUtilsWin32::openFileDialog(const std::string& title, const std::string& filter) const {
    OPENFILENAMEA ofn = {};
    char filename[MAX_PATH] = "";

    // Windows filter format: "Description\0*.ext\0Description2\0*.ext2\0\0"
    // If filter is empty or doesn't contain embedded nulls, use a default
    // The filter string must contain the full null-separated format
    const char* defaultFilter = "All Files\0*.*\0";
    std::string normalizedFilter = filter;
    if (!normalizedFilter.empty() && normalizedFilter.back() != '\0') {
        normalizedFilter.push_back('\0');
    }
    const char* filterPtr = normalizedFilter.empty() ? defaultFilter : normalizedFilter.c_str();

    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterPtr;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }

    return "";
}

std::string PlatformUtilsWin32::saveFileDialog(const SaveFileDialogOptions& options) const {
    OPENFILENAMEA ofn = {};
    char filename[MAX_PATH] = "";

    const char* defaultFilter = "All Files\0*.*\0";
    std::string normalizedFilter = options.filter;
    if (!normalizedFilter.empty() && normalizedFilter.back() != '\0') {
        normalizedFilter.push_back('\0');
    }
    const char* filterPtr = normalizedFilter.empty() ? defaultFilter : normalizedFilter.c_str();

    if (!options.defaultPath.empty()) {
        strncpy_s(filename, options.defaultPath.c_str(), _TRUNCATE);
    }

    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterPtr;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = options.title.c_str();
    ofn.lpstrDefExt = options.defaultExtension.empty() ? nullptr : options.defaultExtension.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }

    return "";
}

namespace {

std::wstring widenUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrowUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string hresultHex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

} // namespace

// The Vista+ common item dialog in folder mode: the same modern Explorer
// window as Open/Save, Unicode throughout. (SHBrowseForFolderA was the XP-era
// tree control and returned ANSI, mangling names outside the system codepage.)
std::string PlatformUtilsWin32::selectFolderDialog(const std::string& title) const {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(init); // S_FALSE (already initialised) also needs balancing
    if (init == RPC_E_CHANGED_MODE) {
        // The thread is already MTA; the shell dialog wants STA and may not show.
        Aestra::Log::warning("[Platform] Folder dialog on an MTA thread; the picker may fail");
    }

    std::string result;
    IFileOpenDialog* dialog = nullptr;
    const HRESULT created =
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(created)) {
        // A failure must not read as the user cancelling: say so.
        Aestra::Log::warning("[Platform] Folder dialog could not be created: " + hresultHex(created));
    } else {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        if (!title.empty()) {
            const std::wstring wideTitle = widenUtf8(title);
            dialog->SetTitle(wideTitle.c_str());
        }
        const HRESULT shown = dialog->Show(GetActiveWindow());
        if (FAILED(shown) && shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            Aestra::Log::warning("[Platform] Folder dialog failed: " + hresultHex(shown));
        }
        if (SUCCEEDED(shown)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = narrowUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (uninit) CoUninitialize();
    return result;
}

std::string PlatformUtilsWin32::getKnownFolderPath(KnownFolder folder) const {
    const KNOWNFOLDERID* id = nullptr;
    switch (folder) {
        case KnownFolder::Desktop: id = &FOLDERID_Desktop; break;
        case KnownFolder::Downloads: id = &FOLDERID_Downloads; break;
        case KnownFolder::Documents: id = &FOLDERID_Documents; break;
        case KnownFolder::Music: id = &FOLDERID_Music; break;
    }
    if (!id) return {};
    PWSTR path = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &path))) {
        result = narrowUtf8(path);
    }
    CoTaskMemFree(path); // required even on failure
    return result;
}

// =============================================================================
// Clipboard
// =============================================================================

void PlatformUtilsWin32::setClipboardText(const std::string& text) const {
    if (!OpenClipboard(nullptr)) {
        return;
    }

    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hMem) {
        char* pMem = (char*)GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, text.c_str(), text.size() + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
    }

    CloseClipboard();
}

std::string PlatformUtilsWin32::getClipboardText() const {
    if (!OpenClipboard(nullptr)) {
        return "";
    }

    std::string result;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData) {
        char* pData = (char*)GlobalLock(hData);
        if (pData) {
            result = std::string(pData);
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
    return result;
}

// =============================================================================
// System Info
// =============================================================================

int PlatformUtilsWin32::getProcessorCount() const {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwNumberOfProcessors;
}

size_t PlatformUtilsWin32::getSystemMemory() const {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return static_cast<size_t>(memInfo.ullTotalPhys);
}

// =============================================================================
// Paths
// =============================================================================

std::string PlatformUtilsWin32::getAppDataPath(const std::string& appName) const {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\" + appName;
    }
    // Fallback to current directory
    return std::string(".");
}

} // namespace Aestra
