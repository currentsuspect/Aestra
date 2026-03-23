// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PlatformWindowWin32.h"

#include "../../../AestraCore/include/AestraAssert.h"
#include "../../../AestraCore/include/AestraLog.h"
#include "../../../AestraUI/External/glad/include/glad/glad.h"
#include "../../../AestraUI/External/glad/include/glad/wglext.h"
#include "../../../Source/resource.h"
#include "PlatformDPIWin32.h"

#include <windowsx.h>

namespace Aestra {

// Static members
const wchar_t* PlatformWindowWin32::WINDOW_CLASS_NAME = L"AestraWindowClass";
bool PlatformWindowWin32::s_classRegistered = false;
HICON PlatformWindowWin32::s_hLargeIcon = nullptr;
HICON PlatformWindowWin32::s_hSmallIcon = nullptr;

// =============================================================================
// Constructor / Destructor
// =============================================================================

PlatformWindowWin32::PlatformWindowWin32()
    : m_hwnd(nullptr), m_hdc(nullptr), m_hglrc(nullptr), m_width(0), m_height(0), m_shouldClose(false),
      m_isFullscreen(false), m_styleBackup(0), m_dpiScale(1.0f), m_creatingThreadId(0) {
    m_wpPrev = {};
    m_wpPrev.length = sizeof(WINDOWPLACEMENT);
    m_creatingThreadId = GetCurrentThreadId();
}

PlatformWindowWin32::~PlatformWindowWin32() {
    destroy();
}

// =============================================================================
// Window Creation
// =============================================================================

bool PlatformWindowWin32::create(const WindowDesc& desc) {
    m_title = desc.title;
    m_title = desc.title;
    m_width = desc.width;
    m_height = desc.height;
    m_isBorderless = !desc.decorated;

    // Register window class
    if (!registerWindowClass()) {
        AESTRA_LOG_ERROR("Failed to register window class");
        return false;
    }

    // Calculate window style
    DWORD style = 0;

    if (m_isBorderless) {
        // Pure WS_POPUP - no frame at all. Resize is handled via WM_NCHITTEST.
        // WS_MAXIMIZEBOX/MINIMIZEBOX = Taskbar integration (right-click menu)
        // This eliminates all Windows-imposed gaps/borders.
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    } else {
        // Standard window
        style = WS_OVERLAPPEDWINDOW;
        if (!desc.resizable) {
            style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        }
    }

    // Calculate window size
    RECT rect = {0, 0, m_width, m_height};

    // Only adjust for borders if we are NOT in our custom borderless mode.
    // In borderless mode, Window Rect == Client Rect.
    if (!m_isBorderless) {
        AdjustWindowRect(&rect, style, FALSE);
    }

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    AESTRA_LOG_INFO("Creating Window: Borderless=" + std::to_string(m_isBorderless) +
                    ", Requested=" + std::to_string(m_width) + "x" + std::to_string(m_height) +
                    ", Calculated=" + std::to_string(windowWidth) + "x" + std::to_string(windowHeight));

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
    DWORD exStyle = WS_EX_APPWINDOW;

    m_hwnd = CreateWindowExW(exStyle, WINDOW_CLASS_NAME, wideTitle, style, x, y, windowWidth, windowHeight, nullptr,
                             nullptr, GetModuleHandle(nullptr),
                             this // Pass 'this' pointer to WM_CREATE
    );
    delete[] wideTitle;

    if (!m_hwnd) {
        AESTRA_LOG_ERROR("Failed to create window");
        return false;
    }

    // Enable DWM Shadows and disable NC rendering using explicit linking
    HMODULE hDwmapi = LoadLibraryA("dwmapi.dll");
    if (hDwmapi && m_isBorderless) {
        // DWMWA_NCRENDERING_POLICY constants
        enum DWMWINDOWATTRIBUTE_LOCAL { DWMWA_NCRENDERING_POLICY_LOCAL = 2 };
        enum DWMNCRENDERINGPOLICY_LOCAL { DWMNCRP_DISABLED_LOCAL = 1 };

        typedef HRESULT(WINAPI * DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetWindowAttributeFunc setAttr = (DwmSetWindowAttributeFunc)GetProcAddress(hDwmapi, "DwmSetWindowAttribute");

        if (setAttr) {
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
            // DWMWA_CAPTION_COLOR = 35 (Windows 11)
            // DWMWA_BORDER_COLOR = 34 (Windows 11)
            // DWMWA_COLOR_NONE = 0xFFFFFFFE (disables the border/caption completely)

            // Enable immersive dark mode to prevent white caption remnants
            BOOL darkMode = TRUE;
            setAttr(m_hwnd, 20, &darkMode, sizeof(darkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE

            // Set caption color to NONE (disable drawing)
            DWORD colorNone = 0xFFFFFFFE;                       // DWMWA_COLOR_NONE
            setAttr(m_hwnd, 35, &colorNone, sizeof(colorNone)); // DWMWA_CAPTION_COLOR

            // Set border color to NONE (disable drawing)
            setAttr(m_hwnd, 34, &colorNone, sizeof(colorNone)); // DWMWA_BORDER_COLOR

            AESTRA_LOG_INFO("DWM dark mode enabled, caption/border disabled");
        }

        typedef struct _MARGINS {
            int cxLeftWidth;
            int cxRightWidth;
            int cyTopHeight;
            int cyBottomHeight;
        } MARGINS, *PMARGINS;

        typedef HRESULT(WINAPI * DwmExtendFrameIntoClientAreaFunc)(HWND, const MARGINS*);
        DwmExtendFrameIntoClientAreaFunc proc =
            (DwmExtendFrameIntoClientAreaFunc)GetProcAddress(hDwmapi, "DwmExtendFrameIntoClientArea");

        if (proc) {
            // Set margins to 0 - completely disable DWM frame extension.
            // This eliminates the black exterior. No DWM shadow, but clean edges.
            MARGINS margins = {0, 0, 0, 0};
            proc(m_hwnd, &margins);
            AESTRA_LOG_INFO("DWM frame extension disabled (margins = 0)");
        }
        FreeLibrary(hDwmapi);
    } else if (hDwmapi) {
        FreeLibrary(hDwmapi);
    }

    // Explicitly set the window icons via WM_SETICON. Load icons here so
    // they are in scope; some shells prefer icons set on the window itself
    // over the class icons for taskbar and Alt+Tab rendering.
    HINSTANCE hInstLocal = GetModuleHandle(nullptr);
    HICON hIconBig =
        (HICON)LoadImageW(hInstLocal, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    HICON hIconSmall =
        (HICON)LoadImageW(hInstLocal, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
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
        AESTRA_LOG_ERROR("Failed to get device context");
        destroy();
        return false;
    }

    // Setup pixel format for OpenGL
    if (!setupPixelFormat()) {
        AESTRA_LOG_ERROR("Failed to setup pixel format");
        destroy();
        return false;
    }

    // Get initial DPI scale
    m_dpiScale = PlatformDPI::getDPIScale(m_hwnd);
    AESTRA_LOG_INFO("Window DPI scale: " + std::to_string(m_dpiScale));

    // Show window logic
    if (desc.startFullscreen) {
        setFullscreen(true);
    } else {
        // Always show normal first to establish the window
        ShowWindow(m_hwnd, SW_SHOW);

        if (desc.startMaximized) {
            // Use PostMessage to trigger maximization ASYNCHRONOUSLY.
            // This ensures the window is fully created, shown, and DWM is ready
            // before we transition to maximized state. This effectively simulates
            // a user clicking the maximize button, which we know works correctly.
            PostMessageW(m_hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        }
    }
    UpdateWindow(m_hwnd);

    // Force a frame recalculation to ensure the "Restored" state is also correct
    // (Borderless with no title bar)
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // CRITICAL: After SWP_FRAMECHANGED, explicitly get the TRUE client rect size
    // and update our internal state + notify renderer. This ensures the viewport
    // uses the correct size after WM_NCCALCSIZE zeroed out the frame.
    if (m_isBorderless) {
        RECT clientRect;
        GetClientRect(m_hwnd, &clientRect);
        int trueWidth = clientRect.right - clientRect.left;
        int trueHeight = clientRect.bottom - clientRect.top;

        AESTRA_LOG_INFO("Post-FRAMECHANGED client rect: " + std::to_string(trueWidth) + "x" +
                        std::to_string(trueHeight));

        if (trueWidth != m_width || trueHeight != m_height) {
            m_width = trueWidth;
            m_height = trueHeight;
            if (m_resizeCallback) {
                m_resizeCallback(m_width, m_height);
            }
        }
    }

    AESTRA_LOG_INFO("Window created successfully");
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
    if (s_hLargeIcon)
        wc.hIcon = s_hLargeIcon;
    else
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (s_hSmallIcon)
        wc.hIconSm = s_hSmallIcon;
    else
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
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
    // First, try to set up MSAA pixel format using WGL extensions
    // This requires creating a temporary context first

    // Basic pixel format for fallback/temp context
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

// Note: MSAA requires setting pixel format before context creation
// For proper MSAA support, implement two-window initialization:
// 1. Create dummy window with basic pixel format
// 2. Create temp GL context to load WGL extensions
// 3. Use wglChoosePixelFormatARB on real window with MSAA attributes
// 4. Create real context
// Current implementation relies on shader-based anti-aliasing (SDF smoothstep)

bool PlatformWindowWin32::createGLContext() {
    if (m_hglrc) {
        return true; // Already created
    }

    // Step 1: Create a temporary legacy context to load WGL extensions
    HGLRC tempCtx = wglCreateContext(m_hdc);
    if (!tempCtx) {
        AESTRA_LOG_ERROR("Failed to create temporary OpenGL context");
        return false;
    }
    if (!wglMakeCurrent(m_hdc, tempCtx)) {
        AESTRA_LOG_ERROR("Failed to make temporary OpenGL context current");
        wglDeleteContext(tempCtx);
        return false;
    }

    // Load wglCreateContextAttribsARB
    auto createAttribs =
        reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));

    HGLRC modernCtx = nullptr;
    int chosenMajor = 0, chosenMinor = 0;

    if (createAttribs) {
        const int attempts[][2] = {{4, 1}, {4, 0}, {3, 3}};
        for (const auto& ver : attempts) {
            int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                             ver[0],
                             WGL_CONTEXT_MINOR_VERSION_ARB,
                             ver[1],
                             WGL_CONTEXT_PROFILE_MASK_ARB,
                             WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                             WGL_CONTEXT_FLAGS_ARB,
                             WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                             0};
            modernCtx = createAttribs(m_hdc, nullptr, attribs);
            if (modernCtx) {
                chosenMajor = ver[0];
                chosenMinor = ver[1];
                break;
            }
        }
    }

    // Destroy temp context
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempCtx);

    if (modernCtx) {
        if (!wglMakeCurrent(m_hdc, modernCtx)) {
            AESTRA_LOG_ERROR("Failed to make modern OpenGL context current");
            wglDeleteContext(modernCtx);
            return false;
        }
        m_hglrc = modernCtx;

        // Note: For proper MSAA, pixel format must be set before context creation
        // This requires a two-window approach (dummy window to load WGL extensions)
        // Our current approach relies on shader-based anti-aliasing (SDF smoothstep)
        // which provides good quality without hardware MSAA

        AESTRA_LOG_INFO("OpenGL core context created (" + std::to_string(chosenMajor) + "." +
                        std::to_string(chosenMinor) + ")");
        return true;
    }

    // Fallback to legacy context if attribs path failed
    m_hglrc = wglCreateContext(m_hdc);
    if (!m_hglrc) {
        AESTRA_LOG_ERROR("Failed to create fallback OpenGL context");
        return false;
    }
    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        AESTRA_LOG_ERROR("Failed to make fallback OpenGL context current");
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
        return false;
    }
    AESTRA_LOG_WARNING("Using legacy OpenGL context (attribs path unavailable).");
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
    typedef BOOL(WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(enabled ? 1 : 0);
    }
}

void PlatformWindowWin32::setCursorVisible(bool visible) {
    assertWindowThread();
    // Always enforce cursor state, do not rely on cached m_cursorVisible
    // because external systems (or DefWindowProc) might have altered the ShowCursor count.
    m_cursorVisible = visible;

    if (visible) {
        // Increment cursor count until it's visible (>= 0)
        int attempts = 0;
        while (ShowCursor(TRUE) < 0) {
            if (++attempts >= 50) {
                AESTRA_LOG_WARNING("setCursorVisible: Failed to show cursor after " + std::to_string(attempts) +
                                   " attempts");
                break;
            }
        }
    } else {
        // Decrement cursor count until it's hidden (< 0)
        int attempts = 0;
        while (ShowCursor(FALSE) >= 0) {
            if (++attempts >= 50) {
                AESTRA_LOG_WARNING("setCursorVisible: Failed to hide cursor after " + std::to_string(attempts) +
                                   " attempts");
                break;
            }
        }
    }
}

// =============================================================================
// Mouse Capture
// =============================================================================

void PlatformWindowWin32::setMouseCapture(bool captured) {
    if (captured) {
        SetCapture(m_hwnd);
    } else {
        ReleaseCapture();
    }
}

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

    // CRITICAL: Use WM_NCCREATE instead of WM_CREATE.
    // WM_NCCALCSIZE is sent *before* WM_CREATE. If we wait for WM_CREATE,
    // the initial layout calculation will use the default handler (with caption),
    // causing a permanent offset/black bar.
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<PlatformWindowWin32*>(cs->lpCreateParams);
        // Store 'window' in GWLP_USERDATA so subsequent messages can find it
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        // Also set m_hwnd immediately so handleMessage can use it
        window->m_hwnd = hwnd;
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
    case WM_GETMINMAXINFO: {
        if (m_isBorderless) {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            MONITORINFO monitor = {sizeof(MONITORINFO)};
            HMONITOR hMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfo(hMonitor, &monitor);

            RECT rcWork = monitor.rcWork;
            RECT rcMonitor = monitor.rcMonitor;

            // Maximize size = Work Area Size
            mmi->ptMaxSize.x = std::abs(rcWork.right - rcWork.left);
            mmi->ptMaxSize.y = std::abs(rcWork.bottom - rcWork.top);

            // Maximize Position = Relative to Monitor
            mmi->ptMaxPosition.x = std::abs(rcWork.left - rcMonitor.left);
            mmi->ptMaxPosition.y = std::abs(rcWork.top - rcMonitor.top);

            // Track Size limits
            mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
            mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;

            return 0;
        }
        break;
    }

    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        if (!(wp->flags & SWP_NOMOVE) || !(wp->flags & SWP_NOSIZE)) {
            AESTRA_LOG_INFO("WM_WINDOWPOSCHANGED: x=" + std::to_string(wp->x) + ", y=" + std::to_string(wp->y) +
                            ", w=" + std::to_string(wp->cx) + ", h=" + std::to_string(wp->cy) +
                            ", flags=" + std::to_string(wp->flags));
        }
        break; // Pass to DefWindowProc
    }

