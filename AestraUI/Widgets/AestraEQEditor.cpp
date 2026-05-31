// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraEQEditor.h"

#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;

NUIColor accent() { return NUIColor(0.55f, 0.40f, 0.92f, 1.0f); }
NUIColor accentSoft() { return NUIColor(0.55f, 0.40f, 0.92f, 0.35f); }
NUIColor graphBg() { return NUIColor(0.045f, 0.043f, 0.064f, 0.96f); }

std::shared_ptr<NUIIcon> makeSvgIcon(const char* svg) {
    return std::make_shared<NUIIcon>(svg);
}

void drawSvgIcon(NUIRenderer& renderer, const std::shared_ptr<NUIIcon>& icon, const NUIRect& button,
                 const NUIColor& color, float size = 13.0f) {
    if (!icon)
        return;
    const NUIRect iconRect{std::round(button.center().x - size * 0.5f), std::round(button.center().y - size * 0.5f),
                           size, size};
    icon->setBounds(iconRect);
    icon->setColor(color);
    icon->onRender(renderer);
}

float topPillTextY(NUIRenderer& renderer, const NUIRect& rect, float fontSize) {
    return std::round(renderer.calculateTextY(rect, fontSize) - 2.0f);
}

std::shared_ptr<NUIIcon> eqTypePrevIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 16h4.5c2 0 2.4-8 5.5-8H20"/>
            <path d="M7 19l-3-3 3-3"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> eqTypeNextIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 8h6c3.1 0 3.5 8 5.5 8H20"/>
            <path d="M17 13l3 3-3 3"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> headphonesIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 13v-1a7 7 0 0 1 14 0v1"/>
            <rect x="3.5" y="12" width="4" height="7" rx="2"/>
            <rect x="16.5" y="12" width="4" height="7" rx="2"/>
            <path d="M9 20c1.7.8 4.3.8 6 0"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> duplicateIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <rect x="9" y="7" width="10" height="10" rx="2"/>
            <path d="M5 15V7a2 2 0 0 1 2-2h8"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronUpIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 15l6-6 6 6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronDownIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 9l6 6 6-6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronLeftIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M15 6l-6 6 6 6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronRightIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M9 6l6 6-6 6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> removeIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.3"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M7 7l10 10"/>
            <path d="M17 7L7 17"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> powerIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 3v8"/>
            <path d="M7.1 6.6a8 8 0 1 0 9.8 0"/>
            <path d="M8.5 15.2c1.9 1.4 5.1 1.4 7 0"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> bypassIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1"
             stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="7"/>
            <path d="M7 17L17 7"/>
            <path d="M9.5 8.5c1.9-.9 4.3-.5 5.8 1"/>
            <path d="M14.5 15.5c-1.9.9-4.3.5-5.8-1"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> outputIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 17h3l4 3V4L7 7H4z"/>
            <path d="M15 8c1.2 1 1.2 7 0 8"/>
            <path d="M18 5c2.5 2.8 2.5 11.2 0 14"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> polarityIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="6.5"/>
            <path d="M7 17L17 7"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> compareSlotIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <rect x="5" y="5" width="14" height="14" rx="4"/>
            <path d="M8 13h8"/>
            <path d="M9 9h6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> compareCopyIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 7h10"/>
            <path d="M12 4l3 3-3 3"/>
            <path d="M19 17H9"/>
            <path d="M12 14l-3 3 3 3"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> analyzerIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 17V9"/>
            <path d="M8 17V6"/>
            <path d="M12 17v-4"/>
            <path d="M16 17V8"/>
            <path d="M20 17v-7"/>
            <path d="M3 19h18"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> rangeIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 4v16"/>
            <path d="M8 8l4-4 4 4"/>
            <path d="M8 16l4 4 4-4"/>
            <path d="M5 12h14"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> tiltIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 16l16-8"/>
            <path d="M5 19h14"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> sourceIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 7h9"/>
            <path d="M11 4l3 3-3 3"/>
            <path d="M19 17h-9"/>
            <path d="M13 14l-3 3 3 3"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> decayIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 6h4l3 12 3-8 2 4h4"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> freezeIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 3v18"/>
            <path d="M5 7l14 10"/>
            <path d="M19 7L5 17"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> maskIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 16c2.4-6 5.6-6 8 0s5.6 6 8 0"/>
            <path d="M4 8c2.4 6 5.6 6 8 0s5.6-6 8 0"/>
            <circle cx="12" cy="12" r="2"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> strengthIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 18V12"/>
            <path d="M12 18V7"/>
            <path d="M19 18V4"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> resetIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 11a7 7 0 1 1 2 5"/>
            <path d="M5 5v6h6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> invertIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 8c2.2-4 5.8-4 8 0s5.8 4 8 0"/>
            <path d="M4 16c2.2 4 5.8 4 8 0s5.8-4 8 0"/>
            <path d="M12 4v16"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> dynamicIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M4 16c2-8 5-8 8 0s6 6 8 0"/>
            <path d="M12 5v14"/>
            <path d="M9 8l3-3 3 3"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> splitLRIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 5v14"/>
            <path d="M19 5v14"/>
            <path d="M9 8l-4 4 4 4"/>
            <path d="M15 8l4 4-4 4"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> splitMSIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M12 4v16"/>
            <path d="M5 8c4 2 10 2 14 0"/>
            <path d="M5 16c4-2 10-2 14 0"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> copyIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <rect x="9" y="7" width="10" height="10" rx="2"/>
            <path d="M5 15V7a2 2 0 0 1 2-2h8"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> pasteIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M8 5h8"/>
            <rect x="6" y="7" width="12" height="13" rx="2"/>
            <path d="M10 3h4l1 2H9z"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> clearAllIcon() {
    static auto icon = makeSvgIcon(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 7h14"/>
            <path d="M9 7V5h6v2"/>
            <path d="M8 10l1 9h6l1-9"/>
            <path d="M10 12l4 4"/>
            <path d="M14 12l-4 4"/>
        </svg>
    )svg");
    return icon;
}

NUIColor bandColor(size_t i) {
    static const NUIColor colors[] = {
        NUIColor(0.376f, 0.647f, 0.980f, 1.0f), // HP #60a5fa
        NUIColor(0.204f, 0.827f, 0.600f, 1.0f), // LS #34d399
        NUIColor(0.655f, 0.545f, 0.980f, 1.0f), // B1 #a78bfa
        NUIColor(0.910f, 0.475f, 0.976f, 1.0f), // B2 #e879f9
        NUIColor(0.984f, 0.573f, 0.235f, 1.0f), // HS #fb923c
        NUIColor(0.973f, 0.443f, 0.443f, 1.0f), // LP #f87171
    };
    return colors[i % 6];
}

const char* bandIdLabel(size_t i) {
    static constexpr const char* labels[] = {"HP", "LS", "B1", "B2", "HS", "LP"};
    return labels[i % 6];
}

float bandFreqHz(size_t idx, float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    switch (idx) {
    case 0:
        return 20.0f * std::pow(25.0f, norm); // HP   20-500
    case 1:
        return 40.0f * std::pow(25.0f, norm); // LS   40-1k
    case 2:
        return 80.0f * std::pow(100.0f, norm); // B1   80-8k
    case 3:
        return 200.0f * std::pow(80.0f, norm); // B2   200-16k
    case 4:
        return 2000.0f * std::pow(10.0f, norm); // HS   2k-20k
    case 5:
        return 1000.0f * std::pow(20.0f, norm); // LP   1k-20k
    default:
        return 1000.0f;
    }
}

float bandNormFromHz(size_t idx, float hz) {
    switch (idx) {
    case 0:
        return std::clamp(std::log10(hz / 20.0f) / std::log10(25.0f), 0.0f, 1.0f);
    case 1:
        return std::clamp(std::log10(hz / 40.0f) / std::log10(25.0f), 0.0f, 1.0f);
    case 2:
        return std::clamp(std::log10(hz / 80.0f) / std::log10(100.0f), 0.0f, 1.0f);
    case 3:
        return std::clamp(std::log10(hz / 200.0f) / std::log10(80.0f), 0.0f, 1.0f);
    case 4:
        return std::clamp(std::log10(hz / 2000.0f) / std::log10(10.0f), 0.0f, 1.0f);
    case 5:
        return std::clamp(std::log10(hz / 1000.0f) / std::log10(20.0f), 0.0f, 1.0f);
    default:
        return 0.5f;
    }
}

float graphFreqHz(float norm) {
    return 20.0f * std::pow(1000.0f, std::clamp(norm, 0.0f, 1.0f));
}

float graphNormFromHz(float hz) {
    return std::clamp(std::log10(std::max(hz, 20.0f) / 20.0f) / 3.0f, 0.0f, 1.0f);
}

std::string formatFrequencyWithUnit(size_t idx, float norm) {
    const float hz = bandFreqHz(idx, norm);
    char buf[32];
    if (hz >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
    } else {
        std::snprintf(buf, sizeof(buf), "%d Hz", static_cast<int>(hz + 0.5f));
    }
    return buf;
}

std::string formatDynamicFrequencyWithUnit(float norm) {
    const float hz = graphFreqHz(norm);
    char buf[32];
    if (hz >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
    } else {
        std::snprintf(buf, sizeof(buf), "%d Hz", static_cast<int>(hz + 0.5f));
    }
    return buf;
}

uint32_t slopeDbFromNorm(float norm) {
    static constexpr uint32_t db[] = {6, 12, 24, 36, 48, 72, 96};
    const int idx = std::clamp(static_cast<int>(std::round(std::clamp(norm, 0.0f, 1.0f) * 6.0f)), 0, 6);
    return db[idx];
}

float slopeNormFromDb(float db) {
    static constexpr float slopes[] = {6.0f, 12.0f, 24.0f, 36.0f, 48.0f, 72.0f, 96.0f};
    int best = 0;
    float bestD = std::abs(db - slopes[0]);
    for (int i = 1; i < 7; ++i) {
        const float d = std::abs(db - slopes[i]);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return static_cast<float>(best) / 6.0f;
}

bool parseFloatPrefix(const std::string& text, float& out) {
    const char* start = text.c_str();
    char* end = nullptr;
    out = std::strtof(start, &end);
    return end != start;
}

float quantizeTypeNorm(float norm) {
    static constexpr float steps[] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 4; ++i) {
        const float d = std::abs(norm - steps[i]);
        if (d < bestD) {
            bestD = d;
            best = steps[i];
        }
    }
    return best;
}

float legacyTypeNorm(Aestra::Audio::Plugins::FilterType type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    switch (type) {
    case FilterType::Bell:
        return 0.0f;
    case FilterType::Notch:
        return 1.0f / 3.0f;
    case FilterType::BandPass:
        return 2.0f / 3.0f;
    case FilterType::Tilt:
        return 1.0f;
    default:
        return 0.0f;
    }
}

const char* middleBandTypeName(float norm) {
    const int idx = std::clamp(static_cast<int>(std::round(quantizeTypeNorm(norm) * 3.0f)), 0, 3);
    static const char* names[] = {"Bell", "Notch", "Band Pass", "Tilt"};
    return names[idx];
}

const char* dynamicBandTypeName(size_t idx) {
    static constexpr const char* names[] = {
        "Low Cut", "Low Shelf", "Bell", "Notch", "Band Pass", "Tilt", "High Shelf", "High Cut",
    };
    return names[std::min(idx, static_cast<size_t>(7))];
}

const char* filterTypeLabel(Aestra::Audio::Plugins::FilterType type, const char* fallback) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    switch (type) {
    case FilterType::Bell:
        return "Bell";
    case FilterType::LowCut:
    case FilterType::HighCut:
        return "Cut";
    case FilterType::LowShelf:
    case FilterType::HighShelf:
        return "Shelf";
    case FilterType::Notch:
        return "Notch";
    case FilterType::BandPass:
        return "Band Pass";
    case FilterType::Tilt:
        return "Tilt";
    }
    return fallback;
}

const char* graphCreateChipLabel(Aestra::Audio::Plugins::FilterType type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    switch (type) {
    case FilterType::LowCut:
        return "+ HP";
    case FilterType::LowShelf:
        return "+ LS";
    case FilterType::Bell:
        return "+ BELL";
    case FilterType::Notch:
        return "+ NOTCH";
    case FilterType::BandPass:
        return "+ BP";
    case FilterType::Tilt:
        return "+ TILT";
    case FilterType::HighShelf:
        return "+ HS";
    case FilterType::HighCut:
        return "+ LP";
    }
    return "+ BAND";
}

std::string bandTypeSuffix(const std::string& typeName) {
    const auto dot = typeName.find("\xC2\xB7");
    if (dot != std::string::npos) {
        auto suffix = typeName.substr(dot + 2);
        while (!suffix.empty() && suffix.front() == ' ') {
            suffix.erase(suffix.begin());
        }
        return suffix;
    }
    const auto space = typeName.find(' ');
    return space != std::string::npos ? typeName.substr(space + 1) : typeName;
}

bool middleBandTypeUsesGain(float norm) {
    const int idx = std::clamp(static_cast<int>(std::round(quantizeTypeNorm(norm) * 3.0f)), 0, 3);
    return idx == 0 || idx == 3;
}

float filterTypeNorm(Aestra::Audio::Plugins::FilterType type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    switch (type) {
    case FilterType::LowCut:
        return 0.0f;
    case FilterType::LowShelf:
        return 1.0f / 7.0f;
    case FilterType::Bell:
        return 2.0f / 7.0f;
    case FilterType::Notch:
        return 3.0f / 7.0f;
    case FilterType::BandPass:
        return 4.0f / 7.0f;
    case FilterType::Tilt:
        return 5.0f / 7.0f;
    case FilterType::HighShelf:
        return 6.0f / 7.0f;
    case FilterType::HighCut:
        return 1.0f;
    default:
        return 2.0f / 7.0f;
    }
}

Aestra::Audio::Plugins::FilterType filterTypeFromNorm(float norm) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    const int idx = std::clamp(static_cast<int>(std::round(std::clamp(norm, 0.0f, 1.0f) * 7.0f)), 0, 7);
    static constexpr FilterType types[] = {
        FilterType::LowCut,   FilterType::LowShelf, FilterType::Bell,      FilterType::Notch,
        FilterType::BandPass, FilterType::Tilt,     FilterType::HighShelf, FilterType::HighCut,
    };
    return types[idx];
}

bool filterTypeUsesSlope(Aestra::Audio::Plugins::FilterType type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    return type == FilterType::LowCut || type == FilterType::HighCut;
}

bool filterTypeUsesGain(Aestra::Audio::Plugins::FilterType type) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    return type == FilterType::Bell || type == FilterType::LowShelf || type == FilterType::HighShelf ||
           type == FilterType::Tilt;
}

float quantizeStereoNorm(float norm) {
    static constexpr float steps[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 5; ++i) {
        const float d = std::abs(norm - steps[i]);
        if (d < bestD) {
            bestD = d;
            best = steps[i];
        }
    }
    return best;
}

const char* stereoModeShortName(float norm) {
    const int idx = std::clamp(static_cast<int>(std::round(quantizeStereoNorm(norm) * 4.0f)), 0, 4);
    static constexpr const char* names[] = {"ST", "L", "R", "M", "S"};
    return names[idx];
}

Aestra::Audio::Plugins::AestraEQ::StereoMode stereoModeFromNorm(float norm) {
    using StereoMode = Aestra::Audio::Plugins::AestraEQ::StereoMode;
    const int idx = std::clamp(static_cast<int>(std::round(quantizeStereoNorm(norm) * 4.0f)), 0, 4);
    static constexpr StereoMode modes[] = {StereoMode::Stereo, StereoMode::Left, StereoMode::Right, StereoMode::Mid,
                                           StereoMode::Side};
    return modes[idx];
}

float stereoNormFromMode(Aestra::Audio::Plugins::AestraEQ::StereoMode mode) {
    using StereoMode = Aestra::Audio::Plugins::AestraEQ::StereoMode;
    switch (mode) {
    case StereoMode::Stereo:
        return 0.0f;
    case StereoMode::Left:
        return 0.25f;
    case StereoMode::Right:
        return 0.5f;
    case StereoMode::Mid:
        return 0.75f;
    case StereoMode::Side:
        return 1.0f;
    }
    return 0.0f;
}

float quantizeSlopeNorm(float norm) {
    static constexpr float steps[] = {0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f};
    float best = steps[0];
    float bestD = std::abs(norm - steps[0]);
    for (int i = 1; i < 7; ++i) {
        float d = std::abs(norm - steps[i]);
        if (d < bestD) {
            bestD = d;
            best = steps[i];
        }
    }
    return best;
}

std::vector<NUIPoint> smoothCurve(const std::vector<NUIPoint>& pts, int subdivisions) {
    if (pts.size() < 4 || subdivisions <= 1)
        return pts;
    std::vector<NUIPoint> result;
    result.reserve(pts.size() * static_cast<size_t>(subdivisions));
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const NUIPoint& p0 = pts[i == 0 ? i : i - 1];
        const NUIPoint& p1 = pts[i];
        const NUIPoint& p2 = pts[i + 1];
        const NUIPoint& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        for (int s = 0; s < subdivisions; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(subdivisions);
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float x =
                0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                        (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
            const float y =
                0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
            result.push_back({x, y});
        }
    }
    result.push_back(pts.back());
    return result;
}

} // namespace

AestraEQEditor::AestraEQEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraEQEditor");
    setPanelTitle("Aestra EQ");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildBands();
    captureCompareSlot(0);
    captureCompareSlot(1);
    m_spectrumWorker = std::thread([this]() { analyzerWorkerMain(); });
}

AestraEQEditor::~AestraEQEditor() {
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_spectrumStop = true;
    }
    m_spectrumCv.notify_all();
    if (m_spectrumWorker.joinable())
        m_spectrumWorker.join();
}

void AestraEQEditor::buildBands() {
    m_bands.clear();
    if (!m_instance)
        return;
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    auto eq = std::dynamic_pointer_cast<EQ>(m_instance);

    for (size_t i = 0; i < kNumBands; ++i) {
        appendLegacyBand(static_cast<uint32_t>(i));
    }
    if (eq) {
        for (uint32_t slot = EQ::kLegacyBandCount; slot < eq->getDynamicBandSlotCount(); ++slot) {
            if (eq->isDynamicBandSlotEnabled(slot)) {
                appendDynamicBand(slot);
            }
        }
    }
    layoutControls();
}

void AestraEQEditor::appendLegacyBand(uint32_t slotIndex) {
    if (!m_instance || slotIndex >= Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount)
        return;

    using EQ = Aestra::Audio::Plugins::AestraEQ;
    auto eq = std::dynamic_pointer_cast<EQ>(m_instance);
    const auto slot = EQ::legacyBandSlot(slotIndex);

    Band b;
    b.slotIndex = slotIndex;
    b.legacySlot = true;
    b.enableId = slot.enableId;
    b.freqId = slot.freqId;
    b.gainId = slot.gainId;
    b.qId = slot.qId;
    b.typeId = slot.typeId;
    b.stereoId = slot.stereoModeId;
    b.name = slot.id;
    b.typeName = std::string(slot.id) + " \u00B7 " + slot.typeLabel;

    if (eq) {
        const auto snapshot = eq->getDynamicBandSlotSnapshot(slotIndex);
        if (!snapshot.enabled)
            return;
        b.slotIndex = snapshot.slotIndex;
        b.legacySlot = snapshot.legacySlot;
        b.typeName = b.name + " \u00B7 " + filterTypeLabel(snapshot.type, slot.typeLabel);
        b.typeNorm = filterTypeNorm(snapshot.type);
        b.stereoNorm = stereoNormFromMode(snapshot.stereoMode);
        b.usesGain = (slot.gainId != 0) && !snapshot.usesSlope && filterTypeUsesGain(snapshot.type);
        b.usesSlope = snapshot.usesSlope;
        b.enabled = snapshot.enabled;
        b.freq = snapshot.frequencyNorm;
        b.gain = b.usesGain ? snapshot.gainNorm : 0.5f;
        b.q = snapshot.qOrSlopeNorm;
        b.dynamicEnabled = snapshot.dynamicEnabled;
        b.targetGain = snapshot.targetGainNorm;
        b.dynamicAmount = eq->getDynamicBandEnvelopeAmount(snapshot.slotIndex);
        b.dynamicThreshold = snapshot.thresholdNorm;
        b.dynamicKnee = snapshot.kneeNorm;
        b.dynamicAttack = snapshot.attackNorm;
        b.dynamicRelease = snapshot.releaseNorm;
        b.sidechainLinked = snapshot.sidechainLinked;
        b.sidechainType = snapshot.sidechainType;
        b.sidechainFreq = snapshot.sidechainFrequencyNorm;
        b.sidechainQ = snapshot.sidechainQNorm;
    } else {
        const float typeValue = b.typeId != 0 ? m_instance->getParameter(b.typeId) : 0.0f;
        b.typeNorm = quantizeTypeNorm(typeValue);
        b.stereoNorm = b.stereoId != 0 ? quantizeStereoNorm(m_instance->getParameter(b.stereoId)) : 0.0f;
        b.typeName = b.typeId != 0 ? (std::string(slot.id) + " \u00B7 " + middleBandTypeName(typeValue)) : b.typeName;
        b.usesGain = (slot.gainId != 0) && (b.typeId == 0 || middleBandTypeUsesGain(typeValue));
        b.usesSlope = slot.usesSlope;
        b.enabled = m_instance->getParameter(b.enableId) > 0.5f;
        b.freq = m_instance->getParameter(b.freqId);
        b.gain = b.usesGain ? m_instance->getParameter(b.gainId) : 0.5f;
        b.q = m_instance->getParameter(b.qId);
    }

    m_bands.push_back(std::move(b));
}

void AestraEQEditor::appendDynamicBand(uint32_t slotIndex) {
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq || slotIndex < Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount ||
        slotIndex >= eq->getDynamicBandSlotCount()) {
        return;
    }

    const auto snapshot = eq->getDynamicBandSlotSnapshot(slotIndex);
    if (snapshot.legacySlot || !snapshot.enabled) {
        return;
    }

    Band b;
    b.slotIndex = snapshot.slotIndex;
    b.legacySlot = false;
    b.name = "B" + std::to_string(snapshot.slotIndex + 1u);
    b.typeName = b.name + " \u00B7 " + filterTypeLabel(snapshot.type, "Bell");
    b.typeNorm = filterTypeNorm(snapshot.type);
    b.stereoNorm = stereoNormFromMode(snapshot.stereoMode);
    b.usesGain = filterTypeUsesGain(snapshot.type);
    b.usesSlope = snapshot.usesSlope;
    b.enabled = snapshot.enabled;
    b.freq = snapshot.frequencyNorm;
    b.gain = b.usesGain ? snapshot.gainNorm : 0.5f;
    b.q = snapshot.qOrSlopeNorm;
    b.dynamicEnabled = snapshot.dynamicEnabled;
    b.targetGain = snapshot.targetGainNorm;
    b.dynamicAmount = eq->getDynamicBandEnvelopeAmount(snapshot.slotIndex);
    b.dynamicThreshold = snapshot.thresholdNorm;
    b.dynamicKnee = snapshot.kneeNorm;
    b.dynamicAttack = snapshot.attackNorm;
    b.dynamicRelease = snapshot.releaseNorm;
    b.sidechainLinked = snapshot.sidechainLinked;
    b.sidechainType = snapshot.sidechainType;
    b.sidechainFreq = snapshot.sidechainFrequencyNorm;
    b.sidechainQ = snapshot.sidechainQNorm;
    m_bands.push_back(std::move(b));
}

void AestraEQEditor::syncBandsFromPlugin() {
    if (!m_instance)
        return;
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (eq) {
        for (uint32_t slot = 0; slot < Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount; ++slot) {
            if (!eq->isDynamicBandSlotEnabled(slot))
                continue;
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found == m_bands.end()) {
                appendLegacyBand(slot);
            }
        }
        for (uint32_t slot = Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount; slot < eq->getDynamicBandSlotCount();
             ++slot) {
            if (!eq->isDynamicBandSlotEnabled(slot))
                continue;
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found == m_bands.end()) {
                appendDynamicBand(slot);
            }
        }
    }
    for (auto& b : m_bands) {
        if (eq) {
            const auto snapshot = eq->getDynamicBandSlotSnapshot(b.slotIndex);
            b.legacySlot = snapshot.legacySlot;
            b.enabled = snapshot.enabled;
            b.freq = snapshot.frequencyNorm;
            const std::string fallback = bandTypeSuffix(b.typeName);
            b.typeName = b.name + " \u00B7 " + filterTypeLabel(snapshot.type, fallback.c_str());
            b.typeNorm = filterTypeNorm(snapshot.type);
            b.stereoNorm = stereoNormFromMode(snapshot.stereoMode);
            b.usesGain = (b.gainId != 0 || !b.legacySlot) && !snapshot.usesSlope && filterTypeUsesGain(snapshot.type);
            b.usesSlope = snapshot.usesSlope;
            b.gain = b.usesGain ? snapshot.gainNorm : 0.5f;
            b.q = snapshot.qOrSlopeNorm;
            b.dynamicEnabled = snapshot.dynamicEnabled;
            b.targetGain = snapshot.targetGainNorm;
            b.dynamicAmount = eq->getDynamicBandEnvelopeAmount(snapshot.slotIndex);
            b.dynamicThreshold = snapshot.thresholdNorm;
            b.dynamicKnee = snapshot.kneeNorm;
            b.dynamicAttack = snapshot.attackNorm;
            b.dynamicRelease = snapshot.releaseNorm;
            b.sidechainLinked = snapshot.sidechainLinked;
            b.sidechainType = snapshot.sidechainType;
            b.sidechainFreq = snapshot.sidechainFrequencyNorm;
            b.sidechainQ = snapshot.sidechainQNorm;
        } else {
            b.enabled = m_instance->getParameter(b.enableId) > 0.5f;
            b.freq = m_instance->getParameter(b.freqId);
            const float typeValue = b.typeId != 0 ? m_instance->getParameter(b.typeId) : 0.0f;
            b.typeNorm = quantizeTypeNorm(typeValue);
            b.stereoNorm = b.stereoId != 0 ? quantizeStereoNorm(m_instance->getParameter(b.stereoId)) : 0.0f;
            if (b.typeId != 0) {
                b.typeName = b.name + " \u00B7 " + middleBandTypeName(typeValue);
                b.usesGain = middleBandTypeUsesGain(typeValue);
            }
            if (b.usesGain)
                b.gain = m_instance->getParameter(b.gainId);
            b.q = m_instance->getParameter(b.qId);
        }
    }
    if (eq) {
        m_bands.erase(std::remove_if(m_bands.begin(), m_bands.end(),
                                     [eq](const Band& band) { return !eq->isDynamicBandSlotEnabled(band.slotIndex); }),
                      m_bands.end());
        if (m_selectedBand >= static_cast<int>(m_bands.size()))
            m_selectedBand = m_bands.empty() ? -1 : static_cast<int>(m_bands.size()) - 1;
        if (m_hoveredBand >= static_cast<int>(m_bands.size()))
            m_hoveredBand = -1;
        if (m_hoveredFloatingBand >= static_cast<int>(m_bands.size()))
            m_hoveredFloatingBand = -1;
        if (m_typeMenuBand >= static_cast<int>(m_bands.size()))
            m_typeMenuBand = -1;
        if (m_stereoMenuBand >= static_cast<int>(m_bands.size()))
            m_stereoMenuBand = -1;
        if (m_bandContextMenuBand >= static_cast<int>(m_bands.size()))
            closeBandContextMenu();
    }
}

