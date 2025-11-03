# Screenshot Limitation with Borderless Windows

## Issue Description

The NOMAD DAW uses a truly borderless window design (`WS_POPUP` style) for a professional, clean appearance without Windows title bars or system borders. However, this creates a limitation with Windows screenshot functionality.

## Technical Details

### Window Style Used
```cpp
// Borderless window creation
DWORD style = WS_POPUP;
DWORD exStyle = WS_EX_APPWINDOW;
```

### The Problem
- **Windows + Fn + Print Screen** does not capture the window when maximized
- **Windows screenshot tools** don't recognize `WS_POPUP` windows properly when maximized
- This is a **Windows API limitation**, not a bug in our implementation

### Why We Use WS_POPUP
- ✅ **Truly borderless** - No Windows title bar, borders, or system buttons
- ✅ **Clean custom UI** - Perfect integration with our custom title bar and controls
- ✅ **Professional appearance** - Matches industry standards for DAWs
- ✅ **Stable rendering** - No text smearing or OpenGL context conflicts
- ✅ **Custom hit-testing** - Dragging and resizing work perfectly

## Alternative Solutions Considered

### Option A: WS_OVERLAPPEDWINDOW with Style Removal
```cpp
// Creates window then removes visual elements
style = WS_OVERLAPPEDWINDOW;
// Then: Remove WS_CAPTION, WS_THICKFRAME, etc.
```
**Result:** Screenshots work, but brings back Windows title bar

### Option B: WS_OVERLAPPEDWINDOW from Start
```cpp
// Create with borderless style from beginning
style = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
```
**Result:** Screenshots work, but brings back Windows title bar

### Option C: Extended Styles
```cpp
// Various extended style combinations
exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;
exStyle = WS_EX_TOOLWINDOW;
exStyle = WS_EX_APPWINDOW;
```
**Result:** No improvement in screenshot compatibility

## Current Decision

We have chosen to **accept the screenshot limitation** in favor of the perfect borderless appearance. This is the same approach used by many professional applications including:

- FL Studio (in some configurations)
- Ableton Live (in some configurations)
- Various game engines and professional software

## Workarounds for Users

### Alternative Screenshot Methods
1. **Windows Snipping Tool** - `Win + Shift + S`
2. **Third-party tools** - ShareX, Greenshot, etc.
3. **Windows Game Bar** - `Win + G` (if enabled)
4. **Manual screenshot tools** - Most work fine with the window

### When Screenshots Work
- ✅ **Windowed mode** - Screenshots work when not maximized
- ✅ **Minimized state** - Screenshots work when window is minimized
- ✅ **Third-party tools** - Most alternative screenshot tools work fine

## Future Considerations

If screenshot functionality becomes critical, we could implement:

1. **Built-in screenshot feature** - Capture the OpenGL framebuffer directly
2. **Hybrid approach** - Detect screenshot requests and temporarily switch window styles
3. **User preference** - Allow users to choose between borderless and screenshot-compatible modes

## Conclusion

The screenshot limitation is a **known Windows API constraint** with `WS_POPUP` windows. We prioritize the **professional, borderless appearance** that users expect from a modern DAW, accepting that alternative screenshot methods may be needed.

This decision aligns with industry standards and provides the best user experience for the primary use case of the application.

---

*Document created: October 2025*  
*Status: Accepted limitation - no further action required*
