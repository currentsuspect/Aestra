// Â© 2025 Nomad Studios â€" All Rights Reserved. Licensed for personal & educational use only.
#include "PlatformWindowWin32.h"
#include "PlatformDPIWin32.h"
#include "../../../NomadCore/include/NomadLog.h"
#include "../../../NomadCore/include/NomadAssert.h"
#include "../../../Source/resource.h"
#include <windowsx.h>

namespace Nomad {

// Static members
const wchar_t* PlatformWindowWin32::WINDOW_CLASS_NAME = L"NomadWindowClass";
bool PlatformWindowWin32::s_classRegistered = false;
HICON PlatformWindowWin32::s_hLargeIcon = nullptr;
HICON PlatformWindowWin32::s_hSmallIcon = nullptr;

// =============================================================================
// Constructor / Destructor
// =============================================================================

PlatformWindowWin32::PlatformWindowWin32()
    : m_hwnd(nullptr)
    , m_hdc(nullptr)
    , m_hglrc(nullptr)
    , m_width(0)
    , m_height(0)
    , m_shouldClose(false)
    , m_isFullscreen(false)
    , m_styleBackup(0)
    , m_dpiScale(1.0f)
{
    m_wpPrev = {};
    m_wpPrev.length = sizeof(WINDOWPLACEMENT);
}

PlatformWindowWin32::~PlatformWindowWin32() {
    destroy();
}

// =============================================================================
// Window Creation
// =============================================================================

bool PlatformWindowWin32::create(const WindowDesc& desc) {
    m_title = desc.title;
    m_width = desc.width;
    m_height = desc.height;

    // Register window class
    if (!registerWindowClass()) {
        NOMAD_LOG_ERROR("Failed to register window class");
        return false;
    }

    // Calculate window size including borders
    DWORD style = WS_OVERLAPPEDWINDOW; // Use standard window style for proper taskbar behavior
    if (!desc.resizable) {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }
    if (!desc.decorated) {
        // Use WS_POPUP for truly borderless window that works with taskbar
        style = WS_POPUP;
    }

    RECT rect = { 0, 0, m_width, m_height };
    AdjustWindowRect(&rect, style, FALSE);

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // Calculate position
    int x = desc.x;
    int y = desc.y;
    if (x == -1 || y == -1) {
        // Center on screen
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = (screenWidth - windowWidth) / 2;
        y = (screenHeight - windowHeight) / 2;
    }

    // Convert title to wide string
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, m_title.c_str(), -1, nullptr, 0);
    wchar_t* wideTitle = new wchar_t[titleLen];
    MultiByteToWideChar(CP_UTF8, 0, m_title.c_str(), -1, wideTitle, titleLen);

    // Create window
    DWORD exStyle = 0;
    if (!desc.decorated) {
        // Use WS_EX_APPWINDOW to make it appear in taskbar and Alt+Tab
        exStyle = WS_EX_APPWINDOW;
    }
    
    m_hwnd = CreateWindowExW(
        exStyle,
        WINDOW_CLASS_NAME,
        wideTitle,
        style,
        x, y,
        windowWidth, windowHeight,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        this  // Pass 'this' pointer to WM_CREATE
    );

    delete[] wideTitle;

    if (!m_hwnd) {
        NOMAD_LOG_ERROR("Failed to create window");
        return false;
    }

    // Explicitly set the window icons via WM_SETICON. Load icons here so
    // they are in scope; some shells prefer icons set on the window itself
    // over the class icons for taskbar and Alt+Tab rendering.
    HINSTANCE hInstLocal = GetModuleHandle(nullptr);
    HICON hIconBig = (HICON)LoadImageW(hInstLocal, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInstLocal, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    if (hIconBig) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        DestroyIcon(hIconBig);
    }
    if (hIconSmall) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        DestroyIcon(hIconSmall);
    }

    // Get device context
    m_hdc = GetDC(m_hwnd);
    if (!m_hdc) {
        NOMAD_LOG_ERROR("Failed to get device context");
        destroy();
        return false;
    }

    // Setup pixel format for OpenGL
    if (!setupPixelFormat()) {
        NOMAD_LOG_ERROR("Failed to setup pixel format");
        destroy();
        return false;
    }

    // Get initial DPI scale
    m_dpiScale = PlatformDPI::getDPIScale(m_hwnd);
    NOMAD_LOG_INFO("Window DPI scale: " + std::to_string(m_dpiScale));

    // Show window
    if (desc.startMaximized) {
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    } else if (desc.startFullscreen) {
        setFullscreen(true);
    } else {
        ShowWindow(m_hwnd, SW_SHOW);
    }
    UpdateWindow(m_hwnd);

    NOMAD_LOG_INFO("Window created successfully");
    return true;
}