    case WM_NCCALCSIZE: {
        if (m_isBorderless) {
            // With pure WS_POPUP, there's no non-client area.
            // WM_GETMINMAXINFO handles work area constraints for maximize.
            // Simply return 0 = whole window is client area.
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        if (m_isBorderless) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // 1. Get Application Feedback
            HitTestResult appResult = HitTestResult::Default;
            if (m_hitTestCallback) {
                POINT clientPt = pt;
                ScreenToClient(m_hwnd, &clientPt);
                appResult = m_hitTestCallback(clientPt.x, clientPt.y);
            }

            // 2. High Priority: Interactive UI Elements (Buttons) override everything
            switch (appResult) {
            case HitTestResult::Client:
                return HTCLIENT;
            case HitTestResult::MinButton:
                return HTMINBUTTON;
            case HitTestResult::MaxButton:
                return HTMAXBUTTON;
            case HitTestResult::CloseButton:
                return HTCLOSE;
            // Resize handles can be explicitly requested by app, though we usually let platform handle it
            case HitTestResult::ResizeTop:
                return HTTOP;
            case HitTestResult::ResizeBottom:
                return HTBOTTOM;
            case HitTestResult::ResizeLeft:
                return HTLEFT;
            case HitTestResult::ResizeRight:
                return HTRIGHT;
            case HitTestResult::ResizeTopLeft:
                return HTTOPLEFT;
            case HitTestResult::ResizeTopRight:
                return HTTOPRIGHT;
            case HitTestResult::ResizeBottomLeft:
                return HTBOTTOMLEFT;
            case HitTestResult::ResizeBottomRight:
                return HTBOTTOMRIGHT;
            case HitTestResult::Nowhere:
                return HTTRANSPARENT;
            default:
                break;
            }

            // 3. Medium Priority: Window Resizing (Standard Edges)
            // Only if NOT maximized and NOT already handled by specific UI controls
            if (!IsZoomed(m_hwnd)) {
                const int borderSize = 8; // Adjust for easy grabbing
                RECT rect;
                GetWindowRect(m_hwnd, &rect);

                bool onLeft = pt.x < rect.left + borderSize;
                bool onRight = pt.x >= rect.right - borderSize;
                bool onTop = pt.y < rect.top + borderSize;
                bool onBottom = pt.y >= rect.bottom - borderSize;

                if (onTop && onLeft)
                    return HTTOPLEFT;
                if (onTop && onRight)
                    return HTTOPRIGHT;
                if (onBottom && onLeft)
                    return HTBOTTOMLEFT;
                if (onBottom && onRight)
                    return HTBOTTOMRIGHT;
                if (onLeft)
                    return HTLEFT;
                if (onRight)
                    return HTRIGHT;
                if (onTop)
                    return HTTOP;
                if (onBottom)
                    return HTBOTTOM;
            }

            // 4. Low Priority: Title Bar Dragging vs Client Content
            if (appResult == HitTestResult::Caption) {
                return HTCAPTION;
            }

            return HTCLIENT;
        }
        break;
    }

