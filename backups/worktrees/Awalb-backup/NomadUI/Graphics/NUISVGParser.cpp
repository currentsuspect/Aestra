// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUISVGParser.h"
#include "NUIRenderer.h"
#include "NUISVGCache.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <iostream>

// Include NanoSVG with implementation
#define NANOSVG_IMPLEMENTATION
#include "../External/nanosvg.h"

// Include NanoSVG rasterizer with implementation
#define NANOSVGRAST_IMPLEMENTATION
#include "../External/nanosvgrast.h"

namespace NomadUI {

// Static cache instance for SVG rasterization
static NUISVGCache svgCache;

// ============================================================================
// NUISVGDocument Implementation
// ============================================================================

NUISVGDocument::~NUISVGDocument() {
    if (nsvgImage_) {
        nsvgDelete(nsvgImage_);
        nsvgImage_ = nullptr;
    }
}

NUISVGDocument::NUISVGDocument(NUISVGDocument&& other) noexcept
    : paths_(std::move(other.paths_))
    , viewBox_(other.viewBox_)
    , hasViewBox_(other.hasViewBox_)
    , width_(other.width_)
    , height_(other.height_)
    , nsvgImage_(other.nsvgImage_)
{
    other.nsvgImage_ = nullptr;
}

NUISVGDocument& NUISVGDocument::operator=(NUISVGDocument&& other) noexcept {
    if (this != &other) {
        // Clean up existing NSVGimage
        if (nsvgImage_) {
            nsvgDelete(nsvgImage_);
        }
        
        // Move data from other
        paths_ = std::move(other.paths_);
        viewBox_ = other.viewBox_;
        hasViewBox_ = other.hasViewBox_;
        width_ = other.width_;
        height_ = other.height_;
        nsvgImage_ = other.nsvgImage_;
        
        // Ensure other's pointer is null
        other.nsvgImage_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Helper Functions
// ============================================================================

// ============================================================================
// Helper Functions
// ============================================================================

static std::string trim(const std::string& str) {
    size_t start = 0;
    size_t end = str.length();
    
    while (start < end && std::isspace(str[start])) start++;
    while (end > start && std::isspace(str[end - 1])) end--;
    
    return str.substr(start, end - start);
}

static std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

// ============================================================================
// NUISVGParser Implementation
// ============================================================================

std::shared_ptr<NUISVGDocument> NUISVGParser::parse(const std::string& svgContent) {
    // NanoSVG requires null-terminated mutable string
    std::vector<char> buffer(svgContent.begin(), svgContent.end());
    buffer.push_back('\0');
    
    // Parse with NanoSVG
    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.0f);
    
    if (!image) {
        std::cerr << "NanoSVG: Failed to parse SVG content (length: " << svgContent.length() << " bytes)" << std::endl;
        return nullptr;
    }
    
    // Create document
    auto doc = std::make_shared<NUISVGDocument>();
    doc->setSize(image->width, image->height);
    doc->setViewBox(0, 0, image->width, image->height);
    doc->setNSVGImage(image);
    
    return doc;
}

std::shared_ptr<NUISVGDocument> NUISVGParser::parseFile(const std::string& filePath) {
    // Parse with NanoSVG
    NSVGimage* image = nsvgParseFromFile(filePath.c_str(), "px", 96.0f);
    
    if (!image) {
        std::cerr << "NanoSVG: Failed to parse SVG file: " << filePath << std::endl;
        return nullptr;
    }
    
    // Create document
    auto doc = std::make_shared<NUISVGDocument>();
    doc->setSize(image->width, image->height);
    doc->setViewBox(0, 0, image->width, image->height);
    doc->setNSVGImage(image);
    
    return doc;
}

std::shared_ptr<NUISVGPath> NUISVGParser::parsePath(const std::string& pathData) {
    auto path = std::make_shared<NUISVGPath>();
    
    std::string data = pathData;
    size_t i = 0;
    
    while (i < data.length()) {
        // Skip whitespace
        while (i < data.length() && std::isspace(data[i])) i++;
        if (i >= data.length()) break;
        
        char cmd = data[i++];
        
        // Parse numbers following the command
        std::string numStr;
        while (i < data.length() && (std::isdigit(data[i]) || data[i] == '.' || data[i] == '-' || data[i] == ',' || std::isspace(data[i]))) {
            if (data[i] != ',') {
                numStr += data[i];
            } else {
                numStr += ' ';
            }
            i++;
        }
        
        auto numbers = parseNumbers(numStr);
        
        switch (cmd) {
            case 'M': // MoveTo (absolute)
            case 'm': // MoveTo (relative)
                if (numbers.size() >= 2) {
                    NUISVGCommand moveCmd(NUISVGCommandType::MoveTo);
                    moveCmd.params = {numbers[0], numbers[1]};
                    path->addCommand(moveCmd);
                }
                break;
                
            case 'L': // LineTo (absolute)
            case 'l': // LineTo (relative)
                if (numbers.size() >= 2) {
                    NUISVGCommand lineCmd(NUISVGCommandType::LineTo);
                    lineCmd.params = {numbers[0], numbers[1]};
                    path->addCommand(lineCmd);
                }
                break;
                
            case 'H': // Horizontal line (absolute)
            case 'h': // Horizontal line (relative)
                if (numbers.size() >= 1) {
                    NUISVGCommand lineCmd(NUISVGCommandType::LineTo);
                    lineCmd.params = {numbers[0], 0.0f}; // Y will be handled by renderer
                    path->addCommand(lineCmd);
                }
                break;
                
            case 'V': // Vertical line (absolute)
            case 'v': // Vertical line (relative)
                if (numbers.size() >= 1) {
                    NUISVGCommand lineCmd(NUISVGCommandType::LineTo);
                    lineCmd.params = {0.0f, numbers[0]}; // X will be handled by renderer
                    path->addCommand(lineCmd);
                }
                break;
                
            case 'C': // Cubic Bezier (absolute)
            case 'c': // Cubic Bezier (relative)
                if (numbers.size() >= 6) {
                    NUISVGCommand curveCmd(NUISVGCommandType::CurveTo);
                    curveCmd.params = {numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]};
                    path->addCommand(curveCmd);
                }
                break;
                
            case 'Z': // ClosePath
            case 'z':
                path->addCommand(NUISVGCommand(NUISVGCommandType::ClosePath));
                break;
        }
    }
    
    return path;
}

NUIColor NUISVGParser::parseColor(const std::string& colorStr) {
    std::string color = trim(colorStr);
    
    // Handle hex colors
    if (color[0] == '#') {
        std::string hex = color.substr(1);
        unsigned int value = std::stoul(hex, nullptr, 16);
        
        if (hex.length() == 6) {
            float r = ((value >> 16) & 0xFF) / 255.0f;
            float g = ((value >> 8) & 0xFF) / 255.0f;
            float b = (value & 0xFF) / 255.0f;
            return NUIColor(r, g, b, 1.0f);
        }
    }
    
    // Handle named colors
    if (color == "black") return NUIColor::black();
    if (color == "white") return NUIColor::white();
    if (color == "red") return NUIColor(1.0f, 0.0f, 0.0f, 1.0f);
    if (color == "green") return NUIColor(0.0f, 1.0f, 0.0f, 1.0f);
    if (color == "blue") return NUIColor(0.0f, 0.0f, 1.0f, 1.0f);
    
    // Default to black
    return NUIColor::black();
}

std::vector<float> NUISVGParser::parseNumbers(const std::string& str) {
    std::vector<float> numbers;
    std::stringstream ss(str);
    float num;
    
    while (ss >> num) {
        numbers.push_back(num);
    }
    
    return numbers;
}

NUISVGTransform NUISVGParser::parseTransform(const std::string& transformStr) {
    NUISVGTransform transform;
    
    // Parse translate(x, y) or translate(x y)
    size_t translatePos = transformStr.find("translate(");
    if (translatePos != std::string::npos) {
        translatePos += 10; // Skip "translate("
        size_t endPos = transformStr.find(")", translatePos);
        if (endPos != std::string::npos) {
            std::string params = transformStr.substr(translatePos, endPos - translatePos);
            auto numbers = parseNumbers(params);
            if (numbers.size() >= 1) {
                transform.translateX = numbers[0];
                if (numbers.size() >= 2) {
                    transform.translateY = numbers[1];
                }
            }
        }
    }
    
    // Parse scale(x, y) or scale(x)
    size_t scalePos = transformStr.find("scale(");
    if (scalePos != std::string::npos) {
        scalePos += 6; // Skip "scale("
        size_t endPos = transformStr.find(")", scalePos);
        if (endPos != std::string::npos) {
            std::string params = transformStr.substr(scalePos, endPos - scalePos);
            auto numbers = parseNumbers(params);
            if (numbers.size() >= 1) {
                transform.scaleX = numbers[0];
                transform.scaleY = numbers.size() >= 2 ? numbers[1] : numbers[0];
            }
        }
    }
    
    // Parse rotate(angle)
    size_t rotatePos = transformStr.find("rotate(");
    if (rotatePos != std::string::npos) {
        rotatePos += 7; // Skip "rotate("
        size_t endPos = transformStr.find(")", rotatePos);
        if (endPos != std::string::npos) {
            std::string params = transformStr.substr(rotatePos, endPos - rotatePos);
            auto numbers = parseNumbers(params);
            if (numbers.size() >= 1) {
                transform.rotation = numbers[0];
            }
        }
    }
    
    return transform;
}

// ============================================================================
// NUISVGRenderer Implementation
// ============================================================================

void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds) {
    // Call the tinted version with no tint (alpha = 0)
    render(renderer, svg, bounds, NUIColor(1.0f, 1.0f, 1.0f, 0.0f));
}

