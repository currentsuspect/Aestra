// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <vector>
#include <sstream>
#include <chrono>

namespace NomadUI {

// Utility functions
template <typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

// ============================================================================
// Basic Types
// ============================================================================

struct NUIPoint {
    float x = 0.0f;
    float y = 0.0f;
    
    NUIPoint() = default;
    NUIPoint(float x, float y) : x(x), y(y) {}
    
    NUIPoint operator+(const NUIPoint& other) const {
        return NUIPoint(x + other.x, y + other.y);
    }
};

struct NUISize {
    float width = 0.0f;
    float height = 0.0f;
    
    NUISize() = default;
    NUISize(float w, float h) : width(w), height(h) {}
};

struct NUIRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    
    NUIRect() = default;
    NUIRect(float x, float y, float w, float h) 
        : x(x), y(y), width(w), height(h) {}
    
    bool contains(float px, float py) const {
        return px >= x && px < x + width &&
               py >= y && py < y + height;
    }
    
    bool contains(const NUIPoint& p) const {
        return contains(p.x, p.y);
    }
    
    float right() const { return x + width; }
    float bottom() const { return y + height; }
    NUIPoint center() const { return {x + width * 0.5f, y + height * 0.5f}; }
    NUIPoint getCentre() const { return center(); }
    float getWidth() const { return width; }
    float getHeight() const { return height; }
    bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
};

