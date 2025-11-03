// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Simple Window Test - Minimal NomadUI Window Demo
 * Tests basic window creation and OpenGL rendering without text/fonts
 */

// Include stddef.h first to get standard ptrdiff_t
#include <cstddef>
#include <cstdint>

// Windows headers first with proper macro definitions
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM
#include <Windows.h>

// GLAD must be included after Windows headers to avoid macro conflicts
#include <glad/glad.h>

// Suppress APIENTRY redefinition warning - both define the same value
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)

#include <iostream>
#include <chrono>
#include <cmath>

// Minimal window class
class SimpleWindow {
public:
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
    bool shouldClose = false;
    
    bool create(const char* title, int width, int height) {
        // Register window class
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "SimpleNomadWindow";
        
        RegisterClassExA(&wc);
        
        // Create window
        hwnd = CreateWindowExA(0, "SimpleNomadWindow", title, WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                               nullptr, nullptr, GetModuleHandleA(nullptr), this);
        
        if (!hwnd) return false;
        
        // Get DC and setup OpenGL
        hdc = GetDC(hwnd);
        
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        
        int format = ChoosePixelFormat(hdc, &pfd);
        SetPixelFormat(hdc, format, &pfd);
        
        // Create GL context
        hglrc = wglCreateContext(hdc);
        wglMakeCurrent(hdc, hglrc);
        
        // Load OpenGL
        if (!gladLoadGL()) {
            std::cerr << "Failed to load OpenGL" << std::endl;
            return false;
        }
        
        ShowWindow(hwnd, SW_SHOW);
        return true;
    }
    
    bool processEvents() {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                shouldClose = true;
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return !shouldClose;
    }
    
    void swap() {
        SwapBuffers(hdc);
    }
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SimpleWindow* win = nullptr;
        
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            win = (SimpleWindow*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)win);
        } else {
            win = (SimpleWindow*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        }
        
        if (win && msg == WM_CLOSE) {
            win->shouldClose = true;
            return 0;
        }
        
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
};

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "  NomadUI - Simple Window Test" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    SimpleWindow window;
    if (!window.create("NomadUI - Simple Window Test", 800, 600)) {
        std::cerr << "Failed to create window!" << std::endl;
        return 1;
    }
    
    std::cout << "\nâœ“ Window created successfully!" << std::endl;
    std::cout << "âœ“ OpenGL context initialized" << std::endl;
    std::cout << "\nRendering animated colors..." << std::endl;
    std::cout << "Close the window to exit.\n" << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    while (window.processEvents()) {
        // Calculate time
        auto now = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(now - startTime).count();
        
        // Animated colors
        float r = 0.1f + 0.05f * std::sin(time * 0.5f);
        float g = 0.1f + 0.05f * std::sin(time * 0.7f);
        float b = 0.15f + 0.05f * std::sin(time * 0.3f);
        
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        window.swap();
        Sleep(16);  // ~60 FPS
    }
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "  âœ“ Test completed successfully!" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    return 0;
}