void AestraEQEditor::layoutControls() {
    const auto b = getBounds();
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;

    constexpr float kBypassW = 92.0f;
    constexpr float kBypassH = 24.0f;
    constexpr float kBypassRightPad = 44.0f;
    constexpr float kOutputW = 104.0f;
    constexpr float kPolarityW = 58.0f;
    constexpr float kCompareW = 40.0f;
    constexpr float kCompareCopyW = 66.0f;
    constexpr float kControlGap = 8.0f;
    m_bypassRect = NUIRect(b.right() - kBypassRightPad - kBypassW, b.y + AestraPanelWindow::TITLE_BAR_H + 4.0f,
                           kBypassW, kBypassH);
    m_outputGainRect = NUIRect(m_bypassRect.x - kControlGap - kOutputW, m_bypassRect.y, kOutputW, kBypassH);
    m_polarityRect = NUIRect(m_outputGainRect.x - kControlGap - kPolarityW, m_bypassRect.y, kPolarityW, kBypassH);
    m_compareCopyRect =
        NUIRect(m_polarityRect.x - kControlGap - kCompareCopyW, m_bypassRect.y, kCompareCopyW, kBypassH);
    m_compareBRect = NUIRect(m_compareCopyRect.x - kControlGap - kCompareW, m_bypassRect.y, kCompareW, kBypassH);
    m_compareARect = NUIRect(m_compareBRect.x - kControlGap - kCompareW, m_bypassRect.y, kCompareW, kBypassH);

    const float graphY = b.y + AestraPanelWindow::TITLE_BAR_H + 36.0f;
    const float inspectorTargetH = m_bandInspectorCollapsed ? 40.0f : 78.0f;
    const float availableGraphH = b.bottom() - kPad - graphY - 16.0f - inspectorTargetH;
    const float graphMaxH = m_bandInspectorCollapsed ? kCurveH + 188.0f : kCurveH + 140.0f;
    const float graphH = std::max(160.0f, std::min(graphMaxH, availableGraphH));
    m_graphBounds = NUIRect(contentX, graphY, contentW, graphH);
    m_curveScaleRect = NUIRect(m_graphBounds.right() - 54.0f, m_graphBounds.y + 8.0f, 44.0f, 22.0f);
    m_analyzerMenuRect = NUIRect(m_curveScaleRect.x - 58.0f, m_curveScaleRect.y, 50.0f, 22.0f);
    m_analyzerPanelRect = NUIRect(m_graphBounds.right() - 438.0f, m_graphBounds.y + 34.0f, 428.0f, 74.0f);
    m_analyzerTiltRect = NUIRect(m_analyzerPanelRect.x + 10.0f, m_analyzerPanelRect.y + 38.0f, 54.0f, 22.0f);
    m_analyzerSourceRect = NUIRect(m_analyzerTiltRect.right() + 8.0f, m_analyzerTiltRect.y, 60.0f, 22.0f);
    m_analyzerStereoRect = NUIRect(m_analyzerSourceRect.right() + 8.0f, m_analyzerTiltRect.y, 36.0f, 22.0f);
    m_analyzerDecayRect = NUIRect(m_analyzerStereoRect.right() + 8.0f, m_analyzerTiltRect.y, 46.0f, 22.0f);
    m_analyzerFreezeRect = NUIRect(m_analyzerDecayRect.right() + 8.0f, m_analyzerTiltRect.y, 46.0f, 22.0f);
    m_analyzerCollisionRect = NUIRect(m_analyzerFreezeRect.right() + 8.0f, m_analyzerTiltRect.y, 64.0f, 22.0f);
    m_analyzerCollisionStrengthRect =
        NUIRect(m_analyzerCollisionRect.right() + 8.0f, m_analyzerTiltRect.y, 54.0f, 22.0f);

    m_bandStripRect = NUIRect();
    m_addBandRect = NUIRect();
    const float inspectorTop = m_graphBounds.bottom() + 16.0f;
    const float inspectorW = std::min(contentW, 640.0f);
    const float inspectorX = contentX + (contentW - inspectorW) * 0.5f;
    const float inspectorH = std::min(inspectorTargetH, std::max(0.0f, b.y + b.height - inspectorTop - kPad));
    m_bandInspectorRect = NUIRect(inspectorX, inspectorTop, inspectorW, inspectorH);
    m_selectedPrevRect = NUIRect();
    m_selectedNextRect = NUIRect();
    m_selectedDuplicateRect = NUIRect();
    m_selectedDeleteRect = NUIRect();
    m_selectedCollapseRect = NUIRect();
    m_selectedSlotRailRect = NUIRect();
    m_nodeQuickActionRect = NUIRect();
    for (auto& r : m_nodeQuickActionRects)
        r = NUIRect();
    const int selectedIdx =
        (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : 0;

    for (size_t i = 0; i < m_bands.size(); ++i) {
        auto& bd = m_bands[i];
        bd.cardBounds = NUIRect();
        bd.enableSwitch = NUIRect();

        bd.typeButton = NUIRect();
        bd.stereoButton = NUIRect();
        bd.freqKnob = NUIRect();
        bd.gainKnob = NUIRect();
        bd.qKnob = NUIRect();
        if (static_cast<int>(i) == selectedIdx && m_bandInspectorRect.height > 36.0f) {
            const float ix = m_bandInspectorRect.x + 14.0f;
            const float iy = m_bandInspectorRect.y + 21.0f;
            const float iw = m_bandInspectorRect.width - 28.0f;
            const float laneH = 21.0f;
            const float rowGap = 10.0f;
            bd.typeButton = NUIRect(ix + 204.0f, iy, 92.0f, 22.0f);
            m_selectedCollapseRect = NUIRect(m_bandInspectorRect.right() - 274.0f, iy, 24.0f, 22.0f);
            m_selectedPrevRect = NUIRect(m_bandInspectorRect.right() - 238.0f, iy, 24.0f, 22.0f);
            m_selectedNextRect = NUIRect(m_bandInspectorRect.right() - 210.0f, iy, 24.0f, 22.0f);
            m_selectedDuplicateRect = NUIRect(m_bandInspectorRect.right() - 174.0f, iy, 34.0f, 22.0f);
            m_selectedDeleteRect = NUIRect(m_bandInspectorRect.right() - 132.0f, iy, 28.0f, 22.0f);
            bd.stereoButton = NUIRect(m_bandInspectorRect.right() - 56.0f, iy, 42.0f, 22.0f);
            bd.enableSwitch = NUIRect();
            if (m_bandInspectorCollapsed)
                continue;
            const float laneTop = iy + 31.0f;
            const float laneW = (iw - rowGap * 2.0f) / 3.0f;
            bd.freqKnob = NUIRect(ix, laneTop, laneW, laneH);
            bd.gainKnob = NUIRect(ix + laneW + rowGap, laneTop, laneW, laneH);
            bd.qKnob = NUIRect(ix + (laneW + rowGap) * 2.0f, laneTop, laneW, laneH);
        }
    }

    if (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size()) && !m_bands.empty()) {
        const auto inner = graphInnerBounds(m_graphBounds);
        const auto node = graphNodePosition(static_cast<size_t>(m_selectedBand), m_graphBounds);
        constexpr float kActionW = 27.0f;
        constexpr float kActionH = 22.0f;
        constexpr float kActionGap = 4.0f;
        constexpr size_t kActionCount = static_cast<size_t>(NodeQuickAction::Count);
        const float stripW =
            kActionW * static_cast<float>(kActionCount) + kActionGap * static_cast<float>(kActionCount - 1) + 10.0f;
        const float stripH = 30.0f;
        float stripX = std::clamp(node.x - stripW * 0.5f, inner.x + 6.0f, inner.right() - stripW - 6.0f);
        constexpr float kToolbarNodeRadius = 7.0f;
        float stripY = node.y < inner.y + inner.height * 0.5f ? node.y + kToolbarNodeRadius + 6.0f
                                                              : node.y - stripH - kToolbarNodeRadius - 6.0f;
        stripY = std::clamp(stripY, inner.y + 4.0f, inner.bottom() - stripH - 4.0f);
        m_nodeQuickActionRect = {stripX, stripY, stripW, stripH};
        float ax = stripX + 5.0f;
        for (size_t i = 0; i < kActionCount; ++i) {
            m_nodeQuickActionRects[i] = {ax, stripY + 4.0f, kActionW, kActionH};
            ax += kActionW + kActionGap;
        }
    }

    if (m_typeMenuBand >= 0 && m_typeMenuBand < static_cast<int>(m_bands.size())) {
        const auto& bd = m_bands[static_cast<size_t>(m_typeMenuBand)];
        constexpr float kMenuW = 126.0f;
        const size_t optionCount = bd.legacySlot ? 4u : m_typeOptionRects.size();
        const float kMenuH = 16.0f + static_cast<float>(optionCount) * 23.0f;
        NUIRect anchor = bd.typeButton.width > 0.0f ? bd.typeButton : bd.cardBounds;
        if (m_bandInspectorCollapsed) {
            const auto floating = floatingBandPanelLayout(m_typeMenuBand, m_graphBounds);
            if (floating.valid)
                anchor = floating.typeRect;
        }
        if (m_typeMenuFromNodeQuickAction && m_typeMenuBand == m_selectedBand &&
            m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::TypeNext)].width > 0.0f) {
            anchor = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::TypeNext)];
        }
        const float menuX = std::clamp(anchor.x, contentX, contentX + contentW - kMenuW);
        const float menuY = std::min(anchor.bottom() + 6.0f, b.y + b.height - kPad - kMenuH);
        m_typeMenuRect = NUIRect(menuX, menuY, kMenuW, kMenuH);
        for (size_t i = 0; i < m_typeOptionRects.size(); ++i) {
            m_typeOptionRects[i] = i < optionCount ? NUIRect(menuX + 8.0f, menuY + 8.0f + static_cast<float>(i) * 23.0f,
                                                             kMenuW - 16.0f, 20.0f)
                                                   : NUIRect();
        }
    } else {
        m_typeMenuRect = NUIRect();
        for (auto& option : m_typeOptionRects)
            option = NUIRect();
    }

    if (m_stereoMenuBand >= 0 && m_stereoMenuBand < static_cast<int>(m_bands.size())) {
        const auto& bd = m_bands[static_cast<size_t>(m_stereoMenuBand)];
        constexpr float kMenuW = 118.0f;
        constexpr float kMenuH = 130.0f;
        NUIRect anchor = bd.stereoButton.width > 0.0f ? bd.stereoButton : bd.cardBounds;
        if (m_bandInspectorCollapsed) {
            const auto floating = floatingBandPanelLayout(m_stereoMenuBand, m_graphBounds);
            if (floating.valid)
                anchor = floating.stereoRect;
        }
        if (m_stereoMenuFromNodeQuickAction && m_stereoMenuBand == m_selectedBand &&
            m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Stereo)].width > 0.0f) {
            anchor = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Stereo)];
        }
        const float menuX = std::clamp(anchor.x, contentX, contentX + contentW - kMenuW);
        const float menuY = std::min(anchor.bottom() + 6.0f, b.y + b.height - kPad - kMenuH);
        m_stereoMenuRect = NUIRect(menuX, menuY, kMenuW, kMenuH);
        for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
            m_stereoOptionRects[i] =
                NUIRect(menuX + 8.0f, menuY + 8.0f + static_cast<float>(i) * 23.0f, kMenuW - 16.0f, 20.0f);
        }
    } else {
        m_stereoMenuRect = NUIRect();
        for (auto& option : m_stereoOptionRects)
            option = NUIRect();
    }

    if (m_bandContextMenuBand < 0 || m_bandContextMenuBand >= static_cast<int>(m_bands.size())) {
        m_bandContextMenuRect = NUIRect();
        for (auto& option : m_bandContextOptionRects) {
            option = NUIRect();
        }
    }
}

bool AestraEQEditor::isBypassed() const {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    return m_instance && m_instance->getParameter(EQ::kParamBypass) > 0.5f;
}

void AestraEQEditor::setBypassed(bool bypassed) {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    if (m_instance)
        m_instance->setParameter(EQ::kParamBypass, bypassed ? 1.0f : 0.0f);
    setDirty(true);
}

float AestraEQEditor::outputGain() const {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    return m_instance ? m_instance->getParameter(EQ::kParamOutputGain) : 0.5f;
}

void AestraEQEditor::setOutputGain(float normalizedGain) {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    if (m_instance) {
        m_instance->setParameter(EQ::kParamOutputGain, std::clamp(normalizedGain, 0.0f, 1.0f));
    }
    setDirty(true);
}

bool AestraEQEditor::polarityInverted() const {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    return m_instance && m_instance->getParameter(EQ::kParamPolarityInvert) > 0.5f;
}

void AestraEQEditor::setPolarityInverted(bool inverted) {
    using EQ = Aestra::Audio::Plugins::AestraEQ;
    if (m_instance)
        m_instance->setParameter(EQ::kParamPolarityInvert, inverted ? 1.0f : 0.0f);
    setDirty(true);
}

float AestraEQEditor::curveDbRange() const {
    static constexpr float kRanges[] = {12.0f, 18.0f, 24.0f, 36.0f};
    return kRanges[std::min<size_t>(m_curveDbRangeIndex, 3u)];
}

void AestraEQEditor::cycleCurveDbRange() {
    m_curveDbRangeIndex = (m_curveDbRangeIndex + 1u) % 4u;
    setDirty(true);
}

void AestraEQEditor::cycleAnalyzerSource() {
    m_analyzerSourceIndex = (m_analyzerSourceIndex + 1u) % 2u;
    m_lastAnalyzerSerial = 0;
    m_pendingAnalyzerSerial = 0;
    setDirty(true);
}

Aestra::Audio::Plugins::AestraEQ::StereoMode AestraEQEditor::analyzerStereoMode() const {
    using StereoMode = Aestra::Audio::Plugins::AestraEQ::StereoMode;
    static constexpr StereoMode kModes[] = {
        StereoMode::Stereo, StereoMode::Left, StereoMode::Right, StereoMode::Mid, StereoMode::Side,
    };
    return kModes[std::min<size_t>(m_analyzerStereoIndex, 4u)];
}

void AestraEQEditor::cycleAnalyzerStereoMode() {
    m_analyzerStereoIndex = (m_analyzerStereoIndex + 1u) % 5u;
    m_lastAnalyzerSerial = 0;
    m_pendingAnalyzerSerial = 0;
    setDirty(true);
}

float AestraEQEditor::analyzerTiltDbPerOct() const {
    static constexpr float kTilts[] = {0.0f, 3.0f, 4.5f};
    const uint32_t idx = std::min<uint32_t>(m_analyzerTiltIndex.load(std::memory_order_relaxed), 2u);
    return kTilts[idx];
}

void AestraEQEditor::cycleAnalyzerTilt() {
    const uint32_t next = (m_analyzerTiltIndex.load(std::memory_order_relaxed) + 1u) % 3u;
    m_analyzerTiltIndex.store(next, std::memory_order_relaxed);
    m_lastAnalyzerSerial = 0;
    m_pendingAnalyzerSerial = 0;
    setDirty(true);
}

void AestraEQEditor::cycleAnalyzerDecay() {
    m_analyzerDecayIndex = (m_analyzerDecayIndex + 1u) % 3u;
    setDirty(true);
}

void AestraEQEditor::cycleAnalyzerCollisionStrength() {
    const uint32_t next = (m_analyzerCollisionStrengthIndex.load(std::memory_order_relaxed) + 1u) % 3u;
    m_analyzerCollisionStrengthIndex.store(next, std::memory_order_relaxed);
    m_lastAnalyzerSerial = 0;
    m_pendingAnalyzerSerial = 0;
    setDirty(true);
}

float AestraEQEditor::analyzerCollisionStrength() const {
    static constexpr float kStrengths[] = {0.72f, 1.0f, 1.38f};
    const uint32_t idx = std::min<uint32_t>(m_analyzerCollisionStrengthIndex.load(std::memory_order_relaxed), 2u);
    return kStrengths[idx];
}

void AestraEQEditor::setAnalyzerFrozen(bool frozen) {
    if (m_analyzerFrozen == frozen)
        return;
    m_analyzerFrozen = frozen;
    if (!frozen) {
        m_lastAnalyzerSerial = 0;
        m_pendingAnalyzerSerial = 0;
    }
    setDirty(true);
}

void AestraEQEditor::setAnalyzerCollisionEnabled(bool enabled) {
    if (m_analyzerCollisionEnabled == enabled)
        return;
    m_analyzerCollisionEnabled = enabled;
    if (!enabled) {
        m_collisionMagnitudes.fill(0.0f);
    } else {
        m_lastAnalyzerSerial = 0;
        m_pendingAnalyzerSerial = 0;
    }
    setDirty(true);
}

void AestraEQEditor::captureCompareSlot(uint32_t slot) {
    if (!m_instance || slot >= m_compareSlots.size())
        return;
    for (uint32_t i = 0; i < Aestra::Audio::Plugins::AestraEQ::kParamCount; ++i) {
        m_compareSlots[slot].params[i] = m_instance->getParameter(i);
    }
    if (auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance)) {
        for (uint32_t i = 0; i < Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands; ++i) {
            m_compareSlots[slot].dynamicSlots[i] = eq->getDynamicBandSlotSnapshot(i);
        }
    }
}

void AestraEQEditor::applyCompareSlot(uint32_t slot) {
    if (!m_instance || slot >= m_compareSlots.size())
        return;
    for (uint32_t i = 0; i < Aestra::Audio::Plugins::AestraEQ::kParamCount; ++i) {
        m_instance->setParameter(i, m_compareSlots[slot].params[i]);
    }
    if (auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance)) {
        for (uint32_t i = Aestra::Audio::Plugins::AestraEQ::kLegacyBandCount;
             i < Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands; ++i) {
            const auto& snapshot = m_compareSlots[slot].dynamicSlots[i];
            if (!snapshot.enabled) {
                eq->clearDynamicBandSlot(i);
                continue;
            }
            Aestra::Audio::Plugins::AestraEQ::DynamicBandSlotDefaults defaults{
                snapshot.enabled,        snapshot.type,
                snapshot.stereoMode,     snapshot.frequencyNorm,
                snapshot.gainNorm,       snapshot.qOrSlopeNorm,
                snapshot.usesSlope,      snapshot.dynamicEnabled,
                snapshot.targetGainNorm, snapshot.thresholdNorm,
                snapshot.kneeNorm,       snapshot.attackNorm,
                snapshot.releaseNorm,    snapshot.sidechainLinked,
                snapshot.sidechainType,  snapshot.sidechainFrequencyNorm,
                snapshot.sidechainQNorm,
            };
            eq->setDynamicBandSlot(i, defaults);
        }
    }
    syncBandsFromPlugin();
    setDirty(true);
}

void AestraEQEditor::switchCompareSlot(uint32_t slot) {
    if (slot >= m_compareSlots.size() || slot == m_compareActiveSlot)
        return;
    captureCompareSlot(m_compareActiveSlot);
    m_compareActiveSlot = slot;
    applyCompareSlot(m_compareActiveSlot);
}

void AestraEQEditor::copyCompareSlotToOther() {
    const uint32_t target = m_compareActiveSlot == 0 ? 1u : 0u;
    captureCompareSlot(m_compareActiveSlot);
    m_compareSlots[target] = m_compareSlots[m_compareActiveSlot];
    setDirty(true);
}

float AestraEQEditor::dynamicTypeNormFromClipboardType(float typeNorm, bool legacyDomain) {
    return filterTypeNorm(clipboardFilterType(typeNorm, legacyDomain));
}

Aestra::Audio::Plugins::FilterType AestraEQEditor::clipboardFilterType(float typeNorm, bool legacyDomain) {
    using FilterType = Aestra::Audio::Plugins::FilterType;
    if (!legacyDomain)
        return filterTypeFromNorm(typeNorm);

    const int idx = std::clamp(static_cast<int>(std::round(quantizeTypeNorm(typeNorm) * 3.0f)), 0, 3);
    static constexpr FilterType legacyTypes[] = {
        FilterType::Bell,
        FilterType::Notch,
        FilterType::BandPass,
        FilterType::Tilt,
    };
    return legacyTypes[idx];
}

void AestraEQEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = isBypassed();
    if (bypassed) {
        const NUIColor bypassRed(0.92f, 0.28f, 0.22f, 1.0f);
        renderer.fillRoundedRect(m_bypassRect, 7.0f, bypassRed.withAlpha(m_bypassHovered ? 0.24f : 0.16f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, bypassRed.withAlpha(m_bypassHovered ? 0.64f : 0.44f));
        drawSvgIcon(renderer, bypassIcon(), {m_bypassRect.x + 9.0f, m_bypassRect.y + 4.0f, 16.0f, 16.0f},
                    bypassRed.withAlpha(0.96f), 14.0f);
        renderer.drawText("BYPASS", {m_bypassRect.x + 31.0f, topPillTextY(renderer, m_bypassRect, 8.6f)}, 8.6f,
                          theme.getColor("textPrimary").withAlpha(0.92f));
    } else {
        const NUIColor activeGreen(0.29f, 0.73f, 0.48f, 1.0f);
        renderer.fillRoundedRect(m_bypassRect, 7.0f, activeGreen.withAlpha(m_bypassHovered ? 0.12f : 0.08f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, activeGreen.withAlpha(m_bypassHovered ? 0.50f : 0.36f));
        drawSvgIcon(renderer, powerIcon(), {m_bypassRect.x + 9.0f, m_bypassRect.y + 4.0f, 16.0f, 16.0f},
                    activeGreen.withAlpha(m_bypassHovered ? 1.0f : 0.90f), 14.0f);
        renderer.drawText("ACTIVE", {m_bypassRect.x + 31.0f, topPillTextY(renderer, m_bypassRect, 8.6f)}, 8.6f,
                          activeGreen.withAlpha(m_bypassHovered ? 1.0f : 0.88f));
    }
}

void AestraEQEditor::drawOutputGainPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float gain = outputGain();
    const NUIColor outline =
        m_outputGainHovered || m_draggingOutputGain ? accent().withAlpha(0.44f) : NUIColor(1, 1, 1, 0.14f);
    const NUIColor fill = m_outputGainHovered || m_draggingOutputGain ? accent().withAlpha(0.11f)
                                                                      : NUIColor(0.035f, 0.034f, 0.048f, 0.88f);
    renderer.fillRoundedRect(m_outputGainRect, 7.0f, fill);
    renderer.strokeRoundedRect(m_outputGainRect, 7.0f, 1.0f, outline);
    std::string gainText = formatGain(gain);
    drawSvgIcon(renderer, outputIcon(), {m_outputGainRect.x + 7.0f, m_outputGainRect.y + 5.0f, 14.0f, 14.0f},
                theme.getColor("textSecondary").withAlpha(m_outputGainHovered ? 0.92f : 0.66f), 13.0f);
    renderer.drawTextCentered("OUT", {m_outputGainRect.x + 23.0f, m_outputGainRect.y, 25.0f, m_outputGainRect.height},
                              8.8f, theme.getColor("textSecondary").withAlpha(0.78f));
    renderer.drawTextCentered(gainText,
                              {m_outputGainRect.x + 50.0f, m_outputGainRect.y,
                               m_outputGainRect.right() - m_outputGainRect.x - 54.0f, m_outputGainRect.height},
                              8.8f, theme.getColor("textPrimary").withAlpha(0.94f));
}

void AestraEQEditor::drawPolarityPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool inverted = polarityInverted();
    const NUIColor base = inverted ? NUIColor(0.95f, 0.68f, 0.32f, 1.0f) : accent();
    const NUIColor fill = inverted ? base.withAlpha(m_polarityHovered ? 0.23f : 0.16f)
                                   : NUIColor(0.035f, 0.034f, 0.048f, m_polarityHovered ? 0.96f : 0.88f);
    renderer.fillRoundedRect(m_polarityRect, 7.0f, fill);
    renderer.strokeRoundedRect(m_polarityRect, 7.0f, 1.0f,
                               inverted ? base.withAlpha(m_polarityHovered ? 0.56f : 0.42f)
                                        : NUIColor(1, 1, 1, m_polarityHovered ? 0.22f : 0.14f));
    drawSvgIcon(renderer, polarityIcon(), {m_polarityRect.x + 8.0f, m_polarityRect.y + 5.0f, 14.0f, 14.0f},
                inverted ? base.withAlpha(1.0f) : theme.getColor("textSecondary").withAlpha(0.72f), 13.0f);
    renderer.drawText(inverted ? "INV" : "POL",
                      {m_polarityRect.x + 29.0f, topPillTextY(renderer, m_polarityRect, 8.2f)}, 8.2f,
                      inverted ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.76f));
}

void AestraEQEditor::drawComparePills(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    auto drawSlot = [&](const NUIRect& r, const char* label, bool active, bool hovered) {
        const NUIColor base = active ? accent() : NUIColor(1, 1, 1, 1);
        renderer.fillRoundedRect(r, 7.0f,
                                 active ? accent().withAlpha(hovered ? 0.16f : 0.10f)
                                        : NUIColor(0.035f, 0.034f, 0.048f, hovered ? 0.98f : 0.88f));
        renderer.strokeRoundedRect(r, 7.0f, 1.0f,
                                   active ? base.withAlpha(hovered ? 0.50f : 0.34f)
                                          : NUIColor(1, 1, 1, hovered ? 0.22f : 0.13f));
        const NUIColor glyph =
            active ? theme.getColor("textPrimary").withAlpha(0.90f) : theme.getColor("textSecondary").withAlpha(0.46f);
        drawSvgIcon(renderer, compareSlotIcon(), {r.x + 6.0f, r.y + 6.0f, 12.0f, 12.0f}, glyph, 11.0f);
        renderer.drawText(label, {r.x + 23.0f, topPillTextY(renderer, r, 8.4f)}, 8.4f,
                          active ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.76f));
    };

    drawSlot(m_compareARect, "A", m_compareActiveSlot == 0, m_compareAHovered);
    drawSlot(m_compareBRect, "B", m_compareActiveSlot == 1, m_compareBHovered);

    const char* label = m_compareActiveSlot == 0 ? "A>B" : "B>A";
    renderer.fillRoundedRect(m_compareCopyRect, 7.0f,
                             m_compareCopyHovered ? accent().withAlpha(0.11f)
                                                  : NUIColor(0.035f, 0.034f, 0.048f, 0.88f));
    renderer.strokeRoundedRect(m_compareCopyRect, 7.0f, 1.0f,
                               m_compareCopyHovered ? accent().withAlpha(0.42f) : NUIColor(1, 1, 1, 0.13f));
    drawSvgIcon(renderer, compareCopyIcon(), {m_compareCopyRect.x + 7.0f, m_compareCopyRect.y + 5.0f, 14.0f, 14.0f},
                theme.getColor("textSecondary").withAlpha(m_compareCopyHovered ? 0.92f : 0.66f), 13.0f);
    renderer.drawText(label, {m_compareCopyRect.x + 28.0f, topPillTextY(renderer, m_compareCopyRect, 8.0f)}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraEQEditor::drawCurveScalePill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float range = curveDbRange();
    char label[16];
    std::snprintf(label, sizeof(label), "\u00B1%.0f", range);
    renderer.fillRoundedRect(m_curveScaleRect, 7.0f,
                             NUIColor(0.045f, 0.043f, 0.064f, m_curveScaleHovered ? 0.94f : 0.76f));
    renderer.strokeRoundedRect(m_curveScaleRect, 7.0f, 1.0f, accent().withAlpha(m_curveScaleHovered ? 0.50f : 0.28f));
    drawSvgIcon(renderer, rangeIcon(), {m_curveScaleRect.x + 5.0f, m_curveScaleRect.y + 5.0f, 14.0f, 14.0f},
                theme.getColor("textSecondary").withAlpha(m_curveScaleHovered ? 0.92f : 0.66f), 12.5f);
    renderer.drawText(label, {m_curveScaleRect.x + 20.0f, topPillTextY(renderer, m_curveScaleRect, 8.0f)}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraEQEditor::drawAnalyzerMenuPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor outline = accent().withAlpha(m_analyzerMenuHovered || m_analyzerPanelOpen ? 0.54f : 0.28f);
    const NUIColor fill = accent().withAlpha(m_analyzerPanelOpen ? 0.18f : (m_analyzerMenuHovered ? 0.13f : 0.08f));
    renderer.fillRoundedRect(m_analyzerMenuRect, 7.0f, fill);
    renderer.strokeRoundedRect(m_analyzerMenuRect, 7.0f, 1.0f, outline);
    drawSvgIcon(renderer, analyzerIcon(), {m_analyzerMenuRect.x + 6.0f, m_analyzerMenuRect.y + 5.0f, 14.0f, 14.0f},
                accent().withAlpha(m_analyzerPanelOpen ? 0.98f : (m_analyzerMenuHovered ? 0.86f : 0.64f)), 13.0f);
    renderer.drawText("ANL", {m_analyzerMenuRect.x + 23.0f, topPillTextY(renderer, m_analyzerMenuRect, 8.0f)}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(m_analyzerPanelOpen ? 0.96f : 0.84f));
}

void AestraEQEditor::drawAnalyzerSettingsPanel(NUIRenderer& renderer) {
    if (!m_analyzerPanelOpen)
        return;

    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_analyzerPanelRect, 8.0f, NUIColor(0.030f, 0.030f, 0.045f, 0.94f));
    renderer.strokeRoundedRect(m_analyzerPanelRect, 8.0f, 1.0f, accent().withAlpha(0.34f));
    renderer.drawText("Analyzer", {m_analyzerPanelRect.x + 10.0f, m_analyzerPanelRect.y + 9.0f}, 9.5f,
                      theme.getColor("textPrimary").withAlpha(0.92f));
    renderer.drawText(m_analyzerSourceIndex == 0 ? "Pre vs Post" : "Post vs Pre",
                      {m_analyzerPanelRect.right() - 76.0f, m_analyzerPanelRect.y + 9.0f}, 8.3f,
                      theme.getColor("textSecondary").withAlpha(0.72f));
    drawAnalyzerTiltPill(renderer);
    drawAnalyzerSourcePill(renderer);
    drawAnalyzerStereoPill(renderer);
    drawAnalyzerDecayPill(renderer);
    drawAnalyzerFreezePill(renderer);
    drawAnalyzerCollisionPill(renderer);
    drawAnalyzerCollisionStrengthPill(renderer);
}

void AestraEQEditor::drawAnalyzerTiltPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float tilt = analyzerTiltDbPerOct();
    char label[20];
    if (tilt <= 0.0f) {
        std::snprintf(label, sizeof(label), "T 0");
    } else {
        std::snprintf(label, sizeof(label), "T %.1f", tilt);
    }
    renderer.fillRoundedRect(m_analyzerTiltRect, 7.0f,
                             NUIColor(0.045f, 0.043f, 0.064f, m_analyzerTiltHovered ? 0.94f : 0.76f));
    renderer.strokeRoundedRect(m_analyzerTiltRect, 7.0f, 1.0f,
                               accent().withAlpha(m_analyzerTiltHovered ? 0.50f : 0.28f));
    drawSvgIcon(renderer, tiltIcon(), {m_analyzerTiltRect.x + 6.0f, m_analyzerTiltRect.y + 5.0f, 13.0f, 13.0f},
                theme.getColor("textSecondary").withAlpha(m_analyzerTiltHovered ? 0.90f : 0.58f), 12.0f);
    renderer.drawText(label,
                      {m_analyzerTiltRect.x + 21.0f, std::round(renderer.calculateTextY(m_analyzerTiltRect, 8.0f))},
                      8.0f, theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraEQEditor::drawAnalyzerSourcePill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool pre = m_analyzerSourceIndex == 0;
    renderer.fillRoundedRect(m_analyzerSourceRect, 7.0f,
                             NUIColor(0.045f, 0.043f, 0.064f, m_analyzerSourceHovered ? 0.94f : 0.76f));
    renderer.strokeRoundedRect(
        m_analyzerSourceRect, 7.0f, 1.0f,
        (pre ? NUIColor(0.46f, 0.78f, 1.0f, 1.0f) : accent()).withAlpha(m_analyzerSourceHovered ? 0.52f : 0.30f));
    drawSvgIcon(
        renderer, sourceIcon(), {m_analyzerSourceRect.x + 6.0f, m_analyzerSourceRect.y + 5.0f, 13.0f, 13.0f},
        (pre ? NUIColor(0.46f, 0.78f, 1.0f, 1.0f) : accent()).withAlpha(m_analyzerSourceHovered ? 0.90f : 0.62f),
        12.0f);
    renderer.drawText(pre ? "PRE" : "POST",
                      {m_analyzerSourceRect.x + 22.0f, std::round(renderer.calculateTextY(m_analyzerSourceRect, 8.0f))},
                      8.0f, theme.getColor("textSecondary").withAlpha(0.88f));
}

