// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUITypes.h"
#include <string>
#include <vector>
#include <memory>

// Forward declaration for NanoSVG
struct NSVGimage;

namespace NomadUI {

// Forward declarations
class NUIRenderer;

/**
 * SVG Path Command Types
 */
enum class NUISVGCommandType {
    MoveTo,
    LineTo,
    CurveTo,
    QuadraticCurveTo,
    ArcTo,
    ClosePath
};

/**
 * SVG Path Command
 */
struct NUISVGCommand {
    NUISVGCommandType type;
    std::vector<float> params;
    
    NUISVGCommand(NUISVGCommandType t) : type(t) {}
};

/**
 * SVG Transform - represents a transform matrix
 */
struct NUISVGTransform {
    float translateX = 0.0f;
    float translateY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotation = 0.0f;
    
    NUIPoint apply(const NUIPoint& point) const {
        // Apply scale, rotation, then translation
        float x = point.x * scaleX;
        float y = point.y * scaleY;
        
        // Apply rotation (if needed)
        if (rotation != 0.0f) {
            float rad = rotation * 3.14159f / 180.0f;
            float cosR = std::cos(rad);
            float sinR = std::sin(rad);
            float newX = x * cosR - y * sinR;
            float newY = x * sinR + y * cosR;
            x = newX;
            y = newY;
        }
        
        return NUIPoint(x + translateX, y + translateY);
    }
};

/**
 * SVG Path - represents a single path element
 */
class NUISVGPath {
public:
    NUISVGPath() = default;
    
    void addCommand(const NUISVGCommand& cmd) { commands_.push_back(cmd); }
    const std::vector<NUISVGCommand>& getCommands() const { return commands_; }
    
    void setFillColor(const NUIColor& color) { fillColor_ = color; hasFill_ = true; }
    void setStrokeColor(const NUIColor& color) { strokeColor_ = color; hasStroke_ = true; }
    void setStrokeWidth(float width) { strokeWidth_ = width; }
    void setTransform(const NUISVGTransform& transform) { transform_ = transform; hasTransform_ = true; }
    
    bool hasFill() const { return hasFill_; }
    bool hasStroke() const { return hasStroke_; }
    bool hasTransform() const { return hasTransform_; }
    NUIColor getFillColor() const { return fillColor_; }
    NUIColor getStrokeColor() const { return strokeColor_; }
    float getStrokeWidth() const { return strokeWidth_; }
    const NUISVGTransform& getTransform() const { return transform_; }
    
private:
    std::vector<NUISVGCommand> commands_;
    NUIColor fillColor_ = NUIColor::black();
    NUIColor strokeColor_ = NUIColor::black();
    float strokeWidth_ = 1.0f;
    bool hasFill_ = false;
    bool hasStroke_ = false;
    NUISVGTransform transform_;
    bool hasTransform_ = false;
};

/**
 * SVG Document - represents a complete SVG
 */
class NUISVGDocument {
public:
    NUISVGDocument() = default;
    ~NUISVGDocument();
    
    // Disable copying (NSVGimage can't be easily copied)
    NUISVGDocument(const NUISVGDocument&) = delete;
    NUISVGDocument& operator=(const NUISVGDocument&) = delete;
    
    // Enable moving with proper ownership transfer
    NUISVGDocument(NUISVGDocument&& other) noexcept;
    NUISVGDocument& operator=(NUISVGDocument&& other) noexcept;
    
    void setViewBox(float x, float y, float width, float height) {
        viewBox_ = NUIRect(x, y, width, height);
        hasViewBox_ = true;
    }
    
    void setSize(float width, float height) {
        width_ = width;
        height_ = height;
    }
    
    void addPath(std::shared_ptr<NUISVGPath> path) { paths_.push_back(path); }
    
    const std::vector<std::shared_ptr<NUISVGPath>>& getPaths() const { return paths_; }
    NUIRect getViewBox() const { return viewBox_; }
    bool hasViewBox() const { return hasViewBox_; }
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }
    
    // NanoSVG integration
    void setNSVGImage(NSVGimage* image) { nsvgImage_ = image; }
    NSVGimage* getNSVGImage() const { return nsvgImage_; }
    bool hasNSVGImage() const { return nsvgImage_ != nullptr; }
    
private:
    std::vector<std::shared_ptr<NUISVGPath>> paths_;
    NUIRect viewBox_;
    bool hasViewBox_ = false;
    float width_ = 0.0f;
    float height_ = 0.0f;
    NSVGimage* nsvgImage_ = nullptr;
};

/**
 * SVG Parser - parses SVG strings into renderable documents
 */
class NUISVGParser {
public:
    static std::shared_ptr<NUISVGDocument> parse(const std::string& svgContent);
    static std::shared_ptr<NUISVGDocument> parseFile(const std::string& filePath);
    
private:
    static std::shared_ptr<NUISVGPath> parsePath(const std::string& pathData);
    static NUIColor parseColor(const std::string& colorStr);
    static std::vector<float> parseNumbers(const std::string& str);
    static NUISVGTransform parseTransform(const std::string& transformStr);
};

/**
 * SVG Renderer - renders SVG documents using NUIRenderer
 */
class NUISVGRenderer {
public:
    static void render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds);
    static void render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds, const NUIColor& tintColor);
    
private:
    static void renderPath(NUIRenderer& renderer, const NUISVGPath& path, const NUIRect& bounds, const NUIRect& viewBox);
    static NUIPoint transformPoint(const NUIPoint& point, const NUIRect& viewBox, const NUIRect& bounds);
};

} // namespace NomadUI