void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds, const NUIColor& tintColor) {
    // Get NSVGimage pointer from document
    NSVGimage* image = svg.getNSVGImage();
    if (!image) {
        std::cerr << "NanoSVG: No image data in SVG document" << std::endl;
        return;
    }
    
    // Calculate target dimensions from bounds
    int w = static_cast<int>(bounds.width);
    int h = static_cast<int>(bounds.height);
    
    if (w <= 0 || h <= 0) {
        std::cerr << "NanoSVG: Invalid render dimensions: " << w << "x" << h << std::endl;
        return;
    }
    
    // Create CacheKey from document, dimensions, and tint color
    NUISVGCache::CacheKey key{&svg, w, h, tintColor};
    
    // Check cache for existing entry before rasterizing
    const auto* cached = svgCache.get(key);
    
    if (cached) {
        // If cache hit, use cached RGBA data and skip rasterization
        renderer.drawTexture(bounds, cached->rgba.data(), cached->width, cached->height);
        return;
    }
    
    // If cache miss, rasterize and store result in cache
    
    // Calculate scale factor to maintain aspect ratio
    float scaleX = bounds.width / image->width;
    float scaleY = bounds.height / image->height;
    float scale = std::min(scaleX, scaleY);
    
    // Allocate RGBA buffer (width Ã— height Ã— 4 bytes)
    std::vector<unsigned char> rgba(w * h * 4);
    
    // Create NSVGrasterizer
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        std::cerr << "NanoSVG: Failed to create rasterizer" << std::endl;
        return;
    }
    
    // Call nsvgRasterize with appropriate parameters
    nsvgRasterize(rast, image, 0, 0, scale, rgba.data(), w, h, w * 4);
    
    // Delete rasterizer
    nsvgDeleteRasterizer(rast);
    
    // Apply color tinting if specified
    if (tintColor.a > 0.0f) {
        for (int i = 0; i < w * h; ++i) {
            int idx = i * 4;
            // Multiply RGB values by tint color RGB components
            rgba[idx + 0] = static_cast<unsigned char>(rgba[idx + 0] * tintColor.r);
            rgba[idx + 1] = static_cast<unsigned char>(rgba[idx + 1] * tintColor.g);
            rgba[idx + 2] = static_cast<unsigned char>(rgba[idx + 2] * tintColor.b);
            // Multiply alpha values by tint color alpha component
            rgba[idx + 3] = static_cast<unsigned char>(rgba[idx + 3] * tintColor.a);
        }
    }
    
    // Store result in cache
    svgCache.put(key, std::move(rgba), w, h);
    
    // Retrieve entry from cache after storing and render
    const auto* entry = svgCache.get(key);
    if (entry) {
        renderer.drawTexture(bounds, entry->rgba.data(), entry->width, entry->height);
    }
}