struct NUIColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    
    NUIColor() = default;
    NUIColor(float r, float g, float b, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
    
    // Create from hex (e.g., 0xa855f7)
    static NUIColor fromHex(uint32_t hex, float alpha = 1.0f) {
        return NUIColor(
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f,
            alpha
        );
    }
    
    // Utility colors
    static NUIColor white() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static NUIColor black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static NUIColor transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    
    NUIColor withAlpha(float newAlpha) const {
        return {r, g, b, newAlpha};
    }
    
    NUIColor withBrightness(float factor) const {
        return {r * factor, g * factor, b * factor, a};
    }
    
    NUIColor lightened(float factor) const {
        return withBrightness(1.0f + factor);
    }
    
    NUIColor darkened(float factor) const {
        return withBrightness(1.0f - factor);
    }
    
    static NUIColor lerp(const NUIColor& a, const NUIColor& b, float t) {
        return NUIColor(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        );
    }
    
    // Modern color palette
    static NUIColor Primary() { return NUIColor(0.2f, 0.4f, 0.8f, 1.0f); }
    static NUIColor Secondary() { return NUIColor(0.6f, 0.6f, 0.6f, 1.0f); }
    static NUIColor Success() { return NUIColor(0.2f, 0.7f, 0.3f, 1.0f); }
    static NUIColor Warning() { return NUIColor(0.9f, 0.6f, 0.1f, 1.0f); }
    static NUIColor Error() { return NUIColor(0.8f, 0.2f, 0.2f, 1.0f); }
    static NUIColor Info() { return NUIColor(0.1f, 0.6f, 0.8f, 1.0f); }
    
    // Dark theme colors
    static NUIColor DarkBackground() { return NUIColor(0.1f, 0.1f, 0.15f, 1.0f); }
    static NUIColor DarkSurface() { return NUIColor(0.15f, 0.15f, 0.2f, 1.0f); }
    static NUIColor DarkBorder() { return NUIColor(0.3f, 0.3f, 0.35f, 1.0f); }
    static NUIColor DarkText() { return NUIColor(0.9f, 0.9f, 0.9f, 1.0f); }
    static NUIColor DarkTextSecondary() { return NUIColor(0.6f, 0.6f, 0.6f, 1.0f); }
    
    // Light theme colors
    static NUIColor LightBackground() { return NUIColor(0.98f, 0.98f, 0.98f, 1.0f); }
    static NUIColor LightSurface() { return NUIColor(0.95f, 0.95f, 0.95f, 1.0f); }
    static NUIColor LightBorder() { return NUIColor(0.8f, 0.8f, 0.8f, 1.0f); }
    static NUIColor LightText() { return NUIColor(0.1f, 0.1f, 0.1f, 1.0f); }
    static NUIColor LightTextSecondary() { return NUIColor(0.4f, 0.4f, 0.4f, 1.0f); }
    
    // HSL color space support
    struct HSL {
        float h = 0.0f; // Hue (0-360)
        float s = 0.0f; // Saturation (0-1)
        float l = 0.0f; // Lightness (0-1)
        float a = 1.0f; // Alpha
        
        HSL() = default;
        HSL(float h, float s, float l, float a = 1.0f) : h(h), s(s), l(l), a(a) {}
    };
    
    // Convert RGB to HSL
    HSL toHSL() const {
        float max = std::max({r, g, b});
        float min = std::min({r, g, b});
        float delta = max - min;
        
        float h = 0.0f;
        if (delta != 0.0f) {
            if (max == r) {
                h = 60.0f * fmodf((g - b) / delta, 6.0f);
            } else if (max == g) {
                h = 60.0f * ((b - r) / delta + 2.0f);
            } else {
                h = 60.0f * ((r - g) / delta + 4.0f);
            }
        }
        if (h < 0.0f) h += 360.0f;
        
        float l = (max + min) / 2.0f;
        float s = (delta == 0.0f) ? 0.0f : delta / (1.0f - std::abs(2.0f * l - 1.0f));
        
        return HSL(h, s, l, a);
    }
    
    // Convert HSL to RGB
    static NUIColor fromHSL(const HSL& hsl) {
        float c = (1.0f - std::abs(2.0f * hsl.l - 1.0f)) * hsl.s;
        float x = c * (1.0f - std::abs(fmodf(hsl.h / 60.0f, 2.0f) - 1.0f));
        float m = hsl.l - c / 2.0f;
        
        float r, g, b;
        if (hsl.h < 60.0f) {
            r = c; g = x; b = 0.0f;
        } else if (hsl.h < 120.0f) {
            r = x; g = c; b = 0.0f;
        } else if (hsl.h < 180.0f) {
            r = 0.0f; g = c; b = x;
        } else if (hsl.h < 240.0f) {
            r = 0.0f; g = x; b = c;
        } else if (hsl.h < 300.0f) {
            r = x; g = 0.0f; b = c;
        } else {
            r = c; g = 0.0f; b = x;
        }
        
        return NUIColor(r + m, g + m, b + m, hsl.a);
    }
    
    // HSL manipulation
    NUIColor withHue(float hue) const {
        HSL hsl = toHSL();
        hsl.h = fmodf(hue, 360.0f);
        return fromHSL(hsl);
    }
    
    NUIColor withSaturation(float saturation) const {
        HSL hsl = toHSL();
        hsl.s = clamp(saturation, 0.0f, 1.0f);
        return fromHSL(hsl);
    }
    
    NUIColor withLightness(float lightness) const {
        HSL hsl = toHSL();
        hsl.l = clamp(lightness, 0.0f, 1.0f);
        return fromHSL(hsl);
    }
    
    // Advanced color operations
    NUIColor withContrast(float contrast) const {
        HSL hsl = toHSL();
        hsl.l = clamp(hsl.l * contrast, 0.0f, 1.0f);
        return fromHSL(hsl);
    }
    
    NUIColor withVibrance(float vibrance) const {
        HSL hsl = toHSL();
        hsl.s = clamp(hsl.s * vibrance, 0.0f, 1.0f);
        return fromHSL(hsl);
    }
    
    NUIColor complementary() const {
        HSL hsl = toHSL();
        hsl.h = fmodf(hsl.h + 180.0f, 360.0f);
        return fromHSL(hsl);
    }
    
    NUIColor triadic1() const {
        HSL hsl = toHSL();
        hsl.h = fmodf(hsl.h + 120.0f, 360.0f);
        return fromHSL(hsl);
    }
    
    NUIColor triadic2() const {
        HSL hsl = toHSL();
        hsl.h = fmodf(hsl.h + 240.0f, 360.0f);
        return fromHSL(hsl);
    }
    
    // Utility functions
    float luminance() const {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }
    
    bool isDark() const {
        return luminance() < 0.5f;
    }
    
    NUIColor textColor() const {
        return isDark() ? LightText() : DarkText();
    }
    
    // Hex color support
    uint32_t toHex() const {
        uint32_t r8 = static_cast<uint32_t>(r * 255.0f) & 0xFF;
        uint32_t g8 = static_cast<uint32_t>(g * 255.0f) & 0xFF;
        uint32_t b8 = static_cast<uint32_t>(b * 255.0f) & 0xFF;
        uint32_t a8 = static_cast<uint32_t>(a * 255.0f) & 0xFF;
        return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
    }
    
    // Smooth color transitions
    static NUIColor lerpHSL(const NUIColor& a, const NUIColor& b, float t) {
        HSL hslA = a.toHSL();
        HSL hslB = b.toHSL();
        
        // Handle hue wrapping
        float h = hslA.h;
        float hDiff = hslB.h - hslA.h;
        if (hDiff > 180.0f) hDiff -= 360.0f;
        else if (hDiff < -180.0f) hDiff += 360.0f;
        h += hDiff * t;
        if (h < 0.0f) h += 360.0f;
        if (h >= 360.0f) h -= 360.0f;
        
        return fromHSL(HSL(
            h,
            hslA.s + (hslB.s - hslA.s) * t,
            hslA.l + (hslB.l - hslA.l) * t,
            hslA.a + (hslB.a - hslA.a) * t
        ));
    }
    
    // Division operator for scaling colors
    NUIColor operator/(float scalar) const {
        return NUIColor(r / scalar, g / scalar, b / scalar, a / scalar);
    }
};

