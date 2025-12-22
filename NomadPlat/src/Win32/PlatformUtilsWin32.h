// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../../include/NomadPlatform.h"
#include "WinHeaders.h"

namespace Nomad {

class PlatformUtilsWin32 : public IPlatformUtils {
public:
    PlatformUtilsWin32();
    ~PlatformUtilsWin32() override;

    // Time
    double getTime() const override;
    void sleep(int milliseconds) const override;

    // File dialogs
    std::string openFileDialog(const std::string& title, const std::string& filter) const override;
    std::string saveFileDialog(const std::string& title, const std::string& filter) const override;
    std::string selectFolderDialog(const std::string& title) const override;

    // Clipboard
    void setClipboardText(const std::string& text) const override;
    std::string getClipboardText() const override;

    // System info
    std::string getPlatformName() const override { return "Windows"; }
    int getProcessorCount() const override;
    size_t getSystemMemory() const override;
    
    // Paths
    std::string getAppDataPath(const std::string& appName) const override;

private:
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_startTime;
};

} // namespace Nomad
