// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "StartupErrorDialog.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Aestra {

void showStartupError(const std::string& title, const std::string& message) {
#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
#else
    std::cerr << title << ": " << message << std::endl;
#endif
}

} // namespace Aestra