// ============================================================================
// Enums
// ============================================================================

enum class NUIMouseButton {
    None,
    Left,
    Right,
    Middle
};

enum class NUIKeyCode {
    Unknown,
    Space, Enter, Escape, Tab, Backspace, Delete,
    Left, Right, Up, Down, Home, End,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // Additional keys
    PageUp, PageDown, Insert, CapsLock, PrintScreen, ScrollLock, Pause,
    NumLock, ContextMenu, Sleep, Power, Wake
};

enum class NUIModifiers {
    None = 0,
    Shift = 1 << 0,
    Ctrl = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3  // Windows key / Command key
};

inline NUIModifiers operator|(NUIModifiers a, NUIModifiers b) {
    return static_cast<NUIModifiers>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool operator&(NUIModifiers a, NUIModifiers b) {
    return (static_cast<int>(a) & static_cast<int>(b)) != 0;
}

enum class NUIEasing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    BounceIn,
    BounceOut,
    ElasticIn,
    ElasticOut,
    BackIn,
    BackOut
};

enum class NUIAlignment {
    Start,
    Center,
    End,
    Stretch
};

enum class NUIDirection {
    Horizontal,
    Vertical
};

enum class NUITextAlignment {
    Left,
    Center,
    Right,
    Justified
};

// ============================================================================
// Event Structures
// ============================================================================

struct NUIMouseEvent {
    NUIPoint position;
    NUIPoint delta;  // For drag events
    NUIMouseButton button = NUIMouseButton::None;
    NUIModifiers modifiers = NUIModifiers::None;
    float wheelDelta = 0.0f;
    bool pressed = false;
    bool released = false;
    bool doubleClick = false;
};

struct NUIKeyEvent {
    NUIKeyCode keyCode = NUIKeyCode::Unknown;
    NUIModifiers modifiers = NUIModifiers::None;
    char character = 0;  // For text input
    bool pressed = false;
    bool released = false;
    bool repeat = false;
};

struct NUIResizeEvent {
    int width = 0;
    int height = 0;
};

// ============================================================================
// Callbacks
// ============================================================================

using NUIMouseCallback = std::function<void(const NUIMouseEvent&)>;
using NUIKeyCallback = std::function<void(const NUIKeyEvent&)>;
using NUIResizeCallback = std::function<void(const NUIResizeEvent&)>;
using NUIUpdateCallback = std::function<void(double deltaTime)>;
using NUIRenderCallback = std::function<void()>;

// ============================================================================
// Coordinate System Utilities
// ============================================================================

/**
 * @brief Create an absolute NUIRect from a parent rect and relative offsets
 * 
 * This utility helps with NomadUI's absolute coordinate system by automatically
 * calculating absolute positions from parent bounds and relative offsets.
 * 
 * @param parent The parent component's bounds (absolute coordinates)
 * @param offsetX Relative X offset from parent's left edge
 * @param offsetY Relative Y offset from parent's top edge
 * @param width Width of the child component
 * @param height Height of the child component
 * @return NUIRect with absolute screen coordinates
 * 
 * @example
 * // Instead of manually calculating:
 * NUIRect parentBounds = getBounds();
 * child->setBounds(NUIRect(parentBounds.x + 10, parentBounds.y + 20, 100, 50));
 * 
 * // Use the helper:
 * child->setBounds(NUIAbsolute(getBounds(), 10, 20, 100, 50));
 */
