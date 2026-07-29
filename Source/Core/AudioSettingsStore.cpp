// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioSettingsStore.h"

#include "AestraPlatform.h"

#include <array>
#include <filesystem>
#include <fstream>

namespace Aestra {

namespace {

struct KeyBinding {
    const char* key;
    int AudioSettings::*field;
};

// The complete key set. Both the parser and the serializer drive off this, so a
// key cannot be readable but unwritable (or the reverse) — which is exactly the
// asymmetry that let `buffersize` be saved by one component and unknown to
// another.
constexpr std::array<KeyBinding, 13> kBindings{{
    {"driver", &AudioSettings::driver},
    {"device", &AudioSettings::deviceId},
    {"input_device", &AudioSettings::inputDeviceId},
    {"samplerate", &AudioSettings::sampleRate},
    {"buffersize", &AudioSettings::bufferSize},
    {"quality_preset", &AudioSettings::qualityPreset},
    {"resampling", &AudioSettings::resampling},
    {"dithering", &AudioSettings::dithering},
    {"threads", &AudioSettings::threads},
    {"dc_removal", &AudioSettings::dcRemoval},
    {"master_limiter", &AudioSettings::masterLimiter},
    {"preview_ducking_db", &AudioSettings::previewDuckingDb},
    {"multi_threading", &AudioSettings::multiThreading},
}};

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

AudioSettings parseAudioSettings(std::istream& in) {
    AudioSettings settings;

    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) {
            continue;
        }

        for (const auto& binding : kBindings) {
            if (key != binding.key) {
                continue;
            }
            // A malformed value leaves the field UNSET rather than defaulting.
            // Defaulting here would be indistinguishable, downstream, from the
            // user having chosen the default.
            try {
                size_t consumed = 0;
                const int parsed = std::stoi(value, &consumed);
                if (consumed == value.size()) {
                    settings.*(binding.field) = parsed;
                }
            } catch (const std::exception&) {
                // leave unset
            }
            break;
        }
    }

    return settings;
}

void serializeAudioSettings(std::ostream& out, const AudioSettings& settings) {
    for (const auto& binding : kBindings) {
        const int value = settings.*(binding.field);
        if (value != AudioSettings::kUnset) {
            out << binding.key << '=' << value << '\n';
        }
    }
}

std::string audioSettingsConfigPath() {
    std::error_code ec;
    if (auto* utils = Aestra::Platform::getUtils()) {
        std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
        if (!appDataDir.empty()) {
            std::filesystem::create_directories(appDataDir, ec);
            if (!ec) {
                std::filesystem::path appDataPath = appDataDir / "audio_settings.conf";
                if (std::filesystem::exists(appDataPath, ec)) {
                    return appDataPath.string();
                }
            }
        }
    }

    // Legacy location: a config written before the app-data move.
    std::filesystem::path legacyPath = std::filesystem::current_path() / "audio_settings.conf";
    if (std::filesystem::exists(legacyPath, ec)) {
        return legacyPath.string();
    }

    if (auto* utils = Aestra::Platform::getUtils()) {
        std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
        if (!appDataDir.empty()) {
            return (appDataDir / "audio_settings.conf").string();
        }
    }
    return legacyPath.string();
}

AudioSettings loadAudioSettings() {
    std::ifstream file(audioSettingsConfigPath());
    if (!file.is_open()) {
        return {}; // all unset — nothing persisted, so nothing to apply
    }
    return parseAudioSettings(file);
}

} // namespace Aestra
