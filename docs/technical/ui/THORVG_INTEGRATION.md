# ThorVG Integration Guide

## Status

ThorVG headers have been downloaded to `AestraUI/External/`:
- `thorvg.h` - SVG parser

## How ThorVG Works

ThorVG is a two-step process:
1. **Parse** - Converts SVG XML to internal representation
2. **Rasterize** - Renders the SVG to an RGBA bitmap

## Integration Steps

### Step 1: Add Texture Support to NUIRenderer

ThorVG outputs RGBA bitmaps, so we need texture rendering:

```cpp
// In NUIRenderer.h
virtual void drawTexture(const NUIRect& bounds, const unsigned char* rgba, int width, int height) = 0;

// In NUIRendererGL.cpp
void NUIRendererGL::drawTexture(const NUIRect& bounds, const unsigned char* rgba, int width, int height) {
    // Create OpenGL texture
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Render textured quad
    // ... OpenGL code to draw textured rectangle ...
    
    glDeleteTextures(1, &texture);
}
```

### Step 2: Update NUISVGParser to Use ThorVG

```cpp
// In NUISVGParser.cpp
#include "../External/thorvg.h"

std::shared_ptr<NUISVGDocument> NUISVGParser::parseFile(const std::string& filePath) {
    auto picture = tvg::Picture::gen();
    if (!picture->load(filePath.c_str())) return nullptr;
    
    auto doc = std::make_shared<NUISVGDocument>();
    float w, h;
    picture->size(&w, &h);
    doc->setSize(w, h);
    doc->setViewBox(0, 0, w, h);
    
    // Store ThorVG picture pointer in document
    // (need to add void* userData field to NUISVGDocument)
    
    return doc;
}
```

### Step 3: Update NUISVGRenderer to Rasterize

```cpp
void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg,
                           const NUIRect& bounds, const NUIColor& tintColor) {
    auto picture = (tvg::Picture*)svg.getUserData();
    if (!picture) return;
    
    // Calculate rasterization size
    int w = (int)bounds.width;
    int h = (int)bounds.height;
    
    // Allocate RGBA buffer
    std::vector<unsigned char> rgba(w * h * 4);
    
    // Rasterize
    auto canvas = tvg::SwCanvas::gen();
    canvas->target(rgba.data(), w, w, h, tvg::ColorSpace::ABGR8888);
    picture->size(bounds.width, bounds.height);
    canvas->push(picture);
    canvas->draw();
    canvas->sync();
    
    // Apply tint color (multiply RGBA by tint)
    for (int i = 0; i < w * h; ++i) {
        rgba[i*4 + 0] = (unsigned char)(rgba[i*4 + 0] * tintColor.r);
        rgba[i*4 + 1] = (unsigned char)(rgba[i*4 + 1] * tintColor.g);
        rgba[i*4 + 2] = (unsigned char)(rgba[i*4 + 2] * tintColor.b);
        rgba[i*4 + 3] = (unsigned char)(rgba[i*4 + 3] * tintColor.a);
    }
    
    // Render texture
    renderer.drawTexture(bounds, rgba.data(), w, h);
}
```

## Why Not Implemented Yet

The current NUIRenderer doesn't have texture/bitmap rendering support. We need to:
1. Add OpenGL texture creation/rendering to NUIRendererGL
2. Handle texture caching (don't rasterize every frame)
3. Add proper memory management for ThorVG picture pointers

## Recommendation

For v1.0, we have two options:

### Option A: Quick ThorVG Integration (2-3 hours)
- Add texture rendering to NUIRendererGL
- Replace our parser with ThorVG
- Your pause icon will render perfectly

### Option B: Keep Current System (Now)
- Current system works for simple stroke-based icons
- Document ThorVG integration for v1.1
- Focus on grids/panels to complete v1.0

## Testing ThorVG

To test if ThorVG works with your pause icon:

```cpp
#include "External/thorvg.h"

// Parse
auto picture = tvg::Picture::gen();
picture->load("Examples/test_pause.svg");
float w, h;
picture->size(&w, &h);
printf("SVG: %f x %f\n", w, h);

// Rasterize to 100x100
unsigned char* rgba = new unsigned char[100 * 100 * 4];
auto canvas = tvg::SwCanvas::gen();
canvas->target(rgba, 100, 100, 100, tvg::ColorSpace::ABGR8888);
picture->size(100, 100);
canvas->push(picture);
canvas->draw();
canvas->sync();

// rgba now contains the rendered icon!
// Save to PNG or render as texture
```

## Next Steps

1. Decide: Quick ThorVG integration now, or defer to v1.1?
2. If now: Implement texture rendering in NUIRendererGL
3. If later: Document and move to grids/panels

ThorVG is ready to use - we just need texture rendering support!