inline NUIRect NUIAbsolute(const NUIRect& parent, float offsetX, float offsetY, float width, float height) {
    return NUIRect(parent.x + offsetX, parent.y + offsetY, width, height);
}

/**
 * @brief Create an absolute NUIPoint from a parent rect and relative offsets
 * 
 * @param parent The parent component's bounds (absolute coordinates)
 * @param offsetX Relative X offset from parent's left edge
 * @param offsetY Relative Y offset from parent's top edge
 * @return NUIPoint with absolute screen coordinates
 * 
 * @example
 * renderer.drawText("Hello", NUIAbsolutePoint(getBounds(), 10, 20), 16, color);
 */
inline NUIPoint NUIAbsolutePoint(const NUIRect& parent, float offsetX, float offsetY) {
    return NUIPoint(parent.x + offsetX, parent.y + offsetY);
}

/**
 * @brief Create a centered absolute NUIRect within a parent rect
 * 
 * @param parent The parent component's bounds (absolute coordinates)
 * @param width Width of the child component
 * @param height Height of the child component
 * @return NUIRect centered within parent, with absolute screen coordinates
 * 
 * @example
 * child->setBounds(NUICentered(getBounds(), 200, 100));
 */
inline NUIRect NUICentered(const NUIRect& parent, float width, float height) {
    float x = parent.x + (parent.width - width) * 0.5f;
    float y = parent.y + (parent.height - height) * 0.5f;
    return NUIRect(x, y, width, height);
}

/**
 * @brief Create an absolute NUIRect aligned to parent's edges
 * 
 * @param parent The parent component's bounds (absolute coordinates)
 * @param left Left margin (or -1 to stretch to parent's left edge)
 * @param top Top margin (or -1 to stretch to parent's top edge)
 * @param right Right margin (or -1 to stretch to parent's right edge)
 * @param bottom Bottom margin (or -1 to stretch to parent's bottom edge)
 * @return NUIRect with absolute screen coordinates
 * 
 * @example
 * // Fill parent with 10px margins on all sides
 * child->setBounds(NUIAligned(getBounds(), 10, 10, 10, 10));
 * 
 * // Dock to top with 10px margins
 * child->setBounds(NUIAligned(getBounds(), 10, 10, 10, -1));
 */
inline NUIRect NUIAligned(const NUIRect& parent, float left, float top, float right, float bottom) {
    float x = parent.x + left;
    float y = parent.y + top;
    float width = parent.width - left - right;
    float height = parent.height - top - bottom;
    return NUIRect(x, y, width, height);
}

/**
 * @brief Stack children horizontally within parent with spacing
 *
 * @param parent The parent bounds
 * @param children Vector of child sizes (width, height)
 * @param spacing Spacing between children
 * @param startIndex Starting index in children vector
 * @return Vector of NUIRect for each child
 *
 * @example
 * std::vector<NUISize> childSizes = {{100, 50}, {200, 50}, {150, 50}};
 * auto rects = NUIStackHorizontal(bounds, childSizes, 10);
 * for (size_t i = 0; i < rects.size(); ++i) {
 *     children[i]->setBounds(rects[i]);
 * }
 */
inline std::vector<NUIRect> NUIStackHorizontal(const NUIRect& parent, const std::vector<NUISize>& children, float spacing, size_t startIndex = 0) {
    std::vector<NUIRect> rects;
    float currentX = parent.x + startIndex * (children[startIndex].width + spacing);
    for (size_t i = startIndex; i < children.size(); ++i) {
        float y = parent.y + (parent.height - children[i].height) / 2.0f; // Center vertically
        rects.push_back(NUIRect(currentX, y, children[i].width, children[i].height));
        currentX += children[i].width + spacing;
    }
    return rects;
}

/**
 * @brief Stack children vertically within parent with spacing
 *
 * @param parent The parent bounds
 * @param children Vector of child sizes (width, height)
 * @param spacing Spacing between children
 * @param startIndex Starting index in children vector
 * @return Vector of NUIRect for each child
 */
