// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Reading a value the way it is written: "3.5k", "3500 Hz", "+2.4 dB", "30 ms",
// "2:1", "85%".
//
// Header-only and shared by the built-in plugins that declare their display
// units (PluginHost.h: ParameterTextInfo), because the alternative is the same
// suffix parsing written twelve times, with only some of them handling "kHz".
//
// This is the inverse of what each plugin's getParameterDisplay() prints, and it
// lives beside the declaration rather than in the command layer on purpose: a
// mapping copied into Muse would be a second source for one number, and the two
// would drift (#1405).

#include "Plugin/PluginHost.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Aestra {
namespace Audio {
namespace ParamText {

/// A value lifted out of text, before it is known to belong to any parameter.
struct ParsedValue {
    double value = 0.0;
    std::string unit; ///< lower-case, no spaces: "hz", "ms", "%", ":1", "" when absent
};

namespace detail {

inline std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

inline std::string lowered(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

inline bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace detail

/**
 * @brief Parse "<number>[multiplier][unit]" into a value and its unit token.
 *
 * The multiplier (k/M/G) is folded into the value, so "3.5k" and "3.5 kHz" both
 * come back as 3500 Hz. @p error is written only on failure.
 */
inline bool parse(const std::string& text, ParsedValue& out, std::string& error) {
    const std::string trimmed = detail::trim(text);
    if (trimmed.empty()) {
        error = "empty value";
        return false;
    }

    // A leading sign, then digits, a decimal part and an exponent. strtod also
    // skips leading space and accepts "inf"/"nan", so the shape is checked first.
    size_t pos = 0;
    if (trimmed[pos] == '+' || trimmed[pos] == '-') ++pos;
    size_t digits = 0;
    while (pos < trimmed.size() && detail::isDigit(trimmed[pos])) {
        ++pos;
        ++digits;
    }
    if (pos < trimmed.size() && trimmed[pos] == '.') {
        ++pos;
        while (pos < trimmed.size() && detail::isDigit(trimmed[pos])) {
            ++pos;
            ++digits;
        }
    }
    if (digits == 0) {
        error = "does not start with a number";
        return false;
    }
    if (pos < trimmed.size() && (trimmed[pos] == 'e' || trimmed[pos] == 'E')) {
        size_t exponent = pos + 1;
        if (exponent < trimmed.size() && (trimmed[exponent] == '+' || trimmed[exponent] == '-')) ++exponent;
        size_t exponentDigits = 0;
        while (exponent < trimmed.size() && detail::isDigit(trimmed[exponent])) {
            ++exponent;
            ++exponentDigits;
        }
        if (exponentDigits > 0) pos = exponent;
    }

    const std::string numberText = trimmed.substr(0, pos);
    const char* begin = numberText.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || *end != '\0' || !std::isfinite(value)) {
        error = "is not a finite number";
        return false;
    }

    // Spaces between the number and its unit are noise: "30 ms".
    std::string rest = detail::trim(trimmed.substr(pos));

    double magnitude = 1.0;
    if (!rest.empty() && (rest[0] == 'k' || rest[0] == 'K')) {
        magnitude = 1.0e3;
        rest = rest.substr(1);
    } else if (!rest.empty() && rest[0] == 'M') {
        magnitude = 1.0e6;
        rest = rest.substr(1);
    } else if (!rest.empty() && rest[0] == 'G') {
        magnitude = 1.0e9;
        rest = rest.substr(1);
    }

    out.value = value * magnitude;
    // "kHz" leaves "hz" behind once the multiplier is lifted, which is the
    // declared unit.
    out.unit = detail::lowered(rest);
    return true;
}

/**
 * @brief Is @p written an acceptable spelling of @p declared?
 *
 * A missing unit means "in the parameter's own unit", which is what "3.5k"
 * relies on. Seconds are accepted where milliseconds are declared, and folded in
 * by scaleFor().
 */
inline bool unitMatches(const std::string& written, const std::string& declared) {
    const std::string want = detail::lowered(declared);
    if (written.empty()) return true;
    if (written == want) return true;
    if (want == "ms" &&
        (written == "s" || written == "sec" || written == "secs" || written == "second" || written == "seconds"))
        return true;
    if (want == "s" && (written == "ms" || written == "msec")) return true;
    if (want == ":1" && (written == "x" || written == "ratio")) return true;
    return false;
}

/// Scales a written value into the declared unit (seconds -> milliseconds).
inline double scaleFor(const std::string& written, const std::string& declared, double value) {
    const std::string want = detail::lowered(declared);
    if (want == "ms" &&
        (written == "s" || written == "sec" || written == "secs" || written == "second" || written == "seconds"))
        return value * 1000.0;
    if (want == "s" && (written == "ms" || written == "msec")) return value / 1000.0;
    return value;
}

/// "80 Hz .. 8000 Hz (log)" — the range an error message quotes back.
inline std::string describeRange(const ParameterTextInfo& info) {
    char buf[128];
    if (info.curve == ParamCurve::Stepped) {
        std::snprintf(buf, sizeof(buf), "one of %zu steps", info.labels.size());
        return buf;
    }
    const char* curve = info.curve == ParamCurve::Log ? "log" : "linear";
    std::snprintf(buf, sizeof(buf), "%.6g %s .. %.6g %s (%s)", info.min, info.unit.c_str(), info.max,
                  info.unit.c_str(), curve);
    return buf;
}

/**
 * @brief Read @p text as this parameter's value, normalized 0..1.
 *
 * Range, unit and curve come from the same declaration get_effects publishes, so
 * a value the caller computed from it lands exactly. Out of range is refused with
 * the range quoted, never silently clamped: the caller asked for a value the
 * parameter cannot hold, and clamping would hide the mistake.
 */
inline bool resolve(const std::string& text, const ParameterTextInfo& info, float& normalized,
                    std::string& error) {
    if (info.curve == ParamCurve::Stepped) {
        if (info.labels.size() < 2) {
            error = "this parameter has no declared steps";
            return false;
        }
        const std::string wanted = detail::lowered(detail::trim(text));
        for (size_t i = 0; i < info.labels.size(); ++i) {
            const std::string label = detail::lowered(info.labels[i]);
            bool matches = label == wanted;
            if (!matches) {
                // "36" selects "36 dB/oct": a label's leading number is its value.
                ParsedValue labelValue;
                std::string labelError;
                if (parse(info.labels[i], labelValue, labelError)) {
                    ParsedValue given;
                    std::string givenError;
                    matches = parse(text, given, givenError) &&
                              std::abs(given.value - labelValue.value) < 1.0e-6 &&
                              (given.unit.empty() || unitMatches(given.unit, labelValue.unit));
                }
            }
            if (matches) {
                normalized = static_cast<float>(i) / static_cast<float>(info.labels.size() - 1);
                return true;
            }
        }
        error = "expected one of: ";
        for (size_t i = 0; i < info.labels.size(); ++i) {
            if (i) error += ", ";
            error += info.labels[i];
        }
        return false;
    }

    ParsedValue parsed;
    std::string parseError;
    if (!parse(text, parsed, parseError)) {
        error = "cannot read '" + text + "' (" + parseError + "); expected " + describeRange(info);
        return false;
    }
    if (!unitMatches(parsed.unit, info.unit)) {
        error = "wrong unit: expect" + std::string(info.unit.empty() ? "s a plain number" : "s " + info.unit) +
                ", got '" + parsed.unit + "'";
        return false;
    }
    const double value = scaleFor(parsed.unit, info.unit, parsed.value);
    if (value < info.min || value > info.max) {
        char got[64];
        std::snprintf(got, sizeof(got), "%.6g", value);
        error = "out of range: got " + std::string(got) + ", expected " + describeRange(info);
        return false;
    }
    const double span = info.max - info.min;
    if (span <= 0.0) {
        error = "this parameter has no range";
        return false;
    }
    double norm = 0.0;
    if (info.curve == ParamCurve::Log) {
        if (info.min <= 0.0 || info.max <= 0.0) {
            error = "log parameter with a non-positive limit";
            return false;
        }
        norm = std::log(value / info.min) / std::log(info.max / info.min);
    } else {
        norm = (value - info.min) / span;
    }
    normalized = static_cast<float>(std::min(1.0, std::max(0.0, norm)));
    return true;
}

/// The value a normalized parameter stands for, in the parameter's own unit.
inline double fromNormalized(float normalized, const ParameterTextInfo& info) {
    const double norm = static_cast<double>(std::min(1.0f, std::max(0.0f, normalized)));
    if (info.curve == ParamCurve::Log && info.min > 0.0 && info.max > 0.0)
        return info.min * std::pow(info.max / info.min, norm);
    return info.min + norm * (info.max - info.min);
}

} // namespace ParamText
} // namespace Audio
} // namespace Aestra