void PlatformWindowWin32::destroy() {
    if (m_hglrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }

    if (m_hdc) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// =============================================================================
// Window Class Registration
// =============================================================================

bool PlatformWindowWin32::registerWindowClass() {
    if (s_classRegistered) {
        return true;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Try to load the application icon from resources. We use the resource
    // name "IDI_APP_ICON" which is defined in Source/app_icon.rc. Load both
    // large and small icons so Alt+Tab and the task switcher use the correct
    // images.
    HINSTANCE hInst = wc.hInstance;
    // Load icons by resource ID. Request explicit sizes so the OS uses appropriately-
    // sized bitmaps for Alt+Tab (large) and taskbar/title (small). If the
    // resource isn't present, fall back to the default application icon.
    s_hLargeIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    s_hSmallIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    if (s_hLargeIcon) wc.hIcon = s_hLargeIcon; else wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (s_hSmallIcon) wc.hIconSm = s_hSmallIcon; else wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    s_classRegistered = true;
    return true;
}

void PlatformWindowWin32::unregisterWindowClass() {
    if (s_classRegistered) {
        UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandle(nullptr));
        
        // Destroy the loaded icons
        if (s_hLargeIcon) {
            DestroyIcon(s_hLargeIcon);
            s_hLargeIcon = nullptr;
        }
        if (s_hSmallIcon) {
            DestroyIcon(s_hSmallIcon);
            s_hSmallIcon = nullptr;
        }
        
        s_classRegistered = false;
    }
}

// =============================================================================
// OpenGL Context
// =============================================================================

bool PlatformWindowWin32::setupPixelFormat() {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (pixelFormat == 0) {
        return false;
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) {
        return false;
    }

    return true;
}

bool PlatformWindowWin32::createGLContext() {
    if (m_hglrc) {
        return true;  // Already created
    }

    m_hglrc = wglCreateContext(m_hdc);
    if (!m_hglrc) {
        NOMAD_LOG_ERROR("Failed to create OpenGL context");
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        NOMAD_LOG_ERROR("Failed to make OpenGL context current");
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
        return false;
    }

    NOMAD_LOG_INFO("OpenGL context created successfully");
    return true;
}

bool PlatformWindowWin32::makeContextCurrent() {
    if (!m_hglrc) {
        return false;
    }
    return wglMakeCurrent(m_hdc, m_hglrc) == TRUE;
}

void PlatformWindowWin32::setVSync(bool enabled) {
    // Try to get wglSwapIntervalEXT
    typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = 
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    
    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(enabled ? 1 : 0);
    }
}

// =============================================================================
// Event Processing
// =============================================================================

bool PlatformWindowWin32::pollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !m_shouldClose;
}

void PlatformWindowWin32::swapBuffers() {
    if (m_hdc) {
        SwapBuffers(m_hdc);
    }
}

// =============================================================================
// Window Procedure
// =============================================================================

