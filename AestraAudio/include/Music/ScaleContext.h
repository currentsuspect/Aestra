// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Scale kind enumeration for musical scales
 */
enum class ScaleKind {
    Chromatic,
    Major,
    Minor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    PentatonicMajor,
    PentatonicMinor,
    Blues
};

/**
 * @brief Scale context for pattern-level editing metadata
 */
struct ScaleContext {
    int rootKey = 0;        // 0-11, C=0, C#=1, etc.
    ScaleKind scaleKind = ScaleKind::Chromatic;
    bool snapToScale = false;

    /**
     * @brief Returns a default scale context (Chromatic, C, no snapping)
     */
    static ScaleContext defaultContext() { return ScaleContext{}; }

    /**
     * @brief Check if scale context has any non-default values
     */
    bool hasNonDefaultValues() const {
        return rootKey != 0 || scaleKind != ScaleKind::Chromatic || snapToScale;
    }
};

/**
 * @brief Clamp root key to valid range 0-11
 */
inline int clampRootKey(int key) {
    if (key < 0) return 0;
    if (key > 11) return 11;
    return key;
}

/**
 * @brief Store a normalized pattern override, or clear it when all values are defaults.
 */
inline void assignScaleContextOverride(std::optional<ScaleContext>& target, ScaleContext context) {
    context.rootKey = clampRootKey(context.rootKey);
    if (context.hasNonDefaultValues()) {
        target = context;
    } else {
        target.reset();
    }
}

/**
 * @brief Convert ScaleKind to string for serialization
 */
inline std::string scaleKindToString(ScaleKind kind) {
    switch (kind) {
        case ScaleKind::Chromatic: return "chromatic";
        case ScaleKind::Major: return "major";
        case ScaleKind::Minor: return "minor";
        case ScaleKind::HarmonicMinor: return "harmonicMinor";
        case ScaleKind::MelodicMinor: return "melodicMinor";
        case ScaleKind::Dorian: return "dorian";
        case ScaleKind::Phrygian: return "phrygian";
        case ScaleKind::Lydian: return "lydian";
        case ScaleKind::Mixolydian: return "mixolydian";
        case ScaleKind::Locrian: return "locrian";
        case ScaleKind::PentatonicMajor: return "pentatonicMajor";
        case ScaleKind::PentatonicMinor: return "pentatonicMinor";
        case ScaleKind::Blues: return "blues";
        default: return "chromatic";
    }
}

/**
 * @brief Convert string to ScaleKind, returns nullopt for unknown values
 */
inline std::optional<ScaleKind> scaleKindFromString(std::string_view str) {
    if (str == "chromatic") return ScaleKind::Chromatic;
    if (str == "major") return ScaleKind::Major;
    if (str == "minor") return ScaleKind::Minor;
    if (str == "harmonicMinor") return ScaleKind::HarmonicMinor;
    if (str == "melodicMinor") return ScaleKind::MelodicMinor;
    if (str == "dorian") return ScaleKind::Dorian;
    if (str == "phrygian") return ScaleKind::Phrygian;
    if (str == "lydian") return ScaleKind::Lydian;
    if (str == "mixolydian") return ScaleKind::Mixolydian;
    if (str == "locrian") return ScaleKind::Locrian;
    if (str == "pentatonicMajor") return ScaleKind::PentatonicMajor;
    if (str == "pentatonicMinor") return ScaleKind::PentatonicMinor;
    if (str == "blues") return ScaleKind::Blues;
    return std::nullopt;  // Unknown scale kind
}

} // namespace Audio
} // namespace Aestra
