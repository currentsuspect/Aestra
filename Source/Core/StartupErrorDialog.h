// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>

namespace Aestra {

// Tells the user why Aestra cannot start when it fails before its own UI exists (for example, no OpenGL 3.3
// driver). Windows shows a message box; elsewhere the message goes to stderr, next to the log.
void showStartupError(const std::string& title, const std::string& message);

} // namespace Aestra