    case WM_NCPAINT: {
        // Prevent Windows from drawing the standard NC area
        if (m_isBorderless) {
            return 0;
        }
        break;
    }

    case WM_NCACTIVATE: {
        if (m_isBorderless) {
            // Return TRUE to indicate we handled activation, but prevent
            // Windows from drawing the default title bar by not calling
            // DefWindowProc. Setting lParam to -1 prevents any NC painting.
            // This is critical for preventing the white line at the top.
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        if (m_closeCallback) {
            m_closeCallback();
            return 0;
        }
        m_shouldClose = true;
        return 0;

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        // For borderless windows with WS_OVERLAPPEDWINDOW, the lParam values
        // may be incorrect during initial setup (before WM_NCCALCSIZE takes effect).
        // Use GetClientRect to get the TRUE client area size.
        if (m_isBorderless && m_hwnd) {
            RECT clientRect;
            if (GetClientRect(m_hwnd, &clientRect)) {
                width = clientRect.right - clientRect.left;
                height = clientRect.bottom - clientRect.top;
            }
        }

        bool sizeChanged = (width != m_width || height != m_height);
        bool minimizedEvent = (wParam == SIZE_MINIMIZED);
        bool restoredEvent = (wParam == SIZE_RESTORED);

        if (sizeChanged) {
            m_width = width;
            m_height = height;
        }

        if (m_resizeCallback && (sizeChanged || minimizedEvent || restoredEvent)) {
            m_resizeCallback(width, height);
        }
        return 0;
    }

    case WM_NCMOUSEMOVE: {
        // Forward NC mouse moves to the app so software cursors don't freeze over Title Bar
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(m_hwnd, &pt);
        if (m_mouseMoveCallback) {
            m_mouseMoveCallback(pt.x, pt.y);
        }
        break;
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
        if (msg == WM_RBUTTONDOWN)
            button = MouseButton::Right;
        else if (msg == WM_MBUTTONDOWN)
            button = MouseButton::Middle;

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
        if (msg == WM_RBUTTONUP)
            button = MouseButton::Right;
        else if (msg == WM_MBUTTONUP)
            button = MouseButton::Middle;

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

    case WM_MOUSEHWHEEL: {
        float delta = -GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
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

    case WM_SETCURSOR: {
        WORD hitTest = LOWORD(lParam);

        // Handle cursor visibility for Client area and Captions (if managed by UI)
        if (hitTest == HTCLIENT || hitTest == HTCAPTION) {
            if (!m_cursorVisible) {
                SetCursor(NULL);
                return TRUE;
            }
        }

        // Force System Resize Cursors (Fixes invisible cursor issue)
        // DefWindowProc sometimes gets overridden or ignored if capture is weird,
        // so we set them explicitly here.
        switch (hitTest) {
        case HTLEFT:
        case HTRIGHT:
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        case HTTOP:
        case HTBOTTOM:
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
            return TRUE;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT:
            SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
            return TRUE;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
            SetCursor(LoadCursor(NULL, IDC_SIZENESW));
            return TRUE;
        }

        break;
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
        float oldScale = m_dpiScale;
        m_dpiScale = PlatformDPI::getDPIScale(m_hwnd);
        if (m_dpiChangeCallback) {
            m_dpiChangeCallback(m_dpiScale);
        }
        RECT* suggestedRect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top,
                     suggestedRect->right - suggestedRect->left, suggestedRect->bottom - suggestedRect->top,
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
    RECT rect = {0, 0, width, height};

    // Only adjust for borders if we are NOT in our custom borderless mode.
    // In "True Borderless" (WS_POPUP + Handling NCCALCSIZE), Window Rect == Client Rect.
    if (!m_isBorderless) {
        AdjustWindowRect(&rect, style, FALSE);
    }

    SetWindowPos(m_hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
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

void PlatformWindowWin32::setCursorPosition(int x, int y) {
    SetCursorPos(x, y);
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
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        m_isFullscreen = true;
    } else {
        // Restore style
        SetWindowLong(m_hwnd, GWL_STYLE, m_styleBackup);

        // Restore window placement
        SetWindowPlacement(m_hwnd, &m_wpPrev);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        m_isFullscreen = false;
    }
}

// =============================================================================
// Key Translation
// =============================================================================

void PlatformWindowWin32::requestClose() {
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
}

KeyCode PlatformWindowWin32::translateKeyCode(WPARAM wParam, LPARAM lParam) {
    // Handle extended keys
    bool extended = (lParam & (1 << 24)) != 0;

    switch (wParam) {
    // Letters
    case 'A':
        return KeyCode::A;
    case 'B':
        return KeyCode::B;
    case 'C':
        return KeyCode::C;
    case 'D':
        return KeyCode::D;
    case 'E':
        return KeyCode::E;
    case 'F':
        return KeyCode::F;
    case 'G':
        return KeyCode::G;
    case 'H':
        return KeyCode::H;
    case 'I':
        return KeyCode::I;
    case 'J':
        return KeyCode::J;
    case 'K':
        return KeyCode::K;
    case 'L':
        return KeyCode::L;
    case 'M':
        return KeyCode::M;
    case 'N':
        return KeyCode::N;
    case 'O':
        return KeyCode::O;
    case 'P':
        return KeyCode::P;
    case 'Q':
        return KeyCode::Q;
    case 'R':
        return KeyCode::R;
    case 'S':
        return KeyCode::S;
    case 'T':
        return KeyCode::T;
    case 'U':
        return KeyCode::U;
    case 'V':
        return KeyCode::V;
    case 'W':
        return KeyCode::W;
    case 'X':
        return KeyCode::X;
    case 'Y':
        return KeyCode::Y;
    case 'Z':
        return KeyCode::Z;

    // Numbers
    case '0':
        return KeyCode::Num0;
    case '1':
        return KeyCode::Num1;
    case '2':
        return KeyCode::Num2;
    case '3':
        return KeyCode::Num3;
    case '4':
        return KeyCode::Num4;
    case '5':
        return KeyCode::Num5;
    case '6':
        return KeyCode::Num6;
    case '7':
        return KeyCode::Num7;
    case '8':
        return KeyCode::Num8;
    case '9':
        return KeyCode::Num9;

    // Function keys
    case VK_F1:
        return KeyCode::F1;
    case VK_F2:
        return KeyCode::F2;
    case VK_F3:
        return KeyCode::F3;
    case VK_F4:
        return KeyCode::F4;
    case VK_F5:
        return KeyCode::F5;
    case VK_F6:
        return KeyCode::F6;
    case VK_F7:
        return KeyCode::F7;
    case VK_F8:
        return KeyCode::F8;
    case VK_F9:
        return KeyCode::F9;
    case VK_F10:
        return KeyCode::F10;
    case VK_F11:
        return KeyCode::F11;
    case VK_F12:
        return KeyCode::F12;

    // Special keys
    case VK_ESCAPE:
        return KeyCode::Escape;
    case VK_TAB:
        return KeyCode::Tab;
    case VK_CAPITAL:
        return KeyCode::CapsLock;
    case VK_SHIFT:
        return KeyCode::Shift;
    case VK_CONTROL:
        return KeyCode::Control;
    case VK_MENU:
        return KeyCode::Alt;
    case VK_SPACE:
        return KeyCode::Space;
    case VK_RETURN:
        return KeyCode::Enter;
    case VK_BACK:
        return KeyCode::Backspace;
    case VK_DELETE:
        return KeyCode::Delete;
    case VK_INSERT:
        return KeyCode::Insert;
    case VK_HOME:
        return KeyCode::Home;
    case VK_END:
        return KeyCode::End;
    case VK_PRIOR:
        return KeyCode::PageUp;
    case VK_NEXT:
        return KeyCode::PageDown;
    case VK_LEFT:
        return KeyCode::Left;
    case VK_UP:
        return KeyCode::Up;
    case VK_RIGHT:
        return KeyCode::Right;
    case VK_DOWN:
        return KeyCode::Down;

    default:
        return KeyCode::Unknown;
    }
}

KeyModifiers PlatformWindowWin32::getKeyModifiers() const {
    assertWindowThread();
    KeyModifiers mods;
    mods.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mods.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    mods.super = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    mods.capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    return mods;
}

// =============================================================================
// Thread Safety
// =============================================================================

void PlatformWindowWin32::assertWindowThread() const {
    AESTRA_ASSERT_MSG(GetCurrentThreadId() == m_creatingThreadId,
                      "PlatformWindowWin32 methods must be called from the same thread that created the window. "
                      "Cross-thread calls to setCursorVisible() and getCurrentModifiers() will cause "
                      "cursor display count desynchronization due to ShowCursor()'s per-thread behavior.");
}

} // namespace Aestra
