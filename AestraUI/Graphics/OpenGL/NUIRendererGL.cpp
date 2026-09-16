// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRendererGL.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_OUTLINE_H
// Profiler include for recording draw calls/triangles
#include "../../../AestraCore/include/AestraProfiler.h"
#include "../../../AestraCore/include/AestraLog.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define NOCOMM
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers to avoid macro conflicts
#include "../../External/glad/include/glad/glad.h"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include "../../External/stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Suppress APIENTRY redefinition warning - both define the same value
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)
#endif

#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

namespace AestraUI {

// The text uniforms are derived, not configured: one float — textContrast_, set
// per frame from background luminance — resolves into all three. They live here
// as functions rather than inline expressions so the shader and
// getTextDiagnostics() cannot disagree about what the pipeline is doing. A
// diagnostic that recomputes its own answer is a second implementation, and
// would eventually report a pipeline that no longer exists.
namespace {
constexpr float kTextGammaTiny  = 0.74f;  // x-small / small atlases
constexpr float kTextGammaLarge = 0.93f;  // medium / regular atlases
constexpr float kTextSharpenTiny  = 0.20f;
constexpr float kTextSharpenLarge = 0.28f;
// ATLAS TIER CONTRACT
//
// A tier's atlas must be baked at or above the largest font size it serves.
// Below that it MAGNIFIES its own glyphs — blurring exactly the text the tier
// exists to sharpen — and, because the aggressive gamma/sharpen treatment is
// keyed to "this tier is heavily supersampled", it then thickens the blur.
//
// F5 was this: Small baked at 16 px served up to 17 px while being treated as
// the most supersampled tier in the set. The static_asserts below make that
// state unrepresentable rather than merely wrong.
constexpr float kTierMaxXSmall = 11.25f;
constexpr float kTierMaxSmall  = 17.0f;
constexpr float kTierMaxMedium = 20.5f;

constexpr int kAtlasSizeXSmall  = 20;
constexpr int kAtlasSizeSmall   = 18;  // was 16: below its own 17 px ceiling
constexpr int kAtlasSizeMedium  = 22;
constexpr int kAtlasSizeRegular = 40;

static_assert(kAtlasSizeXSmall >= kTierMaxXSmall, "XSmall atlas magnifies its own range");
static_assert(kAtlasSizeSmall  >= kTierMaxSmall,  "Small atlas magnifies its own range");
static_assert(kAtlasSizeMedium >= kTierMaxMedium, "Medium atlas magnifies its own range");

// Whether a tier is supersampled enough to need the softening compensation.
// Derived from the ratio rather than named tiers: the old code listed XSmall
// and Small by texture id, so Small kept the heavy treatment long after its
// ratio stopped justifying it. A ratio cannot fall out of step with itself.
constexpr float kHeavySupersampleRatio = 1.5f;
inline bool isHeavilySupersampled(int atlasSize, float maxServed) {
    return maxServed > 0.0f && static_cast<float>(atlasSize) / maxServed >= kHeavySupersampleRatio;
}

inline float resolveTextGamma(bool tinyAtlas, float textContrast) {
    return (tinyAtlas ? kTextGammaTiny : kTextGammaLarge) * textContrast;
}
inline float resolveTextSharpen(bool tinyAtlas) {
    return tinyAtlas ? kTextSharpenTiny : kTextSharpenLarge;
}
} // namespace

// ============================================================================
// UTF-8 Decoding Helper
// ============================================================================

// Decode one UTF-8 codepoint from string, advancing the index
// Returns Unicode codepoint (U+XXXX), or 0 if invalid/end
static uint32_t decodeUTF8(const std::string& text, size_t& index) {
    if (index >= text.length()) return 0;
    
    unsigned char c = static_cast<unsigned char>(text[index++]);
    
    // 1-byte (ASCII): 0xxxxxxx
    if ((c & 0x80) == 0) {
        return c;
    }
    
    // 2-byte: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0) {
        if (index >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80) return 0;
        return ((c & 0x1F) << 6) | (c1 & 0x3F);
    }
    
    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx  
    if ((c & 0xF0) == 0xE0) {
        if (index + 1 >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        uint32_t c2 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;
        return ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    
    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF8) == 0xF0) {
        if (index + 2 >= text.length()) return 0;
        uint32_t c1 = static_cast<unsigned char>(text[index++]);
        uint32_t c2 = static_cast<unsigned char>(text[index++]);
        uint32_t c3 = static_cast<unsigned char>(text[index++]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
        return ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    
    return 0; // Invalid UTF-8
}

static float normalizeSmallTextSize(float requestedSize) {
    // Readability floor for small text, applied to SIZE ONLY.
    //
    // This used to also key off the colour's alpha (dim text floored to 12px,
    // bright text left at its requested size). That coupled rendered size to
    // colour, with two consequences:
    //   1. Two labels written at the "same" size rendered at different sizes if
    //      one was dimmer — so adjacent label/value pairs no longer aligned and
    //      the smaller one looked jagged (e.g. the I/O tab's Source/Auto row).
    //   2. measureText() has no alpha and never applied the floor, so dim small
    //      text measured narrow but rendered wide — layout (wrap/centre/fit)
    //      disagreed with what hit the screen.
    // A single size-based floor keeps small text legible while measure and
    // render always agree. 11px sits between the old two-tier floor (11/12):
    // it lifts the previously-unfloored bright small text (which looked jagged)
    // up to a readable, crisp size, while barely nudging the dim labels that
    // already floored to 12 — so compact one-line rows still fit.
    constexpr float kMinReadableSize = 11.0f;
    return std::max(requestedSize, kMinReadableSize);
}

float NUIRendererGL::getKerningUnits(FT_Face face, uint32_t previousGlyph, uint32_t currentGlyph) const {
    if (!face || !FT_HAS_KERNING(face) || previousGlyph == 0 || currentGlyph == 0) {
        return 0.0f;
    }

    const KerningCacheKey kernKey{face, previousGlyph, currentGlyph};
    auto kernIt = kerningCache_.find(kernKey);
    if (kernIt != kerningCache_.end()) {
        return kernIt->second;
    }

    FT_Vector kerning = {0, 0};
    float kernUnits = 0.0f;
    if (FT_Get_Kerning(face, static_cast<FT_UInt>(previousGlyph), static_cast<FT_UInt>(currentGlyph),
                       FT_KERNING_DEFAULT, &kerning) == 0) {
        kernUnits = static_cast<float>(kerning.x);
    }

    if (kerningCache_.size() >= kKerningCacheMaxSize) {
        // Keep memory bounded without LRU bookkeeping in the text hot path.
        auto it = kerningCache_.begin();
        for (size_t i = 0; i < kKerningCacheMaxSize / 2 && it != kerningCache_.end(); ++i) {
            it = kerningCache_.erase(it);
        }
    }

    kerningCache_[kernKey] = kernUnits;
    return kernUnits;
}

// ============================================================================
// Shader Sources (embedded)
// ============================================================================

static const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
// Batching Attributes
layout(location = 3) in vec2 aRectSize;
layout(location = 4) in vec2 aQuadSize;
layout(location = 5) in float aRadius;
layout(location = 6) in float aBlur;
layout(location = 7) in float aStrokeWidth;
layout(location = 8) in float aPrimitiveType;

out vec2 vTexCoord;
out vec4 vColor;
out vec2 vRectSize;
out vec2 vQuadSize;
out float vRadius;
out float vBlur;
out float vStrokeWidth;
out float vPrimitiveType;

uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vRectSize = aRectSize;
    vQuadSize = aQuadSize;
    vRadius = aRadius;
    vBlur = aBlur;
    vStrokeWidth = aStrokeWidth;
    vPrimitiveType = aPrimitiveType;
}
)";

static const char* fragmentShaderSource = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
in vec2 vRectSize;
in vec2 vQuadSize;
in float vRadius;
in float vBlur;
in float vStrokeWidth;
in float vPrimitiveType;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform bool uUseTexture;
uniform vec2 uTextTexelSize;
uniform float uTextSharpen;
uniform float uTextGamma;

// Squircle SDF Implementation
// Based on "sdContinuousRect" logic but optimized for GLSL 3.3
float sdSquircle(vec2 p, vec2 b, float r) {
    // Effectively a higher order distance metric for the corner
    // For standard performance, we use a modified rounded rect with
    // a curvature adjustment.
    
    // Standard SDF component
    vec2 d = abs(p) - b + vec2(r);
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
    
    return dist;
}

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

float decodeTextCoverage(vec4 texColor) {
    float rgbDelta = abs(texColor.r - texColor.g) + abs(texColor.g - texColor.b) + abs(texColor.b - texColor.r);
    bool looksLCD = rgbDelta > 0.015;
    if (looksLCD) {
        return clamp((texColor.r + texColor.g + texColor.b) / 3.0, 0.0, 1.0);
    }
    return clamp(texColor.a, 0.0, 1.0);
}

float sampleTextCoverage(vec2 uv) {
    vec4 centerTexel = texture(uTexture, uv);
    float center = decodeTextCoverage(centerTexel);
    if (uTextSharpen <= 0.001 || uTextTexelSize.x <= 0.0 || uTextTexelSize.y <= 0.0) {
        return center;
    }

    float n = decodeTextCoverage(texture(uTexture, uv + vec2(0.0, -uTextTexelSize.y)));
    float s = decodeTextCoverage(texture(uTexture, uv + vec2(0.0,  uTextTexelSize.y)));
    float e = decodeTextCoverage(texture(uTexture, uv + vec2( uTextTexelSize.x, 0.0)));
    float w = decodeTextCoverage(texture(uTexture, uv + vec2(-uTextTexelSize.x, 0.0)));
    float neighborhood = (n + s + e + w) * 0.25;
    float sharpened = center + (center - neighborhood) * uTextSharpen;
    return clamp(sharpened, 0.0, 1.0);
}

void main() {
    vec4 color = vColor;
    int primitiveID = int(floor(vPrimitiveType + 0.5));
    
    // Apply texture if enabled (Only for Image=0, BitmapText=4)
    if (uUseTexture && (primitiveID == 0 || primitiveID == 4)) {
        vec4 texColor = texture(uTexture, vTexCoord);
        if (primitiveID == 4) {
               // Bitmap text — atlas stores coverage in alpha channel.
               //
               // The caller's alpha is multiplied, never reshaped. It used to be
               // raised to uTextAlphaLift (0.35 on light themes) to compensate
               // for dark-on-light strokes reading thin. That mechanism could
               // not do the job it was given and did damage instead: pow(1, k)
               // is 1 for every k, so it left primary text — which carries the
               // whole of the problem — untouched, while pulling alpha 0.5 up
               // to 0.78 and collapsing the gap between secondary and primary.
               // Stroke weight is a property of coverage and belongs to
               // uTextGamma, which already carries it. Alpha is what the caller
               // meant by "this label is secondary", and the renderer does not
               // get to reinterpret that.
               float coverage = pow(sampleTextCoverage(vTexCoord), uTextGamma);
               color.a *= coverage;
        } else {
             // Regular textured primitive
             color *= texColor;
        }
    }
    
    if (primitiveID == 1) {
        // Filled Squircle
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        
        // Use vBlur for AA width or smoothness passed from vertex
        // Pass 1.0 or similar via vBlur for standard AA
        float dist = sdSquircle(pos, vRectSize * 0.5, vRadius);
        
        // High quality AA using fwidth
        float aaWidth = fwidth(dist) * 1.25; // Slightly wider AA to calm staircase edges
        float alpha = 1.0 - smoothstep(-aaWidth, aaWidth, dist);
        
        color.a *= alpha;
    }
    
    if (primitiveID == 3) {
        // Stroked Squircle
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        float dist = sdSquircle(pos, vRectSize * 0.5, vRadius);
        
        float halfStroke = vStrokeWidth * 0.5;
        
        // Hairline border logic: 
        // We render a stroke centered on the SDF edge (dist=0)
        // Outline: from dist = -halfStroke to dist = +halfStroke
        
        float aaWidth = fwidth(dist) * 1.0;
        
        // Alpha calculation for ring
        // Equivalent to: 1.0 - smoothstep(halfStroke-aa, halfStroke+aa, abs(dist))
        float alpha = 1.0 - smoothstep(halfStroke - aaWidth, halfStroke + aaWidth, abs(dist));
        
        color.a *= alpha;
    }

    if (primitiveID == 6) {
        // Filled Circle
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        float dist = sdCircle(pos, vRectSize.x * 0.5);
        float aaWidth = max(fwidth(dist) * 1.5, 0.75);
        float alpha = 1.0 - smoothstep(-aaWidth, aaWidth, dist);
        color.a *= alpha;
    }

    if (primitiveID == 7) {
        // Stroked Circle
        vec2 pos = (vTexCoord - 0.5) * vQuadSize;
        float dist = sdCircle(pos, vRectSize.x * 0.5);
        float halfStroke = vStrokeWidth * 0.5;
        float aaWidth = max(fwidth(dist) * 1.25, 0.75);
        float alpha = 1.0 - smoothstep(halfStroke - aaWidth, halfStroke + aaWidth, abs(dist));
        color.a *= alpha;
    }
    
    // Type 5: Solid colored geometry (lines, triangles) - already handled by default color
    
    FragColor = color;
}

)";

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUIRendererGL::NUIRendererGL() {
    renderCache_.setRenderer(this);
}