void AestraEQEditor::drawAnalyzerStereoPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    static constexpr const char* kLabels[] = {"ST", "L", "R", "M", "S"};
    const size_t idx = std::min<size_t>(m_analyzerStereoIndex, 4u);
    renderer.fillRoundedRect(m_analyzerStereoRect, 7.0f,
                             NUIColor(0.045f, 0.043f, 0.064f, m_analyzerStereoHovered ? 0.94f : 0.76f));
    renderer.strokeRoundedRect(m_analyzerStereoRect, 7.0f, 1.0f,
                               accent().withAlpha(m_analyzerStereoHovered ? 0.50f : 0.28f));
    renderer.drawTextCentered(kLabels[idx], m_analyzerStereoRect, 8.5f,
                              theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraEQEditor::drawAnalyzerDecayPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    static constexpr const char* kLabels[] = {"D F", "D M", "D S"};
    const size_t idx = std::min<size_t>(m_analyzerDecayIndex, 2u);
    renderer.fillRoundedRect(m_analyzerDecayRect, 7.0f,
                             NUIColor(0.045f, 0.043f, 0.064f, m_analyzerDecayHovered ? 0.94f : 0.76f));
    renderer.strokeRoundedRect(m_analyzerDecayRect, 7.0f, 1.0f,
                               accent().withAlpha(m_analyzerDecayHovered ? 0.50f : 0.28f));
    drawSvgIcon(renderer, decayIcon(), {m_analyzerDecayRect.x + 6.0f, m_analyzerDecayRect.y + 5.0f, 13.0f, 13.0f},
                theme.getColor("textSecondary").withAlpha(m_analyzerDecayHovered ? 0.90f : 0.58f), 12.0f);
    renderer.drawText(kLabels[idx],
                      {m_analyzerDecayRect.x + 21.0f, std::round(renderer.calculateTextY(m_analyzerDecayRect, 8.0f))},
                      8.0f, theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraEQEditor::drawAnalyzerFreezePill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor base = m_analyzerFrozen ? NUIColor(0.46f, 0.78f, 1.0f, 1.0f) : accent();
    renderer.fillRoundedRect(m_analyzerFreezeRect, 7.0f,
                             base.withAlpha(m_analyzerFrozen ? (m_analyzerFreezeHovered ? 0.30f : 0.22f)
                                                             : (m_analyzerFreezeHovered ? 0.18f : 0.09f)));
    renderer.strokeRoundedRect(m_analyzerFreezeRect, 7.0f, 1.0f,
                               base.withAlpha(m_analyzerFrozen || m_analyzerFreezeHovered ? 0.52f : 0.25f));
    drawSvgIcon(renderer, freezeIcon(), {m_analyzerFreezeRect.x + 6.0f, m_analyzerFreezeRect.y + 5.0f, 13.0f, 13.0f},
                base.withAlpha(m_analyzerFrozen ? 0.98f : (m_analyzerFreezeHovered ? 0.84f : 0.58f)), 12.0f);
    renderer.drawText(
        "FRZ", {m_analyzerFreezeRect.x + 21.0f, std::round(renderer.calculateTextY(m_analyzerFreezeRect, 8.0f))}, 8.0f,
        m_analyzerFrozen ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.82f));
}

void AestraEQEditor::drawAnalyzerCollisionPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor warn(1.0f, 0.42f, 0.22f, 1.0f);
    const NUIColor base = m_analyzerCollisionEnabled ? warn : accent();
    renderer.fillRoundedRect(m_analyzerCollisionRect, 7.0f,
                             base.withAlpha(m_analyzerCollisionEnabled ? (m_analyzerCollisionHovered ? 0.30f : 0.20f)
                                                                       : (m_analyzerCollisionHovered ? 0.16f : 0.07f)));
    renderer.strokeRoundedRect(
        m_analyzerCollisionRect, 7.0f, 1.0f,
        base.withAlpha(m_analyzerCollisionEnabled || m_analyzerCollisionHovered ? 0.52f : 0.24f));
    drawSvgIcon(
        renderer, maskIcon(), {m_analyzerCollisionRect.x + 7.0f, m_analyzerCollisionRect.y + 5.0f, 13.0f, 13.0f},
        base.withAlpha(m_analyzerCollisionEnabled ? 0.96f : (m_analyzerCollisionHovered ? 0.82f : 0.56f)), 12.0f);
    renderer.drawText(
        "MASK", {m_analyzerCollisionRect.x + 23.0f, std::round(renderer.calculateTextY(m_analyzerCollisionRect, 8.0f))},
        8.0f,
        m_analyzerCollisionEnabled ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.80f));
}

void AestraEQEditor::drawAnalyzerCollisionStrengthPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    static constexpr const char* kLabels[] = {"S L", "S M", "S H"};
    const uint32_t idx = std::min<uint32_t>(m_analyzerCollisionStrengthIndex.load(std::memory_order_relaxed), 2u);
    const NUIColor warn(1.0f, 0.42f, 0.22f, 1.0f);
    const NUIColor base = m_analyzerCollisionEnabled ? warn : accent();
    renderer.fillRoundedRect(m_analyzerCollisionStrengthRect, 7.0f,
                             base.withAlpha(m_analyzerCollisionStrengthHovered ? 0.17f : 0.08f));
    renderer.strokeRoundedRect(m_analyzerCollisionStrengthRect, 7.0f, 1.0f,
                               base.withAlpha(m_analyzerCollisionStrengthHovered ? 0.50f : 0.26f));
    drawSvgIcon(renderer, strengthIcon(),
                {m_analyzerCollisionStrengthRect.x + 6.0f, m_analyzerCollisionStrengthRect.y + 5.0f, 13.0f, 13.0f},
                base.withAlpha(m_analyzerCollisionStrengthHovered ? 0.86f : 0.56f), 12.0f);
    renderer.drawText(kLabels[idx],
                      {m_analyzerCollisionStrengthRect.x + 21.0f,
                       std::round(renderer.calculateTextY(m_analyzerCollisionStrengthRect, 8.0f))},
                      8.0f, theme.getColor("textSecondary").withAlpha(m_analyzerCollisionEnabled ? 0.90f : 0.72f));
}

void AestraEQEditor::drawKnob(NUIRenderer& renderer, const NUIRect& bounds, float value, bool active,
                              const NUIColor& a) {
    const float cx = bounds.center().x;
    const float cy = bounds.center().y;
    const float r = bounds.width * 0.42f;
    const NUIColor col = active ? a : NUIColor(a.r, a.g, a.b, 0.30f);
    renderer.fillCircle({cx, cy}, r + 5.0f, col.withAlpha(active ? 0.08f : 0.04f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, col.withAlpha(active ? 0.34f : 0.18f));

    const float sa = kPi * 0.75f;
    const float ea = sa + std::clamp(value, 0.0f, 1.0f) * kPi * 1.5f;
    constexpr int kSegments = 26;
    for (int i = 0; i < kSegments; ++i) {
        const float a1 = sa + (ea - sa) * static_cast<float>(i) / static_cast<float>(kSegments);
        const float a2 = sa + (ea - sa) * static_cast<float>(i + 1) / static_cast<float>(kSegments);
        renderer.drawLine({cx + std::cos(a1) * (r - 3.0f), cy + std::sin(a1) * (r - 3.0f)},
                          {cx + std::cos(a2) * (r - 3.0f), cy + std::sin(a2) * (r - 3.0f)}, 2.4f,
                          col.withAlpha(active ? 0.92f : 0.40f));
    }
    const float pa = sa + std::clamp(value, 0.0f, 1.0f) * kPi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa) * (r - 6.0f), cy + std::sin(pa) * (r - 6.0f)}, 2.2f, col);
}

void AestraEQEditor::drawBandCard(NUIRenderer& renderer, size_t idx) {
    assert(idx < m_bands.size());
    auto& theme = NUIThemeManager::getInstance();
    const auto& bd = m_bands[idx];
    const NUIColor band = bandColor(bd.slotIndex);
    const bool hovered = static_cast<int>(idx) == m_hoveredBand;
    const int selectedIdx =
        (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : 0;
    const bool selected = static_cast<int>(idx) == selectedIdx;
    const bool hot = hovered || selected;
    const std::string bandId = bd.name.empty() ? bandIdLabel(idx) : bd.name;

    if (!selected)
        return;

    const std::string typeLabel = bandTypeSuffix(bd.typeName);
    renderer.fillRoundedRect(m_bandInspectorRect, 7.0f, NUIColor(0.046f, 0.045f, 0.060f, 0.97f));
    renderer.strokeRoundedRect(m_bandInspectorRect, 7.0f, 1.0f, band.withAlpha(0.16f));
    renderer.fillRect({m_bandInspectorRect.x, m_bandInspectorRect.y, m_bandInspectorRect.width, 2.0f},
                      band.withAlpha(0.72f));

    const auto drawInspectorSectionLabel = [&](const std::string& text, float x) {
        renderer.drawTextCentered(text, {x, m_bandInspectorRect.y + 6.0f, 72.0f, 10.0f}, 8.2f,
                                  theme.getColor("textSecondary").withAlpha(0.40f));
    };
    drawInspectorSectionLabel("BAND", m_bandInspectorRect.x + 14.0f);
    if (bd.typeButton.width > 0.0f)
        drawInspectorSectionLabel("FILTER", bd.typeButton.x);
    drawInspectorSectionLabel("ACTIONS", m_selectedCollapseRect.x);
    if (bd.stereoButton.width > 0.0f)
        drawInspectorSectionLabel("MODE", bd.stereoButton.x);

    const NUIRect inspectorHeaderRect{m_bandInspectorRect.x + 14.0f, m_bandInspectorRect.y + 21.0f, 192.0f, 22.0f};
    const size_t activeBandCount = static_cast<size_t>(
        std::count_if(m_bands.begin(), m_bands.end(), [](const Band& band) { return band.enabled; }));
    char bandMeta[56];
    std::snprintf(bandMeta, sizeof(bandMeta), "  \xC2\xB7  Slot %u  \xC2\xB7  %zu of 24 bands", bd.slotIndex + 1u,
                  activeBandCount);
    renderer.drawTextCentered(bandId, {inspectorHeaderRect.x, inspectorHeaderRect.y, 24.0f, inspectorHeaderRect.height},
                              11.0f, band.withAlpha(bd.enabled ? 0.96f : 0.48f));
    renderer.drawTextCentered(bandMeta,
                              {inspectorHeaderRect.x + 26.0f, inspectorHeaderRect.y, inspectorHeaderRect.width - 26.0f,
                               inspectorHeaderRect.height},
                              9.2f, theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.62f : 0.30f));
    const NUIColor sectionRule = theme.getColor("textSecondary").withAlpha(0.11f);
    renderer.drawLine({bd.typeButton.x - 12.0f, m_bandInspectorRect.y + 24.0f},
                      {bd.typeButton.x - 12.0f, m_bandInspectorRect.y + 42.0f}, 1.0f, sectionRule);
    renderer.drawLine({m_selectedCollapseRect.x - 9.0f, m_bandInspectorRect.y + 24.0f},
                      {m_selectedCollapseRect.x - 9.0f, m_bandInspectorRect.y + 42.0f}, 1.0f, sectionRule);
    renderer.drawLine({m_bandInspectorRect.x + 12.0f, m_bandInspectorRect.y + 48.0f},
                      {m_bandInspectorRect.right() - 12.0f, m_bandInspectorRect.y + 48.0f}, 1.0f,
                      theme.getColor("textSecondary").withAlpha(0.070f));
    if (m_selectedSlotRailRect.width > 0.0f) {
        const NUIRect slotRail = m_selectedSlotRailRect;
        renderer.fillRoundedRect(slotRail, 3.0f, NUIColor(1, 1, 1, 0.018f));
        for (uint32_t slot = 0; slot < Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands; ++slot) {
            const float t = Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands > 1
                                ? static_cast<float>(slot) /
                                      static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)
                                : 0.0f;
            const float x = slotRail.x + t * slotRail.width;
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            const bool occupied = found != m_bands.end() && found->enabled;
            const bool selectedSlot = bd.slotIndex == slot;
            const bool hoveredSlot = m_hoveredSelectedSlot == static_cast<int>(slot);
            const NUIColor slotColor =
                occupied ? bandColor(slot).withAlpha(selectedSlot ? 0.96f : (hoveredSlot ? 0.74f : 0.48f))
                         : theme.getColor("textSecondary").withAlpha(0.12f);
            if (selectedSlot || (occupied && hoveredSlot)) {
                renderer.fillRoundedRect({x - 2.0f, slotRail.y - 2.0f, 4.0f, slotRail.height + 4.0f}, 2.0f,
                                         bandColor(slot).withAlpha(selectedSlot ? 0.20f : 0.13f));
            }
            renderer.drawLine({x, slotRail.y + 2.0f}, {x, slotRail.bottom() - 2.0f}, selectedSlot ? 1.8f : 1.0f,
                              slotColor);
        }
        if (m_hoveredSelectedSlot >= 0) {
            const auto found = std::find_if(m_bands.begin(), m_bands.end(), [this](const Band& band) {
                return band.slotIndex == static_cast<uint32_t>(m_hoveredSelectedSlot);
            });
            if (found != m_bands.end()) {
                const size_t hoverIdx = static_cast<size_t>(std::distance(m_bands.begin(), found));
                const auto& hoverBand = *found;
                const float t = Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands > 1
                                    ? static_cast<float>(m_hoveredSelectedSlot) /
                                          static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)
                                    : 0.0f;
                const float tickX = slotRail.x + t * slotRail.width;
                const std::string hoverId = hoverBand.name.empty() ? bandIdLabel(hoverIdx) : hoverBand.name;
                const std::string hoverText = hoverId + "  S" + std::to_string(hoverBand.slotIndex + 1u) + "  " +
                                              formatFreq(hoverIdx, hoverBand.freq);
                const float chipW =
                    std::min(128.0f, std::max(70.0f, static_cast<float>(hoverText.size()) * 5.8f + 16.0f));
                const float chipX = std::clamp(tickX - chipW * 0.5f, m_bandInspectorRect.x + 10.0f,
                                               m_bandInspectorRect.right() - chipW - 10.0f);
                const NUIRect hoverChip{chipX, slotRail.bottom() + 7.0f, chipW, 19.0f};
                const NUIColor hoverColor = bandColor(hoverBand.slotIndex);
                renderer.fillRoundedRect(hoverChip, 5.0f, NUIColor(0.026f, 0.025f, 0.036f, 0.96f));
                renderer.strokeRoundedRect(hoverChip, 5.0f, 1.0f, hoverColor.withAlpha(0.34f));
                renderer.drawTextCentered(hoverText, hoverChip, 7.8f, theme.getColor("textSecondary").withAlpha(0.88f));
            }
        }
    }
    auto drawHeaderButtonFrame = [&](const NUIRect& rect, bool hovered, bool enabled) {
        const NUIColor fill = enabled && hovered ? band.withAlpha(0.13f) : NUIColor(1, 1, 1, enabled ? 0.018f : 0.006f);
        const NUIColor edge =
            enabled ? (hovered ? band.withAlpha(0.34f) : NUIColor(1, 1, 1, 0.085f)) : NUIColor(1, 1, 1, 0.018f);
        renderer.fillRoundedRect(rect, 5.0f, fill);
        renderer.strokeRoundedRect(rect, 5.0f, 1.0f, edge);
        return enabled ? theme.getColor("textSecondary").withAlpha(hovered ? 0.88f : 0.42f)
                       : theme.getColor("textSecondary").withAlpha(0.090f);
    };
    auto drawChevronButton = [&](const NUIRect& rect, bool right, bool hovered, bool enabled) {
        const NUIColor icon = drawHeaderButtonFrame(rect, hovered, enabled);
        drawSvgIcon(renderer, right ? chevronRightIcon() : chevronLeftIcon(), rect, icon, 12.0f);
    };
    auto drawDuplicateButton = [&](const NUIRect& rect, bool hovered, bool enabled) {
        const NUIColor icon = drawHeaderButtonFrame(rect, hovered, enabled);
        drawSvgIcon(renderer, duplicateIcon(), rect, icon, 13.0f);
    };
    auto drawDeleteButton = [&](const NUIRect& rect, bool hovered, bool enabled) {
        const NUIColor deleteRed(0.973f, 0.443f, 0.443f, 1.0f);
        const NUIColor icon =
            enabled ? deleteRed.withAlpha(hovered ? 1.0f : 0.60f) : theme.getColor("textSecondary").withAlpha(0.090f);
        renderer.fillRoundedRect(
            rect, 5.0f, enabled && hovered ? deleteRed.withAlpha(0.08f) : NUIColor(1, 1, 1, enabled ? 0.010f : 0.006f));
        renderer.strokeRoundedRect(rect, 5.0f, 1.0f,
                                   enabled ? deleteRed.withAlpha(hovered ? 0.50f : 0.25f) : NUIColor(1, 1, 1, 0.018f));
        drawSvgIcon(renderer, removeIcon(), rect, icon, 12.0f);
    };
    auto drawCollapseButton = [&](const NUIRect& rect, bool hovered) {
        const NUIColor icon = drawHeaderButtonFrame(rect, hovered, true);
        drawSvgIcon(renderer, m_bandInspectorCollapsed ? chevronDownIcon() : chevronUpIcon(), rect, icon, 12.0f);
    };
    const bool canPrev = adjacentGraphBand(-1) >= 0;
    const bool canNext = adjacentGraphBand(1) >= 0;
    const bool canCopy = static_cast<int>(idx) >= 0 && static_cast<int>(idx) < static_cast<int>(m_bands.size());
    const bool canDelete = !bd.legacySlot;
    drawCollapseButton(m_selectedCollapseRect, m_selectedCollapseHovered);
    drawChevronButton(m_selectedPrevRect, false, m_selectedPrevHovered, canPrev);
    drawChevronButton(m_selectedNextRect, true, m_selectedNextHovered, canNext);
    drawDuplicateButton(m_selectedDuplicateRect, m_selectedDuplicateHovered, canCopy);
    drawDeleteButton(m_selectedDeleteRect, m_selectedDeleteHovered, canDelete);
    if (bd.typeId != 0 || !bd.legacySlot) {
        renderer.fillRoundedRect(bd.typeButton, 5.0f, bd.enabled ? band.withAlpha(0.055f) : NUIColor(1, 1, 1, 0.010f));
        renderer.strokeRoundedRect(bd.typeButton, 5.0f, 1.0f, band.withAlpha(bd.enabled ? 0.22f : 0.08f));
    } else {
        renderer.fillRoundedRect(bd.typeButton, 5.0f, NUIColor(1, 1, 1, bd.enabled ? 0.018f : 0.008f));
    }
    renderer.drawTextCentered(typeLabel, bd.typeButton, 9.0f,
                              theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.72f : 0.36f));
    const float stereoNorm =
        (bd.legacySlot && m_instance && bd.stereoId != 0) ? m_instance->getParameter(bd.stereoId) : bd.stereoNorm;
    const bool stereoScoped = quantizeStereoNorm(stereoNorm) > 0.0f;
    renderer.fillRoundedRect(bd.stereoButton, 5.0f,
                             stereoScoped ? band.withAlpha(bd.enabled ? 0.16f : 0.06f)
                                          : NUIColor(1, 1, 1, bd.enabled ? 0.030f : 0.018f));
    renderer.strokeRoundedRect(bd.stereoButton, 5.0f, 1.0f,
                               stereoScoped ? band.withAlpha(bd.enabled ? 0.36f : 0.12f)
                                            : NUIColor(1, 1, 1, bd.enabled ? 0.08f : 0.04f));
    renderer.drawTextCentered(stereoModeShortName(stereoNorm), bd.stereoButton, 8.0f,
                              stereoScoped ? band.withAlpha(bd.enabled ? 0.96f : 0.42f)
                                           : theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.62f : 0.28f));
    if (m_selectedStereoHovered) {
        const std::string modeHelp = "Stereo / Mid-Side / Left / Right";
        const float tipW = 174.0f;
        const NUIRect tip{std::clamp(bd.stereoButton.center().x - tipW * 0.5f, m_bandInspectorRect.x + 8.0f,
                                     m_bandInspectorRect.right() - tipW - 8.0f),
                          bd.stereoButton.bottom() + 7.0f, tipW, 20.0f};
        renderer.fillRoundedRect(tip, 5.0f, NUIColor(0.026f, 0.025f, 0.036f, 0.96f));
        renderer.strokeRoundedRect(tip, 5.0f, 1.0f, band.withAlpha(0.28f));
        renderer.drawTextCentered(modeHelp, tip, 8.2f, theme.getColor("textSecondary").withAlpha(0.88f));
    }
    if (m_selectedCollapseHovered) {
        const std::string collapseHelp = m_bandInspectorCollapsed ? "Show inspector" : "Hide inspector";
        const float tipW = 96.0f;
        const NUIRect tip{std::clamp(m_selectedCollapseRect.center().x - tipW * 0.5f, m_bandInspectorRect.x + 8.0f,
                                     m_bandInspectorRect.right() - tipW - 8.0f),
                          m_selectedCollapseRect.bottom() + 7.0f, tipW, 20.0f};
        renderer.fillRoundedRect(tip, 5.0f, NUIColor(0.026f, 0.025f, 0.036f, 0.96f));
        renderer.strokeRoundedRect(tip, 5.0f, 1.0f, band.withAlpha(0.28f));
        renderer.drawTextCentered(collapseHelp, tip, 8.2f, theme.getColor("textSecondary").withAlpha(0.88f));
    }

    if (m_bandInspectorCollapsed)
        return;

    auto labelColor = bd.enabled ? theme.getColor("textSecondary").withAlpha(0.78f)
                                 : theme.getColor("textSecondary").withAlpha(0.36f);
    auto valueColor = bd.enabled ? band.withAlpha(0.96f) : band.withAlpha(0.42f);
    auto drawParamLane = [&](const NUIRect& lane, const std::string& label, const std::string& value,
                             float normalized) {
        const float amount = std::clamp(normalized, 0.0f, 1.0f);
        const bool draggingThisLane = m_draggingCardBand == static_cast<int>(idx) &&
                                      std::abs(m_draggingLaneRect.x - lane.x) < 0.5f &&
                                      std::abs(m_draggingLaneRect.y - lane.y) < 0.5f;
        const NUIColor edge =
            bd.enabled ? band.withAlpha(draggingThisLane ? 0.58f : (hot ? 0.38f : 0.19f)) : NUIColor(1, 1, 1, 0.065f);
        const float labelW = 36.0f;
        const float valueW = 64.0f;
        const float trackX = lane.x + labelW + 6.0f;
        const float trackW = std::max(8.0f, lane.width - labelW - valueW - 18.0f);
        const float trackY = lane.center().y;
        const float tickX = trackX + trackW * amount;

        renderer.fillRoundedRect(lane, 5.0f, NUIColor(0.026f, 0.025f, 0.036f, 0.95f));
        renderer.fillRoundedRect({lane.x, lane.y, labelW, lane.height}, 5.0f,
                                 bd.enabled ? band.withAlpha(0.050f) : band.withAlpha(0.018f));
        renderer.fillRoundedRect({trackX, trackY - 1.5f, trackW, 3.0f}, 1.5f, NUIColor(1, 1, 1, 0.080f));
        renderer.fillRoundedRect({trackX, trackY - 1.5f, std::max(2.0f, tickX - trackX), 3.0f}, 1.5f,
                                 bd.enabled ? band.withAlpha(draggingThisLane ? 0.82f : 0.70f) : band.withAlpha(0.12f));
        renderer.fillRoundedRect({lane.right() - valueW - 5.0f, lane.y + 5.0f, valueW, lane.height - 10.0f}, 4.0f,
                                 NUIColor(0, 0, 0, bd.enabled ? 0.18f : 0.08f));
        renderer.strokeRoundedRect(lane, 5.0f, 1.0f, edge);
        renderer.drawTextCentered(label, {lane.x + 4.0f, lane.y, labelW - 5.0f, lane.height}, 8.6f,
                                  labelColor.withAlpha(labelColor.a * 0.84f));
        const float valueFont = value.size() > 8 ? 9.0f : 10.5f;
        renderer.drawTextCentered(value, {lane.right() - valueW - 2.0f, lane.y, valueW, lane.height}, valueFont,
                                  bd.enabled ? theme.getColor("textPrimary").withAlpha(0.90f) : valueColor);
        renderer.fillCircle({tickX, trackY}, draggingThisLane ? 5.6f : 5.0f,
                            bd.enabled ? band.withAlpha(draggingThisLane ? 1.0f : 0.88f) : band.withAlpha(0.20f));
    };
    auto drawSpacerLane = [&](const NUIRect& lane) { renderer.fillRoundedRect(lane, 5.0f, NUIColor(0, 0, 0, 0)); };

    drawParamLane(bd.freqKnob, "FREQ", formatFreq(idx, bd.freq), bd.freq);

    if (bd.usesSlope) {
        drawParamLane(bd.gainKnob, "SLOPE", formatSlope(bd.q), quantizeSlopeNorm(bd.q));
        drawSpacerLane(bd.qKnob);
    } else if (!bd.usesGain) {
        drawParamLane(bd.gainKnob, "Q", formatQ(bd.q), bd.q);
        drawSpacerLane(bd.qKnob);
    } else {
        drawParamLane(bd.gainKnob, "GAIN", formatGain(bd.gain), bd.gain);
        drawParamLane(bd.qKnob, "Q", formatQ(bd.q), bd.q);
    }
}

NUIRect AestraEQEditor::graphInnerBounds(const NUIRect& outer) const {
    return {outer.x + 38.0f, outer.y + 18.0f, outer.width - 48.0f, outer.height - 36.0f};
}

NUIPoint AestraEQEditor::graphNodePosition(size_t bandIdx, const NUIRect& graphBounds) const {
    const auto inner = graphInnerBounds(graphBounds);
    const auto& bd = m_bands[bandIdx];
    const float hz = bd.legacySlot ? bandFreqHz(bd.slotIndex, bd.freq) : graphFreqHz(bd.freq);
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
    const float xnorm = (std::log10(std::max(hz, 20.0f)) - logMin) / (logMax - logMin);
    const float x = inner.x + std::clamp(xnorm, 0.0f, 1.0f) * inner.width;
    float yn = 0.5f;
    if (bd.usesGain) {
        const float gainDb = -18.0f + bd.gain * 36.0f;
        const float range = curveDbRange();
        yn = std::clamp((gainDb + range) / (range * 2.0f), 0.0f, 1.0f);
    }
    const float y = inner.bottom() - yn * inner.height;
    return {x, y};
}

bool AestraEQEditor::createBandAtGraphPoint(const NUIPoint& position) {
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq)
        return false;
    if (eq->findNextAvailableDynamicBandSlot() < 0)
        return false;

    const auto inner = graphInnerBounds(m_graphBounds);
    if (!inner.contains(position))
        return false;

    const float freqNorm = std::clamp((position.x - inner.x) / std::max(1.0f, inner.width), 0.0f, 1.0f);
    const float gainNorm = std::clamp(1.0f - (position.y - inner.y) / std::max(1.0f, inner.height), 0.0f, 1.0f);
    const float db = gainNorm * curveDbRange() * 2.0f - curveDbRange();
    const int32_t slot = eq->createDynamicBandAtGraphPoint(freqNorm, std::clamp((db + 18.0f) / 36.0f, 0.0f, 1.0f));
    if (slot < 0)
        return false;

    appendDynamicBand(static_cast<uint32_t>(slot));
    m_selectedBand = static_cast<int>(m_bands.size()) - 1;
    m_hoveredBand = m_selectedBand;
    m_hoveredBandFromGraph = true;
    setDirty(true);
    return true;
}

int AestraEQEditor::currentFloatingBandIndex() const {
    int idx = m_draggingGraphBand;
    if (idx < 0)
        idx = m_draggingCardBand;
    if (idx < 0)
        idx = m_hoveredFloatingBand;
    if (idx < 0 && m_typeMenuBand >= 0 && !m_typeMenuFromNodeQuickAction)
        idx = m_typeMenuBand;
    if (idx < 0 && m_stereoMenuBand >= 0 && !m_stereoMenuFromNodeQuickAction)
        idx = m_stereoMenuBand;
    if (idx < 0 && m_numericEditActive)
        idx = m_numericEditBand;
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()))
        return -1;
    return idx;
}

AestraEQEditor::FloatingBandPanelLayout AestraEQEditor::floatingBandPanelLayout(int bandIdx,
                                                                                const NUIRect& bounds) const {
    FloatingBandPanelLayout layout;
    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size()))
        return layout;

    const auto& bd = m_bands[static_cast<size_t>(bandIdx)];
    const auto node = graphNodePosition(static_cast<size_t>(bandIdx), bounds);
    constexpr float kW = 178.0f;
    const bool hasThirdRow = bd.usesGain && !bd.usesSlope;
    const float kH = hasThirdRow ? 86.0f : 68.0f;
    const auto inner = graphInnerBounds(bounds);
    const bool nodeOnRight = node.x > inner.x + inner.width * 0.60f;
    const bool nodeInUpperHalf = node.y <= inner.y + inner.height * 0.50f;
    const float desiredX = nodeOnRight ? node.x - kW - 12.0f : node.x + 12.0f;
    const float desiredY = nodeInUpperHalf ? node.y + 18.0f : node.y - kH - 6.0f;
    const float x = std::clamp(desiredX, bounds.x + 8.0f, bounds.right() - kW - 8.0f);
    const float y = std::clamp(desiredY, bounds.y + 34.0f, bounds.bottom() - kH - 8.0f);

    layout.valid = true;
    layout.bandIdx = bandIdx;
    layout.panel = {x, y, kW, kH};
    layout.typeRect = {x + 34.0f, y + 4.0f, 76.0f, 17.0f};
    layout.stereoRect = {x + kW - 48.0f, y + 5.0f, 24.0f, 15.0f};
    layout.enableRect = {x + kW - 20.0f, y + 5.0f, 16.0f, 16.0f};
    layout.freqRect = {x + 8.0f, y + 30.0f, kW - 16.0f, 18.0f};
    layout.gainRect = {x + 8.0f, y + 48.0f, kW - 16.0f, 18.0f};
    layout.qRect = {x + 8.0f, y + 66.0f, kW - 16.0f, 18.0f};
    layout.hasQRow = bd.usesGain && !bd.usesSlope;
    return layout;
}

