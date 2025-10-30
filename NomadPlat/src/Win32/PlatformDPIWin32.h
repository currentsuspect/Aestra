// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <Windows.h>

namespace Nomad {

/**
 * DPI Awareness Helper for Windows
 * Handles high-DPI displays and scaling
 */
class PlatformDPI {
public:
    /**
     * Initialize DPI awareness for the application.
     * Must be called before creating any windows.
     * Returns true if DPI awareness was set successfully.
     */
    static bool initialize();
    
    /**
     * Get DPI scale factor for a window.
     * Returns 1.0 for 96 DPI (100% scaling)
     * Returns 1.5 for 144 DPI (150% scaling)
     * Returns 2.0 for 192 DPI (200% scaling)
     */
    static float getDPIScale(HWND hwnd);
    
    /**
     * Get DPI for a window.
     * Returns 96 for 100% scaling, 144 for 150%, 192 for 200%, etc.
     */
    static int getDPI(HWND hwnd);
    
    /**
     * Scale a value by DPI.
     */
    static int scale(int value, float dpiScale);
    
    /**
     * Unscale a value by DPI.
     */
    static int unscale(int value, float dpiScale);
};

} // namespace Nomad