NUIRendererGL::~NUIRendererGL() {
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool NUIRendererGL::initialize(int width, int height) {
    width_ = width;
    height_ = height;
    
    if (!initializeGL()) {
        return false;
    }
    
    if (!loadShaders()) {
        return false;
    }
    
    createBuffers();
    updateProjectionMatrix();

    // Text rendering is FreeType-only; initialized below.

    // Initialize FreeType
    fontInitialized_ = false;
    if (FT_Init_FreeType(&ftLibrary_) != 0) {
        AESTRA_LOG_ERROR("Could not init FreeType Library");
        return false;
    }

    // Prefer LCD/subpixel rasterization when FreeType can provide filtered LCD masks.
    // If unsupported, we gracefully fall back to grayscale coverage.
    fontUseLCD_ = true;
    if (const char* disableLCD = std::getenv("AESTRA_DISABLE_LCD")) {
        if (disableLCD[0] == '1') {
            fontUseLCD_ = false;
        }
    }
    if (fontUseLCD_ && FT_Library_SetLcdFilter(ftLibrary_, FT_LCD_FILTER_DEFAULT) != 0) {
        fontUseLCD_ = false;
    }
    
        // Try to load the best font for Aestra.
        // Prefer a production-safe medium weight first so the UI hierarchy feels
        // intentional without needing overly aggressive outline emboldening.
        std::vector<std::string> fontPaths;
        if (const char* fontDir = std::getenv("AESTRA_FONT_DIR")) {
            const std::string base(fontDir);
            fontPaths.push_back(base + "/Geist/Geist-Medium.ttf");
            fontPaths.push_back(base + "/Geist/Geist-Regular.ttf");
            fontPaths.push_back(base + "/Geist/Geist-Bold.ttf");
            fontPaths.push_back(base + "/Manrope/Manrope-Regular.ttf");
        }
        std::vector<std::string> fallbackFontPaths = {
            "AestraAssets/fonts/Geist/Geist-Medium.ttf",
            "../AestraAssets/fonts/Geist/Geist-Medium.ttf",
            "../../AestraAssets/fonts/Geist/Geist-Medium.ttf",
            "../../../AestraAssets/fonts/Geist/Geist-Medium.ttf",
            "../../../../AestraAssets/fonts/Geist/Geist-Medium.ttf",

            "AestraAssets/fonts/Geist/Geist-Regular.ttf",
            "../AestraAssets/fonts/Geist/Geist-Regular.ttf",
            "../../AestraAssets/fonts/Geist/Geist-Regular.ttf",
            "../../../AestraAssets/fonts/Geist/Geist-Regular.ttf",
            "../../../../AestraAssets/fonts/Geist/Geist-Regular.ttf",

            "AestraAssets/fonts/Geist/Geist-Bold.ttf",
            "../AestraAssets/fonts/Geist/Geist-Bold.ttf",
            "../../AestraAssets/fonts/Geist/Geist-Bold.ttf",
            "../../../AestraAssets/fonts/Geist/Geist-Bold.ttf",
            "../../../../AestraAssets/fonts/Geist/Geist-Bold.ttf",

            "AestraAssets/fonts/Manrope/Manrope-Regular.ttf",
            "../AestraAssets/fonts/Manrope/Manrope-Regular.ttf",
            "../../AestraAssets/fonts/Manrope/Manrope-Regular.ttf",
            "../../../AestraAssets/fonts/Manrope/Manrope-Regular.ttf",
            "../../../../AestraAssets/fonts/Manrope/Manrope-Regular.ttf",

            // Local-only Apple system fallbacks. Do not bundle these.
            "/usr/share/fonts/apple/SF-Pro-Text-Medium.otf",
            "/usr/share/fonts/apple/SF-Pro-Text-Regular.otf",
            "/usr/share/fonts/apple/SF-Pro-Display-Semibold.otf",
            "/usr/share/fonts/apple/SF-Pro-Display-Regular.otf",

            // System fallbacks (Windows)
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/segoeuisl.ttf",
            "C:/Windows/Fonts/calibri.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/consola.ttf",
            "C:/Windows/Fonts/tahoma.ttf",
            "C:/Windows/Fonts/verdana.ttf",

            // Linux system fonts
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans/DejaVuSans.ttf"
        };
        fontPaths.insert(fontPaths.end(), fallbackFontPaths.begin(), fallbackFontPaths.end());
        
        bool fontLoaded = false;
        for (const auto& fontPath : fontPaths) {
            if (loadFont(fontPath)) {
                fontLoaded = true;
                break;
            }
        }
        
        if (!fontLoaded) {
            // Two different fallbacks, and naming only one of them has misled a
            // reader before: drawText() draws a rectangle per printable character
            // so a fontless build is visibly wrong rather than silently blank,
            // while measureText() estimates width at 0.6 em. Both key off
            // effectiveFontSize, so the placeholder a caller measures is the
            // placeholder it gets.
            AESTRA_LOG_WARNING(
                "Could not load any font: drawText falls back to rectangle placeholders "
                "and measureText to width estimation");
        }

        // Load CJK fallback face (no atlas — glyphs added on demand)
        static const std::vector<std::string> cjkFallbackPaths = {
            // Linux
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            // macOS
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            // Windows
            "C:/Windows/Fonts/meiryo.ttc",
            "C:/Windows/Fonts/msgothic.ttc",
        };
        bool cjkFontLoaded = false;
        for (const auto& path : cjkFallbackPaths) {
            if (FT_New_Face(ftLibrary_, path.c_str(), 0, &ftCJKFace_) == 0) {
                AESTRA_LOG_DEBUG("CJK fallback font loaded: " + path);
                cjkFontLoaded = true;
                break;
            }
        }
        if (!cjkFontLoaded) {
            AESTRA_LOG_WARNING("No CJK fallback font found. CJK characters may not render correctly.");
        }

    // Set initial state
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Ensure we don't write to depth buffer in 2D mode
    glDisable(GL_CULL_FACE);
    // Prefer smoother edges when a multisampled default framebuffer is available.
    glEnable(GL_MULTISAMPLE);

    // ONE COMPOSITING SPACE: gamma (sRGB), everywhere.
    //
    // The palette is authored in sRGB, so the simplest correct pipeline stores
    // it unchanged and blends it where it was authored. GL_FRAMEBUFFER_SRGB
    // would instead have the driver decode, blend in linear light and re-encode,
    // and that is the wrong choice for a UI for two measured reasons:
    //
    //   Alpha stops meaning what the caller meant. Linear blending is correct
    //   light mixing, but a UI's alpha is a perceptual weight — "this label is
    //   secondary" — and CSS, Qt, Cocoa and Skia all composite in gamma space
    //   for exactly that reason. Measured on the light theme, textPrimary at
    //   alpha 0.72 (what track names use) gives 7.2:1 contrast blended in gamma
    //   and 3.1:1 blended in linear. The same alpha on a dark theme goes the
    //   other way, 8.7:1 to 12.2:1. So alpha-based hierarchy read correctly in
    //   dark and failed WCAG in light, from the blend space alone.
    //
    //   It was never uniform anyway. The widget cache renders into a plain
    //   RGBA8 target where GL_FRAMEBUFFER_SRGB does nothing, so cached content
    //   always blended in gamma space while the screen blended in linear. One
    //   space removes the split rather than propagating it.
    //
    // This also retires the probe that used to live here: glEnable does not
    // fail when there is no sRGB attachment, so framebufferSRGBEnabled_ was
    // reporting true on Win32 where no sRGB pixel format is ever requested.
    // Nothing now depends on the answer.
    glDisable(GL_FRAMEBUFFER_SRGB);
    framebufferSRGBEnabled_ = false;
    
    return true;
}

void NUIRendererGL::shutdown() {
    // Cleanup FreeType
    if (fontInitialized_) {
        // Clean up atlas texture
        if (fontAtlasTextureId_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureId_);
            fontAtlasTextureId_ = 0;
        }
        if (fontAtlasTextureIdMedium_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureIdMedium_);
            fontAtlasTextureIdMedium_ = 0;
        }
        if (fontAtlasTextureIdSmall_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureIdSmall_);
            fontAtlasTextureIdSmall_ = 0;
        }
        if (fontAtlasTextureIdXSmall_ != 0) {
            glDeleteTextures(1, &fontAtlasTextureIdXSmall_);
            fontAtlasTextureIdXSmall_ = 0;
        }
        fontCache_.clear();
        fontCacheMedium_.clear();
        fontCacheSmall_.clear();
        fontCacheXSmall_.clear();
        
        if (ftCJKFace_) {
            FT_Done_Face(ftCJKFace_);
            ftCJKFace_ = nullptr;
        }
        FT_Done_Face(ftFace_);
        FT_Done_FreeType(ftLibrary_);
        fontInitialized_ = false;
        fontHasKerning_ = false;
        fontUseLCD_ = false;
        fontAscent_ = fontDescent_ = fontLineHeight_ = 0.0f;
        fontAscentMedium_ = fontDescentMedium_ = fontLineHeightMedium_ = 0.0f;
        fontAscentSmall_ = fontDescentSmall_ = fontLineHeightSmall_ = 0.0f;
        fontAscentXSmall_ = fontDescentXSmall_ = fontLineHeightXSmall_ = 0.0f;
    }
    
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    
    if (primitiveShader_.id) {
        glDeleteProgram(primitiveShader_.id);
        primitiveShader_.id = 0;
    }
    
    // Text rendering cleanup (no font objects to clean up)
}

void NUIRendererGL::resize(int width, int height) {
    width_ = width;
    height_ = height;
    updateProjectionMatrix();
    
    // Invalidate all cached FBOs when the surface size changes
    // to avoid sampling from stale textures after minimize/restore.
    renderCache_.clearAll();
    
    glViewport(0, 0, width, height);
}

// ============================================================================
// Frame Management
// ============================================================================

void NUIRendererGL::beginFrame() {
    // Enforce 2D rendering state at the start of every frame
    // This protects against state pollution from other renderers (e.g. plugins, 3D views)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    if (framebufferSRGBEnabled_) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    vertices_.clear();
    indices_.clear();
    frameCounter_++;
    drawCallCount_ = 0;  // Reset draw call counter each frame
    submittedQuadCount_ = 0;
    // Reset primitive state so the first non-rounded draw in a frame does not inherit rounded settings
    currentPrimitiveType_ = 0;
    currentRadius_ = 0.0f;
    currentBlur_ = 0.0f;
    currentSize_ = {0.0f, 0.0f};
    currentQuadSize_ = {0.0f, 0.0f};
    renderCache_.setCurrentFrame(frameCounter_);
    
    // OPTIMIZED: Don't mark all dirty - let widgets mark their own dirty regions
    // This enables true incremental rendering where only changed areas are redrawn
    // dirtyRegionManager_.markAllDirty(NUISize(static_cast<float>(width_), static_cast<float>(height_)));
    
}

void NUIRendererGL::endFrame() {
    flush();
    
    // Clear dirty regions for next frame
    dirtyRegionManager_.clear();
    
    // Cleanup old caches every 60 frames
    if (frameCounter_ % 60 == 0) {
        renderCache_.cleanup(frameCounter_, 300);
    }
}