void AestraEQEditor::drawSpectrumBackdrop(NUIRenderer& renderer, const NUIRect& bounds) {
    if (m_lastAnalyzerSerial == 0)
        return;
    const auto inner = graphInnerBounds(bounds);
    const size_t n = m_spectrumMagnitudes.size();
    const float div = n > 1 ? static_cast<float>(n - 1) : 1.0f;
    std::vector<NUIPoint> contour;
    contour.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / div;
        const float x = inner.x + t * inner.width;
        const float nextT = static_cast<float>(i + 1) / div;
        const float nextX = i + 1 < n ? inner.x + nextT * inner.width : inner.right();
        const float bw = std::max(1.0f, nextX - x);
        const float mag = std::clamp(m_spectrumMagnitudes[i], 0.0f, 1.0f);
        const float peak = std::clamp(m_spectrumPeakMagnitudes[i], 0.0f, 1.0f);
        const float bh = mag * inner.height;
        const float lowWeight = std::clamp(1.0f - t * 2.2f, 0.0f, 1.0f);
        const float highWeight = std::clamp((t - 0.58f) * 2.4f, 0.0f, 1.0f);
        const float midWeight = 1.0f - std::max(lowWeight, highWeight);
        const NUIColor low(0.34f, 0.70f, 1.00f, 1.0f);
        const NUIColor mid(0.62f, 0.42f, 0.98f, 1.0f);
        const NUIColor high(0.98f, 0.64f, 0.42f, 1.0f);
        const NUIColor band(low.r * lowWeight + mid.r * midWeight + high.r * highWeight,
                            low.g * lowWeight + mid.g * midWeight + high.g * highWeight,
                            low.b * lowWeight + mid.b * midWeight + high.b * highWeight, 1.0f);

        renderer.fillRect({x, inner.bottom() - bh, bw, bh}, band.withAlpha(0.035f + mag * 0.105f));
        if (mag > 0.08f) {
            const float capY = inner.bottom() - bh;
            renderer.drawLine({x, capY}, {x + bw, capY}, 1.0f, band.withAlpha(0.10f + mag * 0.14f));
        }
        if (peak > 0.12f && (i % 2 == 0)) {
            const float peakY = inner.bottom() - peak * inner.height;
            renderer.drawLine({x, peakY}, {x + bw, peakY}, 1.0f, band.withAlpha(0.16f));
        }
        contour.push_back({x + bw * 0.5f, inner.bottom() - bh});
    }

    const auto smooth = smoothCurve(contour, 3);
    if (smooth.size() > 1) {
        renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 1.1f, accent().withAlpha(0.20f));
    }
}

void AestraEQEditor::drawAnalyzerCollisionOverlay(NUIRenderer& renderer, const NUIRect& bounds) {
    if (!m_analyzerCollisionEnabled || m_lastAnalyzerSerial == 0)
        return;

    const auto inner = graphInnerBounds(bounds);
    const size_t n = m_collisionMagnitudes.size();
    if (n < 2)
        return;

    const float div = static_cast<float>(n - 1);
    std::vector<NUIPoint> ridge;
    ridge.reserve(n);
    float peakMask = 0.0f;
    size_t peakBin = 0;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / div;
        const float x = inner.x + t * inner.width;
        const float nextT = static_cast<float>(i + 1) / div;
        const float nextX = i + 1 < n ? inner.x + nextT * inner.width : inner.right();
        const float bw = std::max(1.0f, nextX - x);
        const float mask = std::clamp(m_collisionMagnitudes[i], 0.0f, 1.0f);
        if (mask > peakMask) {
            peakMask = mask;
            peakBin = i;
        }
        if (mask <= 0.015f) {
            ridge.push_back({x + bw * 0.5f, inner.bottom()});
            continue;
        }

        const float h = std::max(1.0f, mask * inner.height * 0.62f);
        const float y = inner.bottom() - h;
        const NUIColor heat(1.0f, 0.35f + mask * 0.16f, 0.12f, 1.0f);
        renderer.fillRect({x, y, bw, h}, heat.withAlpha(0.035f + mask * 0.18f));
        if (mask > 0.18f) {
            renderer.drawLine({x, y}, {x + bw, y}, 1.0f, heat.withAlpha(0.20f + mask * 0.26f));
        }
        ridge.push_back({x + bw * 0.5f, y});
    }

    const auto smooth = smoothCurve(ridge, 3);
    if (smooth.size() > 1) {
        renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 1.25f,
                              NUIColor(1.0f, 0.38f, 0.18f, 0.42f));
    }

    if (peakMask > 0.08f) {
        auto& theme = NUIThemeManager::getInstance();
        const float peakNorm = n > 1 ? static_cast<float>(peakBin) / static_cast<float>(n - 1) : 0.0f;
        const float peakHz = std::pow(10.0f, std::log10(20.0f) + peakNorm * (std::log10(20000.0f) - std::log10(20.0f)));
        char freqBuf[24];
        if (peakHz >= 1000.0f) {
            std::snprintf(freqBuf, sizeof(freqBuf), "%.2fk", peakHz / 1000.0f);
        } else {
            std::snprintf(freqBuf, sizeof(freqBuf), "%.0f", peakHz);
        }
        char maskBuf[32];
        std::snprintf(maskBuf, sizeof(maskBuf), "MASK %.0f%%", peakMask * 100.0f);
        const float chipW = 112.0f;
        const float chipX =
            std::clamp(inner.x + peakNorm * inner.width - chipW * 0.5f, inner.x + 8.0f, inner.right() - chipW - 8.0f);
        const NUIRect chip{chipX, inner.y + 28.0f, chipW, 22.0f};
        const NUIColor heat(1.0f, 0.40f, 0.18f, 1.0f);
        renderer.fillRoundedRect(chip, 6.0f, NUIColor(0.045f, 0.032f, 0.030f, 0.88f));
        renderer.strokeRoundedRect(chip, 6.0f, 1.0f, heat.withAlpha(0.34f + peakMask * 0.22f));
        renderer.drawText(maskBuf, {chip.x + 8.0f, chip.y + 6.0f}, 8.3f, heat.withAlpha(0.92f));
        renderer.drawText(freqBuf, {chip.right() - 38.0f, chip.y + 6.0f}, 8.3f,
                          theme.getColor("textSecondary").withAlpha(0.76f));
    }
}

void AestraEQEditor::drawBandResponseCurves(NUIRenderer& renderer, const NUIRect& bounds) {
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq)
        return;

    const auto inner = graphInnerBounds(bounds);
    const float dbRange = curveDbRange();
    constexpr int kNumPoints = 220;
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);

    for (size_t band = 0; band < m_bands.size(); ++band) {
        if (!m_bands[band].enabled)
            continue;
        const bool selected = static_cast<int>(band) == m_selectedBand;
        const bool hovered = static_cast<int>(band) == m_hoveredBand;
        std::vector<NUIPoint> pts;
        pts.reserve(kNumPoints);
        for (int p = 0; p < kNumPoints; ++p) {
            const float t = static_cast<float>(p) / static_cast<float>(kNumPoints - 1);
            const float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
            const float db = static_cast<float>(eq->getBandMagnitudeResponseDb(m_bands[band].slotIndex, hz));
            const float x = inner.x + t * inner.width;
            const float y = inner.bottom() - std::clamp((db + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * inner.height;
            pts.push_back({x, y});
        }

        const NUIColor c = bandColor(m_bands[band].slotIndex);
        const bool soloed = eq->isBandSoloed(m_bands[band].slotIndex);
        if (soloed || selected || hovered) {
            const auto node = graphNodePosition(band, bounds);
            const float q = std::clamp(m_bands[band].q, 0.0f, 1.0f);
            const float width = soloed ? (126.0f - q * 56.0f) : (82.0f - q * 30.0f);
            const float glowW = std::clamp(width, soloed ? 50.0f : 36.0f, soloed ? 128.0f : 92.0f);
            const float glowX = std::clamp(node.x - glowW * 0.5f, inner.x, inner.right() - glowW);
            const float fadeRange = std::max(1.0f, inner.height * (soloed ? 0.36f : 0.28f));
            const float glowH = std::min(inner.height, fadeRange * 2.0f);
            const float glowY = std::clamp(node.y - glowH * 0.5f, inner.y, inner.bottom() - glowH);
            const NUIRect region{glowX, glowY, glowW, glowH};
            constexpr int kGlowSlices = 28;
            const float peakAlpha = soloed ? 0.085f : 0.052f;
            for (int slice = 0; slice < kGlowSlices; ++slice) {
                const float y0 = region.y + region.height * static_cast<float>(slice) / static_cast<float>(kGlowSlices);
                const float y1 =
                    region.y + region.height * static_cast<float>(slice + 1) / static_cast<float>(kGlowSlices);
                const float cy = (y0 + y1) * 0.5f;
                const float distance = std::abs(cy - node.y) / fadeRange;
                const float falloff = std::clamp(1.0f - distance, 0.0f, 1.0f);
                const float alpha = falloff * falloff * peakAlpha;
                if (alpha > 0.002f) {
                    renderer.fillRect({region.x, y0, region.width, y1 - y0 + 0.75f}, c.withAlpha(alpha));
                }
            }
            renderer.drawLine({node.x, region.y + 2.0f}, {node.x, region.bottom() - 2.0f}, soloed ? 1.4f : 1.0f,
                              c.withAlpha(soloed ? 0.18f : 0.080f));
            if (pts.size() > 1) {
                renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()), soloed ? 9.0f : 6.0f,
                                      c.withAlpha(soloed ? 0.13f : 0.075f));
                renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()), soloed ? 4.4f : 3.0f,
                                      c.withAlpha(soloed ? 0.30f : 0.17f));
            }
        }
        const float alpha = soloed ? 0.74f : (selected || hovered ? 0.48f : 0.22f);
        renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()),
                              soloed ? 1.85f : (selected || hovered ? 1.45f : 1.0f), c.withAlpha(alpha));
    }
}

void AestraEQEditor::drawGraphCursorReadout(NUIRenderer& renderer, const NUIRect& bounds) {
    if (!m_graphCursorVisible || m_draggingGraphBand >= 0)
        return;
    const auto inner = graphInnerBounds(bounds);
    if (!inner.contains(m_graphCursorPoint))
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    const bool canAddBand = eq && eq->findNextAvailableDynamicBandSlot() >= 0;
    const float xNorm = std::clamp((m_graphCursorPoint.x - inner.x) / std::max(1.0f, inner.width), 0.0f, 1.0f);
    const float yNorm = std::clamp(1.0f - (m_graphCursorPoint.y - inner.y) / std::max(1.0f, inner.height), 0.0f, 1.0f);
    const float hz = std::pow(10.0f, std::log10(20.0f) + xNorm * (std::log10(20000.0f) - std::log10(20.0f)));
    const float db = yNorm * curveDbRange() * 2.0f - curveDbRange();
    const auto creationDefaults = Aestra::Audio::Plugins::AestraEQ::dynamicBandGraphCreationDefaults(
        xNorm, std::clamp((db + 18.0f) / 36.0f, 0.0f, 1.0f));
    const NUIColor preview = canAddBand ? accent() : theme.getColor("textSecondary");

    renderer.drawLine({m_graphCursorPoint.x, inner.y}, {m_graphCursorPoint.x, inner.bottom()}, 1.0f,
                      NUIColor(1.0f, 1.0f, 1.0f, canAddBand ? 0.085f : 0.040f));
    renderer.drawLine({inner.x, m_graphCursorPoint.y}, {inner.right(), m_graphCursorPoint.y}, 1.0f,
                      NUIColor(1.0f, 1.0f, 1.0f, canAddBand ? 0.070f : 0.035f));
    renderer.fillCircle(m_graphCursorPoint, 10.0f, preview.withAlpha(canAddBand ? 0.050f : 0.025f));
    renderer.fillCircle(m_graphCursorPoint, 4.0f, NUIColor(0.045f, 0.043f, 0.060f, 0.64f));
    renderer.strokeCircle(m_graphCursorPoint, 7.0f, 1.1f, preview.withAlpha(canAddBand ? 0.34f : 0.16f));
    renderer.drawLine({m_graphCursorPoint.x - 3.0f, m_graphCursorPoint.y},
                      {m_graphCursorPoint.x + 3.0f, m_graphCursorPoint.y}, 1.2f,
                      preview.withAlpha(canAddBand ? 0.62f : 0.28f));
    renderer.drawLine({m_graphCursorPoint.x, m_graphCursorPoint.y - 3.0f},
                      {m_graphCursorPoint.x, m_graphCursorPoint.y + 3.0f}, 1.2f,
                      preview.withAlpha(canAddBand ? 0.62f : 0.28f));

    char freqBuf[24];
    if (hz >= 1000.0f)
        std::snprintf(freqBuf, sizeof(freqBuf), "%.2f kHz", hz / 1000.0f);
    else
        std::snprintf(freqBuf, sizeof(freqBuf), "%.0f Hz", hz);

    constexpr float kW = 116.0f;
    constexpr float kH = 26.0f;
    const bool rightSide = m_graphCursorPoint.x > inner.x + inner.width * 0.58f;
    const bool topSide = m_graphCursorPoint.y < inner.y + inner.height * 0.38f;
    const float x = std::clamp(rightSide ? m_graphCursorPoint.x - kW - 12.0f : m_graphCursorPoint.x + 12.0f,
                               bounds.x + 8.0f, bounds.right() - kW - 8.0f);
    const float y = std::clamp(topSide ? m_graphCursorPoint.y + 12.0f : m_graphCursorPoint.y - kH - 12.0f,
                               bounds.y + 34.0f, bounds.bottom() - kH - 8.0f);
    const NUIRect r{x, y, kW, kH};
    renderer.fillRoundedRect(r, 6.0f, NUIColor(0.030f, 0.030f, 0.045f, canAddBand ? 0.82f : 0.62f));
    renderer.strokeRoundedRect(r, 6.0f, 1.0f, preview.withAlpha(canAddBand ? 0.26f : 0.12f));
    const NUIRect labelChip{r.x + 7.0f, r.y + 5.0f, 44.0f, 16.0f};
    renderer.fillRoundedRect(labelChip, 4.0f, preview.withAlpha(canAddBand ? 0.13f : 0.050f));
    renderer.strokeRoundedRect(labelChip, 4.0f, 1.0f, preview.withAlpha(canAddBand ? 0.24f : 0.090f));
    renderer.drawTextCentered(canAddBand ? graphCreateChipLabel(creationDefaults.type) : "FULL", labelChip, 7.0f,
                              preview.withAlpha(canAddBand ? 0.86f : 0.42f));
    renderer.drawText(freqBuf, {r.x + 60.0f, std::round(renderer.calculateTextY(r, 8.5f))}, 8.5f,
                      theme.getColor("textPrimary").withAlpha(canAddBand ? 0.78f : 0.42f));
}

void AestraEQEditor::drawAnalyzerReadout(NUIRenderer& renderer, const NUIRect& bounds) {
    const auto inner = graphInnerBounds(bounds);
    const auto now = std::chrono::steady_clock::now();
    if (m_cachedAnalyzerReadoutFreq.empty() || now - m_lastAnalyzerReadoutUpdate >= std::chrono::milliseconds(100)) {
        const auto maxIt = std::max_element(m_spectrumMagnitudes.begin(), m_spectrumMagnitudes.end());
        const size_t idx = static_cast<size_t>(std::distance(m_spectrumMagnitudes.begin(), maxIt));
        const float xNorm = m_spectrumMagnitudes.size() > 1
                                ? static_cast<float>(idx) / static_cast<float>(m_spectrumMagnitudes.size() - 1)
                                : 0.0f;
        const float hz = std::pow(10.0f, std::log10(20.0f) + xNorm * (std::log10(20000.0f) - std::log10(20.0f)));
        const float db = -72.0f + std::clamp(*maxIt, 0.0f, 1.0f) * 72.0f;

        char freqBuf[24];
        if (hz >= 1000.0f) {
            std::snprintf(freqBuf, sizeof(freqBuf), "%.2fk Hz", hz / 1000.0f);
        } else {
            std::snprintf(freqBuf, sizeof(freqBuf), "%.0f Hz", hz);
        }
        char dbBuf[24];
        std::snprintf(dbBuf, sizeof(dbBuf), "%+.0f dB", db);
        m_cachedAnalyzerReadoutFreq = freqBuf;
        m_cachedAnalyzerReadoutDb = dbBuf;
        m_lastAnalyzerReadoutUpdate = now;
    }

    auto& theme = NUIThemeManager::getInstance();
    constexpr float kW = 104.0f;
    const float x = inner.x + 10.0f;
    const float y = inner.y + 8.0f;
    renderer.drawTextCentered(m_cachedAnalyzerReadoutFreq, {x, y - 2.0f, 48.0f, 16.0f}, 8.8f,
                              theme.getColor("textSecondary").withAlpha(0.62f));
    renderer.drawTextCentered(m_cachedAnalyzerReadoutDb, {x + 58.0f, y - 2.0f, 50.0f, 16.0f}, 8.8f,
                              theme.getColor("textSecondary").withAlpha(0.62f));
}

void AestraEQEditor::drawNodeHoverTooltip(NUIRenderer& renderer, const NUIRect& bounds) {
    if (!m_hoveredBandFromGraph || m_hoveredBand < 0 || m_hoveredBand >= static_cast<int>(m_bands.size()))
        return;
    if (m_draggingGraphBand >= 0 || m_draggingCardBand >= 0 || m_numericEditActive || m_typeMenuBand >= 0 ||
        m_stereoMenuBand >= 0 || m_bandContextMenuBand >= 0) {
        return;
    }
    if (m_hoveredBand == m_selectedBand && m_nodeQuickActionRect.width > 0.0f)
        return;

    const auto& bd = m_bands[static_cast<size_t>(m_hoveredBand)];
    const auto inner = graphInnerBounds(bounds);
    const auto node = graphNodePosition(static_cast<size_t>(m_hoveredBand), bounds);
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor band = bandColor(bd.slotIndex);
    const std::string bandId = bd.name.empty() ? bandIdLabel(static_cast<size_t>(m_hoveredBand)) : bd.name;
    const std::string type = bandTypeSuffix(bd.typeName);
    const std::string freq = formatFreq(static_cast<size_t>(m_hoveredBand), bd.freq);
    std::string shape = bd.usesSlope ? formatSlope(bd.q) : (bd.usesGain ? formatGain(bd.gain) : ("Q " + formatQ(bd.q)));

    const std::string detail = freq + "   " + shape;
    const float headerW = 5.8f * static_cast<float>(bandId.size() + type.size()) + 28.0f;
    const float detailW = 5.2f * static_cast<float>(detail.size()) + 16.0f;
    const float w = std::clamp(std::max(headerW, detailW), 78.0f, 132.0f);
    constexpr float h = 28.0f;
    const float x = std::clamp(node.x - w * 0.5f, inner.x + 6.0f, inner.right() - w - 6.0f);
    float y = node.y - h - 10.0f;
    if (y < inner.y + 6.0f)
        y = node.y + 10.0f;
    y = std::clamp(y, inner.y + 6.0f, inner.bottom() - h - 6.0f);
    const NUIRect r{x, y, w, h};

    renderer.fillRoundedRect(r, 5.0f, NUIColor(0.030f, 0.030f, 0.040f, 0.90f));
    renderer.strokeRoundedRect(r, 5.0f, 1.0f, band.withAlpha(0.32f));
    const float stemX = std::clamp(node.x, r.x + 8.0f, r.right() - 8.0f);
    const float stemY = r.y > node.y ? r.y : r.bottom();
    renderer.drawLine({node.x, node.y}, {stemX, stemY}, 1.0f, band.withAlpha(0.18f));
    renderer.drawText(bandId, {r.x + 7.0f, r.y + 5.0f}, 8.0f, band.withAlpha(bd.enabled ? 0.96f : 0.44f));
    renderer.drawText(type, {r.x + 25.0f, r.y + 5.0f}, 7.7f,
                      theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.68f : 0.34f));
    renderer.fillCircle({r.right() - 8.0f, r.y + 8.0f}, 2.0f, band.withAlpha(bd.enabled ? 0.88f : 0.34f));
    renderer.drawText(detail, {r.x + 7.0f, r.y + 17.0f}, 7.8f,
                      theme.getColor("textPrimary").withAlpha(bd.enabled ? 0.82f : 0.36f));
}

void AestraEQEditor::drawSelectedNodeQuickActions(NUIRenderer& renderer, const NUIRect& bounds) {
    if (m_selectedBand < 0 || m_selectedBand >= static_cast<int>(m_bands.size()))
        return;
    if (m_nodeQuickActionRect.width <= 0.0f || m_draggingGraphBand >= 0 || m_draggingCardBand >= 0 ||
        m_numericEditActive || m_bandContextMenuBand >= 0) {
        return;
    }

    auto& theme = NUIThemeManager::getInstance();
    const auto& bd = m_bands[static_cast<size_t>(m_selectedBand)];
    const auto node = graphNodePosition(static_cast<size_t>(m_selectedBand), bounds);
    const NUIColor band = bandColor(bd.slotIndex);
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    const bool soloed = eq && eq->isBandSoloed(bd.slotIndex);
    const bool canType = bd.typeId != 0 || !bd.legacySlot;
    const bool canDelete = !bd.legacySlot;

    renderer.fillRoundedRect(m_nodeQuickActionRect, 7.0f, NUIColor(0.028f, 0.027f, 0.038f, 0.94f));
    renderer.strokeRoundedRect(m_nodeQuickActionRect, 7.0f, 1.0f, band.withAlpha(0.32f));
    const float stemX = std::clamp(node.x, m_nodeQuickActionRect.x + 10.0f, m_nodeQuickActionRect.right() - 10.0f);
    const float stemY = m_nodeQuickActionRect.y > node.y ? m_nodeQuickActionRect.y : m_nodeQuickActionRect.bottom();
    renderer.drawLine({node.x, node.y}, {stemX, stemY}, 1.0f, band.withAlpha(0.20f));

    const auto drawBase = [&](NodeQuickAction action, bool enabled, bool destructive = false) -> NUIColor {
        const size_t index = static_cast<size_t>(action);
        const NUIRect r = m_nodeQuickActionRects[index];
        const bool hovered = m_hoveredNodeQuickAction == static_cast<int>(index);
        const bool soloAction = action == NodeQuickAction::Solo && soloed;
        const NUIColor red(0.973f, 0.443f, 0.443f, 1.0f);
        const NUIColor base = destructive ? red : band;
        const NUIColor fill = soloAction ? base.withAlpha(hovered ? 0.24f : 0.17f)
                                         : (enabled && hovered ? base.withAlpha(destructive ? 0.12f : 0.15f)
                                                               : NUIColor(1.0f, 1.0f, 1.0f, enabled ? 0.026f : 0.010f));
        const NUIColor edge = soloAction ? base.withAlpha(hovered ? 0.72f : 0.54f)
                              : enabled  ? (hovered ? base.withAlpha(destructive ? 0.48f : 0.44f)
                                                    : base.withAlpha(destructive ? 0.24f : 0.20f))
                                         : NUIColor(1.0f, 1.0f, 1.0f, 0.040f);
        if (soloAction) {
            renderer.fillRoundedRect({r.x - 3.0f, r.y - 3.0f, r.width + 6.0f, r.height + 6.0f}, 7.0f,
                                     base.withAlpha(0.075f));
            renderer.fillRoundedRect({r.x - 1.5f, r.y - 1.5f, r.width + 3.0f, r.height + 3.0f}, 6.0f,
                                     base.withAlpha(0.10f));
        }
        renderer.fillRoundedRect(r, 5.0f, fill);
        renderer.strokeRoundedRect(r, 5.0f, 1.0f, edge);
        return enabled
                   ? (destructive ? red.withAlpha(hovered ? 0.98f : 0.62f)
                                  : (soloAction ? band.withAlpha(1.0f)
                                                : theme.getColor("textSecondary").withAlpha(hovered ? 0.92f : 0.62f)))
                   : theme.getColor("textSecondary").withAlpha(0.16f);
    };

    const auto prevTypeRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::TypePrev)];
    NUIColor icon = drawBase(NodeQuickAction::TypePrev, canType);
    drawSvgIcon(renderer, eqTypePrevIcon(), prevTypeRect, icon, 14.0f);

    const auto nextTypeRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::TypeNext)];
    icon = drawBase(NodeQuickAction::TypeNext, canType);
    drawSvgIcon(renderer, eqTypeNextIcon(), nextTypeRect, icon, 14.0f);

    const auto stereoRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Stereo)];
    const float stereoNorm =
        (bd.legacySlot && bd.stereoId != 0 && m_instance) ? m_instance->getParameter(bd.stereoId) : bd.stereoNorm;
    const bool scopedStereo = quantizeStereoNorm(stereoNorm) > 0.0f;
    icon = drawBase(NodeQuickAction::Stereo, true);
    renderer.drawTextCentered(stereoModeShortName(stereoNorm), stereoRect, 7.5f,
                              scopedStereo ? band.withAlpha(0.96f) : icon);

    const auto soloRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Solo)];
    icon = drawBase(NodeQuickAction::Solo, true);
    const NUIColor soloColor = soloed ? band.withAlpha(0.98f) : icon;
    if (soloed) {
        renderer.fillCircle(soloRect.center(), 2.4f, band.withAlpha(0.52f));
    }
    drawSvgIcon(renderer, headphonesIcon(), soloRect, soloColor, 14.5f);

    const auto dupRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Duplicate)];
    icon = drawBase(NodeQuickAction::Duplicate, true);
    drawSvgIcon(renderer, duplicateIcon(), dupRect, icon, 13.5f);

    const auto delRect = m_nodeQuickActionRects[static_cast<size_t>(NodeQuickAction::Delete)];
    icon = drawBase(NodeQuickAction::Delete, canDelete, true);
    drawSvgIcon(renderer, removeIcon(), delRect, icon, 12.5f);

    if (m_hoveredNodeQuickAction >= 0 && m_hoveredNodeQuickAction < static_cast<int>(NodeQuickAction::Count)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_nodeQuickActionHoverStarted < std::chrono::milliseconds(300)) {
            setDirty(true);
            return;
        }
        static constexpr const char* kTooltips[] = {
            "Prev Filter Type", "Next Filter Type", "Stereo \u00B7 Mid \u00B7 Left \u00B7 Right",
            "Audition band",    "Duplicate",        "Remove",
        };
        static_assert(std::size(kTooltips) == static_cast<size_t>(NodeQuickAction::Count),
                      "Node quick-action tooltips must match toolbar actions");
        const auto action = static_cast<NodeQuickAction>(m_hoveredNodeQuickAction);
        if (action != NodeQuickAction::Delete || canDelete) {
            const std::string label = kTooltips[static_cast<size_t>(action)];
            const float tipW = std::clamp(static_cast<float>(label.size()) * 5.6f + 16.0f, 58.0f, 170.0f);
            const auto anchor = m_nodeQuickActionRects[static_cast<size_t>(action)];
            const auto inner = graphInnerBounds(bounds);
            const float tipX = std::clamp(anchor.center().x - tipW * 0.5f, inner.x + 6.0f, inner.right() - tipW - 6.0f);
            const bool actionBarBelowNode = m_nodeQuickActionRect.y > node.y;
            float tipY = actionBarBelowNode ? m_nodeQuickActionRect.bottom() + 6.0f : m_nodeQuickActionRect.y - 24.0f;
            if (tipY < inner.y + 4.0f || actionBarBelowNode) {
                tipY = m_nodeQuickActionRect.bottom() + 6.0f;
            }
            if (tipY + 18.0f > inner.bottom() - 4.0f) {
                tipY = m_nodeQuickActionRect.y - 24.0f;
            }
            tipY = std::clamp(tipY, inner.y + 4.0f, inner.bottom() - 22.0f);
            const NUIRect tip{tipX, tipY, tipW, 18.0f};
            renderer.fillRoundedRect(tip, 5.0f, NUIColor(0.026f, 0.025f, 0.036f, 0.94f));
            renderer.strokeRoundedRect(tip, 5.0f, 1.0f, band.withAlpha(0.26f));
            renderer.drawTextCentered(label, tip, 7.7f, theme.getColor("textSecondary").withAlpha(0.86f));
        }
    }
}

