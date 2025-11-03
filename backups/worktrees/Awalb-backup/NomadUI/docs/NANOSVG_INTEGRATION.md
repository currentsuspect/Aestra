# NanoSVG Integration Guide

## Status

NanoSVG headers have been downloaded to `NomadUI/External/`:
- `nanosvg.h` - SVG parser
- `nanosvgrast.h` - SVG rasterizer

## How NanoSVG Works

NanoSVG is a two-step process:
1. **Parse** - Converts SVG XML to internal representation
2. **Rasterize** - Renders the SVG to an RGBA bitmap

## Integration Steps

### Step 1: Add Texture Support to NUIRenderer

NanoSVG outputs RGBA bitmaps, so we need texture rendering:

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

### Step 2: Update NUISVGParser to Use NanoSVG

```cpp
// In NUISVGParser.cpp
#define NANOSVG_IMPLEMENTATION
#include "../External/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION  
#include "../External/nanosvgrast.h"

std::shared_ptr<NUISVGDocument> NUISVGParser::parseFile(const std::string& filePath) {
    NSVGimage* image = nsvgParseFromFile(filePath.c_str(), "px", 96.0f);
    if (!image) return nullptr;
    
    auto doc = std::make_shared<NUISVGDocument>();
    doc->setSize(image->width, image->height);
    doc->setViewBox(0, 0, image->width, image->height);
    
    // Store NanoSVG image pointer in document
    // (need to add void* userData field to NUISVGDocument)
    
    return doc;
}
```

### Step 3: Update NUISVGRenderer to Rasterize

```cpp
void NUISVGRenderer::render(NUIRenderer& renderer, const NUISVGDocument& svg, 
                           const NUIRect& bounds, const NUIColor& tintColor) {
    NSVGimage* image = (NSVGimage*)svg.getUserData();
    if (!image) return;
    
    // Calculate rasterization size
    int w = (int)bounds.width;
    int h = (int)bounds.height;
    
    // Allocate RGBA buffer
    std::vector<unsigned char> rgba(w * h * 4);
    
    // Rasterize
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0, 
                  bounds.width / image->width, 
                  rgba.data(), w, h, w * 4);
    nsvgDeleteRasterizer(rast);
    
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
3. Add proper memory management for NSVGimage pointers

## Recommendation

For v1.0, we have two options:

### Option A: Quick NanoSVG Integration (2-3 hours)
- Add texture rendering to NUIRendererGL
- Replace our parser with NanoSVG
- Your pause icon will render perfectly

### Option B: Keep Current System (Now)
- Current system works for simple stroke-based icons
- Document NanoSVG integration for v1.1
- Focus on grids/panels to complete v1.0

## Testing NanoSVG

To test if NanoSVG works with your pause icon:

```cpp
#define NANOSVG_IMPLEMENTATION
#include "External/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "External/nanosvgrast.h"

// Parse
NSVGimage* image = nsvgParseFromFile("Examples/test_pause.svg", "px", 96.0f);
printf("SVG: %f x %f\n", image->width, image->height);

// Rasterize to 100x100
unsigned char* rgba = new unsigned char[100 * 100 * 4];
NSVGrasterizer* rast = nsvgCreateRasterizer();
nsvgRasterize(rast, image, 0, 0, 1.0f, rgba, 100, 100, 100 * 4);

// rgba now contains the rendered icon!
// Save to PNG or render as texture

nsvgDeleteRasterizer(rast);
nsvgDelete(image);
delete[] rgba;
```

## Next Steps

1. Decide: Quick NanoSVG integration now, or defer to v1.1?
2. If now: Implement texture rendering in NUIRendererGL
3. If later: Document and move to grids/panels

NanoSVG is ready to use - we just need texture rendering support!