void NUIRendererGL::clear(const NUIColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ============================================================================
// State Management
// ============================================================================

void NUIRendererGL::pushTransform(float tx, float ty, float scale) {
    NUITransform2D t;
    t.tx = tx;
    t.ty = ty;
    t.scale = scale;
    transformStack_.push_back(t);
}

void NUIRendererGL::popTransform() {
    if (!transformStack_.empty()) {
        transformStack_.pop_back();
    }
}

void NUIRendererGL::setClipRect(const NUIRect& rect) {
    flush(); // Must flush before changing scissor state!
    glEnable(GL_SCISSOR_TEST);
    scissorEnabled_ = true;
    
    // Transform the rect to global screen coordinates (pixels)
    // Points: Top-Left and Bottom-Right
    float x1 = rect.x;
    float y1 = rect.y;
    float x2 = rect.right();
    float y2 = rect.bottom();

    // The same composition applyTransform() uses. This block used to walk the
    // stack itself, scaling before translating while applyTransform() translated
    // before scaling — so the scissor rectangle and the geometry it was meant to
    // clip disagreed for every scale != 1. Both now go through one function, so
    // they cannot drift apart again.
    if (!transformStack_.empty()) {
        composeTransformStack(transformStack_).applyToRect(x1, y1, x2, y2);
    }
    
    // Normalize if scale was negative (unlikely but safe)
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    
    // Correct rounding to prevent shrinking and add a small padding margin so
    // anti-aliased edges are not shaved off at the clip boundary.
    constexpr int kClipPadding = 2;
    int glX = static_cast<int>(std::floor(x1)) - kClipPadding;
    int glRight = static_cast<int>(std::ceil(x2)) + kClipPadding;
    
    // Convert to GL coords (bottom-up)
    // Range in UI (y-down): [y1, y2]
    // Range in GL (y-up):   [height_ - y2, height_ - y1]
    
    float bottomGL = static_cast<float>(height_) - y2;
    float topGL = static_cast<float>(height_) - y1;
    
    int glY = static_cast<int>(std::floor(bottomGL)) - kClipPadding;
    int glTop = static_cast<int>(std::ceil(topGL)) + kClipPadding;

    glX = std::max(0, glX);
    glY = std::max(0, glY);
    glRight = std::min(width_, glRight);
    glTop = std::min(height_, glTop);

    int glWidth = std::max(0, glRight - glX);
    int glHeight = std::max(0, glTop - glY);

    glScissor(glX, glY, glWidth, glHeight);
    
    // Track renderer-side scissor state so other systems can query it
    scissorEnabled_ = true;
}


void NUIRendererGL::clearClipRect() {
    flush(); // Must flush before changing scissor state to prevent spilling/ruined layout
    glDisable(GL_SCISSOR_TEST);
    scissorEnabled_ = false;
}

void NUIRendererGL::setOpacity(float opacity) {
    globalOpacity_ = opacity;
}

// ============================================================================
// Primitive Drawing
// ============================================================================

void NUIRendererGL::fillRect(const NUIRect& rect, const NUIColor& color) {
    ensureBasicPrimitive();
    addQuad(rect, color);
}

void NUIRendererGL::fillRoundedRect(const NUIRect& rect, float radius, const NUIColor& color) {
    // Batching enabled! We pass size and radius as vertex attributes.
    // Dimensions for both Rect and Quad are the same for standard rounded rects.
    float w = rect.width;
    float h = rect.height;
    
    // Blur is 1.0f for standard anti-aliasing
    // Snap to integers for sharp filling without subpixel shifts
    AestraUI::NUIRect snapped = rect;
    snapped.x = std::floor(rect.x);
    snapped.y = std::floor(rect.y);
    snapped.width = std::round(rect.width);
    snapped.height = std::round(rect.height);

    // Give the fragment AA fringe a little room so filled pills/cards don't look
    // shaved on the bottom/right edge when they sit near a clip boundary.
    constexpr float kAASafePad = 1.5f;
    AestraUI::NUIRect quad = {
        snapped.x - kAASafePad,
        snapped.y - kAASafePad,
        snapped.width + kAASafePad * 2.0f,
        snapped.height + kAASafePad * 2.0f
    };

    // Standard behavior: Clamp radius to fit
    float safeRadius = std::min(radius, std::min(snapped.width * 0.5f, snapped.height * 0.5f));
    addQuad(quad, color, snapped.width, snapped.height, quad.width, quad.height, safeRadius, 1.0f, 0.0f, 1.0f);
}

void NUIRendererGL::strokeRect(const NUIRect& rect, float thickness, const NUIColor& color) {
    // Draw 4 lines
    // Inset the Right and Bottom lines by 1.0f so they stay INSIDE the clip rect (which is integer aligned)
    // This prevents the "aggressive cutoff" where the stroke width causes it to cross the clip boundary.
    drawLine(NUIPoint({rect.x, rect.y}), NUIPoint({rect.right() - 1.0f, rect.y}), thickness, color);
    drawLine(NUIPoint({rect.right() - 1.0f, rect.y}), NUIPoint({rect.right() - 1.0f, rect.bottom() - 1.0f}), thickness, color);
    drawLine(NUIPoint({rect.right() - 1.0f, rect.bottom() - 1.0f}), NUIPoint({rect.x, rect.bottom() - 1.0f}), thickness, color);
    drawLine(NUIPoint({rect.x, rect.bottom() - 1.0f}), NUIPoint({rect.x, rect.y}), thickness, color);
}

void NUIRendererGL::strokeRoundedRect(const NUIRect& rect, float radius, float thickness, const NUIColor& color) {
    float w = rect.width;
    float h = rect.height;
    
    // Blur 1.0f for AA
    // Snap to pixel grid + 0.5f, BUT reduce width/height by 1.0f to keep stroke INSIDE integers
    AestraUI::NUIRect snapped = rect;
    snapped.x = std::floor(rect.x) + 0.5f;
    snapped.y = std::floor(rect.y) + 0.5f;
    snapped.width = std::round(rect.width) - 1.0f;
    snapped.height = std::round(rect.height) - 1.0f;

    // The stroked SDF needs a slightly larger carrier quad than the logical rect,
    // otherwise the AA fringe gets clipped on the bottom/right edge and borders
    // look visibly shaved.
    const float aaPad = std::max(1.5f, thickness * 0.75f + 0.5f);
    AestraUI::NUIRect quad = {
        snapped.x - aaPad,
        snapped.y - aaPad,
        snapped.width + aaPad * 2.0f,
        snapped.height + aaPad * 2.0f
    };

    // Standard behavior: Clamp radius to fit
    float safeRadius = std::min(radius, std::min(snapped.width * 0.5f, snapped.height * 0.5f));
    addQuad(quad, color, snapped.width, snapped.height, quad.width, quad.height, safeRadius, 1.0f, thickness, 3.0f);
}

void NUIRendererGL::fillCircle(const NUIPoint& center, float radius, const NUIColor& color) {
    constexpr float kCirclePad = 1.5f;
    const float diameter = radius * 2.0f;
    const NUIRect quad = {
        center.x - radius - kCirclePad,
        center.y - radius - kCirclePad,
        diameter + kCirclePad * 2.0f,
        diameter + kCirclePad * 2.0f
    };
    addQuad(quad, color, diameter, diameter, quad.width, quad.height, radius, 1.0f, 0.0f, 6.0f);
}

void NUIRendererGL::strokeCircle(const NUIPoint& center, float radius, float thickness, const NUIColor& color) {
    const float aaPad = std::max(1.5f, thickness * 0.75f + 0.5f);
    const float diameter = radius * 2.0f;
    const NUIRect quad = {
        center.x - radius - aaPad,
        center.y - radius - aaPad,
        diameter + aaPad * 2.0f,
        diameter + aaPad * 2.0f
    };
    addQuad(quad, color, diameter, diameter, quad.width, quad.height, radius, 1.0f, thickness, 7.0f);
}

void NUIRendererGL::drawLine(const NUIPoint& start, const NUIPoint& end, float thickness, const NUIColor& color) {
    ensureBasicPrimitive();

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    // Simple line as thin quad
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float len = std::sqrt(dx * dx + dy * dy);
    
    if (len < 0.001f) return;

    // Sub-pixel Snapping for crisp 1px lines (Horizontal/Vertical only)
    float x1 = start.x;
    float y1 = start.y;
    float x2 = end.x;
    float y2 = end.y;

    const float kSnapThreshold = 0.1f;
    if (thickness < 1.5f) { // Only snap thin lines
        if (std::abs(dx) < kSnapThreshold) { // Vertical
            // Snap X to pixel center (N.5)
            float snappedX = std::floor(x1) + 0.5f;
            x1 = snappedX;
            x2 = snappedX;
            // Snap Y to nearest integer
            y1 = std::round(y1);
            y2 = std::round(y2);
        } else if (std::abs(dy) < kSnapThreshold) { // Horizontal
            // Snap Y to pixel center (N.5)
            float snappedY = std::floor(y1) + 0.5f;
            y1 = snappedY;
            y2 = snappedY;
            // Snap X to nearest integer
            x1 = std::round(x1);
            x2 = std::round(x2);
        }
    }
    
    dx = x2 - x1;
    dy = y2 - y1; // Recompute deltas
    len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    const float halfThickness = thickness * 0.5f;
    const float featherWidth = std::max(0.55f, thickness * 0.55f);
    const float innerNx = -dy / len * halfThickness;
    const float innerNy = dx / len * halfThickness;
    const float outerNx = -dy / len * (halfThickness + featherWidth);
    const float outerNy = dx / len * (halfThickness + featherWidth);

    const NUIColor transparent(color.r, color.g, color.b, 0.0f);
    const uint32_t base = static_cast<uint32_t>(vertices_.size());

    addVertex(x1 + outerNx, y1 + outerNy, 0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x1 + innerNx, y1 + innerNy, 0.0f, 0.0f, color,       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x1 - innerNx, y1 - innerNy, 0.0f, 0.0f, color,       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x1 - outerNx, y1 - outerNy, 0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x2 + outerNx, y2 + outerNy, 0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x2 + innerNx, y2 + innerNy, 0.0f, 0.0f, color,       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x2 - innerNx, y2 - innerNy, 0.0f, 0.0f, color,       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    addVertex(x2 - outerNx, y2 - outerNy, 0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);

    auto addQuadIndices = [this](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        indices_.push_back(a);
        indices_.push_back(b);
        indices_.push_back(c);
        indices_.push_back(c);
        indices_.push_back(b);
        indices_.push_back(d);
    };

    addQuadIndices(base + 0, base + 1, base + 4, base + 5);
    addQuadIndices(base + 1, base + 2, base + 5, base + 6);
    addQuadIndices(base + 2, base + 3, base + 6, base + 7);
}

void NUIRendererGL::drawPolyline(const NUIPoint* points, int count, float thickness, const NUIColor& color) {
    ensureBasicPrimitive();

    if (count < 2) return;

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    if (count == 2) {
        drawLine(points[0], points[1], thickness, color);
        return;
    }

    const float halfThickness = thickness * 0.5f;
    const float featherWidth = std::max(0.55f, thickness * 0.55f);
    const uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    const NUIColor transparent(color.r, color.g, color.b, 0.0f);

    auto normalize = [](float x, float y) {
        const float len = std::sqrt(x * x + y * y);
        if (len < 1.0e-4f) {
            return std::pair<float, float>{0.0f, 0.0f};
        }
        return std::pair<float, float>{x / len, y / len};
    };

    for (int i = 0; i < count; ++i) {
        const NUIPoint& curr = points[i];
        const NUIPoint& prev = (i > 0) ? points[i - 1] : points[i];
        const NUIPoint& next = (i + 1 < count) ? points[i + 1] : points[i];

        const auto [prevDirX, prevDirY] = normalize(curr.x - prev.x, curr.y - prev.y);
        const auto [nextDirX, nextDirY] = normalize(next.x - curr.x, next.y - curr.y);

        float nx = 0.0f;
        float ny = 0.0f;

        if (i == 0) {
            nx = -nextDirY;
            ny = nextDirX;
        } else if (i == count - 1) {
            nx = -prevDirY;
            ny = prevDirX;
        } else {
            const float prevNx = -prevDirY;
            const float prevNy = prevDirX;
            const float nextNx = -nextDirY;
            const float nextNy = nextDirX;

            const float miterX = prevNx + nextNx;
            const float miterY = prevNy + nextNy;
            const auto [miterNormX, miterNormY] = normalize(miterX, miterY);

            if (std::abs(miterNormX) < 1.0e-4f && std::abs(miterNormY) < 1.0e-4f) {
                nx = nextNx;
                ny = nextNy;
            } else {
                // TODO(renderer): When the miter limit is exceeded, switch this join
                // to bevel/round geometry instead of clamping the miter scale. The
                // current clamp avoids extreme spikes, but a real join fallback is the
                // correct long-term fix for acute polyline corners.
                const float dot = std::max(0.25f, miterNormX * nextNx + miterNormY * nextNy);
                const float miterScale = std::min(3.0f, 1.0f / dot);
                nx = miterNormX * miterScale;
                ny = miterNormY * miterScale;
            }
        }

        addVertex(curr.x + nx * (halfThickness + featherWidth), curr.y + ny * (halfThickness + featherWidth),
                  0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
        addVertex(curr.x + nx * halfThickness, curr.y + ny * halfThickness,
                  0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
        addVertex(curr.x - nx * halfThickness, curr.y - ny * halfThickness,
                  0.0f, 0.0f, color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
        addVertex(curr.x - nx * (halfThickness + featherWidth), curr.y - ny * (halfThickness + featherWidth),
                  0.0f, 0.0f, transparent, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    }

    for (int i = 0; i < count - 1; ++i) {
        const uint32_t col0 = baseIndex + static_cast<uint32_t>(i * 4);
        const uint32_t col1 = col0 + 4;

        const uint32_t outerLeft0 = col0 + 0;
        const uint32_t innerLeft0 = col0 + 1;
        const uint32_t innerRight0 = col0 + 2;
        const uint32_t outerRight0 = col0 + 3;

        const uint32_t outerLeft1 = col1 + 0;
        const uint32_t innerLeft1 = col1 + 1;
        const uint32_t innerRight1 = col1 + 2;
        const uint32_t outerRight1 = col1 + 3;

        auto addQuadIndices = [this](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            indices_.push_back(a);
            indices_.push_back(b);
            indices_.push_back(c);
            indices_.push_back(c);
            indices_.push_back(b);
            indices_.push_back(d);
        };

        addQuadIndices(outerLeft0, innerLeft0, outerLeft1, innerLeft1);
        addQuadIndices(innerLeft0, innerRight0, innerLeft1, innerRight1);
        addQuadIndices(innerRight0, outerRight0, innerRight1, outerRight1);
    }
}

void NUIRendererGL::fillWaveform(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, const NUIColor& color) {
    // Flush previous batch if untyped or different type to ensure clean state
    ensureBasicPrimitive();
    
    if (count < 2) return;
    
    ensureBasicPrimitive();

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }
    
    // Build triangle strip: top[0], bottom[0], top[1], bottom[1], ...
    // This creates a filled shape between the top and bottom edges
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    
    // Add all vertices - addVertex handles transform internally
    // Use Type 5 (Colored Geometry) for solid crisp rendering
    // Arguments: x, y, u, v, color, rw, rh, qw, qh, radius, blur, strokeWidth, type
    for (int i = 0; i < count; ++i) {
        addVertex(topPoints[i].x, topPoints[i].y, 0, 0, color, 0, 0, 0, 0, 0, 0, 0, 5.0f);
        addVertex(bottomPoints[i].x, bottomPoints[i].y, 0, 1, color, 0, 0, 0, 0, 0, 0, 0, 5.0f);
    }
    
    // Build triangle strip indices
    // For each quad between columns: 4 vertices -> 2 triangles
    for (int i = 0; i < count - 1; ++i) {
        uint32_t topLeft = baseIndex + i * 2;
        uint32_t bottomLeft = baseIndex + i * 2 + 1;
        uint32_t topRight = baseIndex + (i + 1) * 2;
        uint32_t bottomRight = baseIndex + (i + 1) * 2 + 1;
        
        // Triangle 1: topLeft, bottomLeft, topRight
        indices_.push_back(topLeft);
        indices_.push_back(bottomLeft);
        indices_.push_back(topRight);
        
        // Triangle 2: topRight, bottomLeft, bottomRight
        indices_.push_back(topRight);
        indices_.push_back(bottomLeft);
        indices_.push_back(bottomRight);
    }
}

void NUIRendererGL::fillWaveformGradient(const NUIPoint* topPoints, const NUIPoint* bottomPoints, int count, 
                                          const NUIColor& colorTop, const NUIColor& colorBottom) {
    // Flush previous batch if untyped or different type
    ensureBasicPrimitive();

    if (count < 2) return;

    // Ensure we are not using a texture (batch breaking)
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }
    
    // Build triangle strip with gradient colors (bright at top, darker at bottom)
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    
    // Add vertices with different colors for top and bottom edges
    // Fixed: Use Type 5 (Colored Geometry) to prevent texture sampling
    // Arguments: x, y, u, v, color, rw, rh, qw, qh, radius, blur, strokeWidth, type
    for (int i = 0; i < count; ++i) {
        addVertex(topPoints[i].x, topPoints[i].y, 0, 0, colorTop, 0, 0, 0, 0, 0, 0, 0, 5.0f);
        addVertex(bottomPoints[i].x, bottomPoints[i].y, 0, 1, colorBottom, 0, 0, 0, 0, 0, 0, 0, 5.0f);
    }
    
    // Build triangle strip indices
    for (int i = 0; i < count - 1; ++i) {
        uint32_t topLeft = baseIndex + i * 2;
        uint32_t bottomLeft = baseIndex + i * 2 + 1;
        uint32_t topRight = baseIndex + (i + 1) * 2;
        uint32_t bottomRight = baseIndex + (i + 1) * 2 + 1;
        
        // Triangle 1: topLeft, bottomLeft, topRight
        indices_.push_back(topLeft);
        indices_.push_back(bottomLeft);
        indices_.push_back(topRight);
        
        // Triangle 2: topRight, bottomLeft, bottomRight
        indices_.push_back(topRight);
        indices_.push_back(bottomLeft);
        indices_.push_back(bottomRight);
    }
}

// ============================================================================
// Gradient Drawing
// ============================================================================

void NUIRendererGL::fillRectGradient(const NUIRect& rect, const NUIColor& colorStart, const NUIColor& colorEnd, bool vertical) {
    ensureBasicPrimitive();
    if (vertical) {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorStart);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorEnd);
    } else {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorEnd);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorStart);
    }
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::fillCircleGradient(const NUIPoint& center, float radius, const NUIColor& colorInner, const NUIColor& colorOuter) {
    ensureBasicPrimitive();
    // Simple radial gradient
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        addVertex(center.x, center.y, 0.5f, 0.5f, colorInner);
        addVertex(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius, 0, 0, colorOuter);
        addVertex(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius, 1, 1, colorOuter);
    }
}

