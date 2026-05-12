// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUISVGParser.h"
#include "NUIRenderer.h"
#include "NUISVGCache.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

#include <thorvg.h>

namespace AestraUI {

// ============================================================================
// NUISVGDocument Implementation
// ============================================================================
//
// NOTE (architecture): NUISVGDocument stores the raw SVG string (or file path)
// rather than a parsed ThorVG scene graph (tvg::Picture).  This means every
// cache miss triggers a re-parse.  For Aestra's current usage — small UI
// icons <1 KB — this overhead is negligible.  If larger SVGs are introduced
// (e.g. preset artwork, splash screens), consider caching the parsed
// tvg::Picture (or its rasterized output) instead of the source string.
// ============================================================================

NUISVGDocument::NUISVGDocument(NUISVGDocument&& other) noexcept
    : svgContent_(std::move(other.svgContent_))
    , filePath_(std::move(other.filePath_))
    , sourceType_(other.sourceType_)
    , viewBox_(other.viewBox_)
    , hasViewBox_(other.hasViewBox_)
    , width_(other.width_)
    , height_(other.height_)
{
}

NUISVGDocument& NUISVGDocument::operator=(NUISVGDocument&& other) noexcept {
    if (this != &other) {
        svgContent_ = std::move(other.svgContent_);
        filePath_ = std::move(other.filePath_);
        sourceType_ = other.sourceType_;
        viewBox_ = other.viewBox_;
        hasViewBox_ = other.hasViewBox_;
        width_ = other.width_;
        height_ = other.height_;
    }
    return *this;
}

// ============================================================================
// Helper: read file into string
// ============================================================================

static std::string readFileToString(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ============================================================================
// ThorVG initialization (0 threads — DAW-safe)
// ============================================================================

static bool ensureThorVGInitialized() {
    static bool initialized = []() {
        auto res = tvg::Initializer::init(0);
        if (res != tvg::Result::Success) {
            std::cerr << "ThorVG: Initializer::init failed" << std::endl;
            return false;
        }
        return true;
    }();
    return initialized;
}

// ============================================================================
// NUISVGParser Implementation
// ============================================================================

std::shared_ptr<NUISVGDocument> NUISVGParser::parse(const std::string& svgContent) {
    if (!ensureThorVGInitialized()) {
        return nullptr;
    }

    // Use ThorVG to probe dimensions without a full render.
    // We create a temporary 1x1 canvas just to load and inspect the picture.
    auto picture = tvg::Picture::gen();
    if (!picture) {
        std::cerr << "ThorVG: Failed to create picture" << std::endl;
        return nullptr;
    }

    auto res = picture->load(svgContent.data(), static_cast<uint32_t>(svgContent.size()), "svg");
    if (res != tvg::Result::Success) {
        std::cerr << "ThorVG: Failed to parse SVG content (length: " << svgContent.length() << " bytes)" << std::endl;
        tvg::Paint::rel(picture);
        return nullptr;
    }

    float pw = 0.0f, ph = 0.0f;
    picture->size(&pw, &ph);
    tvg::Paint::rel(picture);

    auto doc = std::make_shared<NUISVGDocument>();
    doc->setSVGContent(svgContent);
    doc->setSize(pw, ph);
    doc->setViewBox(0.0f, 0.0f, pw, ph);

    return doc;
}

std::shared_ptr<NUISVGDocument> NUISVGParser::parseFile(const std::string& filePath) {
    std::string content = readFileToString(filePath);
    if (content.empty()) {
        std::cerr << "ThorVG: Failed to read SVG file: " << filePath << std::endl;
        return nullptr;
    }

    auto doc = parse(content);
    if (doc) {
        doc->setFilePath(filePath);
    }
    return doc;
}

// ============================================================================
// NUISVGRenderer Implementation
// ============================================================================

static NUISVGCache svgCache;

void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg, const NUIRect& bounds) {
    render(renderer, svg, bounds, NUIColor(1.0f, 1.0f, 1.0f, 0.0f));
}

void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg,
                            const NUIRect& bounds, const NUIColor& tintColor) {
    int w = static_cast<int>(bounds.width);
    int h = static_cast<int>(bounds.height);

    if (w <= 0 || h <= 0) {
        return;
    }

    // ------------------------------------------------------------------------
    // 1. Cache lookup
    // ------------------------------------------------------------------------
    NUISVGCache::CacheKey key{&svg, w, h, tintColor};
    auto* cached = svgCache.get(key);

    if (cached) {
        if (cached->textureId == 0) {
            cached->textureId = renderer.createTexture(cached->rgba.data(), cached->width, cached->height);
        }
        if (cached->textureId != 0) {
            renderer.drawTexture(cached->textureId, bounds, NUIRect(0, 0, cached->width, cached->height));
        }
        return;
    }

    // ------------------------------------------------------------------------
    // 2. ThorVG rasterization
    // ------------------------------------------------------------------------
    if (!ensureThorVGInitialized()) {
        return;
    }

    auto canvas = tvg::SwCanvas::gen();
    if (!canvas) {
        std::cerr << "ThorVG: Failed to create SwCanvas" << std::endl;
        return;
    }

    // ThorVG ARGB8888S on little-endian gives byte order R,G,B,A — matches GL_RGBA.
    std::vector<uint32_t> buffer(static_cast<size_t>(w) * h, 0);
    auto targetRes = canvas->target(buffer.data(), static_cast<uint32_t>(w),
                                    static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                    tvg::ColorSpace::ARGB8888S);
    if (targetRes != tvg::Result::Success) {
        std::cerr << "ThorVG: SwCanvas::target failed" << std::endl;
        return;
    }

    auto picture = tvg::Picture::gen();
    if (!picture) {
        std::cerr << "ThorVG: Failed to create picture" << std::endl;
        return;
    }

    // Load SVG (always from memory to avoid FILE_IO dependency)
    tvg::Result loadRes;
    if (svg.getSourceType() == NUISVGDocument::SourceType::Memory) {
        const auto& content = svg.getSVGContent();
        loadRes = picture->load(content.data(), static_cast<uint32_t>(content.size()), "svg", nullptr, true);
    } else {
        std::string content = readFileToString(svg.getFilePath());
        if (content.empty()) {
            tvg::Paint::rel(picture);
            return;
        }
        loadRes = picture->load(content.data(), static_cast<uint32_t>(content.size()), "svg", nullptr, true);
    }

    if (loadRes != tvg::Result::Success) {
        std::cerr << "ThorVG: Failed to load SVG" << std::endl;
        tvg::Paint::rel(picture);
        return;
    }

    // Scale to fit bounds while preserving aspect ratio
    float pw = 0.0f, ph = 0.0f;
    picture->size(&pw, &ph);
    if (pw > 0.0f && ph > 0.0f) {
        float scaleX = bounds.width / pw;
        float scaleY = bounds.height / ph;
        float scale = std::min(scaleX, scaleY);
        picture->scale(scale);
    }

    // Render
    auto addRes = canvas->add(picture);
    if (addRes != tvg::Result::Success) {
        std::cerr << "ThorVG: Canvas::add failed" << std::endl;
        canvas->remove(picture);
        tvg::Paint::rel(picture);
        return;
    }

    canvas->draw();
    canvas->sync();
    canvas->remove(picture);
    tvg::Paint::rel(picture);

    // ------------------------------------------------------------------------
    // 3. Tinting
    // ------------------------------------------------------------------------
    if (tintColor.a > 0.0f) {
        uint8_t tintR = static_cast<uint8_t>(std::clamp(tintColor.r, 0.0f, 1.0f) * 255.0f);
        uint8_t tintG = static_cast<uint8_t>(std::clamp(tintColor.g, 0.0f, 1.0f) * 255.0f);
        uint8_t tintB = static_cast<uint8_t>(std::clamp(tintColor.b, 0.0f, 1.0f) * 255.0f);
        uint8_t tintA = static_cast<uint8_t>(std::clamp(tintColor.a, 0.0f, 1.0f) * 255.0f);

        for (int i = 0; i < w * h; ++i) {
            uint8_t* pixel = reinterpret_cast<uint8_t*>(&buffer[i]);
            uint8_t srcA = pixel[3];
            if (srcA == 0) continue;
            pixel[0] = tintR;
            pixel[1] = tintG;
            pixel[2] = tintB;
            pixel[3] = static_cast<uint8_t>((static_cast<uint16_t>(srcA) * tintA) / 255);
        }
    }

    // ------------------------------------------------------------------------
    // 4. Cache + texture upload
    // ------------------------------------------------------------------------
    std::vector<unsigned char> rgba;
    rgba.resize(static_cast<size_t>(w) * h * 4);
    std::memcpy(rgba.data(), buffer.data(), rgba.size());

    svgCache.put(key, std::move(rgba), w, h, &renderer);

    auto* entry = svgCache.get(key);
    if (entry) {
        entry->textureId = renderer.createTexture(entry->rgba.data(), entry->width, entry->height);
        if (entry->textureId != 0) {
            renderer.drawTexture(entry->textureId, bounds, NUIRect(0, 0, entry->width, entry->height));
        }
    }
}

} // namespace AestraUI
