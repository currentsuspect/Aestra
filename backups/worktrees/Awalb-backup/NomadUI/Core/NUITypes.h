// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <vector>

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
    Left, Right, Up, Down,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
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
             a.y + a.height < b.y || b.y + b.height < a.y);
}

} // namespace NomadUI