// ============================================================================
// Effects (Simplified for now)
// ============================================================================

void NUIRendererGL::drawGlow(const NUIRect& rect, float radius, float intensity, const NUIColor& color) {
    // Simple glow as expanded semi-transparent rect
    NUIRect glowRect = rect;
    glowRect.x -= radius;
    glowRect.y -= radius;
    glowRect.width += radius * 2;
    glowRect.height += radius * 2;
    
    NUIColor glowColor = color;
    glowColor.a *= intensity * 0.3f;
    
    fillRect(glowRect, glowColor);
}

void NUIRendererGL::drawShadow(const NUIRect& rect, float offsetX, float offsetY, float blur, const NUIColor& color) {
    AESTRA_ZONE("Renderer_DrawShadow");
    
    // Shadow quad is larger than the rect to contain the blur
    float spread = blur * 2.0f;
    NUIRect shadowQuad = rect;
    shadowQuad.x += offsetX - spread;
    shadowQuad.y += offsetY - spread;
    shadowQuad.width += spread * 2.0f;
    shadowQuad.height += spread * 2.0f;
    
    // Pass RectSize (logic size) and QuadSize (drawing size) separately
    // Radius fixed at 8.0f for standard shadows for now, matching previous logic
    // Type 1 = Filled Rect (Shadows use same SDF logic with blur)
    addQuad(shadowQuad, color, rect.width, rect.height, shadowQuad.width, shadowQuad.height, 8.0f, blur, 0.0f, 1.0f);
}

// ============================================================================
// Text Rendering
// ============================================================================

void NUIRendererGL::drawText(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    const float effectiveFontSize = normalizeSmallTextSize(fontSize);
    // Use FreeType atlas for ALL text - consistent rendering
    
    if (fontInitialized_) {
        // Use centralized atlas selection
        AtlasInfo atlas = selectAtlas(effectiveFontSize);
        
        // Scale factor from baked atlas to requested fontSize
        float scale = effectiveFontSize / static_cast<float>(atlas.atlasSize);
        float scaledAscent = atlas.ascent * scale;

        renderTextWithFont(text, NUIPoint(position.x, position.y + scaledAscent), effectiveFontSize, color);
        return;
    }

    // Fallback only if FreeType not initialized - simple rectangles.
    // Size from effectiveFontSize (not raw fontSize) so this matches the floor
    // measureText() applies, keeping fallback measure and render consistent.
    float charWidth = effectiveFontSize * 0.5f;
    float charHeight = effectiveFontSize * 0.8f;
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c >= 32 && c <= 126) {
            float x = position.x + i * charWidth;
            float y = position.y;
            drawCleanCharacter(c, x, y, charWidth, charHeight, color);
        }
    }
}


// ============================================================================
// High-Quality Text Rendering Helpers
// ============================================================================

float NUIRendererGL::getDPIScale() {
#ifdef _WIN32
    HDC hdc = GetDC(NULL);
    if (hdc) {
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        return dpiX / 96.0f; // 96 is standard DPI
    }
#endif
    return 1.0f; // Default scale
}

NUIRendererGL::AtlasInfo NUIRendererGL::selectAtlas(float fontSize) const {
    AtlasInfo info;
    
    const bool useXSmallAtlas = (fontSize <= kTierMaxXSmall);
    const bool useSmallAtlas = (!useXSmallAtlas && fontSize <= kTierMaxSmall);
    const bool useMediumAtlas = (!useXSmallAtlas && !useSmallAtlas && fontSize <= kTierMaxMedium);
    
    if (useXSmallAtlas && fontAtlasTextureIdXSmall_ != 0 && atlasFontSizeXSmall_ > 0) {
        info.textureId = fontAtlasTextureIdXSmall_;
        info.atlasSize = atlasFontSizeXSmall_;
        info.ascent = fontAscentXSmall_;
        info.descent = fontDescentXSmall_;
        info.lineHeight = fontLineHeightXSmall_;
        info.cache = &fontCacheXSmall_;
    } else if (useSmallAtlas && fontAtlasTextureIdSmall_ != 0 && atlasFontSizeSmall_ > 0) {
        info.textureId = fontAtlasTextureIdSmall_;
        info.atlasSize = atlasFontSizeSmall_;
        info.ascent = fontAscentSmall_;
        info.descent = fontDescentSmall_;
        info.lineHeight = fontLineHeightSmall_;
        info.cache = &fontCacheSmall_;
    } else if (useMediumAtlas && fontAtlasTextureIdMedium_ != 0 && atlasFontSizeMedium_ > 0) {
        info.textureId = fontAtlasTextureIdMedium_;
        info.atlasSize = atlasFontSizeMedium_;
        info.ascent = fontAscentMedium_;
        info.descent = fontDescentMedium_;
        info.lineHeight = fontLineHeightMedium_;
        info.cache = &fontCacheMedium_;
    } else {
        info.textureId = fontAtlasTextureId_;
        info.atlasSize = atlasFontSize_;
        info.ascent = fontAscent_;
        info.descent = fontDescent_;
        info.lineHeight = fontLineHeight_;
        info.cache = &fontCache_;
    }
    
    return info;
}

// Deliberately adjacent to selectAtlas(): the thresholds below must match the
// branch conditions above, and the only thing keeping them matched is that a
// reader changing one has the other on screen. The resolved uniforms come from
// the shared resolveText* helpers rather than being recomputed here, so those
// cannot drift at all.
bool NUIRendererGL::getTextDiagnostics(TextDiagnostics& out) const {
    if (!fontInitialized_) {
        return false;
    }

    out = TextDiagnostics{};
    out.lcdSubpixel = fontUseLCD_;
    out.framebufferSRGB = framebufferSRGBEnabled_;
    out.textContrast = textContrast_;
    out.alphaPreserved = true;  // color.a *= coverage — no reshaping. See the type 4 shader branch.
    out.fontPath = defaultFontPath_.c_str();

    // Order matches selectAtlas()'s branch order, smallest served size first.
    const struct { const char* name; int size; float maxFont; } kTiers[] = {
        {"XSmall",  atlasFontSizeXSmall_, kTierMaxXSmall},
        {"Small",   atlasFontSizeSmall_,  kTierMaxSmall},
        {"Medium",  atlasFontSizeMedium_, kTierMaxMedium},
        {"Regular", static_cast<int>(atlasFontSize_), 0.0f},
    };

    out.tierCount = 0;
    for (const auto& t : kTiers) {
        TextDiagnostics::Tier& tier = out.tiers[out.tierCount++];
        tier.name = t.name;
        tier.atlasSize = t.size;
        tier.maxFontSize = t.maxFont;
        const bool tiny = isHeavilySupersampled(t.size, t.maxFont);
        tier.gamma = resolveTextGamma(tiny, textContrast_);
        tier.sharpen = resolveTextSharpen(tiny);
    }
    return true;
}

void NUIRendererGL::drawCleanCharacter(char c, float x, float y, float width, float height, const NUIColor& color) {
    // Clean character rendering using filled rectangles
    // This creates much more readable text
    
    float charWidth = width * 0.8f;
    float charHeight = height;
    float thickness = charWidth * 0.15f; // Thickness of character elements
    
    // Center the character
    float charX = x + (width - charWidth) * 0.5f;
    float charY = y + (height - charHeight) * 0.5f;
    
    // Draw character using clean filled rectangles
    switch (c) {
        case 'A':
        case 'a':
            // A shape: triangle with crossbar
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.6f, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'B':
        case 'b':
            // B shape: vertical line with two rectangles
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            break;
        case 'C':
        case 'c':
            // C shape: curved rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            break;
        case 'D':
        case 'd':
            // D shape: vertical line with curved right side
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.2f, thickness, charHeight*0.6f), color);
            break;
        case 'E':
        case 'e':
            // E shape: vertical line with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            break;
        case 'F':
        case 'f':
            // F shape: vertical line with two horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            break;
        case 'G':
        case 'g':
            // G shape: C with additional line
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.5f, charWidth*0.3f, thickness), color);
            break;
        case 'H':
        case 'h':
            // H shape: two verticals with horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            break;
        case 'I':
        case 'i':
            // I shape: vertical line with top and bottom
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case 'J':
        case 'j':
            // J shape: vertical with curve
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight*0.7f), color);
            fillRect(NUIRect(charX, charY, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.7f, charWidth*0.4f, thickness), color);
            break;
        case 'K':
        case 'k':
            // K shape: vertical with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.3f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.7f, thickness, charHeight*0.3f), color);
            break;
        case 'L':
        case 'l':
            // L shape: vertical with bottom horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            break;
        case 'M':
        case 'm':
            // M shape: two verticals with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'N':
        case 'n':
            // N shape: two verticals with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.3f, thickness, charHeight*0.4f), color);
            break;
        case 'O':
        case 'o':
            // O shape: rounded rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case 'P':
        case 'p':
            // P shape: vertical with top and middle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.7f, charY, thickness, charHeight*0.5f), color);
            break;
        case 'Q':
        case 'q':
            // O with tail
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'R':
        case 'r':
            // P with diagonal
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.7f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.7f, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case 'S':
        case 's':
            // S shape: three horizontals with verticals
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case 'T':
        case 't':
            // T shape: horizontal with vertical
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            break;
        case 'U':
        case 'u':
            // U shape: two verticals with bottom
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.8f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight*0.8f), color);
            fillRect(NUIRect(charX, charY + charHeight*0.8f, charWidth, thickness), color);
            break;
        case 'V':
        case 'v':
            // V shape: two diagonals
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.5f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'W':
        case 'w':
            // W shape: four verticals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case 'X':
        case 'x':
            // X shape: two diagonals
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.6f, thickness, charHeight*0.4f), color);
            break;
        case 'Y':
        case 'y':
            // Y shape: V with vertical
            fillRect(NUIRect(charX + charWidth*0.2f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight*0.4f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.4f, thickness, charHeight*0.6f), color);
            break;
        case 'Z':
        case 'z':
            // Z shape: three horizontals
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.5f, charWidth*0.4f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '0':
            // 0 shape: rounded rectangle
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '1':
            // 1 shape: vertical with top
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.2f, charY, charWidth*0.4f, thickness), color);
            break;
        case '2':
            // 2 shape: top, middle, bottom
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY + charHeight*0.5f, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '3':
            // 3 shape: three horizontals with vertical
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case '4':
            // 4 shape: vertical with horizontal
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.6f), color);
            fillRect(NUIRect(charX, charY + charHeight*0.4f, charWidth*0.6f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.6f, charY, thickness, charHeight), color);
            break;
        case '5':
            // 5 shape: top, middle, bottom
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '6':
            // 6 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth*0.8f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY + charHeight*0.5f, thickness, charHeight*0.5f), color);
            break;
        case '7':
            // 7 shape: top with vertical
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            break;
        case '8':
            // 8 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '9':
            // 9 shape: vertical with three horizontals
            fillRect(NUIRect(charX, charY, thickness, charHeight*0.5f), color);
            fillRect(NUIRect(charX + charWidth*0.8f, charY, thickness, charHeight), color);
            fillRect(NUIRect(charX, charY, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight*0.5f, charWidth, thickness), color);
            fillRect(NUIRect(charX, charY + charHeight - thickness, charWidth, thickness), color);
            break;
        case '.':
            // Period: small square
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case ',':
            // Comma: small square with tail
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.9f, thickness*0.5f, thickness*0.5f), color);
            break;
        case ':':
            // Colon: two small squares
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.3f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.7f, thickness, thickness), color);
            break;
        case ';':
            // Semicolon: colon with tail
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.3f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.7f, thickness, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.3f, charY + charHeight*0.8f, thickness*0.5f, thickness*0.5f), color);
            break;
        case '!':
            // Exclamation: vertical with dot
            fillRect(NUIRect(charX + charWidth*0.4f, charY, thickness, charHeight*0.7f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case '?':
            // Question mark: curve with dot
            fillRect(NUIRect(charX + charWidth*0.6f, charY, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.2f, thickness, charHeight*0.3f), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.5f, charWidth*0.2f, thickness), color);
            fillRect(NUIRect(charX + charWidth*0.4f, charY + charHeight*0.8f, thickness, thickness), color);
            break;
        case ' ':
            // Space: nothing
            break;
        default:
            // Unknown character: simple rectangle
            fillRect(NUIRect(charX + charWidth*0.2f, charY + charHeight*0.2f, charWidth*0.6f, charHeight*0.6f), color);
            break;
    }
}