void NUISVGRenderer::renderPath(NUIRenderer& renderer, const NUISVGPath& path, const NUIRect& bounds, const NUIRect& viewBox) {
    const auto& commands = path.getCommands();
    if (commands.empty()) return;
    
    NUIPoint currentPoint(0, 0);
    NUIPoint startPoint(0, 0);
    std::vector<NUIPoint> pathPoints;
    
    // Get transform if present
    const NUISVGTransform& transform = path.getTransform();
    
    // Collect all points for fill rendering
    for (const auto& cmd : commands) {
        switch (cmd.type) {
            case NUISVGCommandType::MoveTo:
                if (cmd.params.size() >= 2) {
                    NUIPoint p(cmd.params[0], cmd.params[1]);
                    // Apply transform first, then viewBox transform
                    if (path.hasTransform()) {
                        p = transform.apply(p);
                    }
                    currentPoint = transformPoint(p, viewBox, bounds);
                    startPoint = currentPoint;
                    pathPoints.push_back(currentPoint);
                }
                break;
                
            case NUISVGCommandType::LineTo:
                if (cmd.params.size() >= 2) {
                    NUIPoint p(cmd.params[0], cmd.params[1]);
                    // Apply transform first, then viewBox transform
                    if (path.hasTransform()) {
                        p = transform.apply(p);
                    }
                    NUIPoint nextPoint = transformPoint(p, viewBox, bounds);
                    pathPoints.push_back(nextPoint);
                    currentPoint = nextPoint;
                }
                break;
                
            case NUISVGCommandType::ClosePath:
                if (!pathPoints.empty()) {
                    NUIPoint lastPoint = pathPoints.back();
                    if (lastPoint.x != startPoint.x || lastPoint.y != startPoint.y) {
                        pathPoints.push_back(startPoint);
                    }
                }
                currentPoint = startPoint;
                break;
                
            default:
                break;
        }
    }
    
    // Render fill if present - use triangle fan for convex polygons
    if (path.hasFill() && pathPoints.size() >= 3) {
        // Calculate bounding box for the filled shape
        float minX = pathPoints[0].x, maxX = pathPoints[0].x;
        float minY = pathPoints[0].y, maxY = pathPoints[0].y;
        
        for (const auto& p : pathPoints) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        
        // For simple filled shapes, use fillRect as approximation
        // This works well for rectangles and simple polygons
        NUIRect fillBounds(minX, minY, maxX - minX, maxY - minY);
        renderer.fillRect(fillBounds, path.getFillColor());
    }
    
    // Render stroke
    if (path.hasStroke() && pathPoints.size() >= 2) {
        for (size_t i = 0; i < pathPoints.size() - 1; ++i) {
            renderer.drawLine(pathPoints[i], pathPoints[i + 1], path.getStrokeWidth(), path.getStrokeColor());
        }
    }
}

NUIPoint NUISVGRenderer::transformPoint(const NUIPoint& point, const NUIRect& viewBox, const NUIRect& bounds) {
    // Transform from viewBox coordinates to bounds coordinates
    float scaleX = bounds.width / viewBox.width;
    float scaleY = bounds.height / viewBox.height;
    
    float x = bounds.x + (point.x - viewBox.x) * scaleX;
    float y = bounds.y + (point.y - viewBox.y) * scaleY;
    
    return NUIPoint(x, y);
}

} // namespace NomadUI