void AestraEQEditor::drawResponseCurve(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();

    renderer.fillRoundedRect(bounds, 10.0f, graphBg());
    renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, accentSoft());

    const auto inner = graphInnerBounds(bounds);
    m_lastGraphInner = inner;

    const float dbRange = curveDbRange();
    const int gridStep = dbRange <= 12.0f ? 3 : (dbRange <= 24.0f ? 6 : 12);
    const int gridMax = static_cast<int>(dbRange + 0.5f);
    for (int db = -gridMax; db <= gridMax; db += gridStep) {
        const float y = inner.bottom() - (static_cast<float>(db) + dbRange) / (dbRange * 2.0f) * inner.height;
        const float alpha = (db == 0) ? 0.28f : 0.10f;
        renderer.drawLine({inner.x, y}, {inner.right(), y}, db == 0 ? 1.2f : 1.0f, NUIColor(1.0f, 1.0f, 1.0f, alpha));
        const std::string lbl = (db > 0 ? "+" : "") + std::to_string(db);
        renderer.drawTextCentered(lbl, {bounds.x + 3.0f, y - 7.0f, 26.0f, 14.0f}, 8.6f,
                                  theme.getColor("textSecondary").withAlpha(0.66f));
    }

    const float freqs[] = {30, 100, 300, 1000, 3000, 10000};
    const char* freqLabels[] = {"30", "100", "300", "1k", "3k", "10k"};
    const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
    for (int i = 0; i < 6; ++i) {
        const float norm = (std::log10(freqs[i]) - logMin) / (logMax - logMin);
        const float x = inner.x + norm * inner.width;
        renderer.drawLine({x, inner.y}, {x, inner.bottom()}, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.07f));
        renderer.drawTextCentered(freqLabels[i], {x - 14.0f, inner.bottom() + 1.0f, 28.0f, 14.0f}, 8.6f,
                                  theme.getColor("textSecondary").withAlpha(0.70f));
    }
    drawSpectrumBackdrop(renderer, bounds);
    drawAnalyzerCollisionOverlay(renderer, bounds);

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    drawBandResponseCurves(renderer, bounds);
    drawDynamicDetectorHandle(renderer, bounds);
    drawAnalyzerReadout(renderer, bounds);

    constexpr int kNumPoints = 600;
    std::vector<float> response(kNumPoints, 0.0f);
    for (int p = 0; p < kNumPoints; ++p) {
        const float t = static_cast<float>(p) / static_cast<float>(kNumPoints - 1);
        const float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
        response[p] = eq ? static_cast<float>(eq->getMagnitudeResponseDb(hz)) : 0.0f;
    }

    std::vector<NUIPoint> pts;
    pts.reserve(kNumPoints);
    for (int p = 0; p < kNumPoints; ++p) {
        const float t = static_cast<float>(p) / static_cast<float>(kNumPoints - 1);
        const float x = inner.x + t * inner.width;
        const float y =
            inner.bottom() - std::clamp((response[p] + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * inner.height;
        pts.push_back({x, y});
    }
    auto smooth = smoothCurve(pts, 4);
    float maxAbsResponse = 0.0f;
    for (float db : response) {
        maxAbsResponse = std::max(maxAbsResponse, std::abs(db));
    }

    const NUIColor curveCol(0.78f, 0.62f, 1.0f, 0.95f);
    if (maxAbsResponse > 0.02f) {
        renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 6.0f, curveCol.withAlpha(0.10f));
        renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 3.0f, curveCol.withAlpha(0.30f));
    }
    renderer.drawPolyline(smooth.data(), static_cast<int>(smooth.size()), 1.6f, curveCol);
    drawAnalyzerMenuPill(renderer);
    drawCurveScalePill(renderer);

    // Nodes
    for (size_t i = 0; i < m_bands.size(); ++i) {
        const auto& bd = m_bands[i];
        const bool selected = static_cast<int>(i) == m_selectedBand;
        const bool hovered = static_cast<int>(i) == m_hoveredBand;
        const bool dragging = static_cast<int>(i) == m_draggingGraphBand;
        const bool denseNodes = m_bands.size() > 10;
        const bool quietDynamicNode = denseNodes && !bd.legacySlot && !selected && !hovered && !dragging;
        const NUIPoint node = graphNodePosition(i, bounds);
        const float radius = quietDynamicNode ? 4.2f : (dragging ? 8.0f : (selected || hovered ? 7.0f : 5.5f));
        const NUIColor c = bandColor(bd.slotIndex);
        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        const bool soloed = eq && eq->isBandSoloed(bd.slotIndex);

        const float activeAlpha = (bd.enabled ? 1.0f : 0.36f) * (quietDynamicNode ? 0.70f : 1.0f);
        const bool dynamicBouncing = bd.dynamicEnabled && bd.usesGain && bd.enabled && bd.dynamicAmount > 0.004f;
        if (dynamicBouncing) {
            const float targetDb = -18.0f + std::clamp(bd.targetGain, 0.0f, 1.0f) * 36.0f;
            const float liveGain =
                std::clamp(bd.gain + (bd.targetGain - bd.gain) * std::clamp(bd.dynamicAmount, 0.0f, 1.0f), 0.0f, 1.0f);
            const float liveDb = -18.0f + liveGain * 36.0f;
            const float targetY =
                inner.bottom() - std::clamp((targetDb + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * inner.height;
            const float liveY =
                inner.bottom() - std::clamp((liveDb + dbRange) / (dbRange * 2.0f), 0.0f, 1.0f) * inner.height;
            const NUIPoint targetPoint{node.x, targetY};
            const NUIPoint livePoint{node.x, liveY};
            const float pulse = std::clamp(bd.dynamicAmount, 0.0f, 1.0f);
            renderer.drawLine(node, targetPoint, 1.0f, c.withAlpha(0.12f + pulse * 0.12f));
            renderer.strokeCircle(targetPoint, 5.4f, 1.0f, c.withAlpha(0.20f + pulse * 0.20f));
            renderer.fillCircle(livePoint, radius + 8.0f, c.withAlpha(0.045f + pulse * 0.090f));
            renderer.fillCircle(livePoint, radius + 4.0f, c.withAlpha(0.090f + pulse * 0.13f));
            renderer.strokeCircle(livePoint, radius + 1.2f, 1.4f, c.withAlpha(0.42f + pulse * 0.44f));
            renderer.fillCircle(livePoint, 2.2f, c.withAlpha(0.82f + pulse * 0.18f));
        }
        if (soloed) {
            renderer.fillCircle(node, radius + 10.0f, c.withAlpha(0.14f * activeAlpha));
            renderer.fillCircle(node, radius + 6.0f, c.withAlpha(0.20f * activeAlpha));
        }
        renderer.fillCircle(
            node, radius + (quietDynamicNode ? 2.4f : 4.0f),
            c.withAlpha((selected || dragging || soloed ? 0.18f : (quietDynamicNode ? 0.050f : 0.10f)) * activeAlpha));
        renderer.fillCircle(node, radius, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
        renderer.strokeCircle(node, radius, bd.enabled ? (quietDynamicNode ? 1.1f : 1.6f) : 1.1f,
                              c.withAlpha(bd.enabled ? (quietDynamicNode ? 0.58f : 1.0f) : 0.42f));
        if (bd.enabled) {
            renderer.fillCircle(node, quietDynamicNode ? 1.35f : 2.0f, c.withAlpha(quietDynamicNode ? 0.78f : 1.0f));
        } else {
            renderer.drawLine({node.x - 2.8f, node.y}, {node.x + 2.8f, node.y}, 1.0f, c.withAlpha(0.46f));
        }
        bool below = false;
        for (size_t j = 0; j < m_bands.size(); ++j) {
            if (j == i) {
                continue;
            }
            const NUIPoint otherNode = graphNodePosition(j, bounds);
            if (std::abs(node.x - otherNode.x) < 36.0f) {
                below = node.x > otherNode.x;
                break;
            }
        }
        constexpr float kLabelAboveTopOffset = 22.0f;
        constexpr float kLabelBelowTopOffset = 10.0f;
        if (!below && node.y - kLabelAboveTopOffset < inner.y + 4.0f) {
            below = true;
        }
        if (below && node.y + kLabelBelowTopOffset > inner.bottom() - 10.0f) {
            below = false;
        }
        const bool denseLabels = denseNodes;
        const bool showLabel = !denseLabels || bd.legacySlot || selected || hovered || dragging;
        if (showLabel) {
            const float labelY = below ? node.y + kLabelBelowTopOffset : node.y - kLabelAboveTopOffset;
            const float labelX = std::clamp(node.x - 6.0f, inner.x + 2.0f, inner.right() - 14.0f);
            const std::string nodeLabel = bd.name.empty() ? bandIdLabel(i) : bd.name;
            renderer.drawText(nodeLabel, {labelX, labelY}, denseLabels && !bd.legacySlot ? 8.4f : 9.0f,
                              c.withAlpha((selected || hovered || dragging ? 0.96f : 0.74f) * activeAlpha));
        }
    }

    drawSelectedNodeQuickActions(renderer, bounds);
    drawNodeHoverTooltip(renderer, bounds);
    drawGraphCursorReadout(renderer, bounds);
    drawAnalyzerSettingsPanel(renderer);
    drawFloatingBandWindow(renderer, bounds);
}

void AestraEQEditor::drawDynamicDetectorHandle(NUIRenderer& renderer, const NUIRect& bounds) {
    const int idx = (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : -1;
    if (idx < 0)
        return;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (bd.legacySlot || !bd.dynamicEnabled || bd.sidechainLinked)
        return;

    const auto inner = graphInnerBounds(bounds);
    const NUIColor c = bandColor(bd.slotIndex);
    const auto node = graphNodePosition(static_cast<size_t>(idx), bounds);
    const float x = inner.x + std::clamp(bd.sidechainFreq, 0.0f, 1.0f) * inner.width;
    const float q = std::clamp(bd.sidechainQ, 0.0f, 1.0f);
    const float halfW = std::clamp(64.0f - q * 50.0f, 12.0f, 64.0f);
    const float y = inner.bottom() - 18.0f;
    const float x1 = std::clamp(x - halfW, inner.x + 5.0f, inner.right() - 5.0f);
    const float x2 = std::clamp(x + halfW, inner.x + 5.0f, inner.right() - 5.0f);
    const bool dragging = m_draggingDetectorBand == idx;

    renderer.drawLine({node.x, node.y}, {x, y}, 1.0f, c.withAlpha(dragging ? 0.24f : 0.14f));
    renderer.drawLine({x1, y}, {x2, y}, dragging ? 2.0f : 1.4f, c.withAlpha(dragging ? 0.78f : 0.48f));
    renderer.drawLine({x, y - 13.0f}, {x, y + 13.0f}, 1.0f, c.withAlpha(dragging ? 0.42f : 0.22f));

    const NUIRect handle{x - 6.0f, y - 6.0f, 12.0f, 12.0f};
    renderer.fillRoundedRect({handle.x - 4.0f, handle.y - 4.0f, handle.width + 8.0f, handle.height + 8.0f}, 5.0f,
                             c.withAlpha(dragging ? 0.12f : 0.06f));
    renderer.fillRoundedRect(handle, 3.0f, NUIColor(0.030f, 0.030f, 0.042f, 0.94f));
    renderer.strokeRoundedRect(handle, 3.0f, dragging ? 1.6f : 1.1f, c.withAlpha(dragging ? 0.95f : 0.72f));

    char freqBuf[24];
    const float hz = graphFreqHz(bd.sidechainFreq);
    if (hz >= 1000.0f)
        std::snprintf(freqBuf, sizeof(freqBuf), "%.2fk", hz / 1000.0f);
    else
        std::snprintf(freqBuf, sizeof(freqBuf), "%.0f", hz);
    const NUIRect chip{std::clamp(x - 42.0f, inner.x + 6.0f, inner.right() - 84.0f), y + 11.0f, 84.0f, 19.0f};
    renderer.fillRoundedRect(chip, 5.0f, NUIColor(0.025f, 0.025f, 0.036f, 0.86f));
    renderer.strokeRoundedRect(chip, 5.0f, 1.0f, c.withAlpha(0.24f));
    renderer.drawText("DET", {chip.x + 7.0f, std::round(renderer.calculateTextY(chip, 7.4f))}, 7.4f,
                      c.withAlpha(0.76f));
    renderer.drawText(freqBuf, {chip.x + 31.0f, std::round(renderer.calculateTextY(chip, 7.4f))}, 7.4f,
                      NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.76f));
}

void AestraEQEditor::drawFloatingBandWindow(NUIRenderer& renderer, const NUIRect& bounds) {
    if (!m_bandInspectorCollapsed)
        return;
    const int idx = currentFloatingBandIndex();
    if (idx < 0)
        return;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    const auto layout = floatingBandPanelLayout(idx, bounds);
    if (!layout.valid)
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto node = graphNodePosition(static_cast<size_t>(idx), bounds);
    const NUIRect r = layout.panel;
    const NUIColor c = bandColor(bd.slotIndex);

    renderer.fillRoundedRect(r, 6.0f, NUIColor(0.051f, 0.051f, 0.059f, 0.96f));
    renderer.strokeRoundedRect(r, 6.0f, 1.0f, c.withAlpha(bd.enabled ? 0.35f : 0.16f));
    const float connectorX = std::clamp(node.x, r.x + 12.0f, r.right() - 12.0f);
    const float connectorY = node.y < r.y ? r.y + 8.0f : r.bottom() - 8.0f;
    renderer.drawLine({node.x, node.y}, {connectorX, connectorY}, 1.0f, c.withAlpha(0.24f));

    std::string typeLabel = bandTypeSuffix(bd.typeName);
    const std::string bandId = bd.name.empty() ? bandIdLabel(static_cast<size_t>(idx)) : bd.name;
    constexpr float kTooltipHeaderFont = 11.0f;
    const NUIRect tooltipHeaderTextRect{r.x + 8.0f, r.y + 6.0f, r.width - 28.0f, 14.0f};
    const float tooltipHeaderY = std::round(renderer.calculateTextY(tooltipHeaderTextRect, kTooltipHeaderFont));
    renderer.drawText(bandId, {r.x + 8.0f, tooltipHeaderY}, kTooltipHeaderFont,
                      c.withAlpha(bd.enabled ? 0.98f : 0.44f));
    renderer.fillRoundedRect(layout.typeRect, 4.0f,
                             bd.enabled ? c.withAlpha(0.055f) : NUIColor(1.0f, 1.0f, 1.0f, 0.010f));
    if (bd.typeId != 0 || !bd.legacySlot)
        renderer.strokeRoundedRect(layout.typeRect, 4.0f, 1.0f, c.withAlpha(0.16f));
    renderer.drawTextCentered(typeLabel, layout.typeRect, 8.0f,
                              theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.76f : 0.36f));
    const float stereoNorm =
        (bd.legacySlot && m_instance && bd.stereoId != 0) ? m_instance->getParameter(bd.stereoId) : bd.stereoNorm;
    const bool stereoScoped = quantizeStereoNorm(stereoNorm) > 0.0f;
    renderer.fillRoundedRect(layout.stereoRect, 4.0f,
                             stereoScoped ? c.withAlpha(0.18f) : NUIColor(1.0f, 1.0f, 1.0f, 0.028f));
    renderer.strokeRoundedRect(layout.stereoRect, 4.0f, 1.0f,
                               stereoScoped ? c.withAlpha(0.40f) : NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
    renderer.drawTextCentered(stereoModeShortName(stereoNorm), layout.stereoRect, 7.4f,
                              stereoScoped ? c.withAlpha(0.96f) : theme.getColor("textSecondary").withAlpha(0.58f));
    renderer.fillCircle(layout.enableRect.center(), 3.0f,
                        bd.enabled ? c.withAlpha(0.95f) : theme.getColor("textSecondary").withAlpha(0.28f));
    renderer.drawLine({r.x + 8.0f, r.y + 25.0f}, {r.right() - 8.0f, r.y + 25.0f}, 1.0f, NUIColor(1, 1, 1, 0.07f));

    auto drawRow = [&](const NUIRect& row, const char* label, const std::string& value, float normalized, Knob target) {
        constexpr float labelW = 44.0f;
        const float amount = std::clamp(normalized, 0.0f, 1.0f);
        const bool draggingThis = m_draggingCardBand == idx && m_draggingKnob == target &&
                                  std::abs(m_draggingLaneRect.x - row.x) < 0.5f &&
                                  std::abs(m_draggingLaneRect.y - row.y) < 0.5f;
        const float valueW = std::min(70.0f, std::max(46.0f, static_cast<float>(value.size()) * 5.6f + 12.0f));
        const float trackX = row.x + labelW + 6.0f;
        const float trackW = std::max(10.0f, row.width - labelW - valueW - 16.0f);
        const float trackY = row.center().y + 5.0f;
        const float tickX = trackX + trackW * amount;

        renderer.fillRoundedRect(row, 4.0f, NUIColor(0.018f, 0.018f, 0.024f, 0.86f));
        renderer.fillRoundedRect({row.x, row.y, labelW, row.height}, 4.0f,
                                 bd.enabled ? c.withAlpha(draggingThis ? 0.16f : 0.075f) : c.withAlpha(0.025f));
        renderer.fillRoundedRect({trackX, trackY - 1.0f, trackW, 2.0f}, 1.0f, NUIColor(1, 1, 1, 0.055f));
        renderer.fillRoundedRect({trackX, trackY - 1.0f, std::max(2.0f, tickX - trackX), 2.0f}, 1.0f,
                                 bd.enabled ? c.withAlpha(draggingThis ? 0.55f : 0.26f) : c.withAlpha(0.08f));
        renderer.fillRoundedRect({row.right() - valueW - 4.0f, row.y + 3.0f, valueW, row.height - 6.0f}, 3.0f,
                                 NUIColor(0, 0, 0, bd.enabled ? 0.20f : 0.10f));
        renderer.strokeRoundedRect(row, 4.0f, 1.0f,
                                   bd.enabled ? c.withAlpha(draggingThis ? 0.46f : 0.16f) : NUIColor(1, 1, 1, 0.055f));
        renderer.drawText(label, {row.x + 7.0f, row.y + 4.0f}, 9.5f,
                          theme.getColor("textSecondary").withAlpha(bd.enabled ? 0.64f : 0.32f));
        const float valueFont = value.size() > 8 ? 8.2f : 9.0f;
        renderer.drawText(value, {row.right() - valueW + 2.0f, std::round(renderer.calculateTextY(row, valueFont))},
                          valueFont, c.withAlpha(bd.enabled ? 0.94f : 0.40f));
        renderer.drawLine({tickX, row.y + 4.0f}, {tickX, row.bottom() - 4.0f}, draggingThis ? 1.5f : 1.0f,
                          bd.enabled ? c.withAlpha(draggingThis ? 0.92f : 0.48f) : c.withAlpha(0.16f));
    };

    drawRow(layout.freqRect, "Freq",
            bd.legacySlot ? formatFrequencyWithUnit(bd.slotIndex, bd.freq) : formatDynamicFrequencyWithUnit(bd.freq),
            bd.freq, Knob::Freq);
    if (bd.usesSlope) {
        drawRow(layout.gainRect, "Slope", std::to_string(slopeDbFromNorm(bd.q)) + " dB/oct", quantizeSlopeNorm(bd.q),
                Knob::Gain);
    } else if (bd.usesGain) {
        drawRow(layout.gainRect, "Gain", formatGain(bd.gain), bd.gain, Knob::Gain);
        drawRow(layout.qRect, "Q", formatQ(bd.q), bd.q, Knob::Q);
    } else {
        drawRow(layout.gainRect, "Q", formatQ(bd.q), bd.q, Knob::Q);
    }
}

void AestraEQEditor::drawBandTypeMenu(NUIRenderer& renderer) {
    if (m_typeMenuBand < 0 || m_typeMenuBand >= static_cast<int>(m_bands.size()))
        return;
    const auto& bd = m_bands[static_cast<size_t>(m_typeMenuBand)];
    if (bd.typeId == 0 && bd.legacySlot)
        return;

    auto& theme = NUIThemeManager::getInstance();
    const NUIColor band = bandColor(bd.slotIndex);
    const size_t optionCount = bd.legacySlot ? 4u : m_typeOptionRects.size();
    const int current =
        bd.legacySlot
            ? std::clamp(static_cast<int>(std::round(
                             quantizeTypeNorm(m_instance ? m_instance->getParameter(bd.typeId) : bd.typeNorm) * 3.0f)),
                         0, 3)
            : std::clamp(static_cast<int>(std::round(std::clamp(bd.typeNorm, 0.0f, 1.0f) * 7.0f)), 0, 7);
    renderer.fillRoundedRect(m_typeMenuRect, 8.0f, NUIColor(0.030f, 0.030f, 0.045f, 0.96f));
    renderer.strokeRoundedRect(m_typeMenuRect, 8.0f, 1.0f, band.withAlpha(0.42f));

    for (size_t i = 0; i < optionCount; ++i) {
        const bool selected = static_cast<int>(i) == current;
        const bool hovered = static_cast<int>(i) == m_hoveredTypeOption;
        const auto r = m_typeOptionRects[i];
        if (selected || hovered) {
            renderer.fillRoundedRect(r, 5.0f, band.withAlpha(selected ? 0.22f : 0.12f));
        }
        renderer.drawText(bd.legacySlot ? middleBandTypeName(static_cast<float>(i) / 3.0f) : dynamicBandTypeName(i),
                          {r.x + 8.0f, std::round(renderer.calculateTextY(r, 9.0f))}, 9.0f,
                          selected ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.84f));
    }
}

void AestraEQEditor::drawBandStereoMenu(NUIRenderer& renderer) {
    if (m_stereoMenuBand < 0 || m_stereoMenuBand >= static_cast<int>(m_bands.size()))
        return;
    const auto& bd = m_bands[static_cast<size_t>(m_stereoMenuBand)];

    auto& theme = NUIThemeManager::getInstance();
    const NUIColor band = bandColor(bd.slotIndex);
    const float stereoNorm =
        (bd.legacySlot && bd.stereoId != 0 && m_instance) ? m_instance->getParameter(bd.stereoId) : bd.stereoNorm;
    const int current = std::clamp(static_cast<int>(std::round(quantizeStereoNorm(stereoNorm) * 4.0f)), 0, 4);

    renderer.fillRoundedRect(m_stereoMenuRect, 8.0f, NUIColor(0.030f, 0.030f, 0.045f, 0.96f));
    renderer.strokeRoundedRect(m_stereoMenuRect, 8.0f, 1.0f, band.withAlpha(0.42f));

    static constexpr const char* kLabels[] = {"Stereo", "Left", "Right", "Mid", "Side"};
    static constexpr const char* kShort[] = {"ST", "L", "R", "M", "S"};
    for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
        const bool selected = static_cast<int>(i) == current;
        const bool hovered = static_cast<int>(i) == m_hoveredStereoOption;
        const auto r = m_stereoOptionRects[i];
        if (selected || hovered) {
            renderer.fillRoundedRect(r, 5.0f, band.withAlpha(selected ? 0.22f : 0.12f));
        }
        renderer.drawText(kLabels[i], {r.x + 8.0f, std::round(renderer.calculateTextY(r, 9.0f))}, 9.0f,
                          selected ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.84f));
        const NUIRect chip{r.right() - 26.0f, r.y + 3.0f, 20.0f, 14.0f};
        renderer.strokeRoundedRect(chip, 4.0f, 1.0f,
                                   selected ? band.withAlpha(0.46f) : theme.getColor("textSecondary").withAlpha(0.16f));
        renderer.drawTextCentered(kShort[i], chip, 7.2f,
                                  selected ? band.withAlpha(0.98f) : theme.getColor("textSecondary").withAlpha(0.56f));
    }
}

void AestraEQEditor::drawBandContextMenu(NUIRenderer& renderer) {
    if (m_bandContextMenuBand < 0 || m_bandContextMenuBand >= static_cast<int>(m_bands.size()))
        return;

    auto& theme = NUIThemeManager::getInstance();
    const auto& bd = m_bands[static_cast<size_t>(m_bandContextMenuBand)];
    const NUIColor band = bandColor(bd.slotIndex);
    const std::string bandId = bd.name.empty() ? bandIdLabel(static_cast<size_t>(m_bandContextMenuBand)) : bd.name;
    const std::string typeLabel = bandTypeSuffix(bd.typeName);
    renderer.fillRoundedRect(m_bandContextMenuRect, 8.0f, NUIColor(0.030f, 0.030f, 0.045f, 0.96f));
    renderer.strokeRoundedRect(m_bandContextMenuRect, 8.0f, 1.0f, band.withAlpha(0.34f));
    renderer.drawText(bandId, {m_bandContextMenuRect.x + 10.0f, m_bandContextMenuRect.y + 10.0f}, 9.5f,
                      band.withAlpha(0.96f));
    renderer.drawText(typeLabel, {m_bandContextMenuRect.x + 44.0f, m_bandContextMenuRect.y + 10.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.70f));
    renderer.drawLine({m_bandContextMenuRect.x + 8.0f, m_bandContextMenuRect.y + 29.0f},
                      {m_bandContextMenuRect.right() - 8.0f, m_bandContextMenuRect.y + 29.0f}, 1.0f,
                      NUIColor(1, 1, 1, 0.060f));

    static constexpr BandMenuAction kActions[] = {
        BandMenuAction::Reset,   BandMenuAction::InvertGain, BandMenuAction::ToggleDynamic, BandMenuAction::SplitLR,
        BandMenuAction::SplitMS, BandMenuAction::Copy,       BandMenuAction::Paste,         BandMenuAction::Duplicate,
        BandMenuAction::Delete,  BandMenuAction::ClearAll};
    static constexpr const char* kLabels[] = {"Reset band", "Invert gain",    "Dynamic EQ", "Split L/R",
                                              "Split M/S",  "Copy",           "Paste",      "Duplicate",
                                              "Delete",     "Clear all bands"};
    static constexpr const char* kShortcuts[] = {"Home", "Ctrl+I", "", "", "", "Ctrl+C", "Ctrl+V", "Ctrl+D", "Del", ""};
    static_assert(std::size(kActions) == std::tuple_size_v<decltype(m_bandContextOptionRects)>,
                  "Band context draw actions must match option rows");
    static_assert(std::size(kLabels) == std::size(kActions), "Band context labels must match actions");
    static_assert(std::size(kShortcuts) == std::size(kActions), "Band context shortcuts must match actions");
    const auto contextIcon = [](BandMenuAction action) -> std::shared_ptr<NUIIcon> {
        switch (action) {
        case BandMenuAction::Reset:
            return resetIcon();
        case BandMenuAction::InvertGain:
            return invertIcon();
        case BandMenuAction::ToggleDynamic:
            return dynamicIcon();
        case BandMenuAction::SplitLR:
            return splitLRIcon();
        case BandMenuAction::SplitMS:
            return splitMSIcon();
        case BandMenuAction::Copy:
            return copyIcon();
        case BandMenuAction::Paste:
            return pasteIcon();
        case BandMenuAction::Duplicate:
            return duplicateIcon();
        case BandMenuAction::Delete:
            return removeIcon();
        case BandMenuAction::ClearAll:
            return clearAllIcon();
        }
        return nullptr;
    };
    for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
        const auto r = m_bandContextOptionRects[i];
        const bool hovered = static_cast<int>(i) == m_hoveredBandContextOption;
        const bool enabled = canApplyBandContextAction(kActions[i]);
        const bool dynamic = kActions[i] == BandMenuAction::ToggleDynamic;
        const bool dynamicOn = dynamic && bd.dynamicEnabled;
        const bool destructive = kActions[i] == BandMenuAction::Delete || kActions[i] == BandMenuAction::ClearAll;
        const bool clipboard = kActions[i] == BandMenuAction::Copy || kActions[i] == BandMenuAction::Paste ||
                               kActions[i] == BandMenuAction::Duplicate;
        if (i == 3 || i == 5 || i == 8 || i == 9) {
            renderer.drawLine({m_bandContextMenuRect.x + 10.0f, r.y - 5.0f},
                              {m_bandContextMenuRect.right() - 10.0f, r.y - 5.0f}, 1.0f,
                              NUIColor(1, 1, 1, destructive ? 0.070f : 0.052f));
        }
        if ((hovered && enabled) || dynamicOn) {
            renderer.fillRoundedRect(r, 5.0f,
                                     destructive ? NUIColor(0.973f, 0.443f, 0.443f, 0.095f)
                                                 : band.withAlpha(dynamicOn ? 0.18f : (clipboard ? 0.16f : 0.14f)));
            renderer.strokeRoundedRect(r, 5.0f, 1.0f,
                                       destructive ? NUIColor(0.973f, 0.443f, 0.443f, 0.34f)
                                                   : band.withAlpha(dynamicOn ? 0.36f : 0.24f));
        }
        const NUIColor textColor = destructive
                                       ? NUIColor(0.973f, 0.443f, 0.443f, enabled ? (hovered ? 0.95f : 0.62f) : 0.22f)
                                       : (dynamicOn ? band.withAlpha(0.96f)
                                                    : (enabled ? theme.getColor("textSecondary").withAlpha(0.88f)
                                                               : theme.getColor("textSecondary").withAlpha(0.32f)));
        drawSvgIcon(renderer, contextIcon(kActions[i]), {r.x + 8.0f, r.y + 4.0f, 14.0f, 14.0f}, textColor, 12.5f);
        renderer.drawText(kLabels[i], {r.x + 28.0f, std::round(renderer.calculateTextY(r, 9.0f))}, 9.0f, textColor);
        if (kShortcuts[i][0] != '\0') {
            renderer.drawText(kShortcuts[i], {r.right() - 44.0f, std::round(renderer.calculateTextY(r, 8.2f))}, 8.2f,
                              destructive ? NUIColor(0.973f, 0.443f, 0.443f, enabled ? 0.40f : 0.14f)
                                          : (enabled ? theme.getColor("textSecondary").withAlpha(0.44f)
                                                     : theme.getColor("textSecondary").withAlpha(0.18f)));
        }
    }
}

void AestraEQEditor::drawNumericEditBox(NUIRenderer& renderer) {
    if (!m_numericEditActive)
        return;
    auto& theme = NUIThemeManager::getInstance();
    const auto r = numericEditBounds();
    const NUIColor band = m_numericEditBand >= 0 && m_numericEditBand < static_cast<int>(m_bands.size())
                              ? bandColor(m_bands[static_cast<size_t>(m_numericEditBand)].slotIndex)
                              : accent();
    renderer.fillRoundedRect(r, 7.0f, NUIColor(0.030f, 0.030f, 0.045f, 0.96f));
    renderer.strokeRoundedRect(r, 7.0f, 1.2f, band.withAlpha(0.62f));
    const char* label = "VALUE";
    switch (m_numericEditTarget) {
    case Knob::Freq:
        label = "FREQ";
        break;
    case Knob::Gain:
        if (m_numericEditBand >= 0 && m_numericEditBand < static_cast<int>(m_bands.size())) {
            const auto& bd = m_bands[static_cast<size_t>(m_numericEditBand)];
            label = bd.usesGain ? "GAIN" : (bd.usesSlope ? "SLOPE" : "Q");
        }
        break;
    case Knob::Q:
        label = "Q";
        break;
    default:
        break;
    }
    renderer.drawText(label, {r.x + 10.0f, r.y + 7.0f}, 8.0f, theme.getColor("textSecondary").withAlpha(0.78f));
    renderer.drawText(m_numericEditText + "_", {r.x + 52.0f, r.y + 6.0f}, 10.0f,
                      theme.getColor("textPrimary").withAlpha(0.94f));
}