LRESULT CALLBACK PlatformWindowWin32::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PlatformWindowWin32* window = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<PlatformWindowWin32*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<PlatformWindowWin32*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->handleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PlatformWindowWin32::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST: {
            // For borderless windows, we need to handle hit testing manually
            // This allows the window to be dragged and resized
            DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
            if (!(style & WS_CAPTION)) {
                // Borderless window - enable dragging from the top area
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(m_hwnd, &pt);
                
                // Don't allow resizing when maximized
                if (!isMaximized()) {
                    // Handle resize borders (8 pixel border)
                    const int borderSize = 8;
                    RECT rect;
                    GetClientRect(m_hwnd, &rect);
                    
                    bool onLeft = pt.x < borderSize;
                    bool onRight = pt.x >= rect.right - borderSize;
                    bool onTop = pt.y < borderSize;
                    bool onBottom = pt.y >= rect.bottom - borderSize;
                    
                    // Corners
                    if (onTop && onLeft) return HTTOPLEFT;
                    if (onTop && onRight) return HTTOPRIGHT;
                    if (onBottom && onLeft) return HTBOTTOMLEFT;
                    if (onBottom && onRight) return HTBOTTOMRIGHT;
                    
                    // Edges
                    if (onLeft) return HTLEFT;
                    if (onRight) return HTRIGHT;
                    if (onTop) return HTTOP;
                    if (onBottom) return HTBOTTOM;
                }
                
                // Top 32 pixels are the title bar drag area
                // BUT exclude the right 150 pixels for window control buttons
                if (pt.y >= 0 && pt.y < 32 && pt.x < m_width - 150) {
                    return HTCAPTION;  // Allow dragging
                }
                
                return HTCLIENT;
            }
            break;
        }
        
        case WM_NCPAINT: {
            // For borderless windows (WS_POPUP), don't paint the non-client area
            DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
            if (!(style & WS_CAPTION)) {
                // Don't paint the non-client area (no title bar)
                return 0;
            }
            break;
        }
        
        case WM_SYSCOMMAND: {
            // Handle minimize/restore commands properly for borderless windows
            DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
            if (!(style & WS_CAPTION)) {
                // Borderless window (WS_POPUP) - handle minimize/restore manually
                if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                    // Handle minimize command
                    ShowWindow(m_hwnd, SW_MINIMIZE);
                    return 0;
                }
                else if ((wParam & 0xFFF0) == SC_RESTORE) {
                    // Handle restore command
                    ShowWindow(m_hwnd, SW_RESTORE);
                    return 0;
                }
                else if ((wParam & 0xFFF0) == SC_MAXIMIZE) {
                    // Custom maximize logic (existing code)
                    MONITORINFO mi = { sizeof(mi) };
                    GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
                    SetWindowPos(m_hwnd, nullptr,
                        mi.rcWork.left,
                        mi.rcWork.top,
                        mi.rcWork.right - mi.rcWork.left,
                        mi.rcWork.bottom - mi.rcWork.top,
                        SWP_NOZORDER | SWP_FRAMECHANGED);
                    return 0;
                }
            }
            break;
        }
        
        case WM_CLOSE:
            m_shouldClose = true;
            if (m_closeCallback) {
                m_closeCallback();
            }
            return 0;

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            bool sizeChanged = (width != m_width || height != m_height);
            bool minimizedEvent = (wParam == SIZE_MINIMIZED);
            bool restoredEvent = (wParam == SIZE_RESTORED);

            if (sizeChanged) {
                m_width = width;
                m_height = height;
            }

            // Notify resize callback even when minimized/restored so renderers can
            // flush caches when the window hides/shows without a size delta.
            if (m_resizeCallback && (sizeChanged || minimizedEvent || restoredEvent)) {
                m_resizeCallback(width, height);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (m_mouseMoveCallback) {
                m_mouseMoveCallback(x, y);
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            MouseButton button = MouseButton::Left;
            if (msg == WM_RBUTTONDOWN) button = MouseButton::Right;
            else if (msg == WM_MBUTTONDOWN) button = MouseButton::Middle;
            
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (m_mouseButtonCallback) {
                m_mouseButtonCallback(button, true, x, y);
            }
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            MouseButton button = MouseButton::Left;
            if (msg == WM_RBUTTONUP) button = MouseButton::Right;
            else if (msg == WM_MBUTTONUP) button = MouseButton::Middle;
            
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (m_mouseButtonCallback) {
                m_mouseButtonCallback(button, false, x, y);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            if (m_mouseWheelCallback) {
                m_mouseWheelCallback(delta);
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            KeyCode key = translateKeyCode(wParam, lParam);
            KeyModifiers mods = getKeyModifiers();
            if (m_keyCallback) {
                m_keyCallback(key, true, mods);
            }
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            KeyCode key = translateKeyCode(wParam, lParam);
            KeyModifiers mods = getKeyModifiers();
            if (m_keyCallback) {
                m_keyCallback(key, false, mods);
            }
            return 0;
        }

        case WM_CHAR: {
            if (m_charCallback) {
                m_charCallback(static_cast<unsigned int>(wParam));
            }
            return 0;
        }

        case WM_SETFOCUS:
            if (m_focusCallback) {
                m_focusCallback(true);
            }
            return 0;

        case WM_KILLFOCUS:
            if (m_focusCallback) {
                m_focusCallback(false);
            }
            return 0;

        case WM_DPICHANGED: {
            // Update DPI scale
            float oldScale = m_dpiScale;
            m_dpiScale = PlatformDPI::getDPIScale(m_hwnd);
            
            NOMAD_LOG_INFO("DPI changed: " + std::to_string(oldScale) + " -> " + std::to_string(m_dpiScale));
            
            // Notify callback
            if (m_dpiChangeCallback) {
                m_dpiChangeCallback(m_dpiScale);
            }
            
            // Windows suggests a new window size/position in lParam
            RECT* suggestedRect = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(m_hwnd, nullptr,
                        suggestedRect->left,
                        suggestedRect->top,
                        suggestedRect->right - suggestedRect->left,
                        suggestedRect->bottom - suggestedRect->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
            
            return 0;
        }
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// =============================================================================
// Window Properties
// =============================================================================

void PlatformWindowWin32::setTitle(const std::string& title) {
    m_title = title;
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    wchar_t* wideTitle = new wchar_t[titleLen];
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle, titleLen);
    SetWindowTextW(m_hwnd, wideTitle);
    delete[] wideTitle;
}

void PlatformWindowWin32::setSize(int width, int height) {
    m_width = width;
    m_height = height;
    
    DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, style, FALSE);
    
    SetWindowPos(m_hwnd, nullptr, 0, 0, 
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void PlatformWindowWin32::getSize(int& width, int& height) const {
    width = m_width;
    height = m_height;
}

void PlatformWindowWin32::setPosition(int x, int y) {
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void PlatformWindowWin32::getPosition(int& x, int& y) const {
    RECT rect;
    GetWindowRect(m_hwnd, &rect);
    x = rect.left;
    y = rect.top;
}

// =============================================================================
// Window State
// =============================================================================

void PlatformWindowWin32::show() {
    ShowWindow(m_hwnd, SW_SHOW);
}

void PlatformWindowWin32::hide() {
    ShowWindow(m_hwnd, SW_HIDE);
}

void PlatformWindowWin32::minimize() {
    ShowWindow(m_hwnd, SW_MINIMIZE);
}

void PlatformWindowWin32::maximize() {
    ShowWindow(m_hwnd, SW_MAXIMIZE);
}

void PlatformWindowWin32::restore() {
    ShowWindow(m_hwnd, SW_RESTORE);
}

bool PlatformWindowWin32::isMaximized() const {
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(m_hwnd, &wp);
    return wp.showCmd == SW_MAXIMIZE;
}

bool PlatformWindowWin32::isMinimized() const {
    return IsIconic(m_hwnd) == TRUE;
}

// =============================================================================
// Fullscreen
// =============================================================================

void PlatformWindowWin32::setFullscreen(bool fullscreen) {
    if (fullscreen == m_isFullscreen) {
        return;
    }

    if (fullscreen) {
        // Save current window placement
        m_wpPrev.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_wpPrev);

        // Save current style
        m_styleBackup = GetWindowLong(m_hwnd, GWL_STYLE);

        // Set borderless fullscreen
        DWORD style = m_styleBackup & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER);
        SetWindowLong(m_hwnd, GWL_STYLE, style);

        // Get monitor info
        HMONITOR hMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &mi);

        // Set window to cover entire monitor
        SetWindowPos(m_hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        m_isFullscreen = true;
    } else {
        // Restore style
        SetWindowLong(m_hwnd, GWL_STYLE, m_styleBackup);

        // Restore window placement
        SetWindowPlacement(m_hwnd, &m_wpPrev);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        m_isFullscreen = false;
    }
}

// =============================================================================
// Key Translation
// =============================================================================

KeyCode PlatformWindowWin32::translateKeyCode(WPARAM wParam, LPARAM lParam) {
    // Handle extended keys
    bool extended = (lParam & (1 << 24)) != 0;

    switch (wParam) {
        // Letters
        case 'A': return KeyCode::A;
        case 'B': return KeyCode::B;
        case 'C': return KeyCode::C;
        case 'D': return KeyCode::D;
        case 'E': return KeyCode::E;
        case 'F': return KeyCode::F;
        case 'G': return KeyCode::G;
        case 'H': return KeyCode::H;
        case 'I': return KeyCode::I;
        case 'J': return KeyCode::J;
        case 'K': return KeyCode::K;
        case 'L': return KeyCode::L;
        case 'M': return KeyCode::M;
        case 'N': return KeyCode::N;
        case 'O': return KeyCode::O;
        case 'P': return KeyCode::P;
        case 'Q': return KeyCode::Q;
        case 'R': return KeyCode::R;
        case 'S': return KeyCode::S;
        case 'T': return KeyCode::T;
        case 'U': return KeyCode::U;
        case 'V': return KeyCode::V;
        case 'W': return KeyCode::W;
        case 'X': return KeyCode::X;
        case 'Y': return KeyCode::Y;
        case 'Z': return KeyCode::Z;

        // Numbers
        case '0': return KeyCode::Num0;
        case '1': return KeyCode::Num1;
        case '2': return KeyCode::Num2;
        case '3': return KeyCode::Num3;
        case '4': return KeyCode::Num4;
        case '5': return KeyCode::Num5;
        case '6': return KeyCode::Num6;
        case '7': return KeyCode::Num7;
        case '8': return KeyCode::Num8;
        case '9': return KeyCode::Num9;

        // Function keys
        case VK_F1: return KeyCode::F1;
        case VK_F2: return KeyCode::F2;
        case VK_F3: return KeyCode::F3;
        case VK_F4: return KeyCode::F4;
        case VK_F5: return KeyCode::F5;
        case VK_F6: return KeyCode::F6;
        case VK_F7: return KeyCode::F7;
        case VK_F8: return KeyCode::F8;
        case VK_F9: return KeyCode::F9;
        case VK_F10: return KeyCode::F10;
        case VK_F11: return KeyCode::F11;
        case VK_F12: return KeyCode::F12;

        // Special keys
        case VK_ESCAPE: return KeyCode::Escape;
        case VK_TAB: return KeyCode::Tab;
        case VK_CAPITAL: return KeyCode::CapsLock;
        case VK_SHIFT: return KeyCode::Shift;
        case VK_CONTROL: return KeyCode::Control;
        case VK_MENU: return KeyCode::Alt;
        case VK_SPACE: return KeyCode::Space;
        case VK_RETURN: return KeyCode::Enter;
        case VK_BACK: return KeyCode::Backspace;
        case VK_DELETE: return KeyCode::Delete;
        case VK_INSERT: return KeyCode::Insert;
        case VK_HOME: return KeyCode::Home;
        case VK_END: return KeyCode::End;
        case VK_PRIOR: return KeyCode::PageUp;
        case VK_NEXT: return KeyCode::PageDown;
        case VK_LEFT: return KeyCode::Left;
        case VK_UP: return KeyCode::Up;
        case VK_RIGHT: return KeyCode::Right;
        case VK_DOWN: return KeyCode::Down;

        default: return KeyCode::Unknown;
    }
}

KeyModifiers PlatformWindowWin32::getKeyModifiers() {
    KeyModifiers mods;
    mods.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mods.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    mods.super = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    return mods;
}

} // namespace Nomad
