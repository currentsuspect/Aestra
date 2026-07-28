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