inline std::vector<NUIRect> NUIStackVertical(const NUIRect& parent, const std::vector<NUISize>& children, float spacing, size_t startIndex = 0) {
    std::vector<NUIRect> rects;
    float currentY = parent.y + startIndex * (children[startIndex].height + spacing);
    for (size_t i = startIndex; i < children.size(); ++i) {
        float x = parent.x + (parent.width - children[i].width) / 2.0f; // Center horizontally
        rects.push_back(NUIRect(x, currentY, children[i].width, children[i].height));
        currentY += children[i].height + spacing;
    }
    return rects;
}

/**
 * @brief Position child in a grid cell
 *
 * @param parent The parent bounds
 * @param row Row index (0-based)
 * @param col Column index (0-based)
 * @param rows Total number of rows
 * @param cols Total number of columns
 * @param width Child width (0 for full cell width)
 * @param height Child height (0 for full cell height)
 * @return NUIRect for the child
 */
inline NUIRect NUIGridCell(const NUIRect& parent, int row, int col, int rows, int cols, float width = 0, float height = 0) {
    float cellWidth = parent.width / cols;
    float cellHeight = parent.height / rows;
    float x = parent.x + col * cellWidth;
    float y = parent.y + row * cellHeight;
    if (width == 0) width = cellWidth;
    if (height == 0) height = cellHeight;
    return NUIRect(x, y, width, height);
}

/**
 * @brief Apply scroll offset to absolute coordinates
 *
 * Useful for nested scrollable containers where children need to be adjusted
 * for scroll position.
 *
 * @param rect The original absolute rect
 * @param scrollX Horizontal scroll offset
 * @param scrollY Vertical scroll offset
 * @return Adjusted NUIRect
 *
 * @example
 * // In a scrollable container's layout
 * for (auto& child : children) {
 *     NUIRect original = child->getBounds();
 *     child->setBounds(NUIApplyScrollOffset(original, 0, -scrollY));
 * }
 */
inline NUIRect NUIApplyScrollOffset(const NUIRect& rect, float scrollX, float scrollY) {
    return NUIRect(rect.x - scrollX, rect.y - scrollY, rect.width, rect.height);
}

/**
 * @brief Clamp rect to visible screen area
 *
 * Ensures popups, tooltips, and dropdowns stay on screen.
 *
 * @param rect The rect to clamp
 * @param screenWidth Screen width
 * @param screenHeight Screen height
 * @return Clamped NUIRect
 *
 * @example
 * NUIRect clamped = NUIScreenClamp(popupBounds, screenWidth, screenHeight);
 * popup->setBounds(clamped);
 */
inline NUIRect NUIScreenClamp(const NUIRect& rect, float screenWidth, float screenHeight) {
    float x = clamp(rect.x, 0.0f, screenWidth - rect.width);
    float y = clamp(rect.y, 0.0f, screenHeight - rect.height);
    return NUIRect(x, y, rect.width, rect.height);
}

/**
 * @brief Calculate relative position within parent
 *
 * Converts absolute coordinates to relative offsets from parent.
 * Useful for saving/restoring positions or animations.
 *
 * @param childRect Child's absolute bounds
 * @param parentRect Parent's absolute bounds
 * @return NUIRect with relative coordinates (x,y are offsets from parent)
 *
 * @example
 * NUIRect relative = NUIRelativePosition(child->getBounds(), parent->getBounds());
 * // relative.x is now offset from parent's left edge
 */
inline NUIRect NUIRelativePosition(const NUIRect& childRect, const NUIRect& parentRect) {
    return NUIRect(childRect.x - parentRect.x, childRect.y - parentRect.y, childRect.width, childRect.height);
}

/**
 * @brief Convert relative position back to absolute
 *
 * @param relativeRect Relative bounds (x,y are offsets from parent)
 * @param parentRect Parent's absolute bounds
 * @return NUIRect with absolute coordinates
 */
inline NUIRect NUIAbsoluteFromRelative(const NUIRect& relativeRect, const NUIRect& parentRect) {
    return NUIAbsolute(parentRect, relativeRect.x, relativeRect.y, relativeRect.width, relativeRect.height);
}