void NUIRendererGL::drawCharacter(char c, float x, float y, float width, float height, const NUIColor& color) {
    // Improved character rendering with better proportions and smoother lines
    // This creates a more refined bitmap font representation
    
    float charWidth = width * 0.7f;  // Slightly narrower for better spacing
    float charHeight = height * 0.8f; // Slightly shorter for better proportions
    float lineWidth = charWidth * 0.08f; // Thinner lines for cleaner look
    
    // Adjust height based on character type
    if (c >= 'a' && c <= 'z') charHeight *= 0.85f; // lowercase
    else if (c >= 'A' && c <= 'Z') charHeight *= 0.95f; // uppercase
    else if (c >= '0' && c <= '9') charHeight *= 0.9f; // numbers
    else if (c == ' ') return; // space - don't draw anything
    else if (c == '.' || c == ',' || c == ';' || c == ':') charHeight *= 0.5f; // punctuation
    
    // Center the character both horizontally and vertically
    float charX = x + (width - charWidth) * 0.5f;
    float charY = y + (height - charHeight) * 0.5f;
    
    // Draw character patterns based on ASCII value
    switch (c) {
        case 'A':
        case 'a':
            // A shape: /\ and - (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight), NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.85f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.25f, charY + charHeight*0.6f), NUIPoint(charX + charWidth*0.75f, charY + charHeight*0.6f), lineWidth, color);
            break;
        case 'B':
        case 'b':
            // B shape: | and curves
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.75f), lineWidth, color);
            break;
        case 'C':
        case 'c':
            // C shape: curve
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case 'D':
        case 'd':
            // D shape: | and curve
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.6f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.6f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.6f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.6f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'E':
        case 'e':
            // E shape: | and horizontal lines (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.15f, charY), NUIPoint(charX + charWidth*0.15f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY), NUIPoint(charX + charWidth*0.85f, charY), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight), NUIPoint(charX + charWidth*0.85f, charY + charHeight), lineWidth, color);
            break;
        case 'F':
        case 'f':
            // F shape: | and horizontal lines
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.6f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'G':
        case 'g':
            // G shape: C with line
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'H':
        case 'h':
            // H shape: | | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            break;
        case 'I':
        case 'i':
            // I shape: | with top and bottom
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'J':
        case 'j':
            // J shape: | with curve
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            break;
        case 'K':
        case 'k':
            // K shape: | and diagonal
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'L':
        case 'l':
            // L shape: | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case 'M':
        case 'm':
            // M shape: | \ / |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'N':
        case 'n':
            // N shape: | \ |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'O':
        case 'o':
            // O shape: circle/oval
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case 'P':
        case 'p':
            // P shape: | and P
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            break;
        case 'Q':
        case 'q':
            // Q shape: O with tail
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'R':
        case 'r':
            // R shape: P with diagonal
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.25f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.7f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'S':
        case 's':
            // S shape: S curve (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.5f), NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.9f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.85f, charY + charHeight*0.9f), NUIPoint(charX + charWidth*0.15f, charY + charHeight*0.9f), lineWidth, color);
            break;
        case 'T':
        case 't':
            // T shape: - and | (improved proportions)
            drawLine(NUIPoint(charX + charWidth*0.1f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.9f, charY + charHeight*0.1f), lineWidth, color);
            drawLine(NUIPoint(charX + charWidth*0.5f, charY + charHeight*0.1f), NUIPoint(charX + charWidth*0.5f, charY + charHeight), lineWidth, color);
            break;
        case 'U':
        case 'u':
            // U shape: | | and -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'V':
        case 'v':
            // V shape: \ /
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            break;
        case 'W':
        case 'w':
            // W shape: | \ / |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case 'X':
        case 'x':
            // X shape: \ /
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            break;
        case 'Y':
        case 'y':
            // Y shape: \ / and |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            break;
        case 'Z':
        case 'z':
            // Z shape: - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '0':
            // 0 shape: oval
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.7f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY + charHeight*0.2f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.8f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY + charHeight), NUIPoint(x + charWidth*0.7f, charY + charHeight), lineWidth, color);
            break;
        case '1':
            // 1 shape: |
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.3f, charY), NUIPoint(x + charWidth*0.5f, charY), lineWidth, color);
            break;
        case '2':
            // 2 shape: - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '3':
            // 3 shape: - | -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '4':
            // 4 shape: | \ |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '5':
            // 5 shape: | - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '6':
            // 6 shape: | - \ -
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.8f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.8f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.8f, charY + charHeight), lineWidth, color);
            break;
        case '7':
            // 7 shape: - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '8':
            // 8 shape: | - | - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '9':
            // 9 shape: | - | - |
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.9f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
        case '.':
            // Period: small dot
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ',':
            // Comma: small dot with tail
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            drawLine(NUIPoint(x + charWidth*0.4f, charY + charHeight*0.8f), NUIPoint(x + charWidth*0.3f, charY + charHeight), lineWidth, color);
            break;
        case ':':
            // Colon: two dots
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.3f, charWidth*0.2f, charHeight*0.2f), color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.7f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ';':
            // Semicolon: dot with tail and comma
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.3f, charWidth*0.2f, charHeight*0.2f), color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.7f, charWidth*0.2f, charHeight*0.2f), color);
            drawLine(NUIPoint(x + charWidth*0.4f, charY + charHeight*0.7f), NUIPoint(x + charWidth*0.3f, charY + charHeight), lineWidth, color);
            break;
        case '!':
            // Exclamation: | and dot
            drawLine(NUIPoint(x + charWidth*0.5f, charY), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.7f), lineWidth, color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case '?':
            // Question mark: ? shape
            drawLine(NUIPoint(x + charWidth*0.7f, charY), NUIPoint(x + charWidth*0.1f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight*0.3f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight*0.3f), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.5f, charY + charHeight*0.5f), NUIPoint(x + charWidth*0.5f, charY + charHeight*0.7f), lineWidth, color);
            fillRect(NUIRect(x + charWidth*0.4f, charY + charHeight*0.8f, charWidth*0.2f, charHeight*0.2f), color);
            break;
        case ' ':
            // Space: nothing
            break;
        default:
            // Unknown character: draw a box
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.9f, charY), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY), NUIPoint(x + charWidth*0.1f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.9f, charY), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            drawLine(NUIPoint(x + charWidth*0.1f, charY + charHeight), NUIPoint(x + charWidth*0.9f, charY + charHeight), lineWidth, color);
            break;
    }
}

void NUIRendererGL::drawTextCentered(const std::string& text, const NUIRect& rect, float fontSize, const NUIColor& color) {
    const float effectiveFontSize = normalizeSmallTextSize(fontSize);
    // Measure actual text dimensions
    NUISize textSize = measureText(text, effectiveFontSize);

    // Calculate horizontal centering
    float x = std::round(rect.x + (rect.width - textSize.width) * 0.5f);

    // Calculate vertical centering using real font metrics (Top-Left Y + Ascent).
    float y = std::round(calculateTextY(rect, effectiveFontSize));
    
    drawText(text, NUIPoint(x, y), effectiveFontSize, color);
}

NUIRenderer::FontMetrics NUIRendererGL::getFontMetrics(float fontSize) const {
    NUIRenderer::FontMetrics metrics;
    if (fontInitialized_) {
        const AtlasInfo atlas = selectAtlas(fontSize);
        if (atlas.atlasSize > 0) {
            const float scale = fontSize / static_cast<float>(atlas.atlasSize);
            metrics.ascent = atlas.ascent * scale;
            metrics.descent = atlas.descent * scale;
            metrics.lineHeight = atlas.lineHeight * scale;
            return metrics;
        }
        return metrics;
    }

    // Fallback approximation (matches the base NUIRenderer default).
    metrics.ascent = fontSize * 0.8f;
    metrics.descent = fontSize * 0.2f;
    metrics.lineHeight = metrics.ascent + metrics.descent;
    return metrics;
}

NUISize NUIRendererGL::measureText(const std::string& text, float fontSize) {
    // IMPORTANT: drawText() renders via the FreeType atlas path; prefer matching metrics here
    // so layout (centering/truncation) matches what actually hits the screen.
    // Apply the SAME readability floor drawText() uses, or a small string would
    // measure narrower than it renders and wrap/centre/fit calculations would be
    // wrong (the size is colour-independent, so measureText can apply it too).
    fontSize = normalizeSmallTextSize(fontSize);

    // Handle empty text quickly
    if (text.empty()) {
        if (fontInitialized_) {
            AtlasInfo atlas = selectAtlas(fontSize);
            if (atlas.atlasSize > 0) {
                float scale = fontSize / static_cast<float>(atlas.atlasSize);
                return {0.0f, atlas.lineHeight * scale};
            }
        }
        return {0.0f, fontSize};
    }

    // Check measurement cache first (major performance optimization for repeated strings)
    TextMeasurementKey cacheKey{text, fontSize};
    auto cacheIt = textMeasurementCache_.find(cacheKey);
    if (cacheIt != textMeasurementCache_.end()) {
        return cacheIt->second;
    }

    NUISize result;

    if (fontInitialized_) {
        try {
            AtlasInfo atlas = selectAtlas(fontSize);
            if (atlas.atlasSize <= 0 || atlas.cache == nullptr) {
                result = {text.length() * fontSize * 0.6f, fontSize};
            } else {
                float totalWidth = 0.0f;
                float scale = fontSize / static_cast<float>(atlas.atlasSize);
                FT_UInt previousGlyph = 0;
                FT_Face previousFace = nullptr;

                // UTF-8 decode loop
                size_t index = 0;
                while (index < text.length()) {
                    uint32_t codepoint = decodeUTF8(text, index);
                    if (codepoint == 0) break;
                    
                    auto it = atlas.cache->find(codepoint);
                    if (it == atlas.cache->end()) {
                        if (tryLoadFallbackGlyph(codepoint, atlas.atlasSize)) {
                            it = atlas.cache->find(codepoint);
                        }
                    }
                    if (it == atlas.cache->end()) {
                        // Advance by replacement width so measurement matches rendering
                        totalWidth += fontSize * 0.6f;
                        previousGlyph = 0;
                        previousFace = nullptr;
                        continue;
                    }

                    const FontData& glyph = it->second;

                    if (previousFace == glyph.face && previousGlyph != 0 && glyph.glyphIndex != 0) {
                        totalWidth += (getKerningUnits(glyph.face, previousGlyph, glyph.glyphIndex) / 64.0f) * scale;
                    }

                    totalWidth += (glyph.advance / 64.0f) * scale;
                    previousGlyph = glyph.glyphIndex;
                    previousFace = glyph.face;
                }

                result = {totalWidth, atlas.lineHeight * scale};
            }
        } catch (...) {
            // Fallback to simple estimation
            result = {text.length() * fontSize * 0.6f, fontSize};
        }
    } else {
        // No atlas yet: the caller is measuring before the font finished loading.
        // A width estimate is the only honest answer here — it is wrong, but it
        // is wrong in a bounded way and the measurement cache is keyed per size,
        // so real metrics replace it as soon as the atlas exists.
        result = {text.length() * fontSize * 0.6f, fontSize};
    }
    
    // Store in cache (with simple eviction if needed)
    if (textMeasurementCache_.size() >= kTextMeasurementCacheMaxSize) {
        // Simple eviction: clear half the cache when full
        auto it = textMeasurementCache_.begin();
        for (size_t i = 0; i < kTextMeasurementCacheMaxSize / 2 && it != textMeasurementCache_.end(); ++i) {
            it = textMeasurementCache_.erase(it);
        }
    }
    textMeasurementCache_[cacheKey] = result;
    
    return result;
}


// ============================================================================
// Real Font Rendering with FreeType
// ============================================================================

