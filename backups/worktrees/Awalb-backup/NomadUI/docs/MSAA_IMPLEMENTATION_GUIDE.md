# MSAA Implementation Guide (Phase 2)

## Current Status

✅ **Phase 1 Complete**: Basic anti-aliasing with thin lines and proper blending
🔄 **Phase 2 In Progress**: MSAA detection and enablement

## What We Have Now

The renderer now checks for MSAA support and enables it if available:

```cpp
// In NUIRendererGL::initialize()
GLint samples = 0;
glGetIntegerv(GL_SAMPLES, &samples);
if (samples > 0) {
    glEnable(GL_MULTISAMPLE);
    std::cout << "MSAA enabled with " << samples << "x samples" << std::endl;
}
```

However, the pixel format doesn't request MSAA yet, so `samples` will be 0.

## Full MSAA Implementation

To get true MSAA support, we need to request it during pixel format selection using WGL extensions.

### Step 1: Load WGL Extensions

After creating a temporary OpenGL context, load the WGL extension functions:

```cpp
// In NUIWindowWin32.cpp - add these typedefs at the top
typedef BOOL (WINAPI * PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_SAMPLE_BUFFERS_ARB 0x2041
#define WGL_SAMPLES_ARB 0x2042
```

### Step 2: Create Window with MSAA

Replace the current `setupPixelFormat()` with a two-stage approach:

```cpp
bool NUIWindowWin32::setupPixelFormatWithMSAA() {
    // Stage 1: Create temporary context to load extensions
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    
    int tempFormat = ChoosePixelFormat(TO_HDC(m_hdc), &pfd);
    SetPixelFormat(TO_HDC(m_hdc), tempFormat, &pfd);
    
    HGLRC tempContext = wglCreateContext(TO_HDC(m_hdc));
    wglMakeCurrent(TO_HDC(m_hdc), tempContext);
    
    // Load WGL extension
    PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = 
        (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    
    if (!wglChoosePixelFormatARB) {
        // Extensions not available, use basic format
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempContext);
        return true; // Continue with basic format
    }
    
    // Stage 2: Choose MSAA pixel format
    int pixelFormat;
    UINT numFormats;
    
    int attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, 1,  // Enable MSAA
        WGL_SAMPLES_ARB, 4,          // 4x MSAA
        0
    };
    
    if (!wglChoosePixelFormatARB(TO_HDC(m_hdc), attribs, nullptr, 1, &pixelFormat, &numFormats) || numFormats == 0) {
        // MSAA not available, fall back to basic format
        std::cout << "4x MSAA not available, trying 2x..." << std::endl;
        attribs[17] = 2; // Try 2x MSAA
        
        if (!wglChoosePixelFormatARB(TO_HDC(m_hdc), attribs, nullptr, 1, &pixelFormat, &numFormats) || numFormats == 0) {
            std::cout << "MSAA not supported, using standard rendering" << std::endl;
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tempContext);
            return true;
        }
    }
    
    // Clean up temporary context
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tempContext);
    
    // Recreate window with MSAA pixel format
    // Note: We need to destroy and recreate the window because
    // SetPixelFormat can only be called once per window
    
    // Store current window properties
    RECT rect;
    GetWindowRect(TO_HWND(m_hwnd), &rect);
    int x = rect.left;
    int y = rect.top;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    // Get window title
    wchar_t title[256];
    GetWindowTextW(TO_HWND(m_hwnd), title, 256);
    
    // Destroy old window
    DestroyWindow(TO_HWND(m_hwnd));
    
    // Create new window
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        WINDOW_CLASS_NAME,
        title,
        WS_POPUP | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU,
        x, y, width, height,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this
    );
    
    if (!m_hwnd) {
        std::cerr << "Failed to recreate window with MSAA" << std::endl;
        return false;
    }
    
    // Get new DC and set MSAA pixel format
    m_hdc = GetDC(TO_HWND(m_hwnd));
    DescribePixelFormat(TO_HDC(m_hdc), pixelFormat, sizeof(pfd), &pfd);
    SetPixelFormat(TO_HDC(m_hdc), pixelFormat, &pfd);
    
    std::cout << "MSAA pixel format set successfully!" << std::endl;
    return true;
}
```

### Step 3: Update Window Creation Flow

Modify the `create()` method to use the new MSAA setup:

```cpp
bool NUIWindowWin32::create(const std::string& title, int width, int height) {
    // ... existing code ...
    
    // Get device context
    m_hdc = GetDC(TO_HWND(m_hwnd));
    if (!m_hdc) {
        std::cerr << "Failed to get device context" << std::endl;
        destroy();
        return false;
    }
    
    // Setup pixel format with MSAA support
    if (!setupPixelFormatWithMSAA()) {
        destroy();
        return false;
    }
    
    // Create OpenGL context
    if (!createGLContext()) {
        destroy();
        return false;
    }
    
    return true;
}
```

## Simpler Alternative: MSAA via Framebuffer

If the window recreation approach is too complex, you can use an MSAA framebuffer:

```cpp
// In NUIRendererGL
GLuint msaaFBO, msaaColorBuffer;
GLuint resolveFBO, resolveColorBuffer;

void setupMSAAFramebuffer(int width, int height, int samples) {
    // Create MSAA framebuffer
    glGenFramebuffers(1, &msaaFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);
    
    // Create MSAA color buffer
    glGenRenderbuffers(1, &msaaColorBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColorBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorBuffer);
    
    // Create resolve framebuffer
    glGenFramebuffers(1, &resolveFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
    
    // Create resolve texture
    glGenTextures(1, &resolveColorBuffer);
    glBindTexture(GL_TEXTURE_2D, resolveColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColorBuffer, 0);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void beginFrame() {
    // Render to MSAA framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);
    glClear(GL_COLOR_BUFFER_BIT);
}

void endFrame() {
    // Resolve MSAA to regular framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    
    // Blit to screen
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
```

## Testing MSAA

After implementation, test with:

```cpp
// Check if MSAA is active
GLint samples = 0;
glGetIntegerv(GL_SAMPLES, &samples);
std::cout << "Active MSAA samples: " << samples << std::endl;

// Check max supported samples
GLint maxSamples = 0;
glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
std::cout << "Max supported MSAA samples: " << maxSamples << std::endl;
```

## Expected Results

With 4x MSAA enabled:
- **Icon edges**: Significantly smoother, no jagged lines
- **Diagonal lines**: Clean and crisp
- **Text**: Slightly improved (though text has its own anti-aliasing)
- **Performance**: Minimal impact on modern GPUs (< 5% overhead)

## Quality Comparison

| Feature | Phase 1 (Current) | Phase 2 (MSAA) |
|---------|------------------|----------------|
| Line smoothness | Good | Excellent |
| Edge quality | 7/10 | 9/10 |
| Diagonal lines | Some aliasing | Very smooth |
| Performance | 100% | ~95% |
| Complexity | Low | Medium |

## Recommendation

**Start with the framebuffer approach** - it's simpler and doesn't require window recreation. If you need even better quality, implement the full WGL extension approach later.

## Next Steps

1. ✅ Enable MSAA detection (done)
2. 🔄 Implement MSAA framebuffer (recommended next)
3. ⏳ Implement WGL extension approach (optional, for best quality)
4. ⏳ Test on various hardware
5. ⏳ Add MSAA quality settings (2x, 4x, 8x)

## References

- [OpenGL MSAA Tutorial](https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing)
- [WGL Extensions](https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions#Windows)
- [Framebuffer MSAA](https://www.khronos.org/opengl/wiki/Multisampling)
