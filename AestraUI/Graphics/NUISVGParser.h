// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <string>
#include <memory>

namespace AestraUI {

// Forward declarations
class NUIRenderer;

/**
 * SVG Document — parsed SVG ready for rendering.
 *
 * Stores the original SVG source (memory buffer or file path) so that
 * ThorVG can parse and rasterize it on demand.  Rasterized outputs are
 * cached by NUISVGCache to avoid redundant work.
 */
class NUISVGDocument {
public:
    enum class SourceType {
        Memory, // SVG loaded from an in-memory string
        File    // SVG loaded from a file path
    };

    NUISVGDocument() = default;
    ~NUISVGDocument() = default;

    NUISVGDocument(const NUISVGDocument&) = delete;
    NUISVGDocument& operator=(const NUISVGDocument&) = delete;

    NUISVGDocument(NUISVGDocument&& other) noexcept;
    NUISVGDocument& operator=(NUISVGDocument&& other) noexcept;

    void setSVGContent(std::string content) {
        svgContent_ = std::move(content);
        sourceType_ = SourceType::Memory;
    }

    void setFilePath(std::string path) {
        filePath_ = std::move(path);
        sourceType_ = SourceType::File;
    }

    const std::string& getSVGContent() const { return svgContent_; }
    const std::string& getFilePath() const { return filePath_; }
    SourceType getSourceType() const { return sourceType_; }

    void setViewBox(float x, float y, float width, float height) {
        viewBox_ = NUIRect(x, y, width, height);
        hasViewBox_ = true;
    }

    void setSize(float width, float height) {
        width_ = width;
        height_ = height;
    }

    NUIRect getViewBox() const { return viewBox_; }
    bool hasViewBox() const { return hasViewBox_; }
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }

private:
    std::string svgContent_;
    std::string filePath_;
    SourceType sourceType_ = SourceType::Memory;
    NUIRect viewBox_;
    bool hasViewBox_ = false;
    float width_ = 0.0f;
    float height_ = 0.0f;
};

/**
 * SVG Parser — parses SVG strings / files into renderable documents.
 */
class NUISVGParser {
public:
    static std::shared_ptr<NUISVGDocument> parse(const std::string& svgContent);
    static std::shared_ptr<NUISVGDocument> parseFile(const std::string& filePath);
};

/**
 * SVG Renderer — renders SVG documents using ThorVG.
 */
class NUISVGRenderer {
public:
    static void render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds);
    static void render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds, const NUIColor& tintColor);
};

} // namespace AestraUI
