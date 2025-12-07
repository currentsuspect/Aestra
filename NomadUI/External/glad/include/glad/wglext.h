// Minimal WGL extension definitions required by Nomad (core-profile context creation & vsync).
// This is a trimmed subset of the official wglext.h from Khronos.
#pragma once

#ifndef __wglext_h_
#define __wglext_h_ 1

#include <windows.h>

/* Context creation */
#define WGL_CONTEXT_MAJOR_VERSION_ARB            0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB            0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB              0x2093
#define WGL_CONTEXT_FLAGS_ARB                    0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB             0x9126

#define WGL_CONTEXT_DEBUG_BIT_ARB                0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB   0x0002

#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB         0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

typedef HGLRC (WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);

/* Swap interval */
typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int interval);

#endif // __wglext_h_