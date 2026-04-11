#pragma once
#include "../../include/AestraPlatform.h"

#include <string>

/**
 * Escape a string so it can be safely used within a POSIX shell command.
 *
 * @param input The raw string to escape.
 * @returns The escaped string, quoted or backslash-escaped as needed so the result can be embedded safely in a shell command line.
 */
namespace Aestra {

std::string shellEscape(const std::string& input);

class PlatformUtilsLinux : public IPlatformUtils {
public:
    double getTime() const override;
    void sleep(int milliseconds) const override;

    // File dialogs
    std::string openFileDialog(const std::string& title, const std::string& filter) const override;
    std::string saveFileDialog(const SaveFileDialogOptions& options) const override;
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

} // namespace Aestra