void AestraEQEditor::drawContent(NUIRenderer& renderer, const NUIRect& /*contentRect*/) {
    updateSpectrumSnapshot();
    syncBandsFromPlugin();
    // Recompute layout each frame so cached absolute rects follow window drag.
    layoutControls();
    const bool dynamicAnimating = std::any_of(m_bands.begin(), m_bands.end(), [](const Band& band) {
        return band.enabled && band.dynamicEnabled && band.usesGain && band.dynamicAmount > 0.001f;
    });
    if (dynamicAnimating) {
        setDirty(true);
    }

    drawBypassPill(renderer);
    drawOutputGainPill(renderer);
    drawPolarityPill(renderer);
    drawComparePills(renderer);
    drawResponseCurve(renderer, m_graphBounds);
    for (size_t i = 0; i < m_bands.size(); ++i) {
        drawBandCard(renderer, i);
    }
    drawBandTypeMenu(renderer);
    drawBandStereoMenu(renderer);
    drawBandContextMenu(renderer);
    drawNumericEditBox(renderer);
}

// ---- Spectrum worker ----
void AestraEQEditor::updateSpectrumSnapshot() {
    if (m_analyzerFrozen)
        return;

    std::array<float, 160> next{};
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        if (m_spectrumResultReady) {
            next = m_workerResultMagnitudes;
            m_lastAnalyzerSerial = m_workerResultSerial;
            m_pendingAnalyzerSerial = 0;
            m_spectrumResultReady = false;
            ready = true;
        }
    }
    if (ready) {
        static constexpr float kRiseRates[] = {0.82f, 0.55f, 0.35f};
        static constexpr float kFallHold[] = {0.78f, 0.92f, 0.975f};
        const size_t decayIdx = std::min<size_t>(m_analyzerDecayIndex, 2u);
        const float rise = kRiseRates[decayIdx];
        const float hold = kFallHold[decayIdx];
        const float peakHold = decayIdx == 0 ? 0.88f : (decayIdx == 1 ? 0.945f : 0.982f);
        for (size_t i = 0; i < m_spectrumMagnitudes.size(); ++i) {
            const float cur = m_spectrumMagnitudes[i];
            const float tgt = next[i];
            if (tgt >= cur)
                m_spectrumMagnitudes[i] = cur + (tgt - cur) * rise;
            else
                m_spectrumMagnitudes[i] = cur * hold + tgt * (1.0f - hold);
            m_spectrumPeakMagnitudes[i] = std::max(m_spectrumMagnitudes[i], m_spectrumPeakMagnitudes[i] * peakHold);
        }
    }
    std::array<float, 160> nextCollision{};
    bool collisionReady = false;
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        if (m_spectrumCollisionResultReady) {
            nextCollision = m_workerResultCollisionMagnitudes;
            m_spectrumCollisionResultReady = false;
            collisionReady = true;
        }
    }
    if (collisionReady) {
        const float rise = 0.58f;
        const float hold = 0.88f;
        for (size_t i = 0; i < m_collisionMagnitudes.size(); ++i) {
            const float cur = m_collisionMagnitudes[i];
            const float tgt = nextCollision[i];
            m_collisionMagnitudes[i] = tgt >= cur ? cur + (tgt - cur) * rise : cur * hold + tgt * (1.0f - hold);
        }
    } else if (m_analyzerCollisionEnabled) {
        for (auto& v : m_collisionMagnitudes)
            v *= 0.96f;
    } else {
        for (auto& v : m_collisionMagnitudes)
            v *= 0.82f;
    }

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq)
        return;
    uint64_t serial = 0;
    uint64_t collisionSerial = 0;
    const auto source = m_analyzerSourceIndex == 0 ? Aestra::Audio::Plugins::AestraEQ::AnalyzerSource::Pre
                                                   : Aestra::Audio::Plugins::AestraEQ::AnalyzerSource::Post;
    const auto stereoMode = analyzerStereoMode();
    if (!eq->getAnalyzerWindow(m_analyzerWindow, &serial, source, stereoMode) || m_pendingAnalyzerSerial != 0)
        return;

    std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> collisionWindow{};
    bool hasCollisionWindow = false;
    if (m_analyzerCollisionEnabled) {
        const auto collisionSource = m_analyzerSourceIndex == 0 ? Aestra::Audio::Plugins::AestraEQ::AnalyzerSource::Post
                                                                : Aestra::Audio::Plugins::AestraEQ::AnalyzerSource::Pre;
        hasCollisionWindow = eq->getAnalyzerWindow(collisionWindow, &collisionSerial, collisionSource, stereoMode);
    }
    const uint64_t requestSerial = serial ^ (hasCollisionWindow ? (collisionSerial << 1u) : 0u);
    if (requestSerial == m_lastAnalyzerSerial)
        return;

    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_workerAnalyzerWindow = m_analyzerWindow;
        m_workerAnalyzerCollisionWindow = collisionWindow;
        m_spectrumCollisionWorkPending = hasCollisionWindow;
        m_workerRequestedSerial = requestSerial;
        m_spectrumWorkPending = true;
        m_pendingAnalyzerSerial = requestSerial;
    }
    m_spectrumCv.notify_one();
}

void AestraEQEditor::analyzerWorkerMain() {
    while (true) {
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> win{};
        std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> collisionWin{};
        uint64_t serial = 0;
        bool computeCollision = false;
        {
            std::unique_lock<std::mutex> lock(m_spectrumMutex);
            m_spectrumCv.wait(lock, [this]() { return m_spectrumStop || m_spectrumWorkPending; });
            if (m_spectrumStop)
                return;
            win = m_workerAnalyzerWindow;
            collisionWin = m_workerAnalyzerCollisionWindow;
            computeCollision = m_spectrumCollisionWorkPending;
            serial = m_workerRequestedSerial;
            m_spectrumWorkPending = false;
            m_spectrumCollisionWorkPending = false;
        }
        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        const double sr = eq ? std::max(1.0, eq->getAnalyzerSampleRate()) : 48000.0;
        const float analyzerTilt = analyzerTiltDbPerOct();
        constexpr size_t kBins = 160;
        const auto computeMagnitudes =
            [&](const std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize>& input) {
                std::array<float, kBins> mags{};
                std::array<float, Aestra::Audio::Plugins::AestraEQ::kAnalyzerWindowSize> w{};
                const size_t N = input.size();
                const float div = N > 1 ? static_cast<float>(N - 1) : 1.0f;
                for (size_t n = 0; n < N; ++n) {
                    const float h = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(n) / div);
                    w[n] = input[n] * h;
                }
                const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
                for (size_t bin = 0; bin < kBins; ++bin) {
                    const float t = static_cast<float>(bin) / static_cast<float>(kBins - 1);
                    const float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
                    const float omega = 2.0f * kPi * hz / static_cast<float>(sr);
                    const float cs = std::cos(omega), sn = std::sin(omega);
                    const float coeff = 2.0f * cs;
                    float q1 = 0, q2 = 0;
                    for (size_t n = 0; n < N; ++n) {
                        const float q0 = coeff * q1 - q2 + w[n];
                        q2 = q1;
                        q1 = q0;
                    }
                    const float re = q1 - q2 * cs;
                    const float im = q2 * sn;
                    const float mag = std::sqrt(re * re + im * im) / static_cast<float>(N);
                    const float db = 20.0f * std::log10(std::max(mag * 8.0f, 1.0e-5f));
                    const float tiltedDb = db + analyzerTilt * std::log2(std::max(hz, 20.0f) / 1000.0f);
                    mags[bin] = std::clamp((tiltedDb + 72.0f) / 72.0f, 0.0f, 1.0f);
                }
                return mags;
            };

        const auto mags = computeMagnitudes(win);
        std::array<float, kBins> collision{};
        bool hasCollision = false;
        if (computeCollision) {
            const auto other = computeMagnitudes(collisionWin);
            const float strength = analyzerCollisionStrength();
            const float threshold = 0.24f - strength * 0.045f;
            const float scale = 1.12f + strength * 0.34f;
            const float proximityScale = 1.65f + strength * 0.55f;
            for (size_t i = 0; i < kBins; ++i) {
                const float overlap = std::min(mags[i], other[i]);
                const float proximity = 1.0f - std::min(std::abs(mags[i] - other[i]) * proximityScale, 1.0f);
                collision[i] = std::clamp((overlap - threshold) * scale, 0.0f, 1.0f) * proximity;
            }
            hasCollision = true;
        }
        {
            std::lock_guard<std::mutex> lock(m_spectrumMutex);
            m_workerResultMagnitudes = mags;
            m_workerResultCollisionMagnitudes = collision;
            m_workerResultSerial = serial;
            m_spectrumResultReady = true;
            m_spectrumCollisionResultReady = hasCollision;
        }
    }
}

// ---- Formatters ----
std::string AestraEQEditor::formatFreq(size_t idx, float norm) const {
    const bool dynamic = idx < m_bands.size() && !m_bands[idx].legacySlot;
    const float hz = dynamic ? graphFreqHz(norm) : bandFreqHz(m_bands[idx].slotIndex, norm);
    char buf[32];
    if (hz >= 1000.0f)
        std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
    else
        std::snprintf(buf, sizeof(buf), "%d Hz", static_cast<int>(hz + 0.5f));
    return buf;
}
std::string AestraEQEditor::formatGain(float norm) const {
    float db = -18.0f + norm * 36.0f;
    if (std::abs(db) < 0.05f) {
        db = 0.0f;
    }
    char buf[16];
    if (db == 0.0f) {
        std::snprintf(buf, sizeof(buf), "0.0dB");
    } else if (db > 0.0f) {
        std::snprintf(buf, sizeof(buf), "+%.1fdB", db);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1fdB", db);
    }
    return buf;
}
std::string AestraEQEditor::formatQ(float norm) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", 0.1f + norm * 9.9f);
    return buf;
}
std::string AestraEQEditor::formatSlope(float norm) const {
    return std::to_string(slopeDbFromNorm(norm)) + "dB/oct";
}

// ---- Interaction ----
int AestraEQEditor::hitTestGraphNode(float x, float y) const {
    if (!m_graphBounds.contains({x, y}))
        return -1;
    int best = -1;
    float bestD = 16.0f;
    for (size_t i = 0; i < m_bands.size(); ++i) {
        const NUIPoint n = graphNodePosition(i, m_graphBounds);
        const float d = std::hypot(n.x - x, n.y - y);
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int AestraEQEditor::hitTestDynamicDetectorHandle(float x, float y) const {
    const int idx = (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : -1;
    if (idx < 0 || !m_graphBounds.contains({x, y}))
        return -1;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (bd.legacySlot || !bd.dynamicEnabled || bd.sidechainLinked)
        return -1;

    const auto inner = graphInnerBounds(m_graphBounds);
    const float handleX = inner.x + std::clamp(bd.sidechainFreq, 0.0f, 1.0f) * inner.width;
    const float handleY = inner.bottom() - 18.0f;
    const NUIRect hit{handleX - 12.0f, handleY - 14.0f, 24.0f, 28.0f};
    return hit.contains({x, y}) ? idx : -1;
}

void AestraEQEditor::updateBandFromGraphPosition(int idx, const NUIPoint& p, NUIModifiers modifiers) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    const auto inner = graphInnerBounds(m_graphBounds);
    auto& bd = m_bands[idx];
    const bool fine = modifiers & NUIModifiers::Shift;
    const bool lockFrequency = (modifiers & NUIModifiers::Ctrl) || (modifiers & NUIModifiers::Super);

    float nextFreq = bd.freq;
    float nextGain = bd.gain;
    if (fine) {
        const float dx = (p.x - m_graphDragStartPosition.x) / std::max(1.0f, inner.width);
        const float dy = (m_graphDragStartPosition.y - p.y) / std::max(1.0f, inner.height);
        nextFreq = std::clamp(m_graphDragStartFreq + dx * 0.25f, 0.0f, 1.0f);
        nextGain = std::clamp(m_graphDragStartGain + dy * 0.25f, 0.0f, 1.0f);
    } else {
        const float xn = std::clamp((p.x - inner.x) / std::max(1.0f, inner.width), 0.0f, 1.0f);
        const float yn = std::clamp(1.0f - (p.y - inner.y) / std::max(1.0f, inner.height), 0.0f, 1.0f);
        const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        const float hz = std::pow(10.0f, logMin + xn * (logMax - logMin));
        nextFreq = bd.legacySlot ? bandNormFromHz(bd.slotIndex, hz) : graphNormFromHz(hz);
        const float db = yn * curveDbRange() * 2.0f - curveDbRange();
        nextGain = std::clamp((db + 18.0f) / 36.0f, 0.0f, 1.0f);
    }

    if (lockFrequency)
        nextFreq = m_graphDragStartFreq;
    bd.freq = nextFreq;
    if (bd.legacySlot) {
        m_instance->setParameter(bd.freqId, bd.freq);
    }
    if (bd.usesGain) {
        bd.gain = nextGain;
        if (bd.legacySlot) {
            m_instance->setParameter(bd.gainId, bd.gain);
        }
    }
    if (!bd.legacySlot) {
        writeDynamicBandSnapshot(idx);
    }
    setDirty(true);
}

void AestraEQEditor::updateDynamicDetectorFromGraphPosition(int idx, const NUIPoint& p, NUIModifiers modifiers) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    auto& bd = m_bands[static_cast<size_t>(idx)];
    if (bd.legacySlot || !bd.dynamicEnabled || bd.sidechainLinked)
        return;

    const auto inner = graphInnerBounds(m_graphBounds);
    const bool fine = modifiers & NUIModifiers::Shift;
    if (fine) {
        const float dx = (p.x - m_graphDragStartPosition.x) / std::max(1.0f, inner.width);
        bd.sidechainFreq = std::clamp(m_detectorDragStartFreq + dx * 0.25f, 0.0f, 1.0f);
    } else {
        bd.sidechainFreq = std::clamp((p.x - inner.x) / std::max(1.0f, inner.width), 0.0f, 1.0f);
    }

    bd.sidechainQ = std::clamp(m_detectorDragStartQ + (m_graphDragStartPosition.y - p.y) / 180.0f, 0.0f, 1.0f);
    writeDynamicBandSnapshot(idx);
    setDirty(true);
}

bool AestraEQEditor::writeDynamicBandSnapshot(int bandIdx) {
    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size()))
        return false;
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq)
        return false;
    const auto& bd = m_bands[static_cast<size_t>(bandIdx)];
    if (bd.legacySlot)
        return false;

    Aestra::Audio::Plugins::AestraEQ::DynamicBandSlotDefaults next{
        bd.enabled, filterTypeFromNorm(bd.typeNorm), stereoModeFromNorm(bd.stereoNorm), bd.freq, bd.gain, bd.q, false,
    };
    next.dynamicEnabled = bd.dynamicEnabled;
    next.targetGainNorm = bd.targetGain;
    next.thresholdNorm = bd.dynamicThreshold;
    next.kneeNorm = bd.dynamicKnee;
    next.attackNorm = bd.dynamicAttack;
    next.releaseNorm = bd.dynamicRelease;
    next.sidechainLinked = bd.sidechainLinked;
    next.sidechainType = bd.sidechainType;
    next.sidechainFrequencyNorm = bd.sidechainFreq;
    next.sidechainQNorm = bd.sidechainQ;
    return eq->setDynamicBandSlot(bd.slotIndex, next);
}

int AestraEQEditor::hitTestBandCard(float x, float y, Knob& outKnob) const {
    const int selectedIdx =
        (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : 0;
    if (selectedIdx >= 0 && selectedIdx < static_cast<int>(m_bands.size()) && m_bandInspectorRect.contains({x, y})) {
        const auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
        if ((bd.typeId != 0 || !bd.legacySlot) && bd.typeButton.contains({x, y})) {
            outKnob = Knob::Type;
            return selectedIdx;
        }
        if (bd.stereoButton.contains({x, y})) {
            outKnob = Knob::Stereo;
            return selectedIdx;
        }
        if (bd.freqKnob.contains({x, y})) {
            outKnob = Knob::Freq;
            return selectedIdx;
        }
        if (bd.gainKnob.contains({x, y})) {
            outKnob = Knob::Gain;
            return selectedIdx;
        }
        if (!bd.usesSlope && bd.usesGain && bd.qKnob.contains({x, y})) {
            outKnob = Knob::Q;
            return selectedIdx;
        }
        outKnob = Knob::None;
        return selectedIdx;
    }

    for (size_t i = 0; i < m_bands.size(); ++i) {
        const auto& bd = m_bands[i];
        if (!bd.cardBounds.contains({x, y}))
            continue;
        outKnob = Knob::None;
        return static_cast<int>(i);
    }
    outKnob = Knob::None;
    return -1;
}

void AestraEQEditor::setBandValue(int idx, Knob target, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    auto& bd = m_bands[idx];
    v = std::clamp(v, 0.0f, 1.0f);
    switch (target) {
    case Knob::Enable:
        bd.enabled = v > 0.5f;
        if (bd.legacySlot) {
            m_instance->setParameter(bd.enableId, bd.enabled ? 1.0f : 0.0f);
        } else {
            writeDynamicBandSnapshot(idx);
        }
        if (!bd.enabled) {
            auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
            if (eq && eq->isBandSoloed(bd.slotIndex)) {
                eq->setSoloBandSlot(-1);
            }
        }
        break;
    case Knob::Freq:
        bd.freq = v;
        if (bd.legacySlot) {
            m_instance->setParameter(bd.freqId, v);
        } else {
            writeDynamicBandSnapshot(idx);
        }
        break;
    case Knob::Gain:
        if (bd.usesGain) {
            bd.gain = v;
            if (bd.legacySlot) {
                m_instance->setParameter(bd.gainId, v);
            } else {
                writeDynamicBandSnapshot(idx);
            }
        } else if (bd.usesSlope) {
            bd.q = quantizeSlopeNorm(v);
            if (bd.legacySlot) {
                m_instance->setParameter(bd.qId, bd.q);
            } else {
                writeDynamicBandSnapshot(idx);
            }
        } else {
            bd.q = v;
            if (bd.legacySlot) {
                m_instance->setParameter(bd.qId, bd.q);
            } else {
                writeDynamicBandSnapshot(idx);
            }
        }
        break;
    case Knob::Q:
        if (!bd.usesSlope) {
            bd.q = v;
            if (bd.legacySlot) {
                m_instance->setParameter(bd.qId, v);
            } else {
                writeDynamicBandSnapshot(idx);
            }
        }
        break;
    case Knob::Type:
        if (bd.legacySlot && bd.typeId != 0) {
            const float cur = std::round(quantizeTypeNorm(m_instance->getParameter(bd.typeId)) * 3.0f);
            const float next = std::fmod(cur + 1.0f, 4.0f) / 3.0f;
            m_instance->setParameter(bd.typeId, quantizeTypeNorm(next));
        } else if (!bd.legacySlot) {
            const float cur = std::round(std::clamp(bd.typeNorm, 0.0f, 1.0f) * 7.0f);
            bd.typeNorm = std::fmod(cur + 1.0f, 8.0f) / 7.0f;
            const auto type = filterTypeFromNorm(bd.typeNorm);
            bd.typeName = bd.name + " \u00B7 " + filterTypeLabel(type, "Bell");
            bd.usesGain = filterTypeUsesGain(type);
            bd.gain = bd.usesGain ? bd.gain : 0.5f;
            bd.usesSlope = filterTypeUsesSlope(type);
            writeDynamicBandSnapshot(idx);
        }
        break;
    case Knob::Stereo:
        if (bd.legacySlot && bd.stereoId != 0) {
            const float cur = std::round(quantizeStereoNorm(m_instance->getParameter(bd.stereoId)) * 4.0f);
            const float next = std::fmod(cur + 1.0f, 5.0f) / 4.0f;
            m_instance->setParameter(bd.stereoId, quantizeStereoNorm(next));
        } else if (!bd.legacySlot) {
            const float cur = std::round(quantizeStereoNorm(bd.stereoNorm) * 4.0f);
            const float next = std::fmod(cur + 1.0f, 5.0f) / 4.0f;
            bd.stereoNorm = quantizeStereoNorm(next);
            writeDynamicBandSnapshot(idx);
        }
        break;
    default:
        break;
    }
    setDirty(true);
}

static float normalizedFromLaneX(const NUIRect& lane, float x) {
    return std::clamp((x - lane.x) / std::max(1.0f, lane.width), 0.0f, 1.0f);
}

void AestraEQEditor::setBandType(int idx, uint32_t typeIndex) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    auto& bd = m_bands[static_cast<size_t>(idx)];
    if (bd.typeId == 0 && bd.legacySlot)
        return;
    if (bd.legacySlot) {
        const uint32_t clamped = std::min<uint32_t>(typeIndex, 3u);
        bd.typeNorm = quantizeTypeNorm(static_cast<float>(clamped) / 3.0f);
        m_instance->setParameter(bd.typeId, bd.typeNorm);
    } else {
        const uint32_t clamped = std::min<uint32_t>(typeIndex, 7u);
        const bool wasSlope = bd.usesSlope;
        bd.typeNorm = static_cast<float>(clamped) / 7.0f;
        const auto type = filterTypeFromNorm(bd.typeNorm);
        bd.typeName = bd.name + " \u00B7 " + filterTypeLabel(type, "Bell");
        bd.usesGain = filterTypeUsesGain(type);
        bd.gain = bd.usesGain ? bd.gain : 0.5f;
        bd.usesSlope = filterTypeUsesSlope(type);
        if (bd.usesSlope && !wasSlope) {
            bd.q = type == Aestra::Audio::Plugins::FilterType::LowCut ? 2.0f / 6.0f : 2.0f / 6.0f;
        } else if (!bd.usesSlope && wasSlope) {
            bd.q =
                Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(Aestra::Audio::Plugins::AestraEQ::kParamBell1Q);
        }
        writeDynamicBandSnapshot(idx);
    }
    m_typeMenuBand = -1;
    m_typeMenuFromNodeQuickAction = false;
    m_hoveredTypeOption = -1;
    m_stereoMenuBand = -1;
    m_stereoMenuFromNodeQuickAction = false;
    m_hoveredStereoOption = -1;
    setDirty(true);
}

void AestraEQEditor::setBandStereoMode(int idx, uint32_t modeIndex) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    auto& bd = m_bands[static_cast<size_t>(idx)];
    const uint32_t clamped = std::min<uint32_t>(modeIndex, 4u);
    bd.stereoNorm = quantizeStereoNorm(static_cast<float>(clamped) / 4.0f);
    if (bd.legacySlot) {
        if (bd.stereoId != 0)
            m_instance->setParameter(bd.stereoId, bd.stereoNorm);
    } else {
        writeDynamicBandSnapshot(idx);
    }
    m_stereoMenuBand = -1;
    m_stereoMenuFromNodeQuickAction = false;
    m_hoveredStereoOption = -1;
    setDirty(true);
}

void AestraEQEditor::resetParameterToDefault(uint32_t parameterId) {
    if (!m_instance)
        return;
    m_instance->setParameter(parameterId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(parameterId));
    syncBandsFromPlugin();
    setDirty(true);
}

void AestraEQEditor::resetBandToDefault(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (!bd.legacySlot) {
        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        if (eq) {
            const uint32_t slot = bd.slotIndex;
            auto defaults = Aestra::Audio::Plugins::AestraEQ::dynamicBandSlotDefaults(slot);
            defaults.enabled = true;
            if (eq->setDynamicBandSlot(slot, defaults)) {
                syncBandsFromPlugin();
                const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                                [slot](const Band& band) { return band.slotIndex == slot; });
                m_selectedBand = found == m_bands.end() ? -1 : static_cast<int>(std::distance(m_bands.begin(), found));
            }
            setDirty(true);
        }
        return;
    }
    m_instance->setParameter(bd.enableId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.enableId));
    m_instance->setParameter(bd.freqId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.freqId));
    if (bd.gainId != 0) {
        m_instance->setParameter(bd.gainId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.gainId));
    }
    m_instance->setParameter(bd.qId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.qId));
    if (bd.typeId != 0) {
        m_instance->setParameter(bd.typeId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.typeId));
    }
    if (bd.stereoId != 0) {
        m_instance->setParameter(bd.stereoId, Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(bd.stereoId));
    }
    syncBandsFromPlugin();
    m_selectedBand = idx;
    setDirty(true);
}

bool AestraEQEditor::deleteBand(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return false;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (bd.legacySlot)
        return false;
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq || !eq->clearDynamicBandSlot(bd.slotIndex))
        return false;
    m_bands.erase(m_bands.begin() + idx);
    m_selectedBand = m_bands.empty() ? -1 : std::min(idx, static_cast<int>(m_bands.size()) - 1);
    setDirty(true);
    return true;
}

bool AestraEQEditor::resetBandControlToDefault(int idx, Knob target) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()))
        return false;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (!bd.legacySlot) {
        auto& editable = m_bands[static_cast<size_t>(idx)];
        const auto defaults = Aestra::Audio::Plugins::AestraEQ::dynamicBandSlotDefaults(editable.slotIndex);
        switch (target) {
        case Knob::Freq:
            editable.freq = defaults.frequencyNorm;
            break;
        case Knob::Gain:
            editable.gain = defaults.gainNorm;
            break;
        case Knob::Q:
            editable.q = defaults.qOrSlopeNorm;
            break;
        case Knob::Type:
            editable.typeNorm = filterTypeNorm(defaults.type);
            editable.typeName = editable.name + " \u00B7 " + filterTypeLabel(defaults.type, "Bell");
            editable.usesGain = filterTypeUsesGain(defaults.type);
            editable.gain = editable.usesGain ? editable.gain : 0.5f;
            editable.usesSlope = defaults.usesSlope;
            break;
        case Knob::Stereo:
            editable.stereoNorm = stereoNormFromMode(defaults.stereoMode);
            break;
        case Knob::Enable:
        case Knob::None:
            editable.enabled = defaults.enabled;
            break;
        default:
            return false;
        }
        m_selectedBand = idx;
        writeDynamicBandSnapshot(idx);
        setDirty(true);
        return true;
    }

    uint32_t parameterId = 0;
    bool hasParameter = true;
    switch (target) {
    case Knob::None:
        parameterId = bd.enableId;
        break;
    case Knob::Freq:
        parameterId = bd.freqId;
        break;
    case Knob::Gain:
        parameterId = bd.usesGain ? bd.gainId : bd.qId;
        break;
    case Knob::Q:
        if (bd.usesSlope)
            return false;
        parameterId = bd.qId;
        break;
    case Knob::Type:
        parameterId = bd.typeId;
        m_typeMenuBand = -1;
        m_hoveredTypeOption = -1;
        break;
    case Knob::Stereo:
        parameterId = bd.stereoId;
        break;
    default:
        hasParameter = false;
        break;
    }

    if (!hasParameter)
        return false;
    m_selectedBand = idx;
    resetParameterToDefault(parameterId);
    return true;
}

bool AestraEQEditor::resetGraphBandToDefault(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()))
        return false;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    if (!bd.legacySlot) {
        auto& editable = m_bands[static_cast<size_t>(idx)];
        editable.gain = 0.5f;
        editable.q =
            Aestra::Audio::Plugins::AestraEQ::defaultParameterValue(Aestra::Audio::Plugins::AestraEQ::kParamBell1Q);
        m_selectedBand = idx;
        writeDynamicBandSnapshot(idx);
        setDirty(true);
        return true;
    }
    m_selectedBand = idx;
    resetParameterToDefault(bd.usesGain ? bd.gainId : bd.qId);
    return true;
}

void AestraEQEditor::selectAdjacentBand(int direction) {
    const int next = adjacentGraphBand(direction);
    if (next < 0)
        return;
    m_selectedBand = next;
    m_hoveredBand = next;
    m_typeMenuBand = -1;
    m_hoveredTypeOption = -1;
    m_stereoMenuBand = -1;
    m_hoveredStereoOption = -1;
    closeBandContextMenu();
    setDirty(true);
}

int AestraEQEditor::adjacentGraphBand(int direction) const {
    if (m_bands.empty())
        return -1;
    const int fallback = direction >= 0 ? 0 : static_cast<int>(m_bands.size()) - 1;
    const int current =
        (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : fallback;
    const auto bandHz = [this](int idx) {
        const auto& band = m_bands[static_cast<size_t>(idx)];
        return band.legacySlot ? bandFreqHz(band.slotIndex, band.freq) : graphFreqHz(band.freq);
    };
    std::vector<int> order;
    order.reserve(m_bands.size());
    for (size_t i = 0; i < m_bands.size(); ++i) {
        order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const float ahz = bandHz(a);
        const float bhz = bandHz(b);
        if (std::abs(ahz - bhz) > 0.01f)
            return ahz < bhz;
        return m_bands[static_cast<size_t>(a)].slotIndex < m_bands[static_cast<size_t>(b)].slotIndex;
    });
    const auto it = std::find(order.begin(), order.end(), current);
    if (it == order.end())
        return -1;
    if (direction >= 0) {
        const auto next = std::next(it);
        return next != order.end() ? *next : -1;
    }
    return it != order.begin() ? *std::prev(it) : -1;
}

void AestraEQEditor::openBandContextMenu(int bandIdx, const NUIPoint& position) {
    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size()))
        return;
    m_bandContextMenuBand = bandIdx;
    m_hoveredBandContextOption = -1;
    m_typeMenuBand = -1;
    m_hoveredTypeOption = -1;
    m_stereoMenuBand = -1;
    m_hoveredStereoOption = -1;
    m_selectedBand = bandIdx;

    constexpr float kMenuW = 162.0f;
    constexpr float kMenuH = 300.0f;
    const auto root = getBounds();
    const float x = std::clamp(position.x, root.x + kPad, root.right() - kPad - kMenuW);
    const float y =
        std::clamp(position.y, root.y + AestraPanelWindow::TITLE_BAR_H + 6.0f, root.bottom() - kPad - kMenuH);
    m_bandContextMenuRect = {x, y, kMenuW, kMenuH};
    float optionY = y + 34.0f;
    for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
        if (i == 3 || i == 5) {
            optionY += 6.0f;
        } else if (i == 8 || i == 9) {
            optionY += 8.0f;
        }
        m_bandContextOptionRects[i] = {x + 8.0f, optionY, kMenuW - 16.0f, 20.0f};
        optionY += 23.0f;
    }
    setDirty(true);
}

void AestraEQEditor::closeBandContextMenu() {
    m_bandContextMenuBand = -1;
    m_hoveredBandContextOption = -1;
    m_bandContextMenuRect = NUIRect();
    for (auto& r : m_bandContextOptionRects) {
        r = NUIRect();
    }
    setDirty(true);
}