/**
 * @brief Calculate bounding rect that contains all given rects
 *
 * Useful for invalidation regions or container sizing.
 *
 * @param rects Vector of rects to union
 * @return NUIRect containing all input rects
 *
 * @example
 * std::vector<NUIRect> dirtyRects = {child1->getBounds(), child2->getBounds()};
 * NUIRect invalidationArea = NUIUnionRects(dirtyRects);
 * renderer.invalidateRegion(invalidationArea);
 */
inline NUIRect NUIUnionRects(const std::vector<NUIRect>& rects) {
    if (rects.empty()) return NUIRect(0, 0, 0, 0);
    
    float minX = rects[0].x;
    float minY = rects[0].y;
    float maxX = rects[0].x + rects[0].width;
    float maxY = rects[0].y + rects[0].height;
    
    for (const auto& rect : rects) {
        minX = std::min(minX, rect.x);
        minY = std::min(minY, rect.y);
        maxX = std::max(maxX, rect.x + rect.width);
        maxY = std::max(maxY, rect.y + rect.height);
    }
    
    return NUIRect(minX, minY, maxX - minX, maxY - minY);
}

/**
 * @brief Check if two rects intersect
 *
 * Useful for hit testing or invalidation optimization.
 *
 * @param a First rect
 * @param b Second rect
 * @return true if rects overlap
 */
inline bool NUIRectsIntersect(const NUIRect& a, const NUIRect& b) {
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < b.y);
}

// ============================================================================
// Enhanced Math Utilities
// ============================================================================

template<typename T>
constexpr T pi = static_cast<T>(3.14159265358979323846);

template<typename T>
constexpr T deg2rad = static_cast<T>(0.017453292519943295);

template<typename T>
constexpr T rad2deg = static_cast<T>(57.29577951308232);

template<typename T>
T lerp(T a, T b, T t) {
    return a + (b - a) * t;
}

template<typename T>
T smoothstep(T edge0, T edge1, T x) {
    T t = clamp((x - edge0) / (edge1 - edge0), static_cast<T>(0), static_cast<T>(1));
    return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
}

template<typename T>
T easeInOut(T t) {
    return t < static_cast<T>(0.5) ? 
           static_cast<T>(2) * t * t : 
           static_cast<T>(1) - static_cast<T>(2) * (-t + static_cast<T>(2)) * (-t + static_cast<T>(2)) / static_cast<T>(2);
}

template<typename T>
T degreesToRadians(T degrees) {
    return degrees * deg2rad<T>;
}

template<typename T>
T radiansToDegrees(T radians) {
    return radians * rad2deg<T>;
}