bool NUIRendererGL::loadFont(const std::string& fontPath) {
    if (FT_New_Face(ftLibrary_, fontPath.c_str(), 0, &ftFace_)) {
        AESTRA_LOG_ERROR("Failed to load font: " + fontPath);
        return false;
    }
    defaultFontPath_ = fontPath;
    kerningCache_.clear();

    fontHasKerning_ = FT_HAS_KERNING(ftFace_) != 0;

    auto buildAtlas = [&](int atlasFontSize,
                          uint32_t& atlasTextureId,
                          std::unordered_map<uint32_t, FontData>& cache, // Changed to uint32_t
                          float& outAscent,
                          float& outDescent,
                          float& outLineHeight,
                          int& atlasX,
                          int& atlasY,
                          int& atlasRowHeight) -> bool {
        if (FT_Set_Pixel_Sizes(ftFace_, 0, atlasFontSize) != 0) {
            AESTRA_LOG_ERROR("Failed to set atlas pixel size (" + std::to_string(atlasFontSize) + ") for font: " + fontPath);
            return false;
        }

        // Use precise float metrics
        float rawAscent = static_cast<float>(ftFace_->size->metrics.ascender) / 64.0f;
        float rawDescent = static_cast<float>(-(ftFace_->size->metrics.descender)) / 64.0f;
        float rawLineHeight = rawAscent + rawDescent;

        // FIX: Rebalance Metrics + Expand Spacing
        // 1. Rebalance Ascent so Cap Height is centered (keeps text vertically aligned).
        // 2. Expand Line Height by ~15% to remove "Invisible Border" feel.
        
        float verticalPadding = atlasFontSize * 0.10f; 
        float capHalfHeight = atlasFontSize * 0.35f;
        
        // Center of Expanded Box = (RawLineHeight + Padding) / 2
        // We want Cap Center to match this.
        float expandedLineHeight = rawLineHeight + verticalPadding;
        
        // FIX: Ensure ascent is never smaller than raw font ascent to prevent top cutoff
        outAscent = std::max((expandedLineHeight * 0.5f) + capHalfHeight, rawAscent);
        outDescent = expandedLineHeight - outAscent;
        outLineHeight = expandedLineHeight;

        if (atlasTextureId != 0) {
            glDeleteTextures(1, &atlasTextureId);
            atlasTextureId = 0;
        }
        glGenTextures(1, &atlasTextureId);
        glBindTexture(GL_TEXTURE_2D, atlasTextureId);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            fontAtlasWidth_,
            fontAtlasHeight_,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        atlasX = 0;
        atlasY = 0;
        atlasRowHeight = 0;
        cache.clear();

        int loadedChars = 0;
        const int loadFlagsBase = FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT;
        const int loadFlagsLCD = loadFlagsBase | FT_LOAD_TARGET_LCD;
        const int loadFlagsGray = loadFlagsBase | FT_LOAD_TARGET_LIGHT;
        const int padding = 3;

        // Build character set: ASCII + Unicode symbols
        std::vector<uint32_t> charSet;
        for (uint32_t c = 32; c <= 126; ++c) charSet.push_back(c); // ASCII printable
        
        // Add Unicode transport symbols for UI
        charSet.push_back(0x25B6); // ▶ Play
        charSet.push_back(0x25C0); // ◀ Reverse
        charSet.push_back(0x25A0); // ■ Stop
        charSet.push_back(0x23F8); // ⏸ Pause
        charSet.push_back(0x23EF); // ⏯ Play/Pause
charSet.push_back(0x23F9); // ⏹ Stop
        charSet.push_back(0x23FA); // ⏺ Record

        for (uint32_t codepoint : charSet) {
            FT_UInt glyphIndex = FT_Get_Char_Index(ftFace_, codepoint);
            if (glyphIndex == 0) {
                // Log only for symbols we explicitly requested (ignore control chars if any)
                if (codepoint > 127) {
                    AESTRA_LOG_STREAM_DEBUG << "Font missing glyph for U+" << std::hex << codepoint << std::dec;
                }
                continue; // skip if not in font
            }

            int loadFlags = fontUseLCD_ ? loadFlagsLCD : loadFlagsGray;
            if (FT_Load_Glyph(ftFace_, glyphIndex, loadFlags)) {
                continue;
            }

            // Thicken UI text at all maintained atlas sizes, but scale the
            // embolden amount so small labels gain confidence without turning
            // blurry or overfilled.
            if (ftFace_->glyph->format == FT_GLYPH_FORMAT_OUTLINE && atlasFontSize >= 11) {
                FT_Pos emboldenX = 14;
                FT_Pos emboldenY = 8;
                if (atlasFontSize >= 40) {
                    emboldenX = 36;
                    emboldenY = 18;
                } else if (atlasFontSize >= 22) {
                    emboldenX = 24;
                    emboldenY = 14;
                } else if (atlasFontSize >= 16) {
                    emboldenX = 20;
                    emboldenY = 12;
                } else if (atlasFontSize >= 14) {
                    emboldenX = 18;
                    emboldenY = 11;
                }
                FT_Outline_EmboldenXY(&ftFace_->glyph->outline, emboldenX, emboldenY);
            }

            const FT_Render_Mode renderMode = fontUseLCD_ ? FT_RENDER_MODE_LCD : FT_RENDER_MODE_NORMAL;
            if (FT_Render_Glyph(ftFace_->glyph, renderMode)) {
                continue;
            }

            FT_Bitmap* bitmap = &ftFace_->glyph->bitmap;
            const bool glyphIsLCD = fontUseLCD_ && (bitmap->pixel_mode == FT_PIXEL_MODE_LCD);
            int width = glyphIsLCD ? (bitmap->width / 3) : bitmap->width;
            int height = bitmap->rows;

            // Check if we need to move to next row
            if (atlasX + width + padding >= fontAtlasWidth_) {
                atlasX = 0;
                atlasY += atlasRowHeight + padding;
                atlasRowHeight = 0;
            }

            // Check if atlas is full
            if (atlasY + height + padding >= fontAtlasHeight_) {
                AESTRA_LOG_STREAM_ERROR << "Font atlas full (" << atlasFontSize << "px)!";
                break;
            }

            if (width > 0 && height > 0) {
                std::vector<unsigned char> rgba(static_cast<size_t>(width * height * 4), 0);
                const int rowStride = std::abs(bitmap->pitch);
                const bool flipRows = bitmap->pitch < 0;
                const unsigned char* base = flipRows
                    ? bitmap->buffer + static_cast<long>(rowStride) * (height - 1)
                    : bitmap->buffer;
                for (int y = 0; y < height; ++y) {
                    const unsigned char* srcRow = flipRows
                        ? base - static_cast<long>(y * rowStride)
                        : base + static_cast<long>(y * rowStride);
                    for (int x = 0; x < width; ++x) {
                        unsigned char r = 0, g = 0, b = 0, a = 0;
                        if (glyphIsLCD) {
                            r = srcRow[x * 3 + 0];
                            g = srcRow[x * 3 + 1];
                            b = srcRow[x * 3 + 2];
                            a = std::max({r, g, b});
                        } else {
                            // Store WHITE in RGB — only alpha carries coverage.
                            // This ensures the shader gets white * coverage = text color * coverage.
                            r = g = b = 255;  // White base
                            a = srcRow[x];     // Coverage in alpha
                        }
                        size_t dst = static_cast<size_t>(y * width + x) * 4;
                        rgba[dst + 0] = r;
                        rgba[dst + 1] = g;
                        rgba[dst + 2] = b;
                        rgba[dst + 3] = a;
                    }
                }

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    atlasX,
                    atlasY,
                    width,
                    height,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    rgba.data()
                );
            }

            FontData charData;
            charData.textureId = atlasTextureId;
            charData.face = ftFace_;
            charData.glyphIndex = glyphIndex;
            charData.width = width;
            charData.height = height;
            charData.bearingX = ftFace_->glyph->bitmap_left;
            charData.bearingY = ftFace_->glyph->bitmap_top;
            charData.advance = ftFace_->glyph->advance.x;
            if (codepoint == ' ') {
                // Geist/Manrope declare a 0.20 em space — well under the
                // 0.25-0.33 em of typical UI fonts — so at UI sizes words
                // visually run together ("Audio,MIDI,and"). Floor it at
                // 0.26 em (26.6 fixed point). measureText shares this cache,
                // so layout and rendering stay consistent.
                const int minSpace =
                    static_cast<int>(0.26f * ftFace_->size->metrics.x_ppem * 64.0f);
                charData.advance = std::max(charData.advance, minSpace);
            }

            float invW = 1.0f / fontAtlasWidth_;
            float invH = 1.0f / fontAtlasHeight_;
            charData.u0 = atlasX * invW;
            charData.v0 = atlasY * invH;
            charData.u1 = (atlasX + width) * invW;
            charData.v1 = (atlasY + height) * invH;

            cache[codepoint] = charData; // Cache by Unicode codepoint

            atlasX += width + padding;
            atlasRowHeight = std::max(atlasRowHeight, height);
            loadedChars++;
        }

        AESTRA_LOG_STREAM_DEBUG << "[Text] Font atlas built: " << fontPath
                               << " (" << atlasFontSize << "px, " << loadedChars << " glyphs, "
                               << (fontUseLCD_ ? "LCD subpixel" : "grayscale") << ")";
        return loadedChars > 0;
    };

    // Large atlas (36px) for headings and larger controls without oversoft downsampling.
    {
        const int ATLAS_FONT_SIZE = kAtlasSizeRegular;
        atlasFontSize_ = ATLAS_FONT_SIZE;
        if (!buildAtlas(ATLAS_FONT_SIZE,
                        fontAtlasTextureId_,
                        fontCache_,
                        fontAscent_,
                        fontDescent_,
                        fontLineHeight_,
                        fontAtlasX_,
                        fontAtlasY_,
                        fontAtlasRowHeight_)) {
            return false;
        }
    }

    // Medium atlas (20px) for the common 15-18 px UI copy.
    {
        const int ATLAS_FONT_SIZE_MEDIUM = kAtlasSizeMedium;
        atlasFontSizeMedium_ = ATLAS_FONT_SIZE_MEDIUM;
        (void)buildAtlas(ATLAS_FONT_SIZE_MEDIUM,
                         fontAtlasTextureIdMedium_,
                         fontCacheMedium_,
                         fontAscentMedium_,
                         fontDescentMedium_,
                         fontLineHeightMedium_,
                         fontAtlasXMedium_,
                         fontAtlasYMedium_,
                         fontAtlasRowHeightMedium_);
    }

    // Small atlas tuned for dense UI labels around 10-12 px.
    {
        const int ATLAS_FONT_SIZE_SMALL = kAtlasSizeSmall;
        atlasFontSizeSmall_ = ATLAS_FONT_SIZE_SMALL;
        (void)buildAtlas(ATLAS_FONT_SIZE_SMALL,
                         fontAtlasTextureIdSmall_,
                         fontCacheSmall_,
                         fontAscentSmall_,
                         fontDescentSmall_,
                         fontLineHeightSmall_,
                         fontAtlasXSmall_,
                         fontAtlasYSmall_,
                         fontAtlasRowHeightSmall_);
    }

    // Extra-small atlas for the densest 10-11 px copy.
    {
        const int ATLAS_FONT_SIZE_XSMALL = kAtlasSizeXSmall;
        atlasFontSizeXSmall_ = ATLAS_FONT_SIZE_XSMALL;
        (void)buildAtlas(ATLAS_FONT_SIZE_XSMALL,
                         fontAtlasTextureIdXSmall_,
                         fontCacheXSmall_,
                         fontAscentXSmall_,
                         fontDescentXSmall_,
                         fontLineHeightXSmall_,
                         fontAtlasXXSmall_,
                         fontAtlasYXSmall_,
                         fontAtlasRowHeightXSmall_);
    }

    fontInitialized_ = true;
    AESTRA_LOG_DEBUG("Font loaded: " + fontPath + " (quad atlases enabled)");
    return true;
}

bool NUIRendererGL::tryAddGlyphToAtlas(uint32_t codepoint, FT_Face face, int atlasFontSize,
    uint32_t atlasTextureId, std::unordered_map<uint32_t, FontData>& cache,
    int& atlasX, int& atlasY, int& atlasRowHeight)
{
    if (!face) return false;

    if (FT_Set_Pixel_Sizes(face, 0, atlasFontSize) != 0) {
        return false;
    }

    FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0) {
        return false;
    }

    const int loadFlagsBase = FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT;
    const int loadFlagsLCD = loadFlagsBase | FT_LOAD_TARGET_LCD;
    const int loadFlagsGray = loadFlagsBase | FT_LOAD_TARGET_LIGHT;
    int loadFlags = fontUseLCD_ ? loadFlagsLCD : loadFlagsGray;

    if (FT_Load_Glyph(face, glyphIndex, loadFlags)) {
        return false;
    }

    const FT_Render_Mode renderMode = fontUseLCD_ ? FT_RENDER_MODE_LCD : FT_RENDER_MODE_NORMAL;
    if (FT_Render_Glyph(face->glyph, renderMode)) {
        return false;
    }

    FT_Bitmap* bitmap = &face->glyph->bitmap;
    const bool glyphIsLCD = fontUseLCD_ && (bitmap->pixel_mode == FT_PIXEL_MODE_LCD);
    int width = glyphIsLCD ? (bitmap->width / 3) : bitmap->width;
    int height = bitmap->rows;
    const int padding = 3;

    if (width > 0 && height > 0) {
        if (atlasX + width + padding >= fontAtlasWidth_) {
            atlasX = 0;
            atlasY += atlasRowHeight + padding;
            atlasRowHeight = 0;
        }
        if (atlasY + height + padding >= fontAtlasHeight_) {
            static bool atlasFullWarningLogged = false;
            if (!atlasFullWarningLogged) {
                AESTRA_LOG_WARNING("Font atlas is full. Additional glyphs will not be rendered.");
                atlasFullWarningLogged = true;
            }
            return false; // atlas full
        }

        std::vector<unsigned char> rgba(static_cast<size_t>(width * height * 4), 0);
        const int rowStride = std::abs(bitmap->pitch);
        const bool flipRows = bitmap->pitch < 0;
        const unsigned char* base = flipRows
            ? bitmap->buffer + static_cast<long>(rowStride) * (height - 1)
            : bitmap->buffer;
        for (int y = 0; y < height; ++y) {
            const unsigned char* srcRow = flipRows
                ? base - static_cast<long>(y * rowStride)
                : base + static_cast<long>(y * rowStride);
            for (int x = 0; x < width; ++x) {
                unsigned char r = 0, g = 0, b = 0, a = 0;
                if (glyphIsLCD) {
                    r = srcRow[x * 3 + 0];
                    g = srcRow[x * 3 + 1];
                    b = srcRow[x * 3 + 2];
                    a = std::max({r, g, b});
                } else {
                    r = g = b = 255;
                    a = srcRow[x];
                }
                size_t dst = static_cast<size_t>(y * width + x) * 4;
                rgba[dst + 0] = r;
                rgba[dst + 1] = g;
                rgba[dst + 2] = b;
                rgba[dst + 3] = a;
            }
        }

        glBindTexture(GL_TEXTURE_2D, atlasTextureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0,
            atlasX, atlasY,
            width, height,
            GL_RGBA, GL_UNSIGNED_BYTE,
            rgba.data()
        );
    }

    FontData charData;
    charData.textureId = atlasTextureId;
    charData.face = face;
    charData.glyphIndex = glyphIndex;
    charData.width = width;
    charData.height = height;
    charData.bearingX = face->glyph->bitmap_left;
    charData.bearingY = face->glyph->bitmap_top;
    charData.advance = face->glyph->advance.x;
    if (codepoint == ' ') {
        // Same 0.26 em space floor as the preload path — see comment there.
        const int minSpace = static_cast<int>(0.26f * face->size->metrics.x_ppem * 64.0f);
        charData.advance = std::max(charData.advance, minSpace);
    }
    float invW = 1.0f / fontAtlasWidth_;
    float invH = 1.0f / fontAtlasHeight_;
    charData.u0 = atlasX * invW;
    charData.v0 = atlasY * invH;
    charData.u1 = (atlasX + width) * invW;
    charData.v1 = (atlasY + height) * invH;

    cache[codepoint] = charData;

    if (width > 0 && height > 0) {
        atlasX += width + padding;
        atlasRowHeight = std::max(atlasRowHeight, height);
    }

    return true;
}

bool NUIRendererGL::tryLoadFallbackGlyph(uint32_t codepoint, int atlasSize) {
    if (!ftCJKFace_) return false;

    if (atlasSize == atlasFontSize_) {
        return tryAddGlyphToAtlas(codepoint, ftCJKFace_, atlasSize,
            fontAtlasTextureId_, fontCache_,
            fontAtlasX_, fontAtlasY_, fontAtlasRowHeight_);
    } else if (atlasSize == atlasFontSizeMedium_) {
        return tryAddGlyphToAtlas(codepoint, ftCJKFace_, atlasSize,
            fontAtlasTextureIdMedium_, fontCacheMedium_,
            fontAtlasXMedium_, fontAtlasYMedium_, fontAtlasRowHeightMedium_);
    } else if (atlasSize == atlasFontSizeSmall_) {
        return tryAddGlyphToAtlas(codepoint, ftCJKFace_, atlasSize,
            fontAtlasTextureIdSmall_, fontCacheSmall_,
            fontAtlasXSmall_, fontAtlasYSmall_, fontAtlasRowHeightSmall_);
    } else if (atlasSize == atlasFontSizeXSmall_) {
        return tryAddGlyphToAtlas(codepoint, ftCJKFace_, atlasSize,
            fontAtlasTextureIdXSmall_, fontCacheXSmall_,
            fontAtlasXXSmall_, fontAtlasYXSmall_, fontAtlasRowHeightXSmall_);
    }
    return false;
}