bool AestraEQEditor::canApplyBandContextAction(BandMenuAction action) const {
    if (action == BandMenuAction::ClearAll)
        return m_instance != nullptr;
    if (m_bandContextMenuBand < 0 || m_bandContextMenuBand >= static_cast<int>(m_bands.size()))
        return false;
    const auto& bd = m_bands[static_cast<size_t>(m_bandContextMenuBand)];
    switch (action) {
    case BandMenuAction::Reset:
    case BandMenuAction::Duplicate:
    case BandMenuAction::Copy:
        return true;
    case BandMenuAction::Delete:
        return !bd.legacySlot;
    case BandMenuAction::InvertGain:
        return bd.usesGain && (bd.gainId != 0 || !bd.legacySlot);
    case BandMenuAction::ToggleDynamic:
        return !bd.legacySlot && bd.usesGain;
    case BandMenuAction::SplitLR:
    case BandMenuAction::SplitMS: {
        if (bd.stereoId == 0 && bd.legacySlot)
            return false;
        auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
        if (eq) {
            return eq->findNextAvailableDynamicBandSlot(bd.slotIndex) >= 0;
        }
        return findDuplicateTargetBand(m_bandContextMenuBand) >= 0;
    }
    case BandMenuAction::Paste:
        return m_bandClipboard.valid;
    }
    return false;
}

void AestraEQEditor::copyBandToClipboard(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return;
    const auto& bd = m_bands[static_cast<size_t>(idx)];
    m_bandClipboard.valid = true;
    m_bandClipboard.enabled = bd.enabled;
    m_bandClipboard.hasGain = bd.usesGain && (bd.gainId != 0 || !bd.legacySlot);
    m_bandClipboard.usesSlope = bd.usesSlope;
    m_bandClipboard.hasType = bd.typeId != 0 || !bd.legacySlot;
    m_bandClipboard.typeUsesLegacyDomain = bd.legacySlot;
    m_bandClipboard.hasStereo = bd.stereoId != 0 || !bd.legacySlot;
    m_bandClipboard.freq = bd.freq;
    m_bandClipboard.freqHz = bd.legacySlot ? bandFreqHz(bd.slotIndex, bd.freq) : graphFreqHz(bd.freq);
    m_bandClipboard.gain = bd.gain;
    m_bandClipboard.q = bd.q;
    m_bandClipboard.type = bd.legacySlot && bd.typeId != 0 ? m_instance->getParameter(bd.typeId) : bd.typeNorm;
    m_bandClipboard.stereo =
        bd.legacySlot && bd.stereoId != 0 ? m_instance->getParameter(bd.stereoId) : quantizeStereoNorm(bd.stereoNorm);
    m_bandClipboard.dynamicEnabled = bd.dynamicEnabled;
    m_bandClipboard.targetGain = bd.targetGain;
    m_bandClipboard.dynamicAmount = bd.dynamicAmount;
    m_bandClipboard.dynamicThreshold = bd.dynamicThreshold;
    m_bandClipboard.dynamicKnee = bd.dynamicKnee;
    m_bandClipboard.dynamicAttack = bd.dynamicAttack;
    m_bandClipboard.dynamicRelease = bd.dynamicRelease;
    m_bandClipboard.sidechainLinked = bd.sidechainLinked;
    m_bandClipboard.sidechainType = bd.sidechainType;
    m_bandClipboard.sidechainFreq = bd.sidechainFreq;
    m_bandClipboard.sidechainQ = bd.sidechainQ;
}

bool AestraEQEditor::pasteClipboardToBand(int idx) {
    if (!m_bandClipboard.valid || idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return false;
    auto& bd = m_bands[static_cast<size_t>(idx)];
    if (!bd.legacySlot) {
        bd.enabled = m_bandClipboard.enabled;
        bd.freq = graphNormFromHz(m_bandClipboard.freqHz);
        bd.gain = std::clamp(m_bandClipboard.gain, 0.0f, 1.0f);
        bd.q = std::clamp(m_bandClipboard.q, 0.0f, 1.0f);
        if (m_bandClipboard.hasType) {
            bd.typeNorm = dynamicTypeNormFromClipboardType(m_bandClipboard.type, m_bandClipboard.typeUsesLegacyDomain);
        }
        if (m_bandClipboard.hasStereo) {
            bd.stereoNorm = quantizeStereoNorm(m_bandClipboard.stereo);
        }
        bd.dynamicEnabled = m_bandClipboard.dynamicEnabled;
        bd.targetGain = m_bandClipboard.targetGain;
        bd.dynamicAmount = m_bandClipboard.dynamicAmount;
        bd.dynamicThreshold = m_bandClipboard.dynamicThreshold;
        bd.dynamicKnee = m_bandClipboard.dynamicKnee;
        bd.dynamicAttack = m_bandClipboard.dynamicAttack;
        bd.dynamicRelease = m_bandClipboard.dynamicRelease;
        bd.sidechainLinked = m_bandClipboard.sidechainLinked;
        bd.sidechainType = m_bandClipboard.sidechainType;
        bd.sidechainFreq = m_bandClipboard.sidechainFreq;
        bd.sidechainQ = m_bandClipboard.sidechainQ;
        const auto type = filterTypeFromNorm(bd.typeNorm);
        bd.typeName = bd.name + " \u00B7 " + filterTypeLabel(type, "Bell");
        bd.usesGain = filterTypeUsesGain(type);
        bd.usesSlope = filterTypeUsesSlope(type);
        bd.gain = bd.usesGain ? bd.gain : 0.5f;
        if (!writeDynamicBandSnapshot(idx))
            return false;
        syncBandsFromPlugin();
        m_selectedBand = idx;
        setDirty(true);
        return true;
    }
    m_instance->setParameter(bd.enableId, m_bandClipboard.enabled ? 1.0f : 0.0f);
    m_instance->setParameter(bd.freqId, bandNormFromHz(bd.slotIndex, m_bandClipboard.freqHz));
    if (bd.typeId != 0 && m_bandClipboard.hasType) {
        const auto type = clipboardFilterType(m_bandClipboard.type, m_bandClipboard.typeUsesLegacyDomain);
        m_instance->setParameter(bd.typeId, legacyTypeNorm(type));
    }
    if (bd.stereoId != 0 && m_bandClipboard.hasStereo) {
        m_instance->setParameter(bd.stereoId, quantizeStereoNorm(m_bandClipboard.stereo));
    }
    if (bd.usesGain && bd.gainId != 0 && m_bandClipboard.hasGain) {
        m_instance->setParameter(bd.gainId, std::clamp(m_bandClipboard.gain, 0.0f, 1.0f));
    }
    if (bd.usesSlope) {
        m_instance->setParameter(bd.qId, quantizeSlopeNorm(m_bandClipboard.q));
    } else {
        m_instance->setParameter(bd.qId, std::clamp(m_bandClipboard.q, 0.0f, 1.0f));
    }
    syncBandsFromPlugin();
    m_selectedBand = idx;
    setDirty(true);
    return true;
}

int AestraEQEditor::findDuplicateTargetBand(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()))
        return -1;
    const auto& source = m_bands[static_cast<size_t>(idx)];
    int fallback = -1;
    for (size_t step = 1; step < m_bands.size(); ++step) {
        const int candidate = (idx + static_cast<int>(step)) % static_cast<int>(m_bands.size());
        const auto& target = m_bands[static_cast<size_t>(candidate)];
        if (fallback < 0) {
            fallback = candidate;
        }
        if (target.usesGain == source.usesGain && target.usesSlope == source.usesSlope) {
            return candidate;
        }
    }
    return fallback;
}

bool AestraEQEditor::duplicateBand(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()))
        return false;
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (eq) {
        const auto& source = m_bands[static_cast<size_t>(idx)];
        const float sourceHz = source.legacySlot ? bandFreqHz(source.slotIndex, source.freq) : graphFreqHz(source.freq);
        Aestra::Audio::Plugins::AestraEQ::DynamicBandSlotDefaults copy{
            true,
            source.legacySlot && source.typeId == 0
                ? Aestra::Audio::Plugins::AestraEQ::legacyBandSlot(source.slotIndex).defaultType
                : filterTypeFromNorm(source.typeNorm),
            source.legacySlot && source.stereoId == 0 ? Aestra::Audio::Plugins::AestraEQ::StereoMode::Stereo
                                                      : stereoModeFromNorm(source.stereoNorm),
            graphNormFromHz(sourceHz),
            source.usesGain ? source.gain : 0.5f,
            source.q,
            false,
        };
        copy.dynamicEnabled = source.dynamicEnabled;
        copy.targetGainNorm = source.targetGain;
        copy.thresholdNorm = source.dynamicThreshold;
        copy.kneeNorm = source.dynamicKnee;
        copy.attackNorm = source.dynamicAttack;
        copy.releaseNorm = source.dynamicRelease;
        copy.sidechainLinked = source.sidechainLinked;
        copy.sidechainType = source.sidechainType;
        copy.sidechainFrequencyNorm = source.sidechainFreq;
        copy.sidechainQNorm = source.sidechainQ;
        const int32_t slot = eq->createDynamicBandSlot(copy, source.slotIndex);
        if (slot >= 0) {
            appendDynamicBand(static_cast<uint32_t>(slot));
            m_selectedBand = static_cast<int>(m_bands.size()) - 1;
            setDirty(true);
            return true;
        }
    }
    const int target = findDuplicateTargetBand(idx);
    if (target < 0)
        return false;
    copyBandToClipboard(idx);
    return pasteClipboardToBand(target);
}

bool AestraEQEditor::clearAllBands() {
    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (!eq)
        return false;
    eq->resetToEmptyState();
    syncBandsFromPlugin();
    m_selectedBand = m_bands.empty() ? -1 : 0;
    m_hoveredBand = -1;
    m_hoveredBandFromGraph = false;
    m_hoveredFloatingBand = -1;
    m_typeMenuBand = -1;
    m_hoveredTypeOption = -1;
    m_stereoMenuBand = -1;
    m_hoveredStereoOption = -1;
    setDirty(true);
    return true;
}

bool AestraEQEditor::splitBandStereoPair(int idx, float sourceStereoNorm, float targetStereoNorm) {
    if (idx < 0 || idx >= static_cast<int>(m_bands.size()) || !m_instance)
        return false;
    const auto source = m_bands[static_cast<size_t>(idx)];
    if (source.stereoId == 0 && source.legacySlot)
        return false;

    auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
    if (eq) {
        const float sourceHz = source.legacySlot ? bandFreqHz(source.slotIndex, source.freq) : graphFreqHz(source.freq);
        Aestra::Audio::Plugins::AestraEQ::DynamicBandSlotDefaults copy{
            true,
            source.legacySlot && source.typeId == 0
                ? Aestra::Audio::Plugins::AestraEQ::legacyBandSlot(source.slotIndex).defaultType
                : filterTypeFromNorm(source.typeNorm),
            stereoModeFromNorm(targetStereoNorm),
            graphNormFromHz(sourceHz),
            source.usesGain ? source.gain : 0.5f,
            source.q,
            source.usesSlope,
        };
        copy.dynamicEnabled = source.dynamicEnabled;
        copy.targetGainNorm = source.targetGain;
        copy.thresholdNorm = source.dynamicThreshold;
        copy.kneeNorm = source.dynamicKnee;
        copy.attackNorm = source.dynamicAttack;
        copy.releaseNorm = source.dynamicRelease;
        copy.sidechainLinked = source.sidechainLinked;
        copy.sidechainType = source.sidechainType;
        copy.sidechainFrequencyNorm = source.sidechainFreq;
        copy.sidechainQNorm = source.sidechainQ;
        const int32_t slot = eq->createDynamicBandSlot(copy, source.slotIndex);
        if (slot < 0)
            return false;

        appendDynamicBand(static_cast<uint32_t>(slot));
        const auto found = std::find_if(m_bands.begin(), m_bands.end(), [slot](const Band& band) {
            return band.slotIndex == static_cast<uint32_t>(slot);
        });
        if (found == m_bands.end())
            return false;
        const int target = static_cast<int>(std::distance(m_bands.begin(), found));
        if (m_bands[static_cast<size_t>(idx)].legacySlot) {
            m_instance->setParameter(m_bands[static_cast<size_t>(idx)].stereoId, quantizeStereoNorm(sourceStereoNorm));
        } else {
            m_bands[static_cast<size_t>(idx)].stereoNorm = quantizeStereoNorm(sourceStereoNorm);
            writeDynamicBandSnapshot(idx);
        }
        m_bands[static_cast<size_t>(target)].stereoNorm = quantizeStereoNorm(targetStereoNorm);
        writeDynamicBandSnapshot(target);
        syncBandsFromPlugin();
        m_selectedBand = target;
        setDirty(true);
        return true;
    }

    const int target = findDuplicateTargetBand(idx);
    if (target < 0)
        return false;

    copyBandToClipboard(idx);
    if (!pasteClipboardToBand(target))
        return false;
    auto& refreshedSource = m_bands[static_cast<size_t>(idx)];
    auto& duplicate = m_bands[static_cast<size_t>(target)];
    if (refreshedSource.legacySlot) {
        m_instance->setParameter(refreshedSource.stereoId, quantizeStereoNorm(sourceStereoNorm));
    } else {
        refreshedSource.stereoNorm = quantizeStereoNorm(sourceStereoNorm);
        writeDynamicBandSnapshot(idx);
    }
    if (duplicate.legacySlot) {
        if (duplicate.stereoId == 0)
            return false;
        m_instance->setParameter(duplicate.stereoId, quantizeStereoNorm(targetStereoNorm));
    } else {
        duplicate.stereoNorm = quantizeStereoNorm(targetStereoNorm);
        writeDynamicBandSnapshot(target);
    }
    syncBandsFromPlugin();
    m_selectedBand = target;
    setDirty(true);
    return true;
}

void AestraEQEditor::applyBandContextAction(BandMenuAction action) {
    if (!canApplyBandContextAction(action))
        return;
    const int idx = m_bandContextMenuBand;
    auto& bd = m_bands[static_cast<size_t>(idx)];
    switch (action) {
    case BandMenuAction::Reset:
        resetBandToDefault(idx);
        break;
    case BandMenuAction::InvertGain:
        if (bd.usesGain && m_instance) {
            bd.gain = 1.0f - bd.gain;
            if (bd.legacySlot) {
                m_instance->setParameter(bd.gainId, bd.gain);
            } else {
                writeDynamicBandSnapshot(idx);
            }
            syncBandsFromPlugin();
            setDirty(true);
        }
        break;
    case BandMenuAction::ToggleDynamic:
        if (!bd.legacySlot && bd.usesGain) {
            bd.dynamicEnabled = !bd.dynamicEnabled;
            if (bd.sidechainLinked) {
                bd.sidechainFreq = bd.freq;
                bd.sidechainQ = bd.q;
            }
            writeDynamicBandSnapshot(idx);
            syncBandsFromPlugin();
            setDirty(true);
        }
        break;
    case BandMenuAction::SplitLR:
        splitBandStereoPair(idx, 0.25f, 0.50f);
        break;
    case BandMenuAction::SplitMS:
        splitBandStereoPair(idx, 0.75f, 1.0f);
        break;
    case BandMenuAction::Duplicate:
        duplicateBand(idx);
        break;
    case BandMenuAction::Delete:
        deleteBand(idx);
        break;
    case BandMenuAction::Copy:
        copyBandToClipboard(idx);
        setDirty(true);
        break;
    case BandMenuAction::Paste:
        pasteClipboardToBand(idx);
        break;
    case BandMenuAction::ClearAll:
        clearAllBands();
        break;
    }
    closeBandContextMenu();
}

bool AestraEQEditor::nudgeSelectedBand(const NUIKeyEvent& event) {
    if (!event.pressed || m_selectedBand < 0 || m_selectedBand >= static_cast<int>(m_bands.size()))
        return false;

    const auto& bd = m_bands[static_cast<size_t>(m_selectedBand)];
    const bool fine = event.modifiers & NUIModifiers::Shift;
    const bool shapeMode = (event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Super) ||
                           (event.modifiers & NUIModifiers::Alt);

    switch (event.keyCode) {
    case NUIKeyCode::Left:
    case NUIKeyCode::Right: {
        const float step = fine ? 0.0025f : 0.0125f;
        const float direction = event.keyCode == NUIKeyCode::Right ? 1.0f : -1.0f;
        setBandValue(m_selectedBand, Knob::Freq, bd.freq + direction * step);
        return true;
    }
    case NUIKeyCode::Up:
    case NUIKeyCode::Down: {
        const float direction = event.keyCode == NUIKeyCode::Up ? 1.0f : -1.0f;
        if (shapeMode || !bd.usesGain) {
            const float step = bd.usesSlope ? (1.0f / 6.0f) : (fine ? 0.01f : 0.04f);
            setBandValue(m_selectedBand, bd.usesSlope ? Knob::Gain : Knob::Q, bd.q + direction * step);
        } else {
            const float step = fine ? (0.1f / 36.0f) : (1.0f / 36.0f);
            setBandValue(m_selectedBand, Knob::Gain, bd.gain + direction * step);
        }
        return true;
    }
    case NUIKeyCode::Home:
        return resetGraphBandToDefault(m_selectedBand);
    default:
        break;
    }

    return false;
}

std::string AestraEQEditor::numericEditValueString(int bandIdx, Knob target) const {
    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size()))
        return {};
    const auto& bd = m_bands[static_cast<size_t>(bandIdx)];
    char buf[32];
    switch (target) {
    case Knob::Freq: {
        const float hz = bd.legacySlot ? bandFreqHz(bd.slotIndex, bd.freq) : graphFreqHz(bd.freq);
        if (hz >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.2fk", hz / 1000.0f);
        else
            std::snprintf(buf, sizeof(buf), "%.0f", hz);
        return buf;
    }
    case Knob::Gain:
        if (bd.usesGain) {
            std::snprintf(buf, sizeof(buf), "%.1f", -18.0f + bd.gain * 36.0f);
        } else if (bd.usesSlope) {
            std::snprintf(buf, sizeof(buf), "%u", slopeDbFromNorm(bd.q));
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f", 0.1f + bd.q * 9.9f);
        }
        return buf;
    case Knob::Q:
        std::snprintf(buf, sizeof(buf), "%.2f", 0.1f + bd.q * 9.9f);
        return buf;
    default:
        return {};
    }
}

NUIRect AestraEQEditor::numericEditBounds() const {
    if (m_numericEditBand < 0 || m_numericEditBand >= static_cast<int>(m_bands.size())) {
        return {m_graphBounds.x + 18.0f, m_graphBounds.y + 42.0f, 150.0f, 28.0f};
    }
    const auto& bd = m_bands[static_cast<size_t>(m_numericEditBand)];
    NUIRect anchor = bd.cardBounds;
    if (m_numericEditTarget == Knob::Freq)
        anchor = bd.freqKnob;
    else if (m_numericEditTarget == Knob::Gain)
        anchor = bd.gainKnob;
    else if (m_numericEditTarget == Knob::Q)
        anchor = bd.qKnob;

    const auto floating = floatingBandPanelLayout(m_numericEditBand, m_graphBounds);
    if (floating.valid) {
        if (m_numericEditTarget == Knob::Freq)
            anchor = floating.freqRect;
        else if (m_numericEditTarget == Knob::Gain)
            anchor = floating.gainRect;
        else if (m_numericEditTarget == Knob::Q && floating.hasQRow)
            anchor = floating.qRect;
    }

    constexpr float w = 150.0f;
    constexpr float h = 28.0f;
    const auto root = getBounds();
    const float x = std::clamp(anchor.center().x - w * 0.5f, root.x + kPad, root.right() - kPad - w);
    const float y = std::max(root.y + AestraPanelWindow::TITLE_BAR_H + 8.0f, anchor.y - h - 8.0f);
    return {x, y, w, h};
}

void AestraEQEditor::beginNumericEdit(int bandIdx, Knob target) {
    if (bandIdx < 0 || bandIdx >= static_cast<int>(m_bands.size()))
        return;
    if (target != Knob::Freq && target != Knob::Gain && target != Knob::Q)
        return;
    m_numericEditActive = true;
    m_numericEditBand = bandIdx;
    m_numericEditTarget = target;
    m_numericEditText = numericEditValueString(bandIdx, target);
    m_selectedBand = bandIdx;
    setFocused(true);
    setDirty(true);
}

bool AestraEQEditor::commitNumericEdit() {
    if (!m_numericEditActive)
        return false;
    if (m_numericEditBand < 0 || m_numericEditBand >= static_cast<int>(m_bands.size())) {
        cancelNumericEdit();
        return true;
    }

    float value = 0.0f;
    if (!parseFloatPrefix(m_numericEditText, value)) {
        cancelNumericEdit();
        return true;
    }

    const auto& bd = m_bands[static_cast<size_t>(m_numericEditBand)];
    switch (m_numericEditTarget) {
    case Knob::Freq: {
        const bool kSuffix =
            m_numericEditText.find('k') != std::string::npos || m_numericEditText.find('K') != std::string::npos;
        if (kSuffix)
            value *= 1000.0f;
        setBandValue(m_numericEditBand, Knob::Freq,
                     bd.legacySlot ? bandNormFromHz(bd.slotIndex, value) : graphNormFromHz(value));
        break;
    }
    case Knob::Gain:
        if (bd.usesGain) {
            setBandValue(m_numericEditBand, Knob::Gain, (std::clamp(value, -18.0f, 18.0f) + 18.0f) / 36.0f);
        } else if (bd.usesSlope) {
            setBandValue(m_numericEditBand, Knob::Gain, slopeNormFromDb(std::clamp(value, 6.0f, 96.0f)));
        } else {
            setBandValue(m_numericEditBand, Knob::Gain, (std::clamp(value, 0.1f, 10.0f) - 0.1f) / 9.9f);
        }
        break;
    case Knob::Q:
        setBandValue(m_numericEditBand, Knob::Q, (std::clamp(value, 0.1f, 10.0f) - 0.1f) / 9.9f);
        break;
    default:
        break;
    }

    m_numericEditActive = false;
    m_numericEditBand = -1;
    m_numericEditTarget = Knob::None;
    m_numericEditText.clear();
    setDirty(true);
    return true;
}

void AestraEQEditor::cancelNumericEdit() {
    m_numericEditActive = false;
    m_numericEditBand = -1;
    m_numericEditTarget = Knob::None;
    m_numericEditText.clear();
    setDirty(true);
}

bool AestraEQEditor::handleNumericEditKey(const NUIKeyEvent& event) {
    if (!m_numericEditActive || !event.pressed)
        return false;
    if (event.keyCode == NUIKeyCode::Escape) {
        cancelNumericEdit();
        return true;
    }
    if (event.keyCode == NUIKeyCode::Enter) {
        return commitNumericEdit();
    }
    if (event.keyCode == NUIKeyCode::Backspace) {
        if (!m_numericEditText.empty())
            m_numericEditText.pop_back();
        setDirty(true);
        return true;
    }
    const unsigned char ch = static_cast<unsigned char>(event.character);
    if (std::isdigit(ch) || ch == '.' || ch == '-' || ch == '+' || ch == 'k' || ch == 'K') {
        if (m_numericEditText.size() < 16) {
            m_numericEditText.push_back(static_cast<char>(ch));
            setDirty(true);
        }
        return true;
    }
    return true;
}

bool AestraEQEditor::handleFloatingBandPanelClick(const NUIMouseEvent& event) {
    if (!event.pressed || event.button != NUIMouseButton::Left)
        return false;
    if (!m_bandInspectorCollapsed)
        return false;
    const int idx = currentFloatingBandIndex();
    if (idx < 0)
        return false;
    const auto layout = floatingBandPanelLayout(idx, m_graphBounds);
    if (!layout.valid || !layout.panel.contains(event.position))
        return false;

    auto& bd = m_bands[static_cast<size_t>(idx)];
    m_selectedBand = idx;
    if (layout.enableRect.contains(event.position)) {
        const bool nowEnabled = !bd.enabled;
        setBandValue(idx, Knob::Enable, nowEnabled ? 1.0f : 0.0f);
        return true;
    }
    if ((bd.typeId != 0 || !bd.legacySlot) && layout.typeRect.contains(event.position)) {
        m_typeMenuBand = (m_typeMenuBand == idx) ? -1 : idx;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        m_stereoMenuBand = -1;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        setDirty(true);
        return true;
    }
    if (layout.stereoRect.contains(event.position)) {
        m_stereoMenuBand = (m_stereoMenuBand == idx) ? -1 : idx;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        m_typeMenuBand = -1;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        setDirty(true);
        return true;
    }
    auto beginPanelLaneEdit = [&](Knob target, const NUIRect& lane) {
        if (event.doubleClick) {
            beginNumericEdit(idx, target);
            return;
        }
        m_draggingCardBand = idx;
        m_draggingKnob = target;
        m_draggingLaneRect = lane;
        m_dragStartY = event.position.y;
        m_dragStartValue = (target == Knob::Freq)   ? bd.freq
                           : (target == Knob::Gain) ? (bd.usesSlope ? bd.q : bd.gain)
                                                    : bd.q;
        setBandValue(idx, target, normalizedFromLaneX(lane, event.position.x));
    };
    if (layout.freqRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Freq, layout.freqRect);
        return true;
    }
    if (layout.gainRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Gain, layout.gainRect);
        return true;
    }
    if (layout.hasQRow && layout.qRect.contains(event.position)) {
        beginPanelLaneEdit(Knob::Q, layout.qRect);
        return true;
    }
    return true;
}

bool AestraEQEditor::handleSelectedNodeQuickActionClick(const NUIMouseEvent& event) {
    if (!event.pressed || event.button != NUIMouseButton::Left)
        return false;
    if (m_selectedBand < 0 || m_selectedBand >= static_cast<int>(m_bands.size()))
        return false;
    if (m_nodeQuickActionRect.width <= 0.0f || !m_nodeQuickActionRect.contains(event.position))
        return false;

    auto& bd = m_bands[static_cast<size_t>(m_selectedBand)];
    for (size_t i = 0; i < m_nodeQuickActionRects.size(); ++i) {
        if (!m_nodeQuickActionRects[i].contains(event.position))
            continue;
        const auto action = static_cast<NodeQuickAction>(i);
        switch (action) {
        case NodeQuickAction::TypePrev:
        case NodeQuickAction::TypeNext:
            if (bd.typeId != 0 || !bd.legacySlot) {
                const int optionCount = bd.legacySlot ? 4 : 8;
                const int current =
                    bd.legacySlot
                        ? std::clamp(
                              static_cast<int>(std::round(
                                  quantizeTypeNorm(m_instance ? m_instance->getParameter(bd.typeId) : bd.typeNorm) *
                                  3.0f)),
                              0, 3)
                        : std::clamp(static_cast<int>(std::round(std::clamp(bd.typeNorm, 0.0f, 1.0f) * 7.0f)), 0, 7);
                const int delta = action == NodeQuickAction::TypePrev ? -1 : 1;
                const int next = (current + delta + optionCount) % optionCount;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                closeBandContextMenu();
                setBandType(m_selectedBand, static_cast<uint32_t>(next));
            }
            return true;
        case NodeQuickAction::Stereo:
            m_stereoMenuBand = (m_stereoMenuBand == m_selectedBand) ? -1 : m_selectedBand;
            m_stereoMenuFromNodeQuickAction = m_stereoMenuBand >= 0;
            m_hoveredStereoOption = -1;
            m_typeMenuBand = -1;
            m_typeMenuFromNodeQuickAction = false;
            m_hoveredTypeOption = -1;
            closeBandContextMenu();
            setDirty(true);
            return true;
        case NodeQuickAction::Solo: {
            auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
            if (eq) {
                const int32_t solo = eq->getSoloBandSlot();
                eq->setSoloBandSlot(solo == static_cast<int32_t>(bd.slotIndex) ? -1
                                                                               : static_cast<int32_t>(bd.slotIndex));
                setDirty(true);
            }
            return true;
        }
        case NodeQuickAction::Duplicate:
            copyBandToClipboard(m_selectedBand);
            duplicateBand(m_selectedBand);
            return true;
        case NodeQuickAction::Delete:
            if (!bd.legacySlot) {
                deleteBand(m_selectedBand);
            }
            return true;
        case NodeQuickAction::Count:
            return true;
        }
    }
    return true;
}

