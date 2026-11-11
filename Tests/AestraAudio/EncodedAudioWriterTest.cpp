// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FLAC and MP3 export (v0.8.2): a 1 kHz tone written through EncodedAudioWriter decodes back to the same length and
// level. FLAC is lossless to its bit depth, so the samples themselves come back; MP3 is lossy, so its length and level
// are checked. The encoders load at runtime, so on a machine without one the test says so and skips (exit 77).

#include "IO/EncodedAudioWriter.h"

#include "IO/MiniAudioDecoder.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
constexpr uint32_t kRate = 48000;
constexpr uint32_t kFrames = kRate; // one second

std::vector<float> tone() {
    std::vector<float> v(kFrames * 2);
    for (uint32_t i = 0; i < kFrames; ++i) {
        const float s = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / kRate));
        v[2 * i] = s;
        v[2 * i + 1] = -s; // the channels must not be swapped or summed
    }
    return v;
}

int fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

bool writeFile(const std::string& path, EncodedAudioWriter::Settings s, const std::vector<float>& audio) {
    std::string error;
    auto w = EncodedAudioWriter::open(path, s, error);
    if (!w) {
        std::fprintf(stderr, "open failed: %s\n", error.c_str());
        return false;
    }
    for (uint32_t at = 0; at < kFrames; at += 4096) { // the exporter's sink hands over blocks
        const uint32_t n = std::min<uint32_t>(4096, kFrames - at);
        if (!w->write(audio.data() + static_cast<size_t>(at) * 2, n))
            return false;
    }
    return w->finish();
}
} // namespace

int main() {
    const auto& available = probeEncoders();
    std::printf("encoders: flac=%s (%s) mp3=%s (%s)\n", available.flac ? "yes" : "no", available.flacLibrary.c_str(),
                available.mp3 ? "yes" : "no", available.mp3Library.c_str());
    if (!available.flac && !available.mp3) {
        std::printf("SKIP: neither libFLAC nor libmp3lame is installed here\n");
        return 77;
    }
    const auto dir = std::filesystem::temp_directory_path() / "aestra-encoded-writer-test";
    std::filesystem::create_directories(dir);
    const auto audio = tone();

    if (available.flac) {
        const std::string path = (dir / "tone.flac").string();
        EncodedAudioWriter::Settings s;
        s.format = ExportFormat::Flac;
        s.sampleRate = kRate;
        s.bitsPerSample = 24;
        if (!writeFile(path, s, audio))
            return fail("the FLAC file was written");
        std::vector<float> back;
        uint32_t rate = 0, channels = 0;
        if (!decodeAudioFile(path, back, rate, channels))
            return fail("the FLAC file decodes");
        if (rate != kRate || channels != 2 || back.size() != audio.size())
            return fail("FLAC keeps rate, channels and length");
        float worst = 0.0f;
        for (size_t i = 0; i < audio.size(); ++i)
            worst = std::max(worst, std::fabs(back[i] - audio[i]));
        if (worst > 1.0f / 8388607.0f * 2.0f)
            return fail("24-bit FLAC returns the samples (within a step)");
        std::printf("[PASS] 24-bit FLAC: 1 s stereo round trip, worst sample error %.2e\n", worst);
    }
    if (available.mp3) {
        const std::string path = (dir / "tone.mp3").string();
        EncodedAudioWriter::Settings s;
        s.format = ExportFormat::Mp3;
        s.sampleRate = kRate;
        s.mp3Kbps = 320;
        if (!writeFile(path, s, audio))
            return fail("the MP3 file was written");
        std::vector<float> back;
        uint32_t rate = 0, channels = 0;
        if (!decodeAudioFile(path, back, rate, channels))
            return fail("the MP3 file decodes");
        const double seconds = static_cast<double>(back.size()) / 2.0 / rate;
        // MP3 adds encoder delay and padding: about 1 s, never more than a few frames' worth longer.
        if (rate != kRate || channels != 2 || seconds < 0.99 || seconds > 1.1)
            return fail("MP3 keeps rate, channels and length");
        double sumL = 0.0, sumR = 0.0, cross = 0.0;
        for (size_t i = 0; i < back.size() / 2; ++i) {
            sumL += back[2 * i] * back[2 * i];
            sumR += back[2 * i + 1] * back[2 * i + 1];
            cross += back[2 * i] * back[2 * i + 1];
        }
        const double rmsL = std::sqrt(sumL / (back.size() / 2));
        if (std::fabs(rmsL - 0.3536) > 0.02)
            return fail("MP3 keeps the level (0.5 sine = 0.354 RMS)");
        if (cross >= 0.0)
            return fail("MP3 keeps the channels apart (L and R are opposite)");
        std::printf("[PASS] 320 kbps MP3: %.3f s, RMS %.3f, channels kept apart\n", seconds, rmsL);
    }
    std::string error;
    EncodedAudioWriter::Settings wav;
    wav.format = ExportFormat::Wav;
    if (EncodedAudioWriter::open((dir / "x.wav").string(), wav, error) || error.empty())
        return fail("WAV is refused with a reason");
    std::printf("[PASS] WAV is refused (it has its own writer)\n");
    std::filesystem::remove_all(dir);
    return 0;
}