// Distance calculations
inline float distance(const NUIPoint& a, const NUIPoint& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline float distanceSquared(const NUIPoint& a, const NUIPoint& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

// ============================================================================
// Enhanced Rectangle Operations
// ============================================================================

struct NUIRectangleInt {
    int x = 0, y = 0, width = 0, height = 0;
    
    NUIRectangleInt() = default;
    NUIRectangleInt(int x, int y, int w, int h) : x(x), y(y), width(w), height(h) {}
    
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    
    int right() const { return x + width; }
    int bottom() const { return y + height; }
};

// Rectangle expansion and contraction
inline NUIRect expandRect(const NUIRect& rect, float amount) {
    return NUIRect(rect.x - amount, rect.y - amount, 
                   rect.width + amount * 2.0f, rect.height + amount * 2.0f);
}

inline NUIRect intersectRects(const NUIRect& a, const NUIRect& b) {
    float left = std::max(a.x, b.x);
    float top = std::max(a.y, b.y);
    float right = std::min(a.x + a.width, b.x + b.width);
    float bottom = std::min(a.y + a.height, b.y + b.height);
    
    if (right <= left || bottom <= top) {
        return NUIRect(0, 0, 0, 0); // No intersection
    }
    
    return NUIRect(left, top, right - left, bottom - top);
}

inline NUIRect unionRects(const NUIRect& a, const NUIRect& b) {
    float left = std::min(a.x, b.x);
    float top = std::min(a.y, b.y);
    float right = std::max(a.x + a.width, b.x + b.width);
    float bottom = std::max(a.y + a.height, b.y + b.height);
    
    return NUIRect(left, top, right - left, bottom - top);
}

// ============================================================================
// Enhanced Event Types
// ============================================================================

struct NUIDragEvent {
    NUIPoint position;
    NUIPoint startPosition;
    NUIPoint delta;
    NUIMouseButton button = NUIMouseButton::None;
    NUIModifiers modifiers = NUIModifiers::None;
    bool pressed = false;
    bool released = false;
};

struct NUIScrollEvent {
    NUIPoint position;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    NUIModifiers modifiers = NUIModifiers::None;
};

struct NUIFocusEvent {
    bool focused = false;
    NUIModifiers modifiers = NUIModifiers::None;
};

struct NUIDropEvent {
    NUIPoint position;
    std::vector<std::string> filePaths;
    std::string text;
    bool isFiles = false;
};

// ============================================================================
// Advanced Color Utilities
// ============================================================================

struct NUIColorHSV {
    float h = 0.0f; // Hue (0-360)
    float s = 0.0f; // Saturation (0-1)
    float v = 0.0f; // Value (0-1)
    float a = 1.0f; // Alpha (0-1)
    
    NUIColorHSV() = default;
    NUIColorHSV(float h, float s, float v, float a = 1.0f) : h(h), s(s), v(v), a(a) {}
};

// Convert RGB to HSV
inline NUIColorHSV rgbToHsv(const NUIColor& rgb) {
    float r = rgb.r, g = rgb.g, b = rgb.b;
    float max = std::max({r, g, b});
    float min = std::min({r, g, b});
    float delta = max - min;
    
    float h = 0.0f;
    if (delta != 0.0f) {
        if (max == r) {
            h = 60.0f * fmodf((g - b) / delta, 6.0f);
        } else if (max == g) {
            h = 60.0f * ((b - r) / delta + 2.0f);
        } else {
            h = 60.0f * ((r - g) / delta + 4.0f);
        }
    }
    if (h < 0.0f) h += 360.0f;
    
    float s = max == 0.0f ? 0.0f : delta / max;
    float v = max;
    
    return NUIColorHSV(h, s, v, rgb.a);
}

// Convert HSV to RGB
inline NUIColor hsvToRgb(const NUIColorHSV& hsv) {
    float c = hsv.v * hsv.s;
    float x = c * (1.0f - std::abs(fmodf(hsv.h / 60.0f, 2.0f) - 1.0f));
    float m = hsv.v - c;
    
    float r, g, b;
    if (hsv.h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (hsv.h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (hsv.h < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (hsv.h < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (hsv.h < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }
    
    return NUIColor(r + m, g + m, b + m, hsv.a);
}

// ============================================================================
// String Utilities
// ============================================================================

struct NUIStringUtils {
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    static std::string trim(const std::string& str) {
        const std::string whitespace = " \t\n\r";
        size_t start = str.find_first_not_of(whitespace);
        if (start == std::string::npos) return "";
        
        size_t end = str.find_last_not_of(whitespace);
        return str.substr(start, end - start + 1);
    }
    
    static bool startsWith(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() && 
               str.compare(0, prefix.size(), prefix) == 0;
    }
    
    static bool endsWith(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    
    static std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    static std::string toUpper(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }
};

// ============================================================================
// File System Utilities
// ============================================================================

struct NUIFileUtils {
    static std::string getExtension(const std::string& path) {
        size_t dot = path.find_last_of(".");
        if (dot == std::string::npos) return "";
        return path.substr(dot + 1);
    }
    
    static std::string getFilename(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return path;
        return path.substr(slash + 1);
    }
    
    static std::string getDirectory(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return "";
        return path.substr(0, slash);
    }
    
    static std::string getFilenameWithoutExtension(const std::string& path) {
        std::string filename = getFilename(path);
        size_t dot = filename.find_last_of(".");
        if (dot == std::string::npos) return filename;
        return filename.substr(0, dot);
    }
};

// ============================================================================
// Device and Display Utilities
// ============================================================================

struct NUIDeviceInfo {
    float dpi = 96.0f;
    float scale = 1.0f;
    int screenWidth = 1920;
    int screenHeight = 1080;
    bool isHighDPI = false;
};

struct NUIDisplayMetrics {
    static float getDPIScale() {
        // This would be platform-specific in a real implementation
        return 1.0f; // Default fallback
    }
    
    static float scaleFromDPI(float dpi) {
        return dpi / 96.0f;
    }
    
    static float invScaleFromDPI(float dpi) {
        return 96.0f / dpi;
    }
};

// ============================================================================
// Animation and Easing
// ============================================================================

struct NUIAnimationCurve {
    static float easeInQuad(float t) { return t * t; }
    static float easeOutQuad(float t) { return t * (2.0f - t); }
    static float easeInOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
    
    static float easeInCubic(float t) { return t * t * t; }
    static float easeOutCubic(float t) { return (--t) * t * t + 1.0f; }
    static float easeInOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f; }
    
    static float easeInQuart(float t) { return t * t * t * t; }
    static float easeOutQuart(float t) { return 1.0f - (--t) * t * t * t; }
    static float easeInOutQuart(float t) { return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * (--t) * t * t * t; }
    
    static float easeInQuint(float t) { return t * t * t * t * t; }
    static float easeOutQuint(float t) { return 1.0f + (--t) * t * t * t * t; }
    static float easeInOutQuint(float t) { return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f + 16.0f * (--t) * t * t * t * t; }
    
    // Sine-based easing
    static float easeInSine(float t) { return 1.0f - std::cos(t * pi<float> * 0.5f); }
    static float easeOutSine(float t) { return std::sin(t * pi<float> * 0.5f); }
    static float easeInOutSine(float t) { return -(std::cos(pi<float> * t) - 1.0f) * 0.5f; }
    
    // Exponential easing
    static float easeInExpo(float t) { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
    static float easeOutExpo(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    static float easeInOutExpo(float t) {
        if (t == 0.0f || t == 1.0f) return t;
        if (t < 0.5f) return 0.5f * std::pow(2.0f, 20.0f * t - 10.0f);
        return 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
    }
    
    // Back easing (overshoot)
    static float easeInBack(float t) {
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        return c3 * t * t * t - c1 * t * t;
    }
    
    static float easeOutBack(float t) {
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }
    
    static float easeInOutBack(float t) {
        float c1 = 1.70158f;
        float c2 = c1 * 1.525f;
        if (t < 0.5f) {
            float x = 2.0f * t;
            return (c2 + 1.0f) * x * x * x - c2 * x * x;
        }
        float x = 2.0f * t - 2.0f;
        return 1.0f + (c2 + 1.0f) * std::pow(x, 3.0f) + c2 * std::pow(x, 2.0f);
    }
};

// ============================================================================
// Advanced Callbacks
// ============================================================================

using NUIDragCallback = std::function<void(const NUIDragEvent&)>;
using NUIScrollCallback = std::function<void(const NUIScrollEvent&)>;
using NUIFocusCallback = std::function<void(const NUIFocusEvent&)>;
using NUIDropCallback = std::function<void(const NUIDropEvent&)>;

// ============================================================================
// Performance Monitoring
// ============================================================================

struct NUIProfiler {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    static TimePoint now() { return Clock::now(); }
    
    static double elapsedSeconds(const TimePoint& start, const TimePoint& end) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000000.0;
    }
    
    static long long elapsedMicroseconds(const TimePoint& start, const TimePoint& end) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count();
    }
};

// ============================================================================
// Data Structures
// ============================================================================

template<typename T>
struct NUIOptional {
private:
    T value_;
    bool hasValue_;
    
public:
    NUIOptional() : hasValue_(false) {}
    NUIOptional(const T& value) : value_(value), hasValue_(true) {}
    
    bool hasValue() const { return hasValue_; }
    const T& value() const { return value_; }
    T& value() { return value_; }
    const T& valueOr(const T& defaultValue) const { return hasValue_ ? value_ : defaultValue; }
};

template<typename T>
struct NUIWeakPtr {
    std::weak_ptr<T> ptr_;
    
    NUIWeakPtr() = default;
    NUIWeakPtr(const std::shared_ptr<T>& shared) : ptr_(shared) {}
    
    std::shared_ptr<T> lock() const { return ptr_.lock(); }
    bool expired() const { return ptr_.expired(); }
};

// ============================================================================
// Memory Management
// ============================================================================

struct NUIMemory {
    template<typename T>
    static void safeDelete(T*& ptr) {
        if (ptr) {
            delete ptr;
            ptr = nullptr;
        }
    }
    
    template<typename T>
    static void safeDeleteArray(T*& ptr) {
        if (ptr) {
            delete[] ptr;
            ptr = nullptr;
        }
    }
    
    static size_t getMemoryUsage() {
        // Platform-specific implementation would go here
        return 0;
    }
};

} // namespace NomadUI
