// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraVerbEditor.h"
#include "AestraFile.h"
#include "AestraJSON.h"
#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace AestraUI {

namespace {
constexpr uint32_t kDecay = 0;
constexpr uint32_t kDamping = 1;
constexpr uint32_t kPredelay = 2;
constexpr uint32_t kWidth = 3;
constexpr uint32_t kMix = 4;
constexpr uint32_t kBypass = 5;
constexpr uint32_t kSize = 6;
constexpr uint32_t kDiffusion = 7;
constexpr uint32_t kModRate = 8;
constexpr uint32_t kModDepth = 9;
constexpr uint32_t kMode = 10;
constexpr uint32_t kLowCut = 11;
constexpr uint32_t kHighCut = 12;
constexpr uint32_t kFreeze = 13;
constexpr uint32_t kAttack = 14;
constexpr uint32_t kShape = 15;
constexpr uint32_t kPredelaySync = 16;
constexpr uint32_t kModCharacter = 17;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr int kModeCount = 9;
constexpr float kPresetListTopOffset = 112.0f;
constexpr float kPresetCardHeight = 58.0f;
constexpr float kPresetCardGap = 6.0f;
constexpr float kPresetListBottomPadding = 24.0f;
constexpr float kPresetArtworkSize = 44.0f;
constexpr float kPresetArtworkPixels = 384.0f;

NUIColor verbSurfaceBg() { return NUIColor(0.044f, 0.044f, 0.044f, 0.985f); }
NUIColor verbInsetBg() { return NUIColor(0.025f, 0.025f, 0.025f, 0.965f); }
NUIColor verbGold() { return NUIColor(0.88f, 0.63f, 0.13f, 1.0f); }
NUIColor verbAccent() { return NUIColor(0.498f, 0.353f, 0.941f, 1.0f); }
float presetColumnWidth(float editorWidth) { return std::clamp(editorWidth * 0.235f, 168.0f, 210.0f); }
float editorContentX(const NUIRect& b) { return b.x + 18.0f + presetColumnWidth(b.width) + 18.0f; }
float rightColWidth(float editorWidth) { return std::clamp(editorWidth * 0.29f, 196.0f, 238.0f); }

// Top-left Y that optically centres a single line of text (by cap height) on
// centreY. drawText() offsets the passed Y by the font ascent to reach the
// baseline, so we place the baseline at centreY + capHeight/2 then back the
// ascent out. This tracks adjacent circles/knobs far better than calculateTextY,
// which centres the whole line box (ascent + descent + gap) and leaves label
// text sitting low against a geometric centre.
float opticalTextY(NUIRenderer& renderer, float centreY, float fontSize) {
    const auto metrics = renderer.getFontMetrics(fontSize);
    const float capHeight = fontSize * 0.70f;
    return centreY + capHeight * 0.5f - metrics.ascent;
}

std::string fitVerbText(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return {};
    if (renderer.measureText(text, fontSize).width <= maxWidth) return text;
    constexpr const char* ellipsis = "...";
    const float ellipsisWidth = renderer.measureText(ellipsis, fontSize).width;
    if (ellipsisWidth >= maxWidth) return ellipsis;
    std::string fitted = text;
    while (!fitted.empty() && renderer.measureText(fitted, fontSize).width + ellipsisWidth > maxWidth) {
        fitted.pop_back();
    }
    return fitted + ellipsis;
}

void drawVerbArc(NUIRenderer& renderer,
                 NUIPoint center,
                 float radius,
                 float startAngle,
                 float endAngle,
                 float thickness,
                 NUIColor color) {
    if (endAngle < startAngle) std::swap(startAngle, endAngle);
    if (endAngle - startAngle <= 0.001f) return;
    std::array<NUIPoint, 49> points{};
    const float pointsDivisor = points.size() > 1 ? static_cast<float>(points.size() - 1) : 1.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / pointsDivisor;
        const float angle = startAngle + (endAngle - startAngle) * t;
        points[i] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
}

float protectedLeftDockEdge(const NUIComponent* root, const NUIComponent* self, const NUIRect& parentBounds) {
    if (!root || parentBounds.width <= 0.0f || parentBounds.height <= 0.0f) return parentBounds.x;
    float edge = parentBounds.x;
    const auto scan = [&](const auto& recurse, const NUIComponent* node) -> void {
        if (!node || node == self || !node->isVisible()) return;
        const auto b = node->getBounds();
        const bool leftDocked = b.x <= parentBounds.x + 8.0f;
        const bool railSized = b.width >= 96.0f && b.width <= std::min(620.0f, parentBounds.width * 0.46f);
        const bool verticallySignificant = b.height >= parentBounds.height * 0.40f && b.bottom() >= parentBounds.y + 120.0f;
        if (leftDocked && railSized && verticallySignificant) {
            edge = std::max(edge, b.right());
        }
        for (const auto& child : node->getChildren()) {
            recurse(recurse, child.get());
        }
    };
    scan(scan, root);
    return edge;
}
} // anonymous namespace

constexpr int AestraVerbEditor::categoryForMode(int mode) {
    switch (mode) {
    case 0: case 4: case 6: return 0;
    case 1: case 3: case 5: return 1;
    case 2: case 7: case 8: return 2;
    default: return 3;
    }
}

constexpr int AestraVerbEditor::modesInCategory(int category, int* outModes) {
    switch (category) {
    case 0: outModes[0] = 0; outModes[1] = 4; outModes[2] = 6; return 3;
    case 1: outModes[0] = 1; outModes[1] = 3; outModes[2] = 5; return 3;
    case 2: outModes[0] = 2; outModes[1] = 7; outModes[2] = 8; return 3;
    default: return 0;
    }
}

