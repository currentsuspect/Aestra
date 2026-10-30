// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// FLAC and MP3 export (v0.8.2, FD-61). The encoders are loaded when an export asks for them, not linked: libFLAC
// (BSD) and libmp3lame (LGPL, kept dynamically linked as FD-61 decided). A format whose library is not on the
// machine is reported unavailable rather than failing at link time, so Aestra builds and runs without either.
//
// The exporter renders exactly as for WAV and hands each finished block to write() through its block sink, so the
// encoded file is the same render as a WAV export, quantised by the encoder instead of the WAV writer.

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

enum class ExportFormat { Wav, Flac, Mp3 };

struct EncoderAvailability {
    bool flac = false;
    bool mp3 = false;
    std::string flacLibrary; ///< the library that loaded, for the export dialog's note
    std::string mp3Library;
};

/// Looks for the encoder libraries once per process and remembers the answer.
const EncoderAvailability& probeEncoders();

/// "wav", "flac", "mp3": the extension an export in this format should carry.
const char* exportFormatExtension(ExportFormat format);

class EncodedAudioWriter {
public:
    struct Settings {
        ExportFormat format = ExportFormat::Flac;
        uint32_t sampleRate = 48000;
        uint32_t channels = 2;
        uint32_t bitsPerSample = 24; ///< FLAC: 16 or 24 (16 gets TPDF dither)
        uint32_t mp3Kbps = 320;      ///< MP3: constant bit rate
    };

    /// Opens the file and the encoder. Null with @p error set when the format is unavailable or the file cannot be
    /// written.
    static std::unique_ptr<EncodedAudioWriter> open(const std::string& path, const Settings& settings,
                                                    std::string& error);
    virtual ~EncodedAudioWriter() = default;

    /// Interleaved float frames in [-1, 1]. False when the encoder failed.
    virtual bool write(const float* interleaved, uint32_t frames) = 0;
    /// Flushes and closes the file. False when the last of it could not be written.
    virtual bool finish() = 0;
};

} // namespace Audio
} // namespace Aestra
