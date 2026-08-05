// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// One parser for audio_settings.conf (#649).
//
// The file previously had two independent readers that disagreed about what it
// contained: AestraAudioController::loadSavedAudioSelection read `device` and
// `input_device` and nothing else, while AudioSettingsPage::loadSettings read
// all thirteen keys but could not apply any of them. That is why `samplerate`
// and `buffersize` were persisted, displayed, and applied by nobody — the
// reader that ran at startup did not know they existed.
//
// parse() and serialize() are stream-based and dependency-free so they can be
// tested without a platform layer or a UI; only path resolution and the
// convenience load()/save() wrappers live in the .cpp.

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace Aestra {

/**
 * @brief Audio configuration as persisted in audio_settings.conf.
 *
 * Every field carries an explicit "not present in the file" state. That
 * distinction is load-bearing: applying a *default* over a value the driver
 * negotiated is indistinguishable, at the call site, from applying a value the
 * user chose. Absent must mean "leave it alone", not "apply the default".
 */
struct AudioSettings {
    /// Sentinel for a key that was absent or unparseable.
    static constexpr int kUnset = -1;

    // Device-manager settings: applied before the stream is opened.
    int driver{kUnset};
    int deviceId{kUnset};
    int inputDeviceId{kUnset};
    int sampleRate{kUnset};
    int bufferSize{kUnset};

    // Engine DSP settings: applied once the engine exists.
    int qualityPreset{kUnset};
    int resampling{kUnset};
    int dithering{kUnset};
    int threads{kUnset};
    int previewDuckingDb{kUnset};
    int dcRemoval{kUnset};     ///< tri-state: kUnset / 0 off / 1 on
    int masterLimiter{kUnset}; ///< tri-state
    int multiThreading{kUnset};///< tri-state
};

/// True when a field was actually present in the file.
inline bool isSet(int field) { return field != AudioSettings::kUnset; }

// Presence is not sanity. kUnset rules out exactly one value (-1), so a
// hand-edited or corrupted config can still carry 0 or a negative through
// isSet() and reach a consumer. buffersize=0 is the dangerous one: it is a
// divisor and an allocation size. A value outside these ranges carries no user
// intent, so consumers treat it as absent and fall back to their default —
// loudly, never silently.

/// Plausible device sample rates. Generous on purpose; this rejects garbage,
/// not unusual-but-real hardware.
inline bool isPlausibleSampleRate(int v) { return v >= 8000 && v <= 768000; }

/// Plausible driver buffer sizes, in frames.
inline bool isPlausibleBufferSize(int v) { return v >= 16 && v <= 16384; }

/// Valid DitheringMode enumerators (None, Triangular, HighPass, NoiseShaped).
/// Casting an out-of-range integer to the enum and handing it to the engine is
/// not something the engine is required to survive.
inline bool isPlausibleDitheringMode(int v) { return v >= 0 && v <= 3; }

/// Valid resampling-quality indices as offered by the settings dropdown.
inline bool isPlausibleResamplingIndex(int v) { return v >= 0 && v <= 3; }

/**
 * @brief Parse `key=value` lines. Unknown keys are ignored; malformed values
 *        leave their field unset rather than defaulting.
 *
 * Deliberately tolerant: a corrupt line must not discard the rest of the file,
 * because that would silently reset every setting after it.
 */
AudioSettings parseAudioSettings(std::istream& in);

/**
 * @brief Write every SET field. Unset fields are omitted, so a round trip
 *        through parse/serialize never invents a value that was not there.
 */
void serializeAudioSettings(std::ostream& out, const AudioSettings& settings);

/// Resolved path to audio_settings.conf (app-data, with the legacy cwd fallback).
std::string audioSettingsConfigPath();

/// Convenience: parse the file at audioSettingsConfigPath(). All-unset if absent.
AudioSettings loadAudioSettings();

} // namespace Aestra
