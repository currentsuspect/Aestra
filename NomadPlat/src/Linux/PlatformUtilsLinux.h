#pragma once
#include "../../include/NomadPlatform.h"
#include <string>

namespace Nomad {

class PlatformUtilsLinux : public IPlatformUtils {
public:
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
    std::string getPlatformName() const override;
    int getProcessorCount() const override;
    size_t getSystemMemory() const override;

    // Paths
    std::string getAppDataPath(const std::string& appName) const override;
};

} // namespace Nomad