AestraVerbEditor::AestraVerbEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraVerbEditor");
    setPanelTitle("Aestra Verb");
    setBadgeText("Reverb");
    setSize(kWinW, kWinH);
    m_categoryPills = {{
        {"Room", 0, {}, false, true},
        {"Hall", 1, {}, false, true},
        {"Plate", 2, {}, false, true},
        {"Special", 3, {}, false, false}
    }};
    m_presets = {{
        // Room
        {"Vitruvian Space", "Intimate room for voice and acoustic guitar", 0, 0.35f, 0.28f, 0.54f, 0.58f, 0.24f, 0.05f, 0.52f, 0.30f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/vitruvian-space.png", 0, false, {}, false},
        {"Vocal Booth", "Tight, controlled space for dry vocal tracking", 0, 0.22f, 0.18f, 0.62f, 0.50f, 0.18f, 0.03f, 0.44f, 0.24f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/vocal-booth.png", 0, false, {}, false},
        {"Studio Room", "Natural small room with early reflections", 0, 0.30f, 0.32f, 0.58f, 0.55f, 0.22f, 0.04f, 0.50f, 0.28f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/studio-room.png", 0, false, {}, false},
        {"Dense Studio", "Thick small room, great for drums", 0, 0.38f, 0.42f, 0.70f, 0.60f, 0.28f, 0.06f, 0.56f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/dense-studio.png", 0, false, {}, false},
        {"Bright Room", "Small bright room with crisp reflections", 0, 0.26f, 0.22f, 0.50f, 0.62f, 0.30f, 0.05f, 0.48f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/bright-room.png", 0, false, {}, false},
        // Hall
        {"Grand Hall", "Massive hall with long, lush reverb tail", 1, 0.76f, 0.58f, 0.51f, 0.86f, 0.42f, 0.09f, 0.74f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/grand-hall.png", 0, false, {}, false},
        {"Concert Hall", "Orchestral hall with rich spatial depth", 1, 0.68f, 0.52f, 0.55f, 0.80f, 0.38f, 0.08f, 0.70f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/concert-hall.png", 0, false, {}, false},
        {"Chapel Hall", "Stone chapel with bright, reflective surfaces", 1, 0.72f, 0.62f, 0.48f, 0.90f, 0.44f, 0.10f, 0.76f, 0.40f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/chapel-hall.png", 0, false, {}, false},
        {"Medium Hall", "Well-balanced hall for most sources", 1, 0.58f, 0.46f, 0.52f, 0.78f, 0.36f, 0.07f, 0.66f, 0.30f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/medium-hall.png", 0, false, {}, false},
        {"Soft Hall", "Gentle hall with smooth, dark tail", 1, 0.62f, 0.50f, 0.60f, 0.82f, 0.34f, 0.06f, 0.68f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/soft-hall.png", 0, false, {}, false},
        // Plate
        {"Plate Forge", "Bright metallic plate reverb", 2, 0.54f, 0.48f, 0.46f, 0.82f, 0.34f, 0.10f, 0.78f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/plate-forge.png", 0, false, {}, false},
        {"Classic Plate", "EMT-style smooth plate reverb", 2, 0.50f, 0.44f, 0.44f, 0.78f, 0.32f, 0.09f, 0.74f, 0.30f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/classic-plate.png", 0, false, {}, false},
        {"Dense Plate", "Thick, dense plate for vocals and keys", 2, 0.58f, 0.52f, 0.50f, 0.84f, 0.36f, 0.10f, 0.80f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/dense-plate.png", 0, false, {}, false},
        {"Bright Plate", "Bright, shimmery plate reverb", 2, 0.46f, 0.40f, 0.42f, 0.86f, 0.38f, 0.11f, 0.82f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/bright-plate.png", 0, false, {}, false},
        {"Dark Plate", "Dark, moody plate with long decay", 2, 0.62f, 0.56f, 0.48f, 0.80f, 0.30f, 0.08f, 0.76f, 0.30f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/dark-plate.png", 0, false, {}, false},
        // Cathedral
        {"Celestial Room", "Massive cathedral with ethereal tail", 3, 0.92f, 0.76f, 0.42f, 0.94f, 0.28f, 0.12f, 0.88f, 0.40f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/celestial-room.png", 0, false, {}, false},
        {"Stone Cathedral", "Vast stone cathedral with deep reverb", 3, 0.88f, 0.72f, 0.40f, 0.92f, 0.30f, 0.10f, 0.86f, 0.38f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/stone-cathedral.png", 0, false, {}, false},
        {"Epic Cathedral", "Enormous cathedral for cinematic textures", 3, 0.96f, 0.82f, 0.38f, 0.96f, 0.26f, 0.12f, 0.90f, 0.42f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/epic-cathedral.png", 0, false, {}, false},
        {"Sacred Space", "Holy space with long, shimmering decay", 3, 0.84f, 0.68f, 0.44f, 0.90f, 0.32f, 0.11f, 0.84f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/sacred-space.png", 0, false, {}, false},
        {"Cathedral Warm", "Warm cathedral with gentle low-end", 3, 0.80f, 0.64f, 0.46f, 0.88f, 0.34f, 0.09f, 0.82f, 0.38f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/cathedral-warm.png", 0, false, {}, false},
        // Chamber
        {"Golden Chamber", "Rich, warm chamber with golden reflections", 4, 0.60f, 0.48f, 0.50f, 0.80f, 0.36f, 0.09f, 0.72f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/golden-chamber.png", 0, false, {}, false},
        {"Tape Chamber", "Vintage chamber with tape saturation character", 4, 0.55f, 0.44f, 0.52f, 0.76f, 0.34f, 0.08f, 0.68f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/tape-chamber.png", 0, false, {}, false},
        {"Large Chamber", "Big chamber with natural room ambience", 4, 0.65f, 0.54f, 0.48f, 0.82f, 0.38f, 0.10f, 0.74f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/large-chamber.png", 0, false, {}, false},
        {"Tight Chamber", "Small, tight chamber for subtle space", 4, 0.40f, 0.32f, 0.56f, 0.72f, 0.30f, 0.07f, 0.62f, 0.28f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/tight-chamber.png", 0, false, {}, false},
        {"Bright Chamber", "Bright chamber with clear early reflections", 4, 0.52f, 0.42f, 0.46f, 0.84f, 0.40f, 0.10f, 0.76f, 0.38f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/bright-chamber.png", 0, false, {}, false},
        // Bright Hall
        {"Bright Concert", "Bright concert hall with shimmering tail", 5, 0.68f, 0.50f, 0.44f, 0.88f, 0.44f, 0.10f, 0.80f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/bright-concert.png", 0, false, {}, false},
        {"Crystal Hall", "Sparkling bright hall with crystalline reflections", 5, 0.72f, 0.54f, 0.42f, 0.90f, 0.46f, 0.11f, 0.82f, 0.38f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/crystal-hall.png", 0, false, {}, false},
        {"Glass Hall", "Transparent, glassy hall reverb", 5, 0.64f, 0.46f, 0.46f, 0.86f, 0.42f, 0.09f, 0.78f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/glass-hall.png", 0, false, {}, false},
        {"Silk Hall", "Smooth, silky bright hall", 5, 0.60f, 0.44f, 0.50f, 0.84f, 0.40f, 0.08f, 0.76f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/silk-hall.png", 0, false, {}, false},
        {"Radiant Hall", "Radiant, luminous hall with wide stereo", 5, 0.70f, 0.52f, 0.40f, 0.92f, 0.48f, 0.12f, 0.84f, 0.40f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/radiant-hall.png", 0, false, {}, false},
        // Ambience
        {"Soft Air", "Gentle ambient wash for padding and atmosphere", 6, 0.18f, 0.12f, 0.38f, 0.40f, 0.14f, 0.02f, 0.36f, 0.16f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/soft-air.png", 0, false, {}, false},
        {"Subtle Space", "Minimal ambience for subtle depth", 6, 0.14f, 0.10f, 0.42f, 0.38f, 0.12f, 0.02f, 0.34f, 0.14f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/subtle-space.png", 0, false, {}, false},
        {"Room Tone", "Natural room tone for realistic spaces", 6, 0.20f, 0.14f, 0.40f, 0.42f, 0.16f, 0.03f, 0.38f, 0.18f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/room-tone.png", 0, false, {}, false},
        {"Air Brush", "Light ambient brush for texture", 6, 0.16f, 0.11f, 0.36f, 0.36f, 0.10f, 0.01f, 0.32f, 0.12f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/air-brush.png", 0, false, {}, false},
        {"Breath Space", "Very short, breath-like ambience", 6, 0.12f, 0.08f, 0.44f, 0.34f, 0.08f, 0.01f, 0.30f, 0.10f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/breath-space.png", 0, false, {}, false},
        // Scoring
        {"Cinematic Space", "Vast cinematic space for film scoring", 7, 0.82f, 0.66f, 0.44f, 0.90f, 0.32f, 0.11f, 0.84f, 0.38f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/cinematic-space.png", 0, false, {}, false},
        {"Film Score", "Professional scoring stage ambience", 7, 0.78f, 0.62f, 0.46f, 0.88f, 0.34f, 0.10f, 0.82f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/film-score.png", 0, false, {}, false},
        {"Epic Score", "Massive scoring stage for orchestral works", 7, 0.86f, 0.70f, 0.42f, 0.92f, 0.30f, 0.12f, 0.86f, 0.40f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/epic-score.png", 0, false, {}, false},
        {"Intimate Score", "Intimate scoring stage for small ensemble", 7, 0.70f, 0.56f, 0.50f, 0.84f, 0.36f, 0.09f, 0.78f, 0.34f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/intimate-score.png", 0, false, {}, false},
        {"Dark Score", "Moody, dark scoring ambience", 7, 0.80f, 0.64f, 0.40f, 0.86f, 0.28f, 0.10f, 0.80f, 0.36f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/dark-score.png", 0, false, {}, false},
        // Smooth Plate
        {"Smooth Plate", "Silky smooth plate with gentle character", 8, 0.50f, 0.42f, 0.48f, 0.80f, 0.30f, 0.07f, 0.72f, 0.30f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/smooth-plate.png", 0, false, {}, false},
        {"Velvet Plate", "Soft, velvety plate reverb", 8, 0.46f, 0.38f, 0.50f, 0.78f, 0.28f, 0.06f, 0.70f, 0.28f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/velvet-plate.png", 0, false, {}, false},
        {"Cream Plate", "Smooth, creamy plate with warm tone", 8, 0.52f, 0.44f, 0.46f, 0.82f, 0.32f, 0.08f, 0.74f, 0.32f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/cream-plate.png", 0, false, {}, false},
        {"Liquid Plate", "Fluid, liquid plate reverb", 8, 0.48f, 0.40f, 0.52f, 0.76f, 0.26f, 0.05f, 0.68f, 0.26f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/liquid-plate.png", 0, false, {}, false},
        {"Gentle Plate", "Gentle, unobtrusive plate for vocals", 8, 0.44f, 0.36f, 0.54f, 0.74f, 0.24f, 0.04f, 0.66f, 0.24f, 0.0f, 0.5f, 0, 0,
         "AestraAssets/plugins/AestraVerb/presets/gentle-plate.png", 0, false, {}, false},
    }};
    syncCategoryFromMode();
    buildControls();
}

void AestraVerbEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
    for (auto& knob : m_knobs) {
        if (knob.slider) knob.slider->setPlatformBridge(bridge);
    }
}

void AestraVerbEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;
    struct Meta { const char* label; uint32_t id; float defaultValue; };
    const Meta metas[] = {
        {"Predelay", kPredelay, 0.02f}, {"Size", kSize, 0.52f}, {"Decay", kDecay, 0.56f},
        {"Damping", kDamping, 0.50f}, {"Diffusion", kDiffusion, 0.64f},
        {"Mod Rate", kModRate, 0.42f}, {"Mod Depth", kModDepth, 0.07f},
        {"Width", kWidth, 0.68f}, {"Low Cut", kLowCut, 0.0f}, {"High Cut", kHighCut, 1.0f},
        {"Attack", kAttack, 0.0f}, {"Shape", kShape, 0.5f},
        {"Pre Sync", kPredelaySync, 0.0f}, {"Mod Char", kModCharacter, 0.0f}
    };
    for (const auto& meta : metas) {
        KnobControl k;
        k.label = meta.label;
        k.paramId = meta.id;
        k.defaultValue = meta.defaultValue;
        auto slider = std::make_shared<NUISlider>();
        slider->setStyle(NUISlider::Style::Rotary);
        slider->setRange(0.0, 1.0);
        slider->setValue(std::clamp(getParamValue(meta.id), 0.0f, 1.0f));
        slider->setPlatformBridge(getPlatformBridge());
        slider->setOnValueChange([this, paramId = meta.id](double value) {
            if (m_instance) {
                updateParameter(paramId, static_cast<float>(std::clamp(value, 0.0, 1.0)));
                repaint();
            }
        });
        k.slider = slider;
        addChild(slider);
        m_knobs.push_back(k);
    }
    layoutControls();
}

float AestraVerbEditor::getParamValue(uint32_t paramId) const {
    return m_instance ? std::clamp(m_instance->getParameter(paramId), 0.0f, 1.0f) : 0.0f;
}

std::string AestraVerbEditor::formatParameterValue(uint32_t paramId) const {
    // The DSP owns the parameter mappings. Reusing its display keeps the editor
    // truthful for mode-scaled decay/mod depth, filter cutoffs, and capped
    // tempo-synced predelay instead of duplicating mappings that can drift.
    return m_instance ? m_instance->getParameterDisplay(paramId) : "0";
}

void AestraVerbEditor::syncCategoryFromMode() {
    const int modeIdx = static_cast<int>(std::round(
        getParamValue(kMode) * static_cast<float>(kModeCount - 1)));
    const int newCat = categoryForMode(std::clamp(modeIdx, 0, kModeCount - 1));
    if (newCat == m_selectedCategory && !m_dropdownItems.empty()) return;
    if (newCat != m_selectedCategory) m_presetScroll = 0;
    m_selectedCategory = newCat;
    m_dropdownItems.clear();
    int modes[3] = {};
    const int count = modesInCategory(m_selectedCategory, modes);
    for (int i = 0; i < count; ++i) {
        static const char* modeNames[] = {
            "Room", "Hall", "Plate", "Cathedral", "Chamber",
            "Bright Hall", "Ambience", "Scoring", "Smooth Plate"
        };
        m_dropdownItems.push_back({modeNames[modes[i]], modes[i], {}, false});
    }
    layoutControls();
}

bool AestraVerbEditor::presetIsInSelectedCategory(const PresetButton& preset) const {
    return categoryForMode(std::clamp(preset.mode, 0, kModeCount - 1)) == m_selectedCategory;
}

int AestraVerbEditor::presetCountForSelectedCategory() const {
    return static_cast<int>(std::count_if(m_presets.begin(), m_presets.end(),
                                          [this](const PresetButton& preset) {
                                              return presetIsInSelectedCategory(preset);
                                          }));
}

int AestraVerbEditor::visiblePresetRows() const {
    const auto b = getBounds();
    const float listTop = b.y + kPresetListTopOffset;
    const float listBottom = b.y + b.height - kPresetListBottomPadding;
    const float available = std::max(0.0f, listBottom - listTop);
    return std::max(1, static_cast<int>((available + kPresetCardGap) /
                                        (kPresetCardHeight + kPresetCardGap)));
}

int AestraVerbEditor::maxPresetScroll() const {
    return std::max(0, presetCountForSelectedCategory() - visiblePresetRows());
}

void AestraVerbEditor::layoutControls() {
    if (m_layouting) return;
    m_layouting = true;
    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }
    if (!isDraggingWindow()) {
        enforceBoundsInParent(!m_userPositioned);
    }
    b = getBounds();

    const float presetX = b.x + kPad;
    const float presetW = presetColumnWidth(b.width);
    const float presetY = b.y + kPresetListTopOffset;
    m_presetScroll = std::clamp(m_presetScroll, 0, maxPresetScroll());
    const float scrollOffsetY = static_cast<float>(m_presetScroll) * (kPresetCardHeight + kPresetCardGap);
    int categoryRow = 0;
    for (auto& preset : m_presets) {
        if (!presetIsInSelectedCategory(preset)) {
            preset.bounds = {};
            continue;
        }
        preset.bounds = NUIRect(presetX,
                                presetY + static_cast<float>(categoryRow) * (kPresetCardHeight + kPresetCardGap) - scrollOffsetY,
                                presetW, kPresetCardHeight);
        ++categoryRow;
    }

    const float contentX = editorContentX(b);
    const float contentW = b.width - (contentX - b.x) - kPad;

    // Primary preset family navigation.
    const float catY = b.y + 60.0f;
    const float catH = 28.0f;
    const float catW = contentW / static_cast<float>(kCategoryCount);
    for (size_t i = 0; i < m_categoryPills.size(); ++i) {
        m_categoryPills[i].bounds = NUIRect(contentX + catW * static_cast<float>(i),
                                            catY, catW, catH);
    }

    // Secondary algorithm selector.
    const float ddY = b.y + 94.0f;
    const float ddH = 30.0f;
    m_dropdownButtonBounds = NUIRect(contentX, ddY, contentW, ddH);

    // Dropdown list (positioned below button when open)
    const float itemH = 26.0f;
    m_dropdownListBounds = NUIRect(contentX, ddY + ddH + 2.0f, contentW, itemH * static_cast<float>(m_dropdownItems.size()));
    for (size_t i = 0; i < m_dropdownItems.size(); ++i) {
        m_dropdownItems[i].bounds = NUIRect(contentX, ddY + ddH + 2.0f + itemH * static_cast<float>(i), contentW, itemH);
    }

    const float mainY = b.y + 134.0f;
    const float bodyBottom = b.y + b.height - 14.0f;
    const float bodyH = std::max(360.0f, bodyBottom - mainY);
    const float rightW = rightColWidth(b.width);
    const float centerW = contentW - rightW - 18.0f;
    const float centerX = contentX;
    const float rightX = centerX + centerW + 18.0f;

    const float sectionHeaderH = 26.0f;
    const float sectionGap = 8.0f;
    const float sectionRowStep = std::clamp((bodyH - sectionHeaderH * 3.0f - sectionGap * 2.0f) / 11.0f,
                                            27.0f, 34.0f);
    const float toneY = mainY;
    const float toneH = sectionHeaderH + sectionRowStep * 2.0f;
    const float motionY = toneY + toneH + sectionGap;
    const float motionH = sectionHeaderH + sectionRowStep * 3.0f;
    const float characterY = motionY + motionH + sectionGap;
    const float rowKnobSize = std::clamp(sectionRowStep - 5.0f, 23.0f, 29.0f);

    const float heroHeight = std::clamp(bodyH - 180.0f, 230.0f, 290.0f);

    for (auto& k : m_knobs) {
        if (k.paramId == kDecay) {
            // Reserve a clean visual gap above the lower parameter tiles. The
            // macro used to overlap their top edge at the default editor size.
            const float macroSize = std::min({196.0f, centerW - 30.0f, heroHeight - 104.0f});
            k.bounds = NUIRect(centerX, mainY + 20.0f, centerW, heroHeight - 78.0f);
            const NUIRect knobRect(centerX + (centerW - macroSize) * 0.5f, mainY + 28.0f,
                                   macroSize, macroSize);
            if (k.slider) k.slider->setBounds(knobRect);
            k.verticalLayout = false;
            continue;
        }
        float x = rightX + 10.0f;
        float y = mainY;
        float w = rightW - 20.0f;
        switch (k.paramId) {
        case kPredelay: continue;
        case kSize: continue;
        case kDamping: y = toneY + sectionHeaderH; break;
        case kDiffusion: y = toneY + sectionHeaderH + sectionRowStep; break;
        case kModRate: y = motionY + sectionHeaderH; break;
        case kModDepth: y = motionY + sectionHeaderH + sectionRowStep; break;
        case kWidth: y = motionY + sectionHeaderH + sectionRowStep * 2.0f; break;
        case kLowCut: y = characterY + sectionHeaderH; break;
        case kHighCut: y = characterY + sectionHeaderH + sectionRowStep; break;
        case kAttack: y = characterY + sectionHeaderH + sectionRowStep * 2.0f; break;
        case kShape: y = characterY + sectionHeaderH + sectionRowStep * 3.0f; break;
        case kPredelaySync: y = characterY + sectionHeaderH + sectionRowStep * 4.0f; break;
        case kModCharacter: y = characterY + sectionHeaderH + sectionRowStep * 5.0f; break;
        default: break;
        }
        k.verticalLayout = false;
        k.bounds = NUIRect(x, y, w, sectionRowStep);
        const NUIRect knobRect(x + 1.0f, y + (sectionRowStep - rowKnobSize) * 0.5f,
                               rowKnobSize, rowKnobSize);
        if (k.slider) k.slider->setBounds(knobRect);
    }

    // Two secondary space controls anchor the bottom of the decay hero.
    const float paramRowY = mainY + heroHeight - 64.0f;
    const float paramRowH = 56.0f;
    const float paramGap = 8.0f;
    const float paramW = (centerW - 24.0f - paramGap) * 0.5f;
    m_paramRowBounds[0] = NUIRect(centerX + 12.0f, paramRowY, paramW, paramRowH);
    m_paramRowBounds[1] = NUIRect(centerX + 12.0f + paramW + paramGap, paramRowY, paramW, paramRowH);
    // Position Predelay/Size sliders at their param row knob visual positions
    for (auto& k : m_knobs) {
        if (k.paramId == kPredelay || k.paramId == kSize) {
            const int idx = (k.paramId == kPredelay) ? 0 : 1;
            const auto& pr = m_paramRowBounds[idx];
            k.bounds = pr;
            k.verticalLayout = false;
            const NUIRect knobRect(pr.x + 10.0f, pr.center().y - 16.0f, 32.0f, 32.0f);
            if (k.slider) k.slider->setBounds(knobRect);
        }
    }

    const float mixY = mainY + heroHeight + 10.0f;
    m_mixBounds = NUIRect(centerX + 12.0f, mixY, centerW - 24.0f, 40.0f);
    m_mixTrack = NUIRect(m_mixBounds.x + 42.0f, m_mixBounds.y + 18.0f,
                         m_mixBounds.width - 86.0f, 4.0f);

    // Two-row utility deck: performance controls first, navigation and compare second.
    const float btnGap = 6.0f;
    const float btnW = std::clamp((centerW - 24.0f - btnGap * 3.0f) * 0.25f, 42.0f, 72.0f);
    const float btnRowW = 4.0f * btnW + 3.0f * btnGap;
    const float btnStartX = centerX + (centerW - btnRowW) * 0.5f;
    const float btnRowY = mixY + 58.0f;
    const float btnRow2Y = btnRowY + 34.0f;
    constexpr float btnH = 26.0f;
    m_bypassBounds = NUIRect(btnStartX, btnRowY, btnW, btnH);
    m_freezeBounds = NUIRect(btnStartX + (btnW + btnGap), btnRowY, btnW, btnH);
    m_saveBounds = NUIRect(btnStartX + (btnW + btnGap) * 2.0f, btnRowY, btnW, btnH);
    m_mixLockBounds = NUIRect(btnStartX + (btnW + btnGap) * 3.0f, btnRowY, btnW, btnH);
    m_navPrevBounds = NUIRect(btnStartX, btnRow2Y, btnW, btnH);
    m_navNextBounds = NUIRect(btnStartX + (btnW + btnGap), btnRow2Y, btnW, btnH);
    m_abBoundsA = NUIRect(btnStartX + (btnW + btnGap) * 2.0f, btnRow2Y, btnW, btnH);
    m_abBoundsB = NUIRect(btnStartX + (btnW + btnGap) * 3.0f, btnRow2Y, btnW, btnH);

    m_layouting = false;
}

void AestraVerbEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraVerbEditor::enforceBoundsInParent(bool recenterWhenPossible) {
    auto* parent = getParent();
    if (!parent) return;
    const NUIRect parentBounds = parent->getBounds();
    if (parentBounds.width <= 1.0f || parentBounds.height <= 1.0f) return;
    constexpr float kSafeMargin = 14.0f;
    constexpr float kChromeReserve = 56.0f;
    constexpr float kMinSafeWidth = 720.0f;
    constexpr float kMinSafeHeight = 560.0f;
    const float topReserve = parentBounds.y <= 1.0f && parentBounds.height > kMinSafeHeight + kChromeReserve + kSafeMargin * 2.0f
        ? kChromeReserve : 0.0f;
    NUIRect usable(parentBounds.x + kSafeMargin,
                   parentBounds.y + topReserve + kSafeMargin,
                   std::max(1.0f, parentBounds.width - kSafeMargin * 2.0f),
                   std::max(1.0f, parentBounds.height - topReserve - kSafeMargin * 2.0f));
    const NUIComponent* root = parent;
    while (root && root->getParent()) root = root->getParent();
    const float dockEdge = protectedLeftDockEdge(root, this, parentBounds);
    if (dockEdge > usable.x) {
        const float shift = std::min(usable.width - 1.0f, dockEdge + kSafeMargin - usable.x);
        usable.x += shift;
        usable.width = std::max(1.0f, usable.width - shift);
    }
    auto current = getBounds();
    float targetW = std::min(kWinW, usable.width);
    float targetH = std::min(kWinH, usable.height);
    if (usable.width >= kMinSafeWidth) targetW = std::max(kMinSafeWidth, targetW);
    if (usable.height >= kMinSafeHeight) targetH = std::max(kMinSafeHeight, targetH);
    const bool canFullyCenter = usable.width >= targetW && usable.height >= targetH;
    float targetX = current.x;
    float targetY = current.y;
    if (recenterWhenPossible && canFullyCenter) {
        targetX = usable.x + (usable.width - targetW) * 0.5f;
        targetY = usable.y + (usable.height - targetH) * 0.5f;
    } else {
        targetX = std::clamp(current.x, usable.x, std::max(usable.x, usable.right() - targetW));
        targetY = std::clamp(current.y, usable.y, std::max(usable.y, usable.bottom() - targetH));
    }
    if (std::abs(current.x - targetX) > 0.5f || std::abs(current.y - targetY) > 0.5f ||
        std::abs(current.width - targetW) > 0.5f || std::abs(current.height - targetH) > 0.5f) {
        setBounds(std::round(targetX), std::round(targetY), std::round(targetW), std::round(targetH));
    }
}

void AestraVerbEditor::drawPresetStrip(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    const float catY = b.y + 60.0f;
    const float stripX = b.x + kPad;
    const float stripW = presetColumnWidth(b.width);
    const float stripTopY = catY;
    const float stripBottomY = b.y + b.height - 14.0f;
    const float stripH = stripBottomY - stripTopY;
    renderer.fillRoundedRect({stripX - 5.0f, stripTopY, stripW + 10.0f, stripH}, 8.0f,
                             NUIColor(0.022f, 0.022f, 0.027f, 0.98f));
    renderer.strokeRoundedRect({stripX - 5.0f, stripTopY, stripW + 10.0f, stripH}, 8.0f, 1.0f, NUIColor(1, 1, 1, 0.085f));
    const NUIRect libraryRow(stripX + 7.0f, stripTopY + 5.0f, stripW - 23.0f, 16.0f);
    renderer.drawText("PRESET LIBRARY", {libraryRow.x, std::round(renderer.calculateTextY(libraryRow, 8.5f))}, 8.5f,
                      accent.withAlpha(0.82f));
    static const char* categoryLabels[] = {"ROOMS", "HALLS", "PLATES", "SPECIAL"};
    const int categoryIndex = std::clamp(m_selectedCategory, 0, kCategoryCount - 1);
    const NUIRect categoryRow(stripX + 7.0f, stripTopY + 21.0f, stripW - 28.0f, 18.0f);
    const float categoryTextY = std::round(renderer.calculateTextY(categoryRow, 8.0f));
    renderer.drawText(categoryLabels[categoryIndex], {categoryRow.x, categoryTextY}, 8.0f,
                      theme.getColor("textPrimary").withAlpha(0.48f));
    std::ostringstream presetCountText;
    presetCountText << presetCountForSelectedCategory() << " PRESETS";
    const std::string presetCountLabel = presetCountText.str();
    const float presetCountWidth = renderer.measureText(presetCountLabel, 7.5f).width;
    // Right-align to the category-row's inner edge (shares ROOMS' baseline) so the
    // count sits clearly inside the panel rather than hugging the border.
    renderer.drawText(presetCountLabel, {categoryRow.right() - presetCountWidth, categoryTextY}, 7.5f,
                      theme.getColor("textPrimary").withAlpha(0.32f));
    renderer.drawLine({stripX + 6.0f, b.y + kPresetListTopOffset - 7.0f},
                      {stripX + stripW - 6.0f, b.y + kPresetListTopOffset - 7.0f}, 1.0f,
                      NUIColor(1, 1, 1, 0.055f));

    int activePreset = 0;
    float bestDistance = 1000.0f;
    for (size_t i = 0; i < m_presets.size(); ++i) {
        auto& p = m_presets[i];
        const float distance = std::abs(getParamValue(kMode) - static_cast<float>(p.mode) / static_cast<float>(kModeCount - 1)) * 1.8f +
            std::abs(getParamValue(kSize) - p.size) +
            std::abs(getParamValue(kDecay) - p.decay) +
            std::abs(getParamValue(kWidth) - p.width) * 0.6f;
        if (distance < bestDistance) { bestDistance = distance; activePreset = static_cast<int>(i); }
    }

    const int clipTop = static_cast<int>(std::round(stripTopY));
    const int clipBottom = static_cast<int>(std::round(stripBottomY)) - 10;

    for (size_t i = 0; i < m_presets.size(); ++i) {
        auto& p = m_presets[i];
        const int presetTop = static_cast<int>(std::round(p.bounds.y));
        const int presetBottom = static_cast<int>(std::round(p.bounds.bottom()));
        if (presetTop < clipTop || presetBottom > clipBottom) continue;
        const bool active = static_cast<int>(i) == activePreset && bestDistance < 0.55f;
        const bool focused = static_cast<int>(i) == m_focusedPreset;
        const bool pressed = static_cast<int>(i) == m_pressedPreset;
        const NUIColor presetFill = active ? accent.withAlpha(pressed ? 0.17f : 0.125f)
            : (pressed ? NUIColor(0.060f, 0.052f, 0.078f, 0.99f)
                       : (p.hovered ? NUIColor(0.061f, 0.057f, 0.074f, 0.98f) : verbInsetBg().withAlpha(0.92f)));
        renderer.fillRoundedRect(p.bounds, 6.0f, presetFill);
        renderer.strokeRoundedRect(p.bounds, 5.0f, 1.0f,
                                   active ? accent.withAlpha(0.42f)
                                          : (p.hovered ? accent.withAlpha(0.30f) : NUIColor(1, 1, 1, 0.075f)));
        if (focused)
            renderer.strokeRoundedRect({p.bounds.x + 2.0f, p.bounds.y + 2.0f, p.bounds.width - 4.0f, p.bounds.height - 4.0f},
                                       4.0f, 1.0f, accent.withAlpha(0.28f));
        const NUIRect art(p.bounds.x + 7.0f, p.bounds.y + 7.0f, kPresetArtworkSize, kPresetArtworkSize);
        if (!p.artworkLoadAttempted && !p.artworkPath.empty()) {
            p.artworkTexture = renderer.loadTexture(p.artworkPath);
            p.artworkLoadAttempted = true;
        }
        if (p.artworkTexture != 0) {
            renderer.drawTexture(p.artworkTexture, art,
                                 NUIRect(0.0f, 0.0f, kPresetArtworkPixels, kPresetArtworkPixels));
            renderer.fillRoundedRect(art, 4.0f,
                                     NUIColor(0.0f, 0.0f, 0.0f, active ? 0.02f : (p.hovered ? 0.07f : 0.12f)));
        } else {
            const int modeIdx = std::clamp(p.mode, 0, kModeCount - 1);
            static const float modeHues[] = {0.08f, 0.12f, 0.0f, 0.75f, 0.55f, 0.15f, 0.58f, 0.70f, 0.02f};
            const float hue = modeHues[modeIdx];
            renderer.fillRectGradient(art,
                NUIColor(hue * 0.8f + 0.15f, hue * 0.4f + 0.08f, 0.20f, 1.0f),
                NUIColor(0.029f, 0.029f, 0.029f, 1.0f), true);
            static const char* modeInitials[] = {"R", "H", "P", "C", "Ch", "B", "A", "S", "Sp"};
            const char* initial = (modeIdx >= 0 && modeIdx < kModeCount) ? modeInitials[modeIdx] : "R";
            renderer.drawTextCentered(initial, art, 14.0f, accent.withAlpha(0.65f));
        }
        renderer.strokeRoundedRect(art, 4.0f, 1.0f,
                                   active ? verbGold().withAlpha(0.34f) : NUIColor(1, 1, 1, 0.10f));
        // Left accent bar for selected preset
        if (active) {
            renderer.fillRoundedRect({p.bounds.x, p.bounds.y + 7.0f, 2.5f, p.bounds.height - 14.0f}, 1.25f,
                                     verbGold().withAlpha(0.92f));
        }
        const NUIRect nameRect(p.bounds.x + 57.0f, p.bounds.y, p.bounds.width - 72.0f, p.bounds.height);
        const float nameSize = p.label.size() > 12 ? 9.0f : 10.0f;
        // Name + sub-text are a stacked pair; lift the name above centre so the
        // pair straddles the row centre (was bottom-heavy / looked unbalanced).
        const NUIRect nameLine(nameRect.x, p.bounds.y + 8.0f, nameRect.width, 19.0f);
        const std::string fittedName = fitVerbText(renderer, p.label, nameSize, nameLine.width);
        renderer.drawText(fittedName, {nameLine.x, std::round(renderer.calculateTextY(nameLine, nameSize))},
                          nameSize,
                           active ? verbGold().withAlpha(0.92f) : theme.getColor("textPrimary").withAlpha(0.82f));
        // Keep the card summary stable across algorithms. The canonical decay
        // duration remains visible in the macro because it is mode-dependent.
        static const char* modeNames[] = {"Room", "Hall", "Plate", "Cathedral", "Chamber",
                                          "Bright Hall", "Ambience", "Scoring", "Smooth Plate"};
        const int modeIdx = std::clamp(p.mode, 0, kModeCount - 1);
        constexpr float metadataSize = 8.0f;
        std::ostringstream decayText;
        decayText << std::lround(p.decay * 100.0f) << "%";
        const std::string decayLabel = decayText.str();
        const float decayWidth = renderer.measureText(decayLabel, metadataSize).width;
        const NUIRect metadataLine(nameRect.x, p.bounds.y + 29.0f, nameRect.width, 18.0f);
        const float metadataY = std::round(renderer.calculateTextY(metadataLine, metadataSize));
        const float modeWidth = std::max(0.0f, nameRect.width - decayWidth - 7.0f);
        const std::string fittedMode = fitVerbText(renderer, modeNames[modeIdx], metadataSize, modeWidth);
        const NUIColor metadataColor = theme.getColor("textPrimary").withAlpha(active ? 0.56f : 0.40f);
        renderer.drawText(fittedMode, {metadataLine.x, metadataY}, metadataSize, metadataColor);
        renderer.drawText(decayLabel, {metadataLine.right() - decayWidth, metadataY}, metadataSize, metadataColor);
    }

    const int presetCount = presetCountForSelectedCategory();
    const int rows = visiblePresetRows();
    if (presetCount > rows) {
        const float trackTop = b.y + kPresetListTopOffset;
        const float trackBottom = stripBottomY - 10.0f;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const NUIRect track(stripX + stripW + 1.5f, trackTop, 2.0f, trackHeight);
        renderer.fillRoundedRect(track, 1.0f, NUIColor(1, 1, 1, 0.055f));
        const float thumbHeight = std::max(28.0f, trackHeight * static_cast<float>(rows) / static_cast<float>(presetCount));
        const float scrollRange = static_cast<float>(std::max(1, maxPresetScroll()));
        const float thumbY = trackTop + (trackHeight - thumbHeight) * static_cast<float>(m_presetScroll) / scrollRange;
        renderer.fillRoundedRect({track.x, thumbY, track.width, thumbHeight}, 1.0f,
                                 accent.withAlpha(0.48f));
    }
}

void AestraVerbEditor::drawCategoryPills(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const auto outer = NUIRect(m_categoryPills.front().bounds.x, m_categoryPills.front().bounds.y,
                               m_categoryPills.back().bounds.right() - m_categoryPills.front().bounds.x,
                               m_categoryPills.front().bounds.height);
    renderer.fillRoundedRect(outer, 13.0f, NUIColor(0.027f, 0.027f, 0.027f, 0.985f));
    renderer.strokeRoundedRect(outer, 13.0f, 1.0f, NUIColor(1, 1, 1, 0.14f));
    const float pad = 2.0f;
    for (const auto& pill : m_categoryPills) {
        const bool selected = pill.category == m_selectedCategory;
        const bool pressed = pill.category == m_pressedCategory;
        if (selected) {
            const NUIRect sel(pill.bounds.x + pad, pill.bounds.y + pad,
                              pill.bounds.width - pad * 2.0f, pill.bounds.height - pad * 2.0f);
            renderer.fillRoundedRect(sel, 11.0f, accent.withAlpha(pressed ? 0.58f : 0.52f));
            renderer.strokeRoundedRect(sel, 11.0f, 1.0f, accent.withAlpha(pressed ? 0.64f : 0.58f));
        } else if (pill.hovered && pill.enabled) {
            const NUIRect hov(pill.bounds.x + pad, pill.bounds.y + pad,
                              pill.bounds.width - pad * 2.0f, pill.bounds.height - pad * 2.0f);
            renderer.fillRoundedRect(hov, 11.0f, NUIColor(1, 1, 1, pressed ? 0.060f : 0.040f));
        }
        if (!pill.enabled) {
            const NUIColor muted(1, 1, 1, 0.25f);
            static const char* kLockSvg = R"svg(
                <svg viewBox="0 0 10 11" fill="none" xmlns="http://www.w3.org/2000/svg">
                    <path d="M3 4.5V3a2 2 0 0 1 4 0v1.5M2 4.5h6a.5.5 0 0 1 .5.5v4a.5.5 0 0 1-.5.5H2a.5.5 0 0 1-.5-.5V5a.5.5 0 0 1 .5-.5Z" stroke="currentColor" stroke-width="1"/>
                </svg>
            )svg";
            static auto lockIcon = std::make_shared<NUIIcon>(kLockSvg);
            const float gap = 4.0f;
            const float iconSize = 11.0f;
            const float textW = 38.0f;
            const float totalW = iconSize + gap + textW;
            const float startX = pill.bounds.center().x - totalW * 0.5f;
            const float iconCY = pill.bounds.center().y;
            lockIcon->setBounds({std::round(startX), std::round(iconCY - iconSize * 0.5f), iconSize, iconSize});
            lockIcon->setColor(muted);
            lockIcon->onRender(renderer);
            renderer.drawTextCentered(pill.label, {pill.bounds.x + 14.0f, pill.bounds.y, pill.bounds.width - 14.0f, pill.bounds.height}, 9.5f, muted);
        } else {
            const bool active = selected || pill.hovered;
            const float alpha = active ? 0.96f : (pill.hovered ? 0.70f : 0.50f);
            renderer.drawTextCentered(pill.label, pill.bounds, 9.5f,
                                      theme.getColor("textPrimary").withAlpha(alpha));
        }
    }
}

void AestraVerbEditor::drawModeDropdown(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& btn = m_dropdownButtonBounds;
    const bool anyHovered = m_dropdownOpen;
    renderer.fillRoundedRect(btn, 6.0f, verbInsetBg().withAlpha(0.96f));
    renderer.strokeRoundedRect(btn, 6.0f, 1.0f,
                               anyHovered ? accent.withAlpha(0.45f) : NUIColor(1, 1, 1, 0.12f));

    const int modeIdx = static_cast<int>(std::round(
        getParamValue(kMode) * static_cast<float>(kModeCount - 1)));
    static const char* modeNames[] = {
        "Room", "Hall", "Plate", "Cathedral", "Chamber",
        "Bright Hall", "Ambience", "Scoring", "Smooth Plate"
    };
    // Small caption so the bar reads as a labelled selector rather than a bare box.
    // Centre the mode name on the button, then sit the caption on the SAME baseline
    // (not independently centred) so the small "MODE" doesn't float above it.
    const float ddNameSize = 10.5f;
    const float ddCapSize = 8.0f;
    const float ddNameY = opticalTextY(renderer, btn.center().y, ddNameSize);
    const float ddBaseline = ddNameY + renderer.getFontMetrics(ddNameSize).ascent;
    const float ddCapY = ddBaseline - renderer.getFontMetrics(ddCapSize).ascent;
    renderer.drawText("MODE", {btn.x + 10.0f, std::round(ddCapY)}, ddCapSize,
                      accent.withAlpha(0.52f));
    const char* currentName = (modeIdx >= 0 && modeIdx < kModeCount) ? modeNames[modeIdx] : "Room";
    renderer.drawText(currentName, {btn.x + 48.0f, std::round(ddNameY)}, ddNameSize,
                      theme.getColor("textPrimary").withAlpha(0.92f));

    // Split-button divider + chevron affordance on the right.
    renderer.drawLine({std::round(btn.right() - 28.0f), btn.y + 6.0f},
                      {std::round(btn.right() - 28.0f), btn.bottom() - 6.0f}, 1.0f, NUIColor(1, 1, 1, 0.08f));
    static const char* kChevronDownSvg = R"svg(
        <svg viewBox="0 0 10 6" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M1 1L5 5L9 1" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )svg";
    static const char* kChevronUpSvg = R"svg(
        <svg viewBox="0 0 10 6" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M1 5L5 1L9 5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
        </svg>
    )svg";
    static auto chevronDown = std::make_shared<NUIIcon>(kChevronDownSvg);
    static auto chevronUp = std::make_shared<NUIIcon>(kChevronUpSvg);
    auto& chevronIcon = m_dropdownOpen ? chevronUp : chevronDown;
    const float chevronSize = 10.0f;
    chevronIcon->setBounds({std::round(btn.right() - 19.0f), std::round(btn.center().y - chevronSize * 0.5f),
                            chevronSize, chevronSize});
    chevronIcon->setColor(accent.withAlpha(anyHovered ? 0.90f : 0.70f));
    chevronIcon->onRender(renderer);

    if (m_dropdownOpen && !m_dropdownItems.empty()) {
        renderer.fillRoundedRect(m_dropdownListBounds, 6.0f, NUIColor(0.027f, 0.027f, 0.027f, 0.985f));
        renderer.strokeRoundedRect(m_dropdownListBounds, 6.0f, 1.0f, NUIColor(1, 1, 1, 0.14f));
        for (const auto& item : m_dropdownItems) {
            const bool isCurrent = item.mode == modeIdx;
            if (isCurrent) {
                renderer.fillRoundedRect({item.bounds.x + 2.0f, item.bounds.y + 1.0f,
                                          item.bounds.width - 4.0f, item.bounds.height - 2.0f},
                                         4.0f, accent.withAlpha(0.35f));
            } else if (item.hovered) {
                renderer.fillRoundedRect({item.bounds.x + 2.0f, item.bounds.y + 1.0f,
                                          item.bounds.width - 4.0f, item.bounds.height - 2.0f},
                                         4.0f, accent.withAlpha(0.08f));
            }
            renderer.drawText(item.label, {item.bounds.x + 10.0f,
                                            std::round(renderer.calculateTextY(item.bounds, 10.0f))}, 10.0f,
                              theme.getColor("textPrimary").withAlpha(isCurrent ? 0.96f : (item.hovered ? 0.82f : 0.60f)));
        }
    }
}

void AestraVerbEditor::drawKnob(NUIRenderer& renderer, const KnobControl& k, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect knobRect = k.slider ? k.slider->getBounds() : NUIRect();
    const float cx = knobRect.center().x;
    const float cy = knobRect.center().y;
    const float r = std::clamp(std::min(knobRect.width, knobRect.height) * 0.34f, 10.0f, 18.0f);
    const bool active = k.slider ? k.slider->isDragging() : false;
    const bool hover = k.slider ? k.slider->isHovered() : false;
    const float stateLift = active ? 1.0f : (hover ? 0.55f : 0.0f);

    if (k.paramId == kDecay) {
        const float macroR = std::clamp(std::min(knobRect.width, knobRect.height) * 0.43f, 20.0f, 92.0f);
        const float ringThickness = 11.0f;
        renderer.drawShadow(NUIRect{cx - macroR * 0.82f, cy - macroR * 0.82f, macroR * 1.64f, macroR * 1.64f}, 0.0f, 3.0f, 6.0f,
                            NUIColor(0, 0, 0, 0.52f));
        renderer.fillCircle({cx, cy}, macroR - 10.0f, NUIColor(0.012f, 0.012f, 0.016f, 0.98f));
        renderer.strokeCircle({cx, cy}, macroR - 15.0f, 1.0f, NUIColor(1, 1, 1, 0.045f));
        renderer.strokeCircle({cx, cy}, macroR, ringThickness, NUIColor(0.091f, 0.091f, 0.091f, 1.0f));
        const float dStartAngle = -kPi * 0.5f;
        const float dSweep = kTwoPi * 0.80f;
        const float dValue = k.slider ? k.slider->getValue() : 0.0f;
        const float dEndAngle = dStartAngle + dValue * dSweep;
        for (int i = 0; i <= 10; ++i) {
            const float tickAngle = dStartAngle + dSweep * static_cast<float>(i) / 10.0f;
            const float innerR = macroR + 9.0f;
            const float outerR = innerR + (i % 5 == 0 ? 5.0f : 3.0f);
            renderer.drawLine({cx + std::cos(tickAngle) * innerR, cy + std::sin(tickAngle) * innerR},
                              {cx + std::cos(tickAngle) * outerR, cy + std::sin(tickAngle) * outerR},
                              1.0f, NUIColor(1, 1, 1, i % 5 == 0 ? 0.16f : 0.08f));
        }
        drawVerbArc(renderer, {cx, cy}, macroR, dStartAngle, dEndAngle, ringThickness, accent.withAlpha(0.86f + stateLift * 0.10f));
        const float dotR = 7.0f;
        renderer.fillCircle({cx + std::cos(dEndAngle) * macroR, cy + std::sin(dEndAngle) * macroR}, dotR,
                            verbGold().withAlpha(active ? 1.0f : (hover ? 0.98f : 0.92f)));
        const std::string valueText = formatParameterValue(kDecay);
        const float textH = 44.0f;
        const float labelH = 14.0f;
        const float totalH = textH + 2.0f + labelH;
        const float textTop = cy - totalH * 0.5f;
        renderer.drawTextCentered(valueText, {k.bounds.x, textTop, k.bounds.width, textH}, 34.0f,
                                  theme.getColor("textPrimary").withAlpha(active ? 1.0f : (hover ? 0.97f : 0.94f)));
        renderer.drawTextCentered("DECAY", {k.bounds.x, textTop + textH + 2.0f, k.bounds.width, labelH}, 9.0f,
                                  accent.withAlpha(0.68f));
        return;
    }

    if (k.verticalLayout) {
        renderer.drawShadow(NUIRect{cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f}, 0.0f, 2.0f, 4.0f,
                            NUIColor(0, 0, 0, 0.48f));
        renderer.fillCircle({cx, cy}, r * 0.76f, hover ? verbSurfaceBg().withAlpha(1.0f) : NUIColor(0.039f, 0.039f, 0.039f, 1.0f));
        renderer.strokeCircle({cx, cy}, r * 0.76f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.09f + stateLift * 0.045f));
        renderer.fillCircle({cx - r * 0.19f, cy - r * 0.22f}, r * 0.25f, NUIColor(1, 1, 1, 0.026f + stateLift * 0.016f));
        const float startAngle = kPi * 0.75f;
        const float sweep = kPi * 1.5f;
        const float value = k.slider ? k.slider->getValue() : 0.0;
        const float endAngle = startAngle + value * sweep;
        drawVerbArc(renderer, {cx, cy}, r, startAngle, startAngle + sweep, 3.0f, NUIColor(1, 1, 1, 0.12f + stateLift * 0.025f));
        drawVerbArc(renderer, {cx, cy}, r, startAngle, endAngle, 3.0f, accent.withAlpha(0.86f + stateLift * 0.11f));
        const float pa = startAngle + value * kPi * 1.5f;
        renderer.drawLine({cx, cy}, {cx + std::cos(pa) * (r * 0.56f), cy + std::sin(pa) * (r * 0.56f)}, 2.0f,
                          NUIColor(1.0f, 1.0f, 1.0f, active ? 0.98f : (hover ? 0.92f : 0.84f)));
        const float labelY = k.bounds.y + knobRect.height + 6.0f;
        renderer.drawTextCentered(k.label, {k.bounds.x, labelY, k.bounds.width, 14.0f}, 10.0f,
                                  theme.getColor("textPrimary").withAlpha(0.74f + stateLift * 0.12f));
        renderer.drawTextCentered(formatParameterValue(k.paramId), {k.bounds.x, labelY + 14.0f, k.bounds.width, 14.0f}, 10.5f,
                                  theme.getColor("textPrimary").withAlpha(0.60f + stateLift * 0.20f));
        return;
    }

    renderer.drawShadow(NUIRect{cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f}, 0.0f, 2.0f, 4.0f,
                        NUIColor(0, 0, 0, 0.48f));
    if (hover) renderer.strokeCircle({cx, cy}, r + 1.5f, 1.0f, accent.withAlpha(0.25f));
    renderer.fillCircle({cx, cy}, r * 0.76f, hover ? verbSurfaceBg().withAlpha(1.0f) : NUIColor(0.039f, 0.039f, 0.039f, 1.0f));
    renderer.strokeCircle({cx, cy}, r * 0.76f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.09f + stateLift * 0.045f));
    renderer.fillCircle({cx - r * 0.19f, cy - r * 0.22f}, r * 0.25f, NUIColor(1, 1, 1, 0.026f + stateLift * 0.016f));
    const float startAngle = kPi * 0.75f;
    const float sweep = kPi * 1.5f;
    const float value = k.slider ? k.slider->getValue() : 0.0;
    const float endAngle = startAngle + value * sweep;
    drawVerbArc(renderer, {cx, cy}, r, startAngle, startAngle + sweep, 3.0f, NUIColor(1, 1, 1, 0.12f + stateLift * 0.025f));
    drawVerbArc(renderer, {cx, cy}, r, startAngle, endAngle, 3.0f, accent.withAlpha(0.86f + stateLift * 0.11f));
    const float pa = startAngle + value * kPi * 1.5f;
    renderer.drawLine({cx, cy}, {cx + std::cos(pa) * (r * 0.56f), cy + std::sin(pa) * (r * 0.56f)}, 2.0f,
                      NUIColor(1.0f, 1.0f, 1.0f, active ? 0.98f : (hover ? 0.92f : 0.84f)));
    const float textX = knobRect.right() + 9.0f;
    // Optically centre the label + value on the knob's centre (see opticalTextY).
    const float labelTextY = std::round(opticalTextY(renderer, cy, 9.5f));
    renderer.drawText(k.label, {textX, labelTextY}, 9.5f,
                      theme.getColor("textPrimary").withAlpha(0.74f + stateLift * 0.12f));
    const NUIColor valueColor = (active || hover) ? accent.withAlpha(0.70f + stateLift * 0.15f)
                                                   : theme.getColor("textPrimary").withAlpha(0.55f);
    const std::string valueText = formatParameterValue(k.paramId);
    const float valueFontSize = k.paramId == kPredelaySync ? 7.5f : 9.5f;
    const float valueWidth = renderer.measureText(valueText, valueFontSize).width;
    const float valueTextY = std::round(opticalTextY(renderer, cy, valueFontSize));
    // Clear breathing room from the card border so long values ("20000Hz") don't
    // read as bleeding into the edge.
    renderer.drawText(valueText, {k.bounds.right() - valueWidth - 11.0f, valueTextY}, valueFontSize, valueColor);
}

void AestraVerbEditor::drawMixSlider(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = getParamValue(kMix);
    const float stateLift = m_draggingMix ? 1.0f : (m_mixHovered ? 0.55f : 0.0f);
    if (m_mixFocused)
        renderer.strokeRoundedRect({m_mixBounds.x - 6.0f, m_mixBounds.y + 2.0f, m_mixBounds.width + 12.0f, m_mixBounds.height - 4.0f},
                                   7.0f, 1.0f, accent.withAlpha(0.24f));
    const float mixTextY = std::round(renderer.calculateTextY(m_mixBounds, 10.0f));
    renderer.drawText("MIX", {m_mixBounds.x, mixTextY}, 10.0f,
                      theme.getColor("textPrimary").withAlpha(0.70f + stateLift * 0.12f));
    renderer.fillRoundedRect(m_mixTrack, 2.0f, NUIColor(1, 1, 1, 0.13f + stateLift * 0.035f));
    renderer.fillRoundedRect({m_mixTrack.x, m_mixTrack.y, m_mixTrack.width * mix, m_mixTrack.height}, 2.0f,
                             accent.withAlpha(0.82f + stateLift * 0.10f));
    const float thumbX = m_mixTrack.x + m_mixTrack.width * mix;
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 7.5f, accent.withAlpha(0.20f + stateLift * 0.10f));
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 6.0f,
                        theme.getColor("textPrimary").withAlpha(0.92f + stateLift * 0.06f));
    const std::string mixValue = formatParameterValue(kMix);
    const float mixValueWidth = renderer.measureText(mixValue, 10.0f).width;
    renderer.drawText(mixValue, {m_mixBounds.right() - mixValueWidth, mixTextY}, 10.0f,
                      accent.withAlpha(0.80f + stateLift * 0.14f));
}

void AestraVerbEditor::drawBypassPill(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float v = getParamValue(kBypass);
    const bool on = v > 0.5f;
    renderer.fillRoundedRect(m_bypassBounds, 6.0f, on ? accent.withAlpha(0.52f) : NUIColor(0, 0, 0, 0));
    renderer.strokeRoundedRect(m_bypassBounds, 6.0f, 1.0f, on ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("BYP", m_bypassBounds, 9.0f, NUIColor(1, 1, 1, on ? 0.95f : 0.60f));
}

void AestraVerbEditor::drawFreezePill(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float v = getParamValue(kFreeze);
    const bool on = v > 0.5f;
    renderer.fillRoundedRect(m_freezeBounds, 6.0f, on ? accent.withAlpha(0.52f) : NUIColor(0, 0, 0, 0));
    renderer.strokeRoundedRect(m_freezeBounds, 6.0f, 1.0f, on ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("FRZ", m_freezeBounds, 9.0f, NUIColor(1, 1, 1, on ? 0.95f : 0.60f));
}

void AestraVerbEditor::drawPresetNav(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_navPrevBounds, 6.0f, m_navPrevHovered ? NUIColor(1, 1, 1, 0.08f) : NUIColor(0, 0, 0, 0));
    renderer.strokeRoundedRect(m_navPrevBounds, 6.0f, 1.0f, m_navPrevHovered ? accent.withAlpha(0.4f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("<", m_navPrevBounds, 8.5f, theme.getColor("textPrimary").withAlpha(m_navPrevHovered ? 0.9f : 0.60f));
    renderer.fillRoundedRect(m_navNextBounds, 6.0f, m_navNextHovered ? NUIColor(1, 1, 1, 0.08f) : NUIColor(0, 0, 0, 0));
    renderer.strokeRoundedRect(m_navNextBounds, 6.0f, 1.0f, m_navNextHovered ? accent.withAlpha(0.4f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered(">", m_navNextBounds, 8.5f, theme.getColor("textPrimary").withAlpha(m_navNextHovered ? 0.9f : 0.60f));
    renderer.fillRoundedRect(m_saveBounds, 6.0f, m_saveHovered ? accent.withAlpha(0.35f) : NUIColor(0, 0, 0, 0));
    renderer.strokeRoundedRect(m_saveBounds, 6.0f, 1.0f, m_saveHovered ? accent.withAlpha(0.5f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("SAVE", m_saveBounds, 9.0f, theme.getColor("textPrimary").withAlpha(m_saveHovered ? 0.92f : 0.60f));
}

void AestraVerbEditor::drawABButtons(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const bool aActive = m_activeAB == 1;
    const bool bActive = m_activeAB == 2;
    renderer.fillRoundedRect(m_abBoundsA, 6.0f, aActive ? accent.withAlpha(0.52f) : (m_abHoveredA ? NUIColor(1, 1, 1, 0.08f) : NUIColor(0, 0, 0, 0)));
    renderer.strokeRoundedRect(m_abBoundsA, 6.0f, 1.0f, aActive ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("A", m_abBoundsA, 9.0f, theme.getColor("textPrimary").withAlpha(aActive ? 0.95f : 0.60f));
    renderer.fillRoundedRect(m_abBoundsB, 6.0f, bActive ? accent.withAlpha(0.52f) : (m_abHoveredB ? NUIColor(1, 1, 1, 0.08f) : NUIColor(0, 0, 0, 0)));
    renderer.strokeRoundedRect(m_abBoundsB, 6.0f, 1.0f, bActive ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered("B", m_abBoundsB, 9.0f, theme.getColor("textPrimary").withAlpha(bActive ? 0.95f : 0.60f));
}

void AestraVerbEditor::drawMixLock(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_mixLockBounds, 6.0f, m_mixLocked ? accent.withAlpha(0.52f) : (m_mixLockHovered ? NUIColor(1, 1, 1, 0.08f) : NUIColor(0, 0, 0, 0)));
    renderer.strokeRoundedRect(m_mixLockBounds, 6.0f, 1.0f, m_mixLocked ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.15f));
    renderer.drawTextCentered(m_mixLocked ? "LOCK" : "UNLCK", m_mixLockBounds, 9.0f, theme.getColor("textPrimary").withAlpha(m_mixLocked ? 0.95f : 0.60f));
}

void AestraVerbEditor::drawSectionLabels(NUIRenderer& renderer) {
    auto b = getBounds();
    const float mainX = editorContentX(b);
    const float contentW = b.width - (mainX - b.x) - kPad;
    const float mainY = b.y + 134.0f;
    const float bodyBottom = b.y + b.height - 14.0f;
    const float bodyH = std::max(360.0f, bodyBottom - mainY);
    const float rightW = rightColWidth(b.width);
    const float centerW = contentW - rightW - 18.0f;
    const float rightX = mainX + centerW + 18.0f;
    constexpr float headerH = 26.0f;
    constexpr float gap = 8.0f;
    const float rowStep = std::clamp((bodyH - headerH * 3.0f - gap * 2.0f) / 11.0f, 27.0f, 34.0f);
    const float toneH = headerH + rowStep * 2.0f;
    const float motionY = mainY + toneH + gap;
    const float motionH = headerH + rowStep * 3.0f;
    const float characterY = motionY + motionH + gap;
    const float characterH = headerH + rowStep * 6.0f;

    auto drawSection = [&](const char* label, const char* hint, float y, float height, int rows) {
        const NUIRect card(rightX, y, rightW, height);
        renderer.fillRoundedRect(card, 9.0f, NUIColor(0.030f, 0.030f, 0.035f, 0.97f));
        renderer.strokeRoundedRect(card, 9.0f, 1.0f, NUIColor(1, 1, 1, 0.075f));
        renderer.fillCircle({rightX + 13.0f, y + 13.0f}, 2.0f, verbAccent().withAlpha(0.72f));
        const NUIRect headerRow(rightX + 21.0f, y, rightW - 33.0f, headerH);
        renderer.drawText(label, {headerRow.x, std::round(renderer.calculateTextY(headerRow, 8.5f))}, 8.5f,
                          NUIColor(0.72f, 0.70f, 0.80f, 0.78f));
        const float hintWidth = renderer.measureText(hint, 7.5f).width;
        renderer.drawText(hint, {headerRow.right() - hintWidth,
                                  std::round(renderer.calculateTextY(headerRow, 7.5f))}, 7.5f,
                          NUIColor(1, 1, 1, 0.26f));
        for (int i = 1; i < rows; ++i) {
            const float lineY = y + headerH + rowStep * static_cast<float>(i);
            renderer.drawLine({rightX + 10.0f, lineY}, {rightX + rightW - 10.0f, lineY},
                              1.0f, NUIColor(1, 1, 1, 0.035f));
        }
    };
    drawSection("TONE", "COLOR", mainY, toneH, 2);
    drawSection("MOTION", "STEREO", motionY, motionH, 3);
    drawSection("CHARACTER", "DETAIL", characterY, characterH, 6);
}

void AestraVerbEditor::drawParamRow(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const uint32_t params[] = {kPredelay, kSize};
    for (int i = 0; i < 2; ++i) {
        const auto& rect = m_paramRowBounds[i];
        const float cx = rect.x + 26.0f;
        const float cy = rect.center().y;
        const float r = 14.0f;
        KnobControl temp;
        temp.paramId = params[i];
        for (const auto& k : m_knobs) {
            if (k.paramId == params[i]) { temp = k; break; }
        }
        const bool active = temp.slider ? temp.slider->isDragging() : false;
        const bool hover = temp.slider ? temp.slider->isHovered() : false;
        const float stateLift = active ? 1.0f : (hover ? 0.55f : 0.0f);
        renderer.fillRoundedRect(rect, 7.0f,
                                 hover ? NUIColor(0.034f, 0.031f, 0.044f, 0.98f)
                                       : NUIColor(0.020f, 0.020f, 0.024f, 0.92f));
        renderer.strokeRoundedRect(rect, 7.0f, 1.0f,
                                   active ? accent.withAlpha(0.46f)
                                          : (hover ? accent.withAlpha(0.24f) : NUIColor(1, 1, 1, 0.065f)));
        renderer.fillCircle({cx, cy}, r, NUIColor(0.01f, 0.01f, 0.01f, 1.0f));
        renderer.strokeCircle({cx, cy}, r, 1.0f, NUIColor(1, 1, 1, 0.10f + stateLift * 0.045f));
        if (hover) renderer.strokeCircle({cx, cy}, r + 1.5f, 1.0f, accent.withAlpha(0.20f));
        const float startAngle = kPi * 0.75f;
        const float sweep = kPi * 1.5f;
        const float value = temp.slider ? temp.slider->getValue() : 0.0f;
        const float endAngle = startAngle + value * sweep;
        drawVerbArc(renderer, {cx, cy}, r, startAngle, startAngle + sweep, 3.0f, NUIColor(0.166f, 0.166f, 0.166f, 1.0f));
        drawVerbArc(renderer, {cx, cy}, r, startAngle, endAngle, 3.0f, accent.withAlpha(0.86f + stateLift * 0.11f));
        const float pa = startAngle + value * kPi * 1.5f;
        renderer.fillCircle({cx + std::cos(pa) * (r - 2.0f), cy + std::sin(pa) * (r - 2.0f)}, 2.5f,
                            theme.getColor("textPrimary").withAlpha(0.90f + stateLift * 0.08f));
        const float textX = rect.x + 52.0f;
        const NUIRect labelLine(textX, rect.y + 8.0f, rect.right() - textX - 10.0f, 18.0f);
        const NUIRect valueLine(textX, rect.y + 27.0f, rect.right() - textX - 10.0f, 18.0f);
        renderer.drawText(temp.label, {labelLine.x, std::round(renderer.calculateTextY(labelLine, 8.5f))}, 8.5f,
                          theme.getColor("textPrimary").withAlpha(0.50f + stateLift * 0.15f));
        const NUIColor valColor = (active || hover) ? accent.withAlpha(0.85f) : theme.getColor("textPrimary").withAlpha(0.55f);
        const std::string valueText = fitVerbText(renderer, formatParameterValue(temp.paramId), 10.5f, valueLine.width);
        renderer.drawText(valueText, {valueLine.x, std::round(renderer.calculateTextY(valueLine, 10.5f))}, 10.5f, valColor);
    }
}

void AestraVerbEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    auto b = getBounds();
    NUIColor accent = verbAccent();
    const float mainX = editorContentX(b);
    const float contentW = b.width - (mainX - b.x) - kPad;
    const float mainY = b.y + 134.0f;
    const float bodyBottom = b.y + b.height - 14.0f;
    const float bodyH = std::max(360.0f, bodyBottom - mainY);
    const float rightW = rightColWidth(b.width);
    const float centerW = contentW - rightW - 18.0f;
    const float heroHeight = std::clamp(bodyH - 180.0f, 230.0f, 290.0f);

    renderer.fillRectGradient({b.x + 8.0f, b.y + 48.0f, b.width - 16.0f, b.height - 58.0f},
                              NUIColor(0.024f, 0.022f, 0.030f, 0.98f),
                              NUIColor(0.012f, 0.013f, 0.017f, 0.99f), true);
    drawPresetStrip(renderer, accent);
    drawCategoryPills(renderer, accent);

    renderer.fillRoundedRect({mainX - 8.0f, mainY - 6.0f, contentW + 16.0f, bodyH + 12.0f}, 11.0f,
                             NUIColor(0.018f, 0.018f, 0.022f, 0.94f));
    renderer.strokeRoundedRect({mainX - 8.0f, mainY - 6.0f, contentW + 16.0f, bodyH + 12.0f}, 11.0f, 1.0f,
                               NUIColor(1, 1, 1, 0.055f));

    const NUIRect heroCard(mainX, mainY, centerW, heroHeight);
    renderer.fillRoundedRect(heroCard, 10.0f, NUIColor(0.026f, 0.025f, 0.033f, 0.98f));
    renderer.strokeRoundedRect(heroCard, 10.0f, 1.0f, accent.withAlpha(0.16f));
    renderer.fillCircle({heroCard.x + 15.0f, heroCard.y + 15.0f}, 2.0f, accent.withAlpha(0.82f));
    const NUIRect heroHeader(heroCard.x + 23.0f, heroCard.y + 2.0f, heroCard.width - 37.0f, 26.0f);
    renderer.drawText("SPACE", {heroHeader.x, std::round(renderer.calculateTextY(heroHeader, 8.5f))}, 8.5f,
                      NUIColor(0.76f, 0.72f, 0.88f, 0.76f));
    const std::string modeName = formatParameterValue(kMode);
    const float modeWidth = renderer.measureText(modeName, 8.5f).width;
    renderer.drawText(modeName, {heroHeader.right() - modeWidth,
                                  std::round(renderer.calculateTextY(heroHeader, 8.5f))}, 8.5f,
                      NUIColor(1, 1, 1, 0.34f));

    const NUIRect mixCard(m_mixBounds.x - 10.0f, m_mixBounds.y - 5.0f,
                          m_mixBounds.width + 20.0f, m_mixBounds.height + 10.0f);
    renderer.fillRoundedRect(mixCard, 9.0f, NUIColor(0.026f, 0.025f, 0.033f, 0.96f));
    renderer.strokeRoundedRect(mixCard, 9.0f, 1.0f, NUIColor(1, 1, 1, 0.06f));

    const NUIRect utilityCard(m_bypassBounds.x - 10.0f, m_bypassBounds.y - 10.0f,
                              m_mixLockBounds.right() - m_bypassBounds.x + 20.0f,
                              m_abBoundsB.bottom() - m_bypassBounds.y + 20.0f);
    renderer.fillRoundedRect(utilityCard, 9.0f, NUIColor(0.023f, 0.023f, 0.029f, 0.94f));
    renderer.strokeRoundedRect(utilityCard, 9.0f, 1.0f, NUIColor(1, 1, 1, 0.055f));

    drawSectionLabels(renderer);
    for (const auto& k : m_knobs) {
        if (k.paramId == kPredelay || k.paramId == kSize) continue;
        drawKnob(renderer, k, accent);
    }
    drawParamRow(renderer, accent);
    drawMixSlider(renderer, accent);
    // Button grid with SVG icons: BYP FRZ SAVE UNLCK / < > A B
    {
        static const char* kBypassSvg = R"svg(<svg viewBox="0 0 14 14" fill="none"><circle cx="7" cy="7" r="5.5" stroke="currentColor" stroke-width="1.1"/><line x1="7" y1="2" x2="7" y2="7" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/></svg>)svg";
        static const char* kFreezeSvg = R"svg(<svg viewBox="0 0 14 14" fill="none"><line x1="7" y1="1" x2="7" y2="13" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/><line x1="1" y1="7" x2="13" y2="7" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/><line x1="3" y1="3" x2="11" y2="11" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/><line x1="11" y1="3" x2="3" y2="11" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/></svg>)svg";
        static const char* kSaveSvg = R"svg(<svg viewBox="0 0 14 14" fill="none"><path d="M2 2h8l2 2v8a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1Z" stroke="currentColor" stroke-width="1.1"/><rect x="4" y="2" width="5" height="4" rx="0.5" stroke="currentColor" stroke-width="1.1"/><rect x="3" y="9" width="8" height="3" rx="0.5" stroke="currentColor" stroke-width="1.1"/></svg>)svg";
        static const char* kLockSvg = R"svg(<svg viewBox="0 0 14 14" fill="none"><rect x="3" y="6" width="8" height="6.5" rx="1" stroke="currentColor" stroke-width="1.1"/><path d="M5 6V4.5a2 2 0 0 1 4 0V6" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/></svg>)svg";
        static const char* kUnlockSvg = R"svg(<svg viewBox="0 0 14 14" fill="none"><rect x="3" y="6" width="8" height="6.5" rx="1" stroke="currentColor" stroke-width="1.1"/><path d="M5 6V4.5a2 2 0 0 1 4 0" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"/></svg>)svg";
        static auto bypassIcon = std::make_shared<NUIIcon>(kBypassSvg);
        static auto freezeIcon = std::make_shared<NUIIcon>(kFreezeSvg);
        static auto saveIcon = std::make_shared<NUIIcon>(kSaveSvg);
        static auto lockIcon = std::make_shared<NUIIcon>(kLockSvg);
        static auto unlockIcon = std::make_shared<NUIIcon>(kUnlockSvg);

        struct BtnInfo { NUIRect bounds; bool on; bool hov; const char* label; const char* tip; };
        BtnInfo btns[] = {
            {m_bypassBounds, getParamValue(kBypass) > 0.5f, m_bypassHovered, "BYP", "Bypass"},
            {m_freezeBounds, getParamValue(kFreeze) > 0.5f, m_freezeHovered, "FRZ", "Freeze"},
            {m_saveBounds, false, m_saveHovered, "SAVE", "Save Preset"},
            {m_mixLockBounds, m_mixLocked, m_mixLockHovered, "MIX", m_mixLocked ? "Unlock Mix" : "Lock Mix"},
        };
        for (auto& btn : btns) {
            renderer.fillRoundedRect(btn.bounds, 5.0f, btn.on ? accent.withAlpha(0.52f) : (btn.hov ? NUIColor(1, 1, 1, 0.06f) : NUIColor(0, 0, 0, 0)));
            renderer.strokeRoundedRect(btn.bounds, 5.0f, 1.0f, btn.on ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.14f));
        }
        auto drawBtnIcon = [&](NUIIcon* icon, const BtnInfo& btn) {
            constexpr float iconSize = 10.0f;
            constexpr float fontSize = 7.5f;
            constexpr float gap = 4.0f;
            const float labelWidth = renderer.measureText(btn.label, fontSize).width;
            const float groupX = btn.bounds.center().x - (iconSize + gap + labelWidth) * 0.5f;
            icon->setBounds({std::round(groupX), std::round(btn.bounds.center().y - iconSize * 0.5f),
                             iconSize, iconSize});
            const NUIColor color(1, 1, 1, btn.on ? 0.95f : (btn.hov ? 0.82f : 0.55f));
            icon->setColor(color);
            icon->onRender(renderer);
            renderer.drawText(btn.label, {groupX + iconSize + gap,
                                           std::round(renderer.calculateTextY(btn.bounds, fontSize))},
                              fontSize, color);
        };
        drawBtnIcon(bypassIcon.get(), btns[0]);
        drawBtnIcon(freezeIcon.get(), btns[1]);
        drawBtnIcon(saveIcon.get(), btns[2]);
        drawBtnIcon(m_mixLocked ? lockIcon.get() : unlockIcon.get(), btns[3]);

        // Nav and A/B buttons — SVG chevrons for nav, text for A/B
        static const char* kChevronLeftSvg = R"svg(<svg viewBox="0 0 10 10" fill="none"><path d="M6.5 2L3.5 5L6.5 8" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" stroke-linejoin="round"/></svg>)svg";
        static const char* kChevronRightSvg = R"svg(<svg viewBox="0 0 10 10" fill="none"><path d="M3.5 2L6.5 5L3.5 8" stroke="currentColor" stroke-width="1.1" stroke-linecap="round" stroke-linejoin="round"/></svg>)svg";
        static auto chevLeft = std::make_shared<NUIIcon>(kChevronLeftSvg);
        static auto chevRight = std::make_shared<NUIIcon>(kChevronRightSvg);
        {
            const auto& r = m_navPrevBounds;
            const bool hov = m_navPrevHovered;
            renderer.fillRoundedRect(r, 5.0f, hov ? NUIColor(1, 1, 1, 0.06f) : NUIColor(0, 0, 0, 0));
            renderer.strokeRoundedRect(r, 5.0f, 1.0f, hov ? accent.withAlpha(0.4f) : NUIColor(1, 1, 1, 0.14f));
            const float iconSize = 10.0f;
            chevLeft->setBounds({std::round(r.center().x - iconSize * 0.5f), std::round(r.center().y - iconSize * 0.5f), iconSize, iconSize});
            chevLeft->setColor(NUIColor(1, 1, 1, hov ? 0.82f : 0.55f));
            chevLeft->onRender(renderer);
        }
        {
            const auto& r = m_navNextBounds;
            const bool hov = m_navNextHovered;
            renderer.fillRoundedRect(r, 5.0f, hov ? NUIColor(1, 1, 1, 0.06f) : NUIColor(0, 0, 0, 0));
            renderer.strokeRoundedRect(r, 5.0f, 1.0f, hov ? accent.withAlpha(0.4f) : NUIColor(1, 1, 1, 0.14f));
            const float iconSize = 10.0f;
            chevRight->setBounds({std::round(r.center().x - iconSize * 0.5f), std::round(r.center().y - iconSize * 0.5f), iconSize, iconSize});
            chevRight->setColor(NUIColor(1, 1, 1, hov ? 0.82f : 0.55f));
            chevRight->onRender(renderer);
        }
        const NUIRect abBounds[] = {m_abBoundsA, m_abBoundsB};
        const char* abLabels[] = {"A", "B"};
        for (int i = 0; i < 2; ++i) {
            const auto& r = abBounds[i];
            bool on = (i == 0) ? (m_activeAB == 1) : (m_activeAB == 2);
            bool hov = (i == 0) ? m_abHoveredA : m_abHoveredB;
            renderer.fillRoundedRect(r, 5.0f, on ? accent.withAlpha(0.52f) : (hov ? NUIColor(1, 1, 1, 0.06f) : NUIColor(0, 0, 0, 0)));
            renderer.strokeRoundedRect(r, 5.0f, 1.0f, on ? accent.withAlpha(0.65f) : NUIColor(1, 1, 1, 0.14f));
            renderer.drawTextCentered(abLabels[i], r, 8.5f, NUIColor(1, 1, 1, on ? 0.95f : (hov ? 0.82f : 0.55f)));
        }

        // Tooltips for hovered utility buttons
        for (auto& btn : btns) {
            if (btn.hov && btn.tip) {
                const NUIRect tipBounds(btn.bounds.x, btn.bounds.y - 18.0f, btn.bounds.width, 16.0f);
                renderer.fillRoundedRect(tipBounds, 3.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.92f));
                renderer.drawTextCentered(btn.tip, tipBounds, 8.0f, NUIColor(1, 1, 1, 0.78f));
            }
        }
    }
    if (m_hoveredPreset >= 0 && m_hoveredPreset < static_cast<int>(m_presets.size())) {
        const auto& tip = m_presets[static_cast<size_t>(m_hoveredPreset)];
        if (!tip.tooltip.empty()) {
            const NUIRect tipBounds(tip.bounds.x, tip.bounds.bottom() + 4.0f, tip.bounds.width, 20.0f);
            renderer.fillRoundedRect(tipBounds, 4.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.92f));
            renderer.drawText(tip.tooltip, {tip.bounds.x + 6.0f, tip.bounds.bottom() + 8.0f}, 8.5f,
                              NUIColor(1, 1, 1, 0.78f));
        }
    }
    drawModeDropdown(renderer, accent);
}

void AestraVerbEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    if (auto* parent = getParent()) {
        const auto parentBounds = parent->getBounds();
        const bool parentChanged = !m_haveParentSnapshot ||
            std::abs(parentBounds.x - m_lastParentBounds.x) > 0.5f ||
            std::abs(parentBounds.y - m_lastParentBounds.y) > 0.5f ||
            std::abs(parentBounds.width - m_lastParentBounds.width) > 0.5f ||
            std::abs(parentBounds.height - m_lastParentBounds.height) > 0.5f;
        if (parentChanged && !isDraggingWindow()) {
            m_lastParentBounds = parentBounds;
            m_haveParentSnapshot = true;
            enforceBoundsInParent(!m_userPositioned);
            layoutControls();
        } else if (!m_haveParentSnapshot) {
            m_lastParentBounds = parentBounds;
            m_haveParentSnapshot = true;
        }
    }
    syncCategoryFromMode();
}

int AestraVerbEditor::hitTestCategory(float x, float y) const {
    for (size_t i = 0; i < m_categoryPills.size(); ++i) {
        if (m_categoryPills[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

int AestraVerbEditor::hitTestDropdown(float x, float y) const {
    if (m_dropdownButtonBounds.contains({x, y})) return -2;
    if (m_dropdownOpen) {
        for (size_t i = 0; i < m_dropdownItems.size(); ++i) {
            if (m_dropdownItems[i].bounds.contains({x, y})) return static_cast<int>(i);
        }
    }
    return -1;
}

int AestraVerbEditor::hitTestPreset(float x, float y) const {
    for (size_t i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraVerbEditor::hitTestMix(float x, float y) const { return m_mixBounds.contains({x, y}); }
bool AestraVerbEditor::hitTestBypass(float x, float y) const { return m_bypassBounds.contains({x, y}); }
bool AestraVerbEditor::hitTestFreeze(float x, float y) const { return m_freezeBounds.contains({x, y}); }

bool AestraVerbEditor::hitTestPresetNav(float x, float y, int& direction) const {
    if (m_navPrevBounds.contains({x, y})) { direction = -1; return true; }
    if (m_navNextBounds.contains({x, y})) { direction = 1; return true; }
    return false;
}

bool AestraVerbEditor::hitTestAB(float x, float y, bool& isA) const {
    if (m_abBoundsA.contains({x, y})) { isA = true; return true; }
    if (m_abBoundsB.contains({x, y})) { isA = false; return true; }
    return false;
}

bool AestraVerbEditor::hitTestMixLock(float x, float y) const { return m_mixLockBounds.contains({x, y}); }

void AestraVerbEditor::updateParameter(uint32_t paramId, float v) {
    if (!m_instance) return;
    m_instance->setParameter(paramId, std::clamp(v, 0.0f, 1.0f));
    for (auto& k : m_knobs) {
        if (k.paramId == paramId && k.slider) {
            k.slider->setValue(getParamValue(paramId));
            break;
        }
    }
    setDirty(true);
}

void AestraVerbEditor::applyPreset(const PresetButton& preset) {
    const float mix = m_mixLocked ? getParamValue(kMix) : preset.mix;
    updateParameter(kMode, static_cast<float>(preset.mode) / static_cast<float>(kModeCount - 1));
    updateParameter(kSize, preset.size);
    updateParameter(kDecay, preset.decay);
    updateParameter(kDamping, preset.damping);
    updateParameter(kDiffusion, preset.diffusion);
    updateParameter(kModRate, preset.modRate);
    updateParameter(kModDepth, preset.modDepth);
    updateParameter(kWidth, preset.width);
    if (!m_mixLocked) updateParameter(kMix, mix);
    updateParameter(kAttack, preset.attack);
    updateParameter(kShape, preset.shape);
    updateParameter(kPredelaySync, static_cast<float>(preset.predelaySync) / 6.0f);
    updateParameter(kModCharacter, static_cast<float>(preset.modCharacter) / 2.0f);
}

void AestraVerbEditor::loadUserPresets() {
    m_userPresets.clear();
    const std::string dir = getUserPresetDir();
    namespace fs = std::filesystem;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".aeverb") continue;
        const std::string json = Aestra::File::readAllText(entry.path().string());
        if (json.empty()) continue;
        auto parsed = Aestra::JSON::parse(json);
        if (!parsed.isObject()) continue;
        PresetButton p;
        p.label = parsed.has("name") ? parsed["name"].asString() : entry.path().stem().string();
        p.tooltip = parsed.has("tooltip") ? parsed["tooltip"].asString() : "User preset";
        p.mode = parsed.has("mode") ? parsed["mode"].asInt() : 0;
        p.size = parsed.has("size") ? static_cast<float>(parsed["size"].asNumber()) : 0.5f;
        p.decay = parsed.has("decay") ? static_cast<float>(parsed["decay"].asNumber()) : 0.5f;
        p.damping = parsed.has("damping") ? static_cast<float>(parsed["damping"].asNumber()) : 0.5f;
        p.diffusion = parsed.has("diffusion") ? static_cast<float>(parsed["diffusion"].asNumber()) : 0.7f;
        p.modRate = parsed.has("modRate") ? static_cast<float>(parsed["modRate"].asNumber()) : 0.4f;
        // Legacy fallback stays 0.14 (the pre-retune default) so an old user
        // preset saved without a modDepth field loads exactly as it sounded when
        // created. New presets persist modDepth explicitly; only fresh instances
        // use the calmer 0.07 default.
        p.modDepth = parsed.has("modDepth") ? static_cast<float>(parsed["modDepth"].asNumber()) : 0.14f;
        p.width = parsed.has("width") ? static_cast<float>(parsed["width"].asNumber()) : 0.68f;
        p.mix = parsed.has("mix") ? static_cast<float>(parsed["mix"].asNumber()) : 0.36f;
        p.attack = parsed.has("attack") ? static_cast<float>(parsed["attack"].asNumber()) : 0.0f;
        p.shape = parsed.has("shape") ? static_cast<float>(parsed["shape"].asNumber()) : 0.5f;
        p.predelaySync = parsed.has("predelaySync") ? parsed["predelaySync"].asInt() : 0;
        p.modCharacter = parsed.has("modCharacter") ? parsed["modCharacter"].asInt() : 0;
        m_userPresets.push_back(p);
    }
}

void AestraVerbEditor::saveUserPreset(const std::string& name) {
    const std::string dir = getUserPresetDir();
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) fs::create_directories(dir);
    Aestra::JSON obj = Aestra::JSON::object();
    obj.set("name", Aestra::JSON(name));
    obj.set("tooltip", Aestra::JSON("User preset"));
    obj.set("mode", Aestra::JSON(static_cast<double>(std::round(getParamValue(kMode) * static_cast<float>(kModeCount - 1)))));
    obj.set("size", Aestra::JSON(static_cast<double>(getParamValue(kSize))));
    obj.set("decay", Aestra::JSON(static_cast<double>(getParamValue(kDecay))));
    obj.set("damping", Aestra::JSON(static_cast<double>(getParamValue(kDamping))));
    obj.set("diffusion", Aestra::JSON(static_cast<double>(getParamValue(kDiffusion))));
    obj.set("modRate", Aestra::JSON(static_cast<double>(getParamValue(kModRate))));
    obj.set("modDepth", Aestra::JSON(static_cast<double>(getParamValue(kModDepth))));
    obj.set("width", Aestra::JSON(static_cast<double>(getParamValue(kWidth))));
    obj.set("mix", Aestra::JSON(static_cast<double>(getParamValue(kMix))));
    obj.set("attack", Aestra::JSON(static_cast<double>(getParamValue(kAttack))));
    obj.set("shape", Aestra::JSON(static_cast<double>(getParamValue(kShape))));
    obj.set("predelaySync", Aestra::JSON(static_cast<double>(std::round(getParamValue(kPredelaySync) * 6.0f))));
    obj.set("modCharacter", Aestra::JSON(static_cast<double>(std::round(getParamValue(kModCharacter) * 2.0f))));
    const std::string path = dir + "/" + name + ".aeverb";
    Aestra::File::writeAllText(path, obj.toString(2));
    loadUserPresets();
}

void AestraVerbEditor::deleteUserPreset(const std::string& name) {
    const std::string path = getUserPresetDir() + "/" + name + ".aeverb";
    namespace fs = std::filesystem;
    if (fs::exists(path)) fs::remove(path);
    loadUserPresets();
}

std::string AestraVerbEditor::getUserPresetDir() const {
    return std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/Documents/Aestra/presets/AestraVerb";
}

float AestraVerbEditor::paramDefault(uint32_t paramId) {
    switch (paramId) {
    case kDecay: return 0.56f; case kDamping: return 0.50f; case kPredelay: return 0.02f;
    case kWidth: return 0.68f; case kMix: return 0.36f; case kSize: return 0.52f;
    case kDiffusion: return 0.64f; case kModRate: return 0.42f; case kModDepth: return 0.07f;
    case kLowCut: return 0.0f; case kHighCut: return 1.0f; case kFreeze: return 0.0f;
    case kAttack: return 0.0f; case kShape: return 0.5f;
    case kPredelaySync: return 0.0f; case kModCharacter: return 0.0f;
    default: return 0.0f;
    }
}

std::string AestraVerbEditor::parameterTooltip(uint32_t paramId) const {
    switch (paramId) {
    case kDecay: return "Reverb tail length. Higher = longer decay.";
    case kDamping: return "High-frequency damping. Higher = darker tail.";
    case kPredelay: return "Pre-reverb delay. Separates dry from wet.";
    case kWidth: return "Stereo width. 100% = full stereo, 0% = mono.";
    case kMix: return "Dry/wet balance.";
    case kSize: return "Room size multiplier. Affects delay line lengths.";
    case kDiffusion: return "Early echo density. Higher = smoother texture.";
    case kModRate: return "Modulation speed. Higher = faster pitch wobble.";
    case kModDepth: return "Modulation depth. Higher = more pitch variation.";
    case kLowCut: return "Post-reverb low cut (20-2000 Hz). Removes rumble from wet signal.";
    case kHighCut: return "Post-reverb high cut (200-20000 Hz). Tames brightness of wet signal.";
    case kFreeze: return "Freeze: sustains current reverb tail infinitely.";
    case kAttack: return "Early-to-late balance. 0% = late-dominant, 100% = early-dominant.";
    case kShape: return "Attack envelope shape. 0% = soft onset, 100% = hard/sharp.";
    case kPredelaySync: return "Tempo-synced predelay divisions (requires host BPM).";
    case kModCharacter: return "Modulation character: Random (smooth), Chorus (pitched), Chaotic (wow-and-flutter).";
    default: return "";
    }
}

std::string AestraVerbEditor::presetTooltip(const PresetButton& preset) const {
    return preset.tooltip;
}

std::string AestraVerbEditor::modeTooltip(int mode) const {
    switch (mode) {
    case 0: return "Room: Short, intimate space. Dry early reflections, tight tail.";
    case 1: return "Hall: Large concert hall. Long lush tail, dense diffusion.";
    case 2: return "Plate: Metallic plate reverb. Bright, smooth, dense character.";
    case 3: return "Cathedral: Vast stone cathedral. Extremely long, dense reverb.";
    case 4: return "Chamber: Medium recording chamber. Balanced warmth and density.";
    case 5: return "Bright Hall: Bright concert hall. Shimmering, lively reflections.";
    case 6: return "Ambience: Very short, subtle space. Near-dry atmosphere and texture.";
    case 7: return "Scoring: Scoring stage ambience. Cinematic depth, orchestral reverb.";
    case 8: return "Smooth Plate: Silky smooth plate variant. Gentle, unobtrusive character.";
    default: return "";
    }
}

bool AestraVerbEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    // Forward events to knob sliders (AestraPanelWindow doesn't dispatch to children)
    // Always forward drag/release to the actively-dragging slider so it can stop
    for (auto& k : m_knobs) {
        if (!k.slider) continue;
        const auto sb = k.slider->getBounds();
        const bool hasBounds = sb.width >= 16.0f && sb.height >= 16.0f;
        if (!hasBounds) continue;
        const bool overKnob = sb.contains({event.position.x, event.position.y});
        const bool dragging = k.slider->isDragging();
        // An open dropdown is a modal input layer. Keep forwarding an active
        // drag so it can release cleanly, but do not let new presses reach a
        // knob underneath the dropdown list.
        if ((overKnob && !m_dropdownOpen) || dragging) {
            if (k.slider->onMouseEvent(event)) return true;
        }
    }
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    auto b = getBounds();
    const bool contains = b.contains(event.position);
    auto b2 = getBounds();
    const float contentX = editorContentX(b2);
    const float contentW = b2.width - (contentX - b2.x) - kPad;
    const float mainY = b2.y + 128.0f;
    const auto updateHoverState = [&]() {
        const int ch = contains ? hitTestCategory(event.position.x, event.position.y) : -1;
        const int ddh = contains ? hitTestDropdown(event.position.x, event.position.y) : -1;
        const int ph = contains ? hitTestPreset(event.position.x, event.position.y) : -1;
        const bool mixHovered = contains && hitTestMix(event.position.x, event.position.y);
        const bool bypassHovered = contains && hitTestBypass(event.position.x, event.position.y);
        const bool freezeHovered = contains && hitTestFreeze(event.position.x, event.position.y);
        int navDir = 0;
        const bool navHovered = contains && hitTestPresetNav(event.position.x, event.position.y, navDir);
        const bool navPrevH = navHovered && navDir == -1;
        const bool navNextH = navHovered && navDir == 1;
        const bool saveHovered = contains && event.position.x >= m_saveBounds.x && event.position.x <= m_saveBounds.right() &&
                                 event.position.y >= m_saveBounds.y && event.position.y <= m_saveBounds.bottom();
        bool isA = false;
        const bool abHovered = contains && hitTestAB(event.position.x, event.position.y, isA);
        const bool abHA = abHovered && isA;
        const bool abHB = abHovered && !isA;
        const bool mixLockH = contains && hitTestMixLock(event.position.x, event.position.y);
        if (ch == m_hoveredCategory && ddh == m_hoveredDropdownItem && ph == m_hoveredPreset && mixHovered == m_mixHovered &&
            bypassHovered == m_bypassHovered && freezeHovered == m_freezeHovered &&
            navPrevH == m_navPrevHovered && navNextH == m_navNextHovered &&
            saveHovered == m_saveHovered && abHA == m_abHoveredA && abHB == m_abHoveredB &&
            mixLockH == m_mixLockHovered) return;
        m_hoveredCategory = ch;
        m_hoveredDropdownItem = ddh;
        m_hoveredPreset = ph;
        m_mixHovered = mixHovered;
        m_bypassHovered = bypassHovered;
        m_freezeHovered = freezeHovered;
        m_navPrevHovered = navPrevH;
        m_navNextHovered = navNextH;
        m_saveHovered = saveHovered;
        m_abHoveredA = abHA;
        m_abHoveredB = abHB;
        m_mixLockHovered = mixLockH;
        for (size_t i = 0; i < m_categoryPills.size(); ++i) m_categoryPills[i].hovered = (static_cast<int>(i) == ch);
        for (size_t i = 0; i < m_dropdownItems.size(); ++i) m_dropdownItems[i].hovered = (static_cast<int>(i) == ddh);
        for (size_t i = 0; i < m_presets.size(); ++i) m_presets[i].hovered = (static_cast<int>(i) == ph);
        setDirty(true);
    };
    if (!isDraggingWindow() && !m_draggingMix) updateHoverState();
    if (event.released) {
        if (m_pressedCategory != -1 || m_pressedPreset != -1) {
            m_pressedCategory = -1;
            m_pressedPreset = -1;
            setDirty(true);
        }
    }
    if (!contains && !isDraggingWindow() && !m_draggingMix) {
        if (m_hoveredCategory != -1 || m_hoveredDropdownItem != -1 || m_hoveredPreset != -1 || m_mixHovered ||
            m_bypassHovered || m_freezeHovered || m_navPrevHovered || m_navNextHovered ||
            m_saveHovered || m_abHoveredA || m_abHoveredB || m_mixLockHovered) {
            m_hoveredCategory = -1;
            m_hoveredDropdownItem = -1;
            m_hoveredPreset = -1;
            m_mixHovered = false;
            m_bypassHovered = false;
            m_freezeHovered = false;
            m_navPrevHovered = false;
            m_navNextHovered = false;
            m_saveHovered = false;
            m_abHoveredA = false;
            m_abHoveredB = false;
            m_mixLockHovered = false;
            for (auto& pill : m_categoryPills) pill.hovered = false;
            for (auto& item : m_dropdownItems) item.hovered = false;
            for (auto& preset : m_presets) preset.hovered = false;
            setDirty(true);
        }
        if (m_dropdownOpen) {
            m_dropdownOpen = false;
            setDirty(true);
        }
        return false;
    }
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Category pill click
        const int catIdx = hitTestCategory(event.position.x, event.position.y);
        if (catIdx >= 0 && catIdx < kCategoryCount && m_categoryPills[catIdx].enabled) {
            m_pressedCategory = catIdx;
            m_focusedCategory = catIdx;
            m_focusedPreset = -1;
            m_mixFocused = false;
            m_selectedCategory = catIdx;
            m_presetScroll = 0;
            m_dropdownItems.clear();
            int modes[3] = {};
            const int count = modesInCategory(catIdx, modes);
            for (int i = 0; i < count; ++i) {
                static const char* modeNames[] = {
                    "Room", "Hall", "Plate", "Cathedral", "Chamber",
                    "Bright Hall", "Ambience", "Scoring", "Smooth Plate"
                };
                m_dropdownItems.push_back({modeNames[modes[i]], modes[i], {}, false});
            }
            // Select first mode in category
            if (count > 0) {
                updateParameter(kMode, static_cast<float>(modes[0]) / static_cast<float>(kModeCount - 1));
            }
            m_dropdownOpen = true;
            layoutControls();
            setDirty(true);
            return true;
        }
        // Dropdown interaction
        const int ddIdx = hitTestDropdown(event.position.x, event.position.y);
        if (ddIdx == -2) {
            // Clicked dropdown button — toggle open (only if there are items)
            if (!m_dropdownItems.empty()) m_dropdownOpen = !m_dropdownOpen;
            setDirty(true);
            return true;
        }
        if (ddIdx >= 0 && m_dropdownOpen && ddIdx < static_cast<int>(m_dropdownItems.size())) {
            // Clicked dropdown item
            const auto& item = m_dropdownItems[ddIdx];
            updateParameter(kMode, static_cast<float>(item.mode) / static_cast<float>(kModeCount - 1));
            m_dropdownOpen = false;
            setDirty(true);
            return true;
        }
        if (m_dropdownOpen && !m_dropdownButtonBounds.contains({event.position.x, event.position.y}) &&
            !m_dropdownListBounds.contains({event.position.x, event.position.y})) {
            m_dropdownOpen = false;
            setDirty(true);
            return true;
        }
        // Preset click
        const int presetIdx = hitTestPreset(event.position.x, event.position.y);
        if (presetIdx >= 0) {
            m_pressedPreset = presetIdx;
            m_focusedPreset = presetIdx;
            m_focusedCategory = -1;
            m_mixFocused = false;
            applyPreset(m_presets[static_cast<size_t>(presetIdx)]);
            return true;
        }
        if (hitTestMix(event.position.x, event.position.y)) {
            m_draggingMix = true;
            m_mixFocused = true;
            m_focusedCategory = -1;
            m_focusedPreset = -1;
            updateParameter(kMix, (event.position.x - m_mixTrack.x) / std::max(1.0f, m_mixTrack.width));
            return true;
        }
        if (hitTestBypass(event.position.x, event.position.y)) {
            updateParameter(kBypass, getParamValue(kBypass) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (hitTestFreeze(event.position.x, event.position.y)) {
            updateParameter(kFreeze, getParamValue(kFreeze) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        int navDir = 0;
        if (hitTestPresetNav(event.position.x, event.position.y, navDir)) {
            m_presetScroll = std::clamp(m_presetScroll + navDir, 0, maxPresetScroll());
            layoutControls();
            setDirty(true);
            return true;
        }
        bool isA = false;
        if (hitTestAB(event.position.x, event.position.y, isA)) {
            if (isA) {
                if (m_activeAB == 1) {
                    for (uint32_t i = 0; i < kParamCount; ++i) updateParameter(i, m_abA.params[i]);
                    m_activeAB = 0;
                } else {
                    for (uint32_t i = 0; i < kParamCount; ++i) m_abA.params[i] = getParamValue(i);
                    m_abA.valid = true;
                    m_activeAB = 1;
                }
            } else {
                if (m_activeAB == 2) {
                    for (uint32_t i = 0; i < kParamCount; ++i) updateParameter(i, m_abB.params[i]);
                    m_activeAB = 0;
                } else {
                    for (uint32_t i = 0; i < kParamCount; ++i) m_abB.params[i] = getParamValue(i);
                    m_abB.valid = true;
                    m_activeAB = 2;
                }
            }
            setDirty(true);
            return true;
        }
        if (hitTestMixLock(event.position.x, event.position.y)) {
            m_mixLocked = !m_mixLocked;
            setDirty(true);
            return true;
        }
        if (m_saveHovered) {
            saveUserPreset("User " + std::to_string(m_userPresets.size() + 1));
            return true;
        }
    }
    if (event.pressed && event.button == NUIMouseButton::Left && event.doubleClick) {
        for (auto& k : m_knobs) {
            if (k.slider && k.slider->getBounds().contains({event.position.x, event.position.y})) {
                updateParameter(k.paramId, k.defaultValue);
                if (k.slider) k.slider->setValue(k.defaultValue);
                setDirty(true);
                return true;
            }
        }
    }
    if (std::abs(event.wheelDelta) > 0.001f && contains) {
        const auto b3 = getBounds();
        const float stripLeft = b3.x + kPad - 5.0f;
        const float stripRight = b3.x + kPad + presetColumnWidth(b3.width) + 5.0f;
        const float stripTop = b3.y + 54.0f;
        const float stripBottom = stripTop + b3.height - 54.0f;
        if (event.position.x >= stripLeft && event.position.x <= stripRight &&
            event.position.y >= stripTop && event.position.y <= stripBottom) {
            const int prev = m_presetScroll;
            if (event.wheelDelta > 0.0f) {
                m_presetScroll = std::max(0, m_presetScroll - 1);
            } else {
                m_presetScroll = std::min(maxPresetScroll(), m_presetScroll + 1);
            }
            if (m_presetScroll != prev) {
                layoutControls();
                setDirty(true);
            }
            return true;
        }
    }
    if (m_draggingMix) {
        if (event.released && event.button == NUIMouseButton::Left) {
            m_draggingMix = false;
            setDirty(true);
            return true;
        }
        updateParameter(kMix, (event.position.x - m_mixTrack.x) / std::max(1.0f, m_mixTrack.width));
        return true;
    }
    if (!event.pressed && !event.released) updateHoverState();
    return contains;
}

} // namespace AestraUI