bool AestraEQEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    const auto b = getBounds();
    if (event.pressed && b.contains(event.position)) {
        setFocused(true);
    }
    if (AestraPanelWindow::onMouseEvent(event))
        return true;

    const bool contains = b.contains(event.position);
    const bool dragging =
        m_draggingGraphBand >= 0 || m_draggingDetectorBand >= 0 || m_draggingCardBand >= 0 || m_draggingOutputGain;
    if (!contains && !isDraggingWindow() && !dragging)
        return false;

    if (event.pressed && event.button == NUIMouseButton::Left && m_numericEditActive &&
        !numericEditBounds().contains(event.position)) {
        commitNumericEdit();
    }

    if (handleFloatingBandPanelClick(event))
        return true;

    if (handleSelectedNodeQuickActionClick(event))
        return true;

    if (event.pressed && event.button == NUIMouseButton::Right) {
        using EQ = Aestra::Audio::Plugins::AestraEQ;
        closeBandContextMenu();
        if (m_typeMenuBand >= 0 && !m_typeMenuRect.contains(event.position)) {
            m_typeMenuBand = -1;
            m_hoveredTypeOption = -1;
            setDirty(true);
        }
        if (m_stereoMenuBand >= 0 && !m_stereoMenuRect.contains(event.position)) {
            m_stereoMenuBand = -1;
            m_hoveredStereoOption = -1;
            setDirty(true);
        }
        if (m_bypassRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamBypass);
            return true;
        }
        if (m_outputGainRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamOutputGain);
            return true;
        }
        if (m_polarityRect.contains(event.position)) {
            resetParameterToDefault(EQ::kParamPolarityInvert);
            return true;
        }
        if (m_compareCopyRect.contains(event.position)) {
            copyCompareSlotToOther();
            return true;
        }

        const int floatingIdx = currentFloatingBandIndex();
        if (floatingIdx >= 0) {
            const auto floating = floatingBandPanelLayout(floatingIdx, m_graphBounds);
            if (floating.valid && floating.panel.contains(event.position)) {
                openBandContextMenu(floatingIdx, event.position);
                return true;
            }
        }

        Knob knob = Knob::None;
        const int cardIdx = hitTestBandCard(event.position.x, event.position.y, knob);
        if (cardIdx >= 0) {
            openBandContextMenu(cardIdx, event.position);
            return true;
        }

        const int graphIdx = hitTestGraphNode(event.position.x, event.position.y);
        if (graphIdx >= 0) {
            openBandContextMenu(graphIdx, event.position);
            return true;
        }
    }

    if (m_bandContextMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        static constexpr BandMenuAction kActions[] = {BandMenuAction::Reset,         BandMenuAction::InvertGain,
                                                      BandMenuAction::ToggleDynamic, BandMenuAction::SplitLR,
                                                      BandMenuAction::SplitMS,       BandMenuAction::Copy,
                                                      BandMenuAction::Paste,         BandMenuAction::Duplicate,
                                                      BandMenuAction::Delete,        BandMenuAction::ClearAll};
        static_assert(std::size(kActions) == std::tuple_size_v<decltype(m_bandContextOptionRects)>,
                      "Band context click actions must match rendered option rows");
        for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
            if (m_bandContextOptionRects[i].contains(event.position)) {
                applyBandContextAction(kActions[i]);
                return true;
            }
        }
        if (!m_bandContextMenuRect.contains(event.position)) {
            closeBandContextMenu();
        }
    }

    if (m_typeMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_typeOptionRects.size(); ++i) {
            if (m_typeOptionRects[i].contains(event.position)) {
                setBandType(m_typeMenuBand, static_cast<uint32_t>(i));
                return true;
            }
        }
        if (!m_typeMenuRect.contains(event.position)) {
            m_typeMenuBand = -1;
            m_hoveredTypeOption = -1;
            setDirty(true);
        }
    }

    if (m_stereoMenuBand >= 0 && event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
            if (m_stereoOptionRects[i].contains(event.position)) {
                setBandStereoMode(m_stereoMenuBand, static_cast<uint32_t>(i));
                return true;
            }
        }
        if (!m_stereoMenuRect.contains(event.position)) {
            m_stereoMenuBand = -1;
            m_hoveredStereoOption = -1;
            setDirty(true);
        }
    }

    // Bypass click
    if (event.pressed && event.button == NUIMouseButton::Left && m_bypassRect.contains(event.position)) {
        setBypassed(!isBypassed());
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_polarityRect.contains(event.position)) {
        setPolarityInverted(!polarityInverted());
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareARect.contains(event.position)) {
        switchCompareSlot(0);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareBRect.contains(event.position)) {
        switchCompareSlot(1);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_compareCopyRect.contains(event.position)) {
        copyCompareSlotToOther();
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_curveScaleRect.contains(event.position)) {
        cycleCurveDbRange();
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_analyzerMenuRect.contains(event.position)) {
        m_analyzerPanelOpen = !m_analyzerPanelOpen;
        setDirty(true);
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerSourceRect.contains(event.position)) {
        cycleAnalyzerSource();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerStereoRect.contains(event.position)) {
        cycleAnalyzerStereoMode();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerTiltRect.contains(event.position)) {
        cycleAnalyzerTilt();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerDecayRect.contains(event.position)) {
        cycleAnalyzerDecay();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerFreezeRect.contains(event.position)) {
        setAnalyzerFrozen(!m_analyzerFrozen);
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerCollisionRect.contains(event.position)) {
        setAnalyzerCollisionEnabled(!m_analyzerCollisionEnabled);
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        m_analyzerCollisionStrengthRect.contains(event.position)) {
        cycleAnalyzerCollisionStrength();
        return true;
    }

    if (m_analyzerPanelOpen && event.pressed && event.button == NUIMouseButton::Left &&
        !m_analyzerPanelRect.contains(event.position)) {
        m_analyzerPanelOpen = false;
        setDirty(true);
    }

    if (event.wheelDelta != 0.0f && m_outputGainRect.contains(event.position)) {
        const float stepDb = (event.modifiers & NUIModifiers::Shift) ? 0.1f : 1.0f;
        setOutputGain(outputGain() + (event.wheelDelta > 0 ? stepDb : -stepDb) / 36.0f);
        return true;
    }

    // Wheel on graph nodes adjusts Q/slope
    if (event.wheelDelta != 0.0f) {
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            auto& bd = m_bands[idx];
            m_selectedBand = idx;
            if (bd.usesSlope) {
                const float cur = std::round(quantizeSlopeNorm(bd.q) * 6.0f);
                const float nx = std::clamp(cur + (event.wheelDelta > 0 ? 1.0f : -1.0f), 0.0f, 6.0f);
                bd.q = nx / 6.0f;
            } else {
                const float step = (event.modifiers & NUIModifiers::Shift) ? 0.01f : 0.04f;
                bd.q = std::clamp(bd.q + (event.wheelDelta > 0 ? step : -step), 0.0f, 1.0f);
            }
            if (bd.legacySlot && m_instance)
                m_instance->setParameter(bd.qId, bd.q);
            else
                writeDynamicBandSnapshot(idx);
            setDirty(true);
            return true;
        }
    }

    // Press
    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int selectedIdx =
            (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : 0;
        const NUIRect slotRailHit{m_selectedSlotRailRect.x, m_selectedSlotRailRect.y - 7.0f,
                                  m_selectedSlotRailRect.width, m_selectedSlotRailRect.height + 14.0f};
        if (m_selectedSlotRailRect.width > 0.0f && slotRailHit.contains(event.position)) {
            const float t =
                std::clamp((event.position.x - m_selectedSlotRailRect.x) / std::max(1.0f, m_selectedSlotRailRect.width),
                           0.0f, 1.0f);
            const uint32_t slot = static_cast<uint32_t>(
                std::round(t * static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)));
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found != m_bands.end()) {
                m_selectedBand = static_cast<int>(std::distance(m_bands.begin(), found));
                m_typeMenuBand = -1;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                closeBandContextMenu();
                setDirty(true);
                return true;
            }
        }
        if (m_selectedCollapseRect.contains(event.position)) {
            m_bandInspectorCollapsed = !m_bandInspectorCollapsed;
            m_typeMenuBand = -1;
            m_typeMenuFromNodeQuickAction = false;
            m_hoveredTypeOption = -1;
            m_stereoMenuBand = -1;
            m_stereoMenuFromNodeQuickAction = false;
            m_hoveredStereoOption = -1;
            setDirty(true);
            return true;
        }
        if (m_selectedPrevRect.contains(event.position)) {
            selectAdjacentBand(-1);
            return true;
        }
        if (m_selectedNextRect.contains(event.position)) {
            selectAdjacentBand(1);
            return true;
        }
        if (m_selectedDuplicateRect.contains(event.position)) {
            copyBandToClipboard(selectedIdx);
            setDirty(true);
            return true;
        }
        if (m_selectedDeleteRect.contains(event.position)) {
            if (selectedIdx >= 0 && selectedIdx < static_cast<int>(m_bands.size()) &&
                !m_bands[static_cast<size_t>(selectedIdx)].legacySlot) {
                deleteBand(selectedIdx);
            }
            return true;
        }
        int detectorIdx = hitTestDynamicDetectorHandle(event.position.x, event.position.y);
        if (detectorIdx >= 0) {
            m_selectedBand = detectorIdx;
            m_draggingDetectorBand = detectorIdx;
            auto& bd = m_bands[static_cast<size_t>(detectorIdx)];
            m_graphDragStartPosition = event.position;
            m_detectorDragStartFreq = bd.sidechainFreq;
            m_detectorDragStartQ = bd.sidechainQ;
            updateDynamicDetectorFromGraphPosition(detectorIdx, event.position, event.modifiers);
            return true;
        }
        int idx = hitTestGraphNode(event.position.x, event.position.y);
        if (idx >= 0) {
            m_selectedBand = idx;
            m_bandInspectorCollapsed = false;
            auto& bd = m_bands[static_cast<size_t>(idx)];
            if (event.doubleClick) {
                beginNumericEdit(idx, bd.usesGain ? Knob::Gain : Knob::Q);
                return true;
            }
            if (!bd.enabled) {
                setBandValue(idx, Knob::Enable, 1.0f);
            }
            m_draggingGraphBand = idx;
            m_graphDragStartPosition = event.position;
            m_graphDragStartFreq = bd.freq;
            m_graphDragStartGain = bd.gain;
            updateBandFromGraphPosition(idx, event.position, event.modifiers);
            return true;
        }
        if (graphInnerBounds(m_graphBounds).contains(event.position) && createBandAtGraphPoint(event.position)) {
            m_draggingGraphBand = m_selectedBand;
            m_graphDragStartPosition = event.position;
            m_graphDragStartFreq = m_selectedBand >= 0 ? m_bands[static_cast<size_t>(m_selectedBand)].freq : 0.5f;
            m_graphDragStartGain = m_selectedBand >= 0 ? m_bands[static_cast<size_t>(m_selectedBand)].gain : 0.5f;
            return true;
        }
        if (m_outputGainRect.contains(event.position)) {
            m_draggingOutputGain = true;
            m_dragStartY = event.position.y;
            m_dragStartValue = outputGain();
            return true;
        }
        Knob knob = Knob::None;
        int cardIdx = hitTestBandCard(event.position.x, event.position.y, knob);
        if (cardIdx >= 0) {
            m_selectedBand = cardIdx;
            if (event.doubleClick && knob != Knob::None && knob != Knob::Enable && knob != Knob::Type &&
                knob != Knob::Stereo) {
                beginNumericEdit(cardIdx, knob);
                return true;
            }
            if (knob == Knob::None) {
                setDirty(true);
                return true;
            }
            if (knob == Knob::Enable) {
                auto& bd = m_bands[cardIdx];
                const bool nowEnabled = !bd.enabled;
                setBandValue(cardIdx, Knob::Enable, nowEnabled ? 1.0f : 0.0f);
                return true;
            }
            if (knob == Knob::Type) {
                m_typeMenuBand = (m_typeMenuBand == cardIdx) ? -1 : cardIdx;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                m_stereoMenuBand = -1;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                setDirty(true);
                return true;
            }
            if (knob == Knob::Stereo) {
                m_stereoMenuBand = (m_stereoMenuBand == cardIdx) ? -1 : cardIdx;
                m_stereoMenuFromNodeQuickAction = false;
                m_hoveredStereoOption = -1;
                m_typeMenuBand = -1;
                m_typeMenuFromNodeQuickAction = false;
                m_hoveredTypeOption = -1;
                setDirty(true);
                return true;
            }
            m_draggingCardBand = cardIdx;
            m_draggingKnob = knob;
            m_draggingLaneRect = (knob == Knob::Freq)   ? m_bands[cardIdx].freqKnob
                                 : (knob == Knob::Gain) ? m_bands[cardIdx].gainKnob
                                                        : m_bands[cardIdx].qKnob;
            m_dragStartY = event.position.y;
            const auto& bd = m_bands[cardIdx];
            m_dragStartValue = (knob == Knob::Freq)   ? bd.freq
                               : (knob == Knob::Gain) ? (bd.usesSlope ? bd.q : bd.gain)
                                                      : bd.q;
            setBandValue(cardIdx, knob, normalizedFromLaneX(m_draggingLaneRect, event.position.x));
            return true;
        }
    }

    // Drag
    if (m_draggingGraphBand >= 0) {
        updateBandFromGraphPosition(m_draggingGraphBand, event.position, event.modifiers);
        if (!event.pressed && event.button == NUIMouseButton::Left)
            m_draggingGraphBand = -1;
        return true;
    }
    if (m_draggingDetectorBand >= 0) {
        updateDynamicDetectorFromGraphPosition(m_draggingDetectorBand, event.position, event.modifiers);
        if (!event.pressed && event.button == NUIMouseButton::Left)
            m_draggingDetectorBand = -1;
        return true;
    }
    if (m_draggingOutputGain) {
        setOutputGain(m_dragStartValue + (m_dragStartY - event.position.y) / 180.0f);
        if (!event.pressed && event.button == NUIMouseButton::Left)
            m_draggingOutputGain = false;
        return true;
    }
    if (m_draggingCardBand >= 0) {
        setBandValue(m_draggingCardBand, m_draggingKnob, normalizedFromLaneX(m_draggingLaneRect, event.position.x));
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingCardBand = -1;
            m_draggingKnob = Knob::None;
            m_draggingLaneRect = NUIRect();
        }
        return true;
    }

    // Hover
    if (!event.pressed && !event.released) {
        Knob k = Knob::None;
        const int detectorIdx = hitTestDynamicDetectorHandle(event.position.x, event.position.y);
        const int graphIdx = detectorIdx >= 0 ? -1 : hitTestGraphNode(event.position.x, event.position.y);
        int idx = graphIdx;
        int floatingPanelHover = -1;
        if (floatingPanelHover < 0) {
            const int floatingCandidate = m_hoveredFloatingBand;
            if (floatingCandidate >= 0 && floatingCandidate < static_cast<int>(m_bands.size())) {
                const auto floating = floatingBandPanelLayout(floatingCandidate, m_graphBounds);
                if (floating.valid && floating.panel.contains(event.position)) {
                    floatingPanelHover = floatingCandidate;
                }
            }
        }
        if (idx < 0 && contains)
            idx = hitTestBandCard(event.position.x, event.position.y, k);
        const bool overGraphNode = graphIdx >= 0 || detectorIdx >= 0;
        const bool overNodeQuickActions = m_nodeQuickActionRect.contains(event.position);
        const auto graphInner = graphInnerBounds(m_graphBounds);
        const NUIRect graphControlZone{graphInner.right() - 116.0f, graphInner.y - 2.0f, 118.0f, 28.0f};
        const bool graphCursorVisible = graphInner.contains(event.position) && !overGraphNode &&
                                        !overNodeQuickActions && !graphControlZone.contains(event.position);
        if (graphCursorVisible != m_graphCursorVisible ||
            (graphCursorVisible && (std::abs(event.position.x - m_graphCursorPoint.x) > 0.5f ||
                                    std::abs(event.position.y - m_graphCursorPoint.y) > 0.5f))) {
            m_graphCursorVisible = graphCursorVisible;
            m_graphCursorPoint = event.position;
            setDirty(true);
        }
        const bool hoveredFromGraph = graphIdx >= 0;
        if (idx != m_hoveredBand || hoveredFromGraph != m_hoveredBandFromGraph) {
            m_hoveredBand = idx;
            m_hoveredBandFromGraph = hoveredFromGraph;
            setDirty(true);
        }
        if (floatingPanelHover != m_hoveredFloatingBand) {
            m_hoveredFloatingBand = floatingPanelHover;
            setDirty(true);
        }
        const bool hover = m_bypassRect.contains(event.position);
        if (hover != m_bypassHovered) {
            m_bypassHovered = hover;
            setDirty(true);
        }
        int typeOptionHover = -1;
        if (m_typeMenuBand >= 0) {
            for (size_t i = 0; i < m_typeOptionRects.size(); ++i) {
                if (m_typeOptionRects[i].contains(event.position)) {
                    typeOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (typeOptionHover != m_hoveredTypeOption) {
            m_hoveredTypeOption = typeOptionHover;
            setDirty(true);
        }
        int stereoOptionHover = -1;
        if (m_stereoMenuBand >= 0) {
            for (size_t i = 0; i < m_stereoOptionRects.size(); ++i) {
                if (m_stereoOptionRects[i].contains(event.position)) {
                    stereoOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (stereoOptionHover != m_hoveredStereoOption) {
            m_hoveredStereoOption = stereoOptionHover;
            setDirty(true);
        }
        int bandContextOptionHover = -1;
        if (m_bandContextMenuBand >= 0) {
            for (size_t i = 0; i < m_bandContextOptionRects.size(); ++i) {
                if (m_bandContextOptionRects[i].contains(event.position)) {
                    bandContextOptionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (bandContextOptionHover != m_hoveredBandContextOption) {
            m_hoveredBandContextOption = bandContextOptionHover;
            setDirty(true);
        }
        int nodeQuickActionHover = -1;
        if (m_nodeQuickActionRect.contains(event.position)) {
            for (size_t i = 0; i < m_nodeQuickActionRects.size(); ++i) {
                if (m_nodeQuickActionRects[i].contains(event.position)) {
                    nodeQuickActionHover = static_cast<int>(i);
                    break;
                }
            }
        }
        if (nodeQuickActionHover != m_hoveredNodeQuickAction) {
            m_hoveredNodeQuickAction = nodeQuickActionHover;
            m_nodeQuickActionHoverStarted = std::chrono::steady_clock::now();
            setDirty(true);
        }
        const bool outputHover = m_outputGainRect.contains(event.position);
        if (outputHover != m_outputGainHovered) {
            m_outputGainHovered = outputHover;
            setDirty(true);
        }
        const bool polarityHover = m_polarityRect.contains(event.position);
        if (polarityHover != m_polarityHovered) {
            m_polarityHovered = polarityHover;
            setDirty(true);
        }
        const bool compareAHover = m_compareARect.contains(event.position);
        if (compareAHover != m_compareAHovered) {
            m_compareAHovered = compareAHover;
            setDirty(true);
        }
        const bool compareBHover = m_compareBRect.contains(event.position);
        if (compareBHover != m_compareBHovered) {
            m_compareBHovered = compareBHover;
            setDirty(true);
        }
        const bool compareCopyHover = m_compareCopyRect.contains(event.position);
        if (compareCopyHover != m_compareCopyHovered) {
            m_compareCopyHovered = compareCopyHover;
            setDirty(true);
        }
        const bool prevHover = m_selectedPrevRect.contains(event.position);
        if (prevHover != m_selectedPrevHovered) {
            m_selectedPrevHovered = prevHover;
            setDirty(true);
        }
        const bool nextHover = m_selectedNextRect.contains(event.position);
        if (nextHover != m_selectedNextHovered) {
            m_selectedNextHovered = nextHover;
            setDirty(true);
        }
        const bool duplicateHover = m_selectedDuplicateRect.contains(event.position);
        if (duplicateHover != m_selectedDuplicateHovered) {
            m_selectedDuplicateHovered = duplicateHover;
            setDirty(true);
        }
        const bool deleteHover = m_selectedDeleteRect.contains(event.position);
        if (deleteHover != m_selectedDeleteHovered) {
            m_selectedDeleteHovered = deleteHover;
            setDirty(true);
        }
        const bool collapseHover = m_selectedCollapseRect.contains(event.position);
        if (collapseHover != m_selectedCollapseHovered) {
            m_selectedCollapseHovered = collapseHover;
            setDirty(true);
        }
        const int selectedForHover =
            (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : -1;
        const bool stereoHover = selectedForHover >= 0 &&
                                 m_bands[static_cast<size_t>(selectedForHover)].stereoButton.contains(event.position);
        if (stereoHover != m_selectedStereoHovered) {
            m_selectedStereoHovered = stereoHover;
            setDirty(true);
        }
        const bool scaleHover = m_curveScaleRect.contains(event.position);
        if (scaleHover != m_curveScaleHovered) {
            m_curveScaleHovered = scaleHover;
            setDirty(true);
        }
        const bool analyzerMenuHover = m_analyzerMenuRect.contains(event.position);
        if (analyzerMenuHover != m_analyzerMenuHovered) {
            m_analyzerMenuHovered = analyzerMenuHover;
            setDirty(true);
        }
        const bool analyzerSourceHover = m_analyzerPanelOpen && m_analyzerSourceRect.contains(event.position);
        if (analyzerSourceHover != m_analyzerSourceHovered) {
            m_analyzerSourceHovered = analyzerSourceHover;
            setDirty(true);
        }
        const bool analyzerStereoHover = m_analyzerPanelOpen && m_analyzerStereoRect.contains(event.position);
        if (analyzerStereoHover != m_analyzerStereoHovered) {
            m_analyzerStereoHovered = analyzerStereoHover;
            setDirty(true);
        }
        const bool analyzerTiltHover = m_analyzerPanelOpen && m_analyzerTiltRect.contains(event.position);
        if (analyzerTiltHover != m_analyzerTiltHovered) {
            m_analyzerTiltHovered = analyzerTiltHover;
            setDirty(true);
        }
        const bool analyzerDecayHover = m_analyzerPanelOpen && m_analyzerDecayRect.contains(event.position);
        if (analyzerDecayHover != m_analyzerDecayHovered) {
            m_analyzerDecayHovered = analyzerDecayHover;
            setDirty(true);
        }
        const bool analyzerFreezeHover = m_analyzerPanelOpen && m_analyzerFreezeRect.contains(event.position);
        if (analyzerFreezeHover != m_analyzerFreezeHovered) {
            m_analyzerFreezeHovered = analyzerFreezeHover;
            setDirty(true);
        }
        const bool analyzerCollisionHover = m_analyzerPanelOpen && m_analyzerCollisionRect.contains(event.position);
        if (analyzerCollisionHover != m_analyzerCollisionHovered) {
            m_analyzerCollisionHovered = analyzerCollisionHover;
            setDirty(true);
        }
        const bool analyzerCollisionStrengthHover =
            m_analyzerPanelOpen && m_analyzerCollisionStrengthRect.contains(event.position);
        if (analyzerCollisionStrengthHover != m_analyzerCollisionStrengthHovered) {
            m_analyzerCollisionStrengthHovered = analyzerCollisionStrengthHover;
            setDirty(true);
        }
        int hoveredSlot = -1;
        const NUIRect slotRailHit{m_selectedSlotRailRect.x, m_selectedSlotRailRect.y - 7.0f,
                                  m_selectedSlotRailRect.width, m_selectedSlotRailRect.height + 14.0f};
        if (m_selectedSlotRailRect.width > 0.0f && slotRailHit.contains(event.position)) {
            const float t =
                std::clamp((event.position.x - m_selectedSlotRailRect.x) / std::max(1.0f, m_selectedSlotRailRect.width),
                           0.0f, 1.0f);
            const uint32_t slot = static_cast<uint32_t>(
                std::round(t * static_cast<float>(Aestra::Audio::Plugins::AestraEQ::kMaxDynamicBands - 1)));
            const auto found = std::find_if(m_bands.begin(), m_bands.end(),
                                            [slot](const Band& band) { return band.slotIndex == slot; });
            if (found != m_bands.end()) {
                hoveredSlot = static_cast<int>(slot);
            }
        }
        if (hoveredSlot != m_hoveredSelectedSlot) {
            m_hoveredSelectedSlot = hoveredSlot;
            setDirty(true);
        }
    }

    return contains;
}

bool AestraEQEditor::onKeyEvent(const NUIKeyEvent& event) {
    if (!isVisible())
        return false;
    if (handleNumericEditKey(event))
        return true;
    if (m_typeMenuBand >= 0 && event.pressed && event.keyCode == NUIKeyCode::Escape) {
        m_typeMenuBand = -1;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        setDirty(true);
        return true;
    }
    if (m_stereoMenuBand >= 0 && event.pressed && event.keyCode == NUIKeyCode::Escape) {
        m_stereoMenuBand = -1;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        setDirty(true);
        return true;
    }
    if (m_bandContextMenuBand >= 0 && event.pressed && event.keyCode == NUIKeyCode::Escape) {
        closeBandContextMenu();
        return true;
    }
    const bool command = (event.modifiers & NUIModifiers::Ctrl) || (event.modifiers & NUIModifiers::Super);
    const int selectedIdx =
        (m_selectedBand >= 0 && m_selectedBand < static_cast<int>(m_bands.size())) ? m_selectedBand : -1;
    auto closeTransientBandMenus = [this]() {
        m_typeMenuBand = -1;
        m_typeMenuFromNodeQuickAction = false;
        m_hoveredTypeOption = -1;
        m_stereoMenuBand = -1;
        m_stereoMenuFromNodeQuickAction = false;
        m_hoveredStereoOption = -1;
        closeBandContextMenu();
    };
    if (event.pressed && !command && m_bandContextMenuBand >= 0) {
        bool matched = true;
        BandMenuAction action = BandMenuAction::Reset;
        switch (event.keyCode) {
        case NUIKeyCode::Home:
        case NUIKeyCode::R:
            action = BandMenuAction::Reset;
            break;
        case NUIKeyCode::I:
            action = BandMenuAction::InvertGain;
            break;
        case NUIKeyCode::E:
            action = BandMenuAction::ToggleDynamic;
            break;
        case NUIKeyCode::L:
            action = BandMenuAction::SplitLR;
            break;
        case NUIKeyCode::M:
            action = BandMenuAction::SplitMS;
            break;
        case NUIKeyCode::C:
            action = BandMenuAction::Copy;
            break;
        case NUIKeyCode::V:
            action = BandMenuAction::Paste;
            break;
        case NUIKeyCode::D:
            action = BandMenuAction::Duplicate;
            break;
        case NUIKeyCode::Delete:
        case NUIKeyCode::Backspace:
            action = BandMenuAction::Delete;
            break;
        case NUIKeyCode::X:
            action = BandMenuAction::ClearAll;
            break;
        default:
            matched = false;
            break;
        }
        if (matched) {
            if (canApplyBandContextAction(action)) {
                applyBandContextAction(action);
            }
            return true;
        }
    }
    if (event.pressed && !command) {
        if (event.keyCode == NUIKeyCode::B) {
            setBypassed(!isBypassed());
            closeTransientBandMenus();
            return true;
        }
        if (event.keyCode == NUIKeyCode::P) {
            setPolarityInverted(!polarityInverted());
            closeTransientBandMenus();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Num1) {
            switchCompareSlot(0);
            closeTransientBandMenus();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Num2) {
            switchCompareSlot(1);
            closeTransientBandMenus();
            return true;
        }
        if (selectedIdx >= 0) {
            if (event.keyCode == NUIKeyCode::E) {
                setBandValue(selectedIdx, Knob::Enable,
                             m_bands[static_cast<size_t>(selectedIdx)].enabled ? 0.0f : 1.0f);
                closeTransientBandMenus();
                return true;
            }
            if (event.keyCode == NUIKeyCode::R) {
                const bool reset = resetGraphBandToDefault(selectedIdx);
                closeTransientBandMenus();
                return reset;
            }
            if (event.keyCode == NUIKeyCode::S) {
                auto eq = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraEQ>(m_instance);
                if (eq) {
                    const auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
                    const int32_t solo = eq->getSoloBandSlot();
                    eq->setSoloBandSlot(
                        solo == static_cast<int32_t>(bd.slotIndex) ? -1 : static_cast<int32_t>(bd.slotIndex));
                    closeTransientBandMenus();
                    setDirty(true);
                    return true;
                }
            }
            if (event.keyCode == NUIKeyCode::T) {
                const auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
                if (bd.typeId != 0 || !bd.legacySlot) {
                    m_typeMenuBand = (m_typeMenuBand == selectedIdx) ? -1 : selectedIdx;
                    m_typeMenuFromNodeQuickAction = false;
                    m_hoveredTypeOption = -1;
                    m_stereoMenuBand = -1;
                    m_stereoMenuFromNodeQuickAction = false;
                    m_hoveredStereoOption = -1;
                    closeBandContextMenu();
                    setDirty(true);
                    return true;
                }
            }
            if (event.keyCode == NUIKeyCode::M) {
                const auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
                const float stereoNorm = (bd.legacySlot && bd.stereoId != 0 && m_instance)
                                             ? m_instance->getParameter(bd.stereoId)
                                             : bd.stereoNorm;
                const uint32_t next =
                    (static_cast<uint32_t>(std::round(quantizeStereoNorm(stereoNorm) * 4.0f)) + 1u) % 5u;
                setBandStereoMode(selectedIdx, next);
                closeTransientBandMenus();
                return true;
            }
            if (event.keyCode == NUIKeyCode::I) {
                auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
                if (bd.usesGain && m_instance && (bd.gainId != 0 || !bd.legacySlot)) {
                    bd.gain = 1.0f - bd.gain;
                    if (bd.legacySlot) {
                        m_instance->setParameter(bd.gainId, bd.gain);
                    } else {
                        writeDynamicBandSnapshot(selectedIdx);
                    }
                    syncBandsFromPlugin();
                    closeTransientBandMenus();
                    setDirty(true);
                    return true;
                }
            }
        }
    }
    if (event.pressed && selectedIdx >= 0 &&
        (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) &&
        !m_bands[static_cast<size_t>(selectedIdx)].legacySlot) {
        deleteBand(selectedIdx);
        closeTransientBandMenus();
        return true;
    }
    if (event.pressed && selectedIdx >= 0 && event.keyCode == NUIKeyCode::Home) {
        const bool reset = resetGraphBandToDefault(selectedIdx);
        closeTransientBandMenus();
        return reset;
    }
    if (event.pressed && selectedIdx >= 0 && event.keyCode == NUIKeyCode::Tab) {
        const int direction = (event.modifiers & NUIModifiers::Shift) ? -1 : 1;
        if (adjacentGraphBand(direction) >= 0) {
            selectAdjacentBand(direction);
            return true;
        }
    }
    if (event.pressed && command && selectedIdx >= 0) {
        if (event.keyCode == NUIKeyCode::I) {
            auto& bd = m_bands[static_cast<size_t>(selectedIdx)];
            if (bd.usesGain && m_instance && (bd.gainId != 0 || !bd.legacySlot)) {
                bd.gain = 1.0f - bd.gain;
                if (bd.legacySlot) {
                    m_instance->setParameter(bd.gainId, bd.gain);
                } else {
                    writeDynamicBandSnapshot(selectedIdx);
                }
                syncBandsFromPlugin();
                closeTransientBandMenus();
                setDirty(true);
                return true;
            }
        }
        if (event.keyCode == NUIKeyCode::C) {
            copyBandToClipboard(selectedIdx);
            closeTransientBandMenus();
            setDirty(true);
            return true;
        }
        if (event.keyCode == NUIKeyCode::V) {
            pasteClipboardToBand(selectedIdx);
            closeTransientBandMenus();
            return true;
        }
        if (event.keyCode == NUIKeyCode::D) {
            duplicateBand(selectedIdx);
            closeTransientBandMenus();
            return true;
        }
    }
    if (nudgeSelectedBand(event))
        return true;
    return AestraPanelWindow::onKeyEvent(event);
}

} // namespace AestraUI
