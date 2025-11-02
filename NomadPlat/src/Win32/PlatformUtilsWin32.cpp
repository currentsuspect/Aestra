// Â© 2025 Nomad Studios â€" All Rights Reserved. Licensed for personal & educational use only.
#include "PlatformUtilsWin32.h"
#include "PlatformWindowWin32.h"
#include "../../../NomadCore/include/NomadLog.h"
#include <commdlg.h>
#include <shlobj.h>
#include <thread>

namespace Nomad {

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

    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter.empty() ? "All Files\0*.*\0" : filter.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }

    return "";
}

std::string PlatformUtilsWin32::saveFileDialog(const std::string& title, const std::string& filter) const {
    OPENFILENAMEA ofn = {};
    char filename[MAX_PATH] = "";

    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter.empty() ? "All Files\0*.*\0" : filter.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }

    return "";
}

std::string PlatformUtilsWin32::selectFolderDialog(const std::string& title) const {
    BROWSEINFOA bi = {};
    char path[MAX_PATH];

    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }

    return "";
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

} // namespace Nomad