void NUIRendererGL::renderTextWithFont(const std::string& text, const NUIPoint& position, float fontSize, const NUIColor& color) {
    AESTRA_ZONE("Text_Render");
    if (!fontInitialized_) {
        return; // Can't render without font
    }

    // Use centralized atlas selection
    AtlasInfo atlas = selectAtlas(fontSize);
    
    // Ensure we are using the font atlas texture
    if (currentTextureId_ != atlas.textureId) {
        flush();
        currentTextureId_ = atlas.textureId;
    }
    
    // Straight (non-premultiplied) alpha, matching every other blend site. See the
    // note above beginOffscreen() for why the alpha channel uses ONE rather than
    // SRC_ALPHA.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
    // Pre-allocate vertex buffer space (4 vertices per glyph, 6 indices per glyph)
    // This avoids repeated vector resizing for large text blocks
    size_t estimatedGlyphs = text.length();
    vertices_.reserve(vertices_.size() + estimatedGlyphs * 4);
    indices_.reserve(indices_.size() + estimatedGlyphs * 6);
    
    // fontSize is in logical pixels - no DPI scaling needed here
    // Scale factor from atlas size to requested font size
    float scale = fontSize / static_cast<float>(atlas.atlasSize);
    // The caller's colour is used as given. This used to be rewritten for any
    // text at or below 13.5 px — which is most of the UI — and the rewrite is
    // the larger half of V8-C9 finding F7:
    //
    //   alpha was scaled by 1.08 + 0.02, then floored at 0.94 / 0.88 / 0.84
    //   depending on a tier the renderer inferred from the alpha it was handed,
    //   with a further 0.84 floor below 11.75 px; then the colour itself was
    //   brightened toward a luminance target.
    //
    // The effect was that a label requested at alpha 0.25 rendered at 0.84, so
    // primary, secondary and tertiary text converged on the same weight. The
    // diagnostic's alpha ramp measured 0.891 of full ink at a requested 0.25 —
    // alpha-based hierarchy was not attenuated, it was very nearly absent.
    //
    // It cannot be rescued by retuning the constants, because the mechanism is
    // wrong in kind: the renderer was inferring a semantic tier from alpha and
    // then overriding the caller's choice for it. Tiers are the caller's to
    // decide and the theme's to define. If small text reads too dim after this,
    // the fix is the theme's alpha values — a decision someone can see and
    // argue with — not a floor buried in the glyph path.
    const NUIColor& glyphColor = color;

    // position.y is the BASELINE position (already adjusted by caller)
    // Pixel-align starting position for crisp text
    float x = std::round(position.x);
    float baseline = std::round(position.y);
    FT_UInt previousGlyph = 0;
    FT_Face previousFace = nullptr;
    // Calculate total bounds for debug

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    float startX = x;
    
    // UTF-8 decode loop
    size_t index = 0;
    while (index < text.length()) {
        size_t prevIndex = index;
        uint32_t codepoint = decodeUTF8(text, index);
        
        if (codepoint == 0) {
            // Prevent blank text: skip one byte and retry or continue
            // decodeUTF8 might have advanced index partially.
            // Ensure we advance at least by 1 to avoid infinite loop (decodeUTF8 handles this but let's be safe)
            if (index == prevIndex) index++;
            continue; 
        }

        auto it = atlas.cache->find(codepoint);
        if (it == atlas.cache->end()) {
            if (tryLoadFallbackGlyph(codepoint, atlas.atlasSize)) {
                it = atlas.cache->find(codepoint);
            }
        }
        if (it == atlas.cache->end()) {
            // Still missing — advance by replacement width so layout doesn't collapse
            x += fontSize * 0.6f;
            previousGlyph = 0;
            previousFace = nullptr;
            continue;
        }

        const FontData& ch = it->second;

        // Apply kerning for better spacing (cached to avoid repeated FT_Get_Kerning)
        if (previousFace == ch.face && previousGlyph != 0 && ch.glyphIndex != 0) {
            x += (getKerningUnits(ch.face, previousGlyph, ch.glyphIndex) / 64.0f) * scale;
        }
        previousGlyph = ch.glyphIndex;
        previousFace = ch.face;
        
        // Scale glyph metrics from the atlas to the target size
        float scaledBearingX = ch.bearingX * scale;
        float scaledBearingY = ch.bearingY * scale;
        float w = ch.width * scale;
        float h = ch.height * scale;
        
        // Snap the glyph quad to the pixel grid so the bitmap atlas samples
        // 1:1 — subpixel placement smears the tiny 9-12px labels into a jagged
        // blur. Only the pen start was snapped before, so every glyph after the
        // first drifted off-grid. All four edges are rounded from their true
        // positions (not width-rounded), so each glyph spans whole pixels
        // without accumulating drift; the pen advance keeps fractional width so
        // kerning/spacing stay accurate.
        const float gx = x + scaledBearingX;
        const float gy = baseline - scaledBearingY; // Top of glyph
        const float xpos = std::round(gx);
        const float ypos = std::round(gy);
        float xposR = std::round(gx + w);
        float yposB = std::round(gy + h);

        // Rounding all four edges collapses glyphs that are only about a pixel
        // thick: the hyphen-minus rasterises to 7x1 at the small atlases, so
        // round(gy) == round(gy + h) produced a zero-height quad and the glyph
        // silently disappeared — taking the sign off every negative number
        // whose baseline happened to land on the wrong side of .5. Keep at
        // least one pixel whenever the source glyph has real coverage.
        if (ch.width > 0 && xposR <= xpos) xposR = xpos + 1.0f;
        if (ch.height > 0 && yposB <= ypos) yposB = ypos + 1.0f;

        minX = std::min(minX, xpos);
        minY = std::min(minY, ypos);
        maxX = std::max(maxX, xposR);
        maxY = std::max(maxY, yposB);

        // Draw textured quad for character
        // Type 4 = Bitmap Text
        addVertex(xpos,  yposB, ch.u0, ch.v1, glyphColor, 0,0,0,0, 0,0,0, 4.0f);
        addVertex(xposR, yposB, ch.u1, ch.v1, glyphColor, 0,0,0,0, 0,0,0, 4.0f);
        addVertex(xposR, ypos,  ch.u1, ch.v0, glyphColor, 0,0,0,0, 0,0,0, 4.0f);
        addVertex(xpos,  ypos,  ch.u0, ch.v0, glyphColor, 0,0,0,0, 0,0,0, 4.0f);
        
        // Add indices for quad
        uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 0);
        indices_.push_back(base + 2);
        indices_.push_back(base + 3);
        
        // Advance cursor with sub-pixel precision to match measurement
        x += (ch.advance / 64.0f) * scale;
    }

    if (debugTextBounds_) {
        // Draw debug overlay (requires breaking batch)
        // Store current text state to restore? No, just flush.
        flush();
        
        // 1. Draw Text Bounds (Green)
        strokeRect(NUIRect(minX, minY, maxX - minX, maxY - minY), 1.0f, NUIColor(0, 1, 0, 1));
        
        // 2. Draw Baseline (Blue)
        drawLine(NUIPoint(minX, baseline), NUIPoint(maxX, baseline), 1.0f, NUIColor(0, 0, 1, 1));
        
        // 3. Draw Clip Rect (Red)
        if (scissorEnabled_) {
            GLint scissor[4];
            glGetIntegerv(GL_SCISSOR_BOX, scissor);
            // Convert GL bottom-up scissor back to UI coordinates top-down
            // Window height needed... using height_ member
            float sx = (float)scissor[0];
            float sy = (float)(height_ - scissor[1] - scissor[3]);
            float sw = (float)scissor[2];
            float sh = (float)scissor[3];
            strokeRect(NUIRect(sx, sy, sw, sh), 1.0f, NUIColor(1, 0, 0, 1));
        }
        
        // Restore text state for potential next text calls
        // Actually, the caller will set state if needed, but we should make sure we don't leave it in primitive mode if caller expects text batching.
        // It's safer to just let the next draw setup its state.
        
        // However, we need to ensure we don't accidentally leave a bound texture if we used strokeRect which clears it.
    }

    // Restore blend func for non-text geometry
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // Already default
}

// ============================================================================
// Texture/Image Drawing (Placeholder)
// ============================================================================

