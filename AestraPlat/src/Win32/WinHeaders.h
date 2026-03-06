// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Windows header quarantine - ONLY include this file inside AestraPlat/src/Win32/
// This ensures Windows headers are never included outside the Windows platform implementation.

#ifndef _WIN32
#error "WinHeaders.h may only be included in Windows platform implementation files"
#endif

// Prevent Windows.h from polluting global namespace
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Include Windows headers
#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>

// Additional Windows headers as needed (only add what's actually used)
// #include <dwmapi.h>
// #include <mmdeviceapi.h>
// #include <audioclient.h>