void NUIRendererGL::drawTexture(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    const TextureData& td = it->second;

    AESTRA_ZONE("Texture_Draw");

    ensureBasicPrimitive();

    // Check if we need to switch textures (batch breaking)
    if (currentTextureId_ != td.glId) {
        flush(); // Draw pending batch with old texture
        currentTextureId_ = td.glId; // Switch to new texture
    }

    // Compute texture coordinates (sourceRect is in pixels)
    float tx0 = 0.0f, ty0 = 0.0f, tx1 = 1.0f, ty1 = 1.0f;
    if (td.width > 0 && td.height > 0) {
        float srcX0 = sourceRect.x;
        float srcY0 = sourceRect.y;
        float srcX1 = sourceRect.x + sourceRect.width;
        float srcY1 = sourceRect.y + sourceRect.height;

        const float invWidth = 1.0f / static_cast<float>(td.width);
        const float invHeight = 1.0f / static_cast<float>(td.height);

        tx0 = srcX0 * invWidth;
        tx1 = srcX1 * invWidth;
        ty0 = srcY0 * invHeight;
        ty1 = srcY1 * invHeight;

        if (sourceRect.width < 0.0f) std::swap(tx0, tx1);
        if (sourceRect.height < 0.0f) std::swap(ty0, ty1);
    }

    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(destRect.x, destRect.y, tx0, ty0, white);
    addVertex(destRect.right(), destRect.y, tx1, ty0, white);
    addVertex(destRect.right(), destRect.bottom(), tx1, ty1, white);
    addVertex(destRect.x, destRect.bottom(), tx0, ty1, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::drawTexture(const NUIRect& bounds, const unsigned char* rgba, 
                                int width, int height) {
    // Validate input parameters
    if (!rgba || width <= 0 || height <= 0) {
        AESTRA_LOG_ERROR("OpenGL: Invalid texture data (rgba=" + std::string(rgba ? "valid" : "null") + ", width=" + std::to_string(width) + ", height=" + std::to_string(height) + ")");
        return;
    }

    AESTRA_ZONE("Texture_Upload");

    // Flush any pending batched geometry before texture rendering
    flush();

    // Create a temporary texture, upload pixels, draw and delete (legacy one-shot path)
    GLuint texture;
    glGenTextures(1, &texture);
    if (texture == 0) {
        AESTRA_LOG_ERROR("OpenGL: Failed to generate texture");
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Draw textured quad using the uploaded texture
    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(bounds.x, bounds.y, 0.0f, 0.0f, white);
    addVertex(bounds.right(), bounds.y, 1.0f, 0.0f, white);
    addVertex(bounds.right(), bounds.bottom(), 1.0f, 1.0f, white);
    addVertex(bounds.x, bounds.bottom(), 0.0f, 1.0f, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);

    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    glUniform1i(primitiveShader_.primitiveTypeLoc, 0);
    glUniform2f(primitiveShader_.textTexelSizeLoc, 0.0f, 0.0f);
    glUniform1f(primitiveShader_.textSharpenLoc, 0.0f);
    glUniform1f(primitiveShader_.textGammaLoc, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(primitiveShader_.textureLoc, 0);
    glUniform1i(primitiveShader_.useTextureLoc, 1);

    submittedQuadCount_ += indices_.size() / 6;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;
    if (Aestra::Profiler::getInstance().isEnabled()) {
        Aestra::Profiler::getInstance().recordDrawCall();
        Aestra::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
    }

    vertices_.clear();
    indices_.clear();

    glDeleteTextures(1, &texture);
}

void NUIRendererGL::drawTextureFlippedV(uint32_t textureId, const NUIRect& destRect, const NUIRect& sourceRect) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    const TextureData& td = it->second;

    AESTRA_ZONE("Texture_Draw_FlippedV");

    // Flush batch to ensure correct ordering
    flush();

    // Compute normalized texture coordinates, with V flipped
    float tx0 = 0.0f, ty0 = 1.0f, tx1 = 1.0f, ty1 = 0.0f; // note V flipped
    if (td.width > 0 && td.height > 0) {
        float srcX0 = sourceRect.x;
        float srcY0 = sourceRect.y;
        float srcX1 = sourceRect.x + sourceRect.width;
        float srcY1 = sourceRect.y + sourceRect.height;

        const float invWidth = 1.0f / static_cast<float>(td.width);
        const float invHeight = 1.0f / static_cast<float>(td.height);

        tx0 = srcX0 * invWidth;
        tx1 = srcX1 * invWidth;
        // Flip V: swap mapping of top/bottom
        ty0 = srcY1 * invHeight; // top
        ty1 = srcY0 * invHeight; // bottom
    }

    NUIColor white(1.0f, 1.0f, 1.0f, 1.0f);
    addVertex(destRect.x, destRect.y, tx0, ty0, white);
    addVertex(destRect.right(), destRect.y, tx1, ty0, white);
    addVertex(destRect.right(), destRect.bottom(), tx1, ty1, white);
    addVertex(destRect.x, destRect.bottom(), tx0, ty1, white);

    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);

    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    glUniform1i(primitiveShader_.primitiveTypeLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, td.glId);
    glUniform1i(primitiveShader_.textureLoc, 0);
    glUniform1i(primitiveShader_.useTextureLoc, 1);

    submittedQuadCount_ += indices_.size() / 6;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;
    if (Aestra::Profiler::getInstance().isEnabled()) {
        Aestra::Profiler::getInstance().recordDrawCall();
        Aestra::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
    }

    vertices_.clear();
    indices_.clear();
}

// ============================================================================
// Texture/Image Drawing (Stub implementations)
// ============================================================================

uint32_t NUIRendererGL::loadTexture(const std::string& filepath) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::array<std::string, 5> candidates = {
        filepath,
        "../" + filepath,
        "../../" + filepath,
        "../../../" + filepath,
        "../../../../" + filepath,
    };

    for (const auto& candidate : candidates) {
        unsigned char* rgba = stbi_load(candidate.c_str(), &width, &height, &channels, 4);
        if (!rgba) continue;

        const uint32_t textureId = createTexture(rgba, width, height);
        stbi_image_free(rgba);
        return textureId;
    }

    return 0;
}

uint32_t NUIRendererGL::createTexture(const uint8_t* data, int width, int height) {
    if (width <= 0 || height <= 0) return 0;

    AESTRA_ZONE("Texture_Create");

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return 0;

    glBindTexture(GL_TEXTURE_2D, tex);
    // Allocate storage (data can be null for empty texture)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    uint32_t id = nextTextureId_++;
    textures_[id] = TextureData{ static_cast<uint32_t>(tex), width, height };
    return id;
}

void NUIRendererGL::deleteTexture(uint32_t textureId) {
    if (textureId == 0) return;
    auto it = textures_.find(textureId);
    if (it == textures_.end()) return;
    uint32_t glId = it->second.glId;
    if (glId != 0) glDeleteTextures(1, &glId);
    textures_.erase(it);
}

uint32_t NUIRendererGL::renderToTextureBegin(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    
    // Flush current batch to screen before switching target
    flush();

    // Create FBO if needed
    if (fbo_ == 0) {
        glGenFramebuffers(1, &fbo_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Create texture
    uint32_t texId = createTexture(nullptr, width, height);
    if (texId == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    
    // Attach texture to FBO
    uint32_t glTexId = getGLTextureId(texId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glTexId, 0);

    // Check status
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        AESTRA_LOG_ERROR("FBO incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    // Save state
    glGetIntegerv(GL_VIEWPORT, fboPrevViewport_);
    widthBackup_ = width_;
    heightBackup_ = height_;

    // Set up FBO state
    glViewport(0, 0, width, height);
    
    // Update projection for FBO (Ortho 0..width, 0..height)
    width_ = width;
    height_ = height;
    updateProjectionMatrix();

    // Clear FBO (transparent black)
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    renderingToTexture_ = true;
    lastRenderTextureId_ = texId;
    fboWidth_ = width;
    fboHeight_ = height;

    return texId;
}

uint32_t NUIRendererGL::renderToTextureEnd() {
    if (!renderingToTexture_) return 0;

    // Flush batch to FBO
    flush();

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(fboPrevViewport_[0], fboPrevViewport_[1], fboPrevViewport_[2], fboPrevViewport_[3]);
    
    width_ = widthBackup_;
    height_ = heightBackup_;
    updateProjectionMatrix();

    renderingToTexture_ = false;
    return lastRenderTextureId_;
}



uint32_t NUIRendererGL::getGLTextureId(uint32_t textureId) const {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return 0;
    }
    return it->second.glId;
}

// NOTE on blending: all default blend sites use glBlendFuncSeparate with
// (ONE, ONE_MINUS_SRC_ALPHA) for the alpha channel. The classic
// (SRC_ALPHA, ...) alpha blend squares coverage: text drawn at alpha a onto
// an opaque cache texel left it at 1-a+a^2 < 1, so composites re-blended
// glyphs against the surface behind the panel — invisible over dark themes,
// a gray wash over light ones (and sub-1.0 backbuffer alpha for DWM).
void NUIRendererGL::beginOffscreen(int width, int height) {
    // No colour-space switch here any more. The cache renders into a plain
    // RGBA8 target and the screen composites in the same gamma space, so the
    // cached and direct paths are now the same pipeline — which is the point.
    // This function used to flip uOutputLinear off on the way in and back on at
    // the way out, and that asymmetry was the whole of F3.

    // Backup current size and projection
    widthBackup_ = width_;
    heightBackup_ = height_;
    std::memcpy(projectionBackup_, projectionMatrix_, sizeof(projectionMatrix_));

    // Switch to offscreen size and update projection
    width_ = width;
    height_ = height;
    updateProjectionMatrix();
    // Set viewport to match offscreen target so draw calls map correctly
    glViewport(0, 0, width, height);
}

void NUIRendererGL::endOffscreen() {
    // Flush offscreen geometry before restoring the screen projection.
    flush();

    // Restore original projection and size
    width_ = widthBackup_;
    height_ = heightBackup_;
    // Restore backup matrix explicitly (avoid precision drift)
    std::memcpy(projectionMatrix_, projectionBackup_, sizeof(projectionBackup_));
    // Restore viewport to the original backbuffer size
    glViewport(0, 0, width_, height_);
}

// ============================================================================
// Batching
// ============================================================================

void NUIRendererGL::beginBatch() {
    batching_ = true;
}

void NUIRendererGL::endBatch() {
    batching_ = false;
    flush();
}

void NUIRendererGL::flush() {
    AESTRA_ZONE("Renderer_Flush");
    AESTRA_ZONE("Renderer_Flush");
    if (vertices_.empty()) {
        return;
    }
    
    // Upload vertex data
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);
    
    // Use shader
    glUseProgram(primitiveShader_.id);
    glUniformMatrix4fv(primitiveShader_.projectionLoc, 1, GL_FALSE, projectionMatrix_);
    glUniform2f(primitiveShader_.textTexelSizeLoc, 0.0f, 0.0f);
    glUniform1f(primitiveShader_.textSharpenLoc, 0.0f);
    glUniform1f(primitiveShader_.textGammaLoc, 1.0f);
    // Note: opacity is already in vertex colors
    glUniform1i(primitiveShader_.primitiveTypeLoc, currentPrimitiveType_);
    // Default to no texturing; enable below if a texture is bound
    glUniform1i(primitiveShader_.useTextureLoc, 0);
    
    if (currentPrimitiveType_ == 1) {
        // Uniforms moved to attributes for batching
    } else if (currentPrimitiveType_ == 2) {
        // Text
        glUniform1f(primitiveShader_.smoothnessLoc, currentSmoothness_);
    } else if (currentPrimitiveType_ == 3) {
        // Stroked rounded rect (Uniforms moved to attributes)
    } else {
        glUniform1i(primitiveShader_.useTextureLoc, 0);
    }
    
    // Bind current texture if needed
    if (currentTextureId_ != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTextureId_);
        glUniform1i(primitiveShader_.textureLoc, 0);
        glUniform1i(primitiveShader_.useTextureLoc, 1);
        const bool isFontAtlasTexture = (currentTextureId_ == fontAtlasTextureId_
            || currentTextureId_ == fontAtlasTextureIdMedium_
            || currentTextureId_ == fontAtlasTextureIdSmall_
            || currentTextureId_ == fontAtlasTextureIdXSmall_);
        if (isFontAtlasTexture) {
            glUniform2f(primitiveShader_.textTexelSizeLoc, 1.0f / fontAtlasWidth_, 1.0f / fontAtlasHeight_);
            // The x-small/small atlases are supersampled down the hardest, so
            // their stems average to a washed mid-grey. Fullness comes from the
            // coverage lift (gamma < 1 thickens strokes uniformly); the unsharp
            // mask stays gentle because a strong one undershoots and erodes thin
            // features — the 'e' crossbar thins to a 'c' and edges go ragged.
            // Derived from this tier's actual supersampling ratio, not from a
            // list of tier names. The old test named XSmall and Small, so Small
            // kept the heavy compensation long after its ratio (16/17, i.e.
            // magnification) stopped justifying it — that mis-classification
            // was half of F5.
            const bool tinyAtlas =
                (currentTextureId_ == fontAtlasTextureIdXSmall_
                     ? isHeavilySupersampled(atlasFontSizeXSmall_, kTierMaxXSmall)
                 : currentTextureId_ == fontAtlasTextureIdSmall_
                     ? isHeavilySupersampled(atlasFontSizeSmall_, kTierMaxSmall)
                 : currentTextureId_ == fontAtlasTextureIdMedium_
                     ? isHeavilySupersampled(atlasFontSizeMedium_, kTierMaxMedium)
                     : false);
            glUniform1f(primitiveShader_.textSharpenLoc, resolveTextSharpen(tinyAtlas));
            glUniform1f(primitiveShader_.textGammaLoc, resolveTextGamma(tinyAtlas, textContrast_));
        }
    }
    
    // Draw
    submittedQuadCount_ += indices_.size() / 6;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    drawCallCount_++;  // Track draw call
    // Record with global profiler
    {
        if (Aestra::Profiler::getInstance().isEnabled()) {
            Aestra::Profiler::getInstance().recordDrawCall();
            Aestra::Profiler::getInstance().recordTriangles(static_cast<uint32_t>(indices_.size() / 3));
        }
    }

    // Clear for next batch
    vertices_.clear();
    indices_.clear();
    
    // Reset texture state after flush? No, keep it until it changes or frame ends.
    // Actually, if we just flushed, we might want to reset if the next call is a non-textured primitive.
    // But for now, we rely on the caller setting currentTextureId_ = 0 for non-textured.
    // Let's enforce that: if we flushed, we don't necessarily change the state, 
    // but addVertex/addQuad should probably set currentTextureId_ = 0 if they don't use textures.
    // For now, let's just clear the buffers.
}

void NUIRendererGL::ensureBasicPrimitive() {
    // Switch back to flat primitives when the previous draw used the rounded-rect path
    if (currentPrimitiveType_ != 0) {
        flush();
        currentPrimitiveType_ = 0;
        currentRadius_ = 0.0f;
        currentBlur_ = 0.0f;
        currentSize_ = {0.0f, 0.0f};
        currentQuadSize_ = {0.0f, 0.0f};
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

bool NUIRendererGL::initializeGL() {
    // Load OpenGL functions with GLAD
    if (!gladLoadGL()) {
        // Failed to load OpenGL functions
        return false;
    }
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    
    // OpenGL context should already be created by platform layer
    return true;
}

bool NUIRendererGL::loadShaders() {
    uint32_t vertShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    uint32_t fragShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    
    if (!vertShader || !fragShader) {
        return false;
    }
    
    primitiveShader_.id = linkProgram(vertShader, fragShader);
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    if (!primitiveShader_.id) {
        return false;
    }
    
    // Get uniform locations
    primitiveShader_.projectionLoc = glGetUniformLocation(primitiveShader_.id, "uProjection");
    // Note: opacity is baked into vertex colors, no uniform needed
    primitiveShader_.primitiveTypeLoc = glGetUniformLocation(primitiveShader_.id, "uPrimitiveType");
    primitiveShader_.textureLoc = glGetUniformLocation(primitiveShader_.id, "uTexture");
    primitiveShader_.useTextureLoc = glGetUniformLocation(primitiveShader_.id, "uUseTexture");
    primitiveShader_.smoothnessLoc = glGetUniformLocation(primitiveShader_.id, "uSmoothness");
    primitiveShader_.textTexelSizeLoc = glGetUniformLocation(primitiveShader_.id, "uTextTexelSize");
    primitiveShader_.textSharpenLoc = glGetUniformLocation(primitiveShader_.id, "uTextSharpen");
    primitiveShader_.textGammaLoc = glGetUniformLocation(primitiveShader_.id, "uTextGamma");

    
    return true;
}

void NUIRendererGL::createBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    
    glBindVertexArray(vao_);
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    
    // TexCoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    
    // Color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

    // Rect Size
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, rw));

    // Quad Size
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, qw));

    // Radius
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, radius));

    // Blur
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, blur));

    // Stroke Width
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, strokeWidth));

    // Primitive Type
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, primitiveType));
    
    glBindVertexArray(0);
}

uint32_t NUIRendererGL::compileShader(const char* source, uint32_t type) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        // Error logging
        return 0;
    }
    
    return shader;
}

uint32_t NUIRendererGL::linkProgram(uint32_t vertexShader, uint32_t fragmentShader) {
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        // Error logging
        return 0;
    }
    
    return program;
}

void NUIRendererGL::addVertex(float x, float y, float u, float v, const NUIColor& color,
                             float rw, float rh, float qw, float qh, 
                             float radius, float blur, float strokeWidth, float type) {
    applyTransform(x, y);
    
    Vertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.u = u;
    vertex.v = v;
    vertex.r = color.r;
    vertex.g = color.g;
    vertex.b = color.b;
    vertex.a = color.a * globalOpacity_;
    vertex.rw = rw;
    vertex.rh = rh;
    vertex.qw = qw;
    vertex.qh = qh;
    vertex.radius = radius;
    vertex.blur = blur;
    vertex.strokeWidth = strokeWidth;
    vertex.primitiveType = type;
    
    vertices_.push_back(vertex);
}

void NUIRendererGL::addQuad(const NUIRect& rect, const NUIColor& color,
                             float rw, float rh, float qw, float qh, 
                             float radius, float blur, float strokeWidth, float type) {
    // Optimization: If Primitive Type is Rect (1) or Stroke (3), we IGNORE texture state!
    // So we don't need to flush if currentTextureId_ != 0.
    // Only flush if we are Type 0, 2, 4 AND texture mismatches.
    bool textureMatters = (type == 0.0f || type == 2.0f || type == 4.0f);
    
    if (textureMatters && currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }

    addVertex(rect.x, rect.y, 0.0f, 0.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.right(), rect.y, 1.0f, 0.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.right(), rect.bottom(), 1.0f, 1.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    addVertex(rect.x, rect.bottom(), 0.0f, 1.0f, color, rw, rh, qw, qh, radius, blur, strokeWidth, type);
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::applyTransform(float& x, float& y) {
    if (transformStack_.empty()) {
        return;
    }

    // One composition, shared with setClipRect(). These used to be two separate
    // walks of the same stack in opposite operation orders, so clipped geometry
    // and its clip rectangle landed in different places for any scale != 1.
    composeTransformStack(transformStack_).applyToPoint(x, y);
}

void NUIRendererGL::updateProjectionMatrix() {
    // Orthographic projection matrix
    float left = 0.0f;
    float right = static_cast<float>(width_);
    float bottom = static_cast<float>(height_);
    float top = 0.0f;
    float nearPlane = -1.0f;
    float farPlane = 1.0f;
    
    std::memset(projectionMatrix_, 0, sizeof(projectionMatrix_));
    
    projectionMatrix_[0] = 2.0f / (right - left);
    projectionMatrix_[5] = 2.0f / (top - bottom);
    projectionMatrix_[10] = -2.0f / (farPlane - nearPlane);
    projectionMatrix_[12] = -(right + left) / (right - left);
    projectionMatrix_[13] = -(top + bottom) / (top - bottom);
    projectionMatrix_[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    projectionMatrix_[15] = 1.0f;
}

// ============================================================================
// Performance Optimizations
// ============================================================================

void NUIRendererGL::invalidateCache(uint64_t widgetId) {
    renderCache_.invalidate(widgetId);
}

bool NUIRendererGL::renderCachedOrUpdate(uint64_t widgetId, const NUIRect& destRect,
                                         const std::function<void()>& renderCallback) {
    // If caching is disabled, just render directly
    if (!renderCache_.isEnabled()) {
        renderCallback();
        return false;
    }

    // Get or create cache entry for this widget
    // Use destination rect size
    NUISize size(destRect.width, destRect.height);
    CachedRenderData* cache = renderCache_.getOrCreateCache(widgetId, size);
    
    if (!cache) {
        renderCallback();
        return false;
    }

    // Delegate to render cache manager to render from cache or update it
    renderCache_.renderCachedOrUpdate(cache, destRect, renderCallback);
    return true;
}

void NUIRendererGL::setDirtyRegionTrackingEnabled(bool enabled) {
    dirtyRegionManager_.setEnabled(enabled);
}

void NUIRendererGL::setCachingEnabled(bool enabled) {
    renderCache_.setEnabled(enabled);
}

void NUIRendererGL::getOptimizationStats(size_t& batchedQuads, size_t& dirtyRegions, 
                                        size_t& cachedWidgets, size_t& cacheMemoryBytes) {
    batchedQuads = submittedQuadCount_ + indices_.size() / 6;
    dirtyRegions = dirtyRegionManager_.getDirtyRegionCount();
    cachedWidgets = renderCache_.getCacheCount();
    cacheMemoryBytes = renderCache_.getMemoryUsage();
}

} // namespace AestraUI
