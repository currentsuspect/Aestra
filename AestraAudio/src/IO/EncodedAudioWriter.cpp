// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "IO/EncodedAudioWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <random>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Aestra {
namespace Audio {

namespace {

#ifdef _WIN32
// Windows loads the encoders only from Aestra's own folder, never by bare name: the default DLL search order includes
// the current directory, where a planted libmp3lame.dll would run inside Aestra.
void* loadFromAppFolder(const char* name) {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return nullptr;
    std::wstring path(exe, n);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return nullptr;
    path.resize(slash + 1);
    for (const char* c = name; *c; ++c)
        path.push_back(static_cast<wchar_t>(*c));
    return reinterpret_cast<void*>(
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
}
#endif

void* openFirst(std::initializer_list<const char*> names, std::string& loaded) {
    for (const char* name : names) {
#ifdef _WIN32
        void* lib = loadFromAppFolder(name);
#else
        void* lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
        if (lib) {
            loaded = name;
            return lib;
        }
    }
    return nullptr;
}

void* symbol(void* lib, const char* name) {
    if (!lib)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

// --- libFLAC: only the stream-encoder calls an export needs --------------------------------------------------------
struct FlacApi {
    using New = void* (*)();
    using SetU = int (*)(void*, unsigned);
    using InitFile = int (*)(void*, const char*, void*, void*);
    using Process = int (*)(void*, const int32_t*, unsigned);
    using Finish = int (*)(void*);
    using Delete = void (*)(void*);
    New create = nullptr;
    SetU channels = nullptr, bits = nullptr, rate = nullptr, level = nullptr;
    InitFile initFile = nullptr;
    Process process = nullptr;
    Finish finish = nullptr;
    Delete destroy = nullptr;
    bool ok() const { return create && channels && bits && rate && level && initFile && process && finish && destroy; }
};

// --- libmp3lame -------------------------------------------------------------------------------------------------
struct LameApi {
    using Init = void* (*)();
    using SetI = int (*)(void*, int);
    using InitParams = int (*)(void*);
    using EncodeFloat = int (*)(void*, const float*, int, unsigned char*, int);
    using Flush = int (*)(void*, unsigned char*, int);
    using Close = int (*)(void*);
    Init init = nullptr;
    SetI inRate = nullptr, outRate = nullptr, channels = nullptr, bitrate = nullptr, quality = nullptr;
    InitParams initParams = nullptr;
    EncodeFloat encode = nullptr; // lame_encode_buffer_interleaved_ieee_float (LAME 3.100+)
    Flush flush = nullptr;
    Close close = nullptr;
    bool ok() const {
        return init && inRate && outRate && channels && bitrate && quality && initParams && encode && flush && close;
    }
};

struct Libraries {
    FlacApi flac;
    LameApi lame;
    EncoderAvailability availability;
};

const Libraries& libraries() {
    static Libraries libs;
    static std::once_flag once;
    std::call_once(once, [] {
        std::string name;
        void* flac = openFirst(
            {
#ifdef _WIN32
                "libFLAC.dll",
                "FLAC.dll",
#elif defined(__APPLE__)
                                   "libFLAC.dylib", "/opt/homebrew/lib/libFLAC.dylib", "/usr/local/lib/libFLAC.dylib",
#else
                                   "libFLAC.so.14", "libFLAC.so.12", "libFLAC.so.8", "libFLAC.so",
#endif
            },
            name);
        if (flac) {
            auto& f = libs.flac;
            f.create = reinterpret_cast<FlacApi::New>(symbol(flac, "FLAC__stream_encoder_new"));
            f.channels = reinterpret_cast<FlacApi::SetU>(symbol(flac, "FLAC__stream_encoder_set_channels"));
            f.bits = reinterpret_cast<FlacApi::SetU>(symbol(flac, "FLAC__stream_encoder_set_bits_per_sample"));
            f.rate = reinterpret_cast<FlacApi::SetU>(symbol(flac, "FLAC__stream_encoder_set_sample_rate"));
            f.level = reinterpret_cast<FlacApi::SetU>(symbol(flac, "FLAC__stream_encoder_set_compression_level"));
            f.initFile = reinterpret_cast<FlacApi::InitFile>(symbol(flac, "FLAC__stream_encoder_init_file"));
            f.process = reinterpret_cast<FlacApi::Process>(symbol(flac, "FLAC__stream_encoder_process_interleaved"));
            f.finish = reinterpret_cast<FlacApi::Finish>(symbol(flac, "FLAC__stream_encoder_finish"));
            f.destroy = reinterpret_cast<FlacApi::Delete>(symbol(flac, "FLAC__stream_encoder_delete"));
            libs.availability.flac = f.ok();
            if (f.ok())
                libs.availability.flacLibrary = name;
        }
        void* lame = openFirst(
            {
#ifdef _WIN32
                "libmp3lame.dll",
                "lame_enc.dll",
#elif defined(__APPLE__)
                                   "libmp3lame.dylib", "/opt/homebrew/lib/libmp3lame.dylib", "/usr/local/lib/libmp3lame.dylib",
#else
                                   "libmp3lame.so.0", "libmp3lame.so",
#endif
            },
            name);
        if (lame) {
            auto& l = libs.lame;
            l.init = reinterpret_cast<LameApi::Init>(symbol(lame, "lame_init"));
            l.inRate = reinterpret_cast<LameApi::SetI>(symbol(lame, "lame_set_in_samplerate"));
            l.outRate = reinterpret_cast<LameApi::SetI>(symbol(lame, "lame_set_out_samplerate"));
            l.channels = reinterpret_cast<LameApi::SetI>(symbol(lame, "lame_set_num_channels"));
            l.bitrate = reinterpret_cast<LameApi::SetI>(symbol(lame, "lame_set_brate"));
            l.quality = reinterpret_cast<LameApi::SetI>(symbol(lame, "lame_set_quality"));
            l.initParams = reinterpret_cast<LameApi::InitParams>(symbol(lame, "lame_init_params"));
            l.encode =
                reinterpret_cast<LameApi::EncodeFloat>(symbol(lame, "lame_encode_buffer_interleaved_ieee_float"));
            l.flush = reinterpret_cast<LameApi::Flush>(symbol(lame, "lame_encode_flush"));
            l.close = reinterpret_cast<LameApi::Close>(symbol(lame, "lame_close"));
            libs.availability.mp3 = l.ok();
            if (l.ok())
                libs.availability.mp3Library = name;
        }
        // The libraries stay loaded for the life of the process: the probe answer and the encoders share them.
    });
    return libs;
}

class FlacWriter final : public EncodedAudioWriter {
public:
    FlacWriter(const FlacApi& api, void* encoder, uint32_t channels, uint32_t bits)
        : m_api(api), m_encoder(encoder), m_channels(channels), m_bits(bits), m_rng(0x5eed1234u) {}
    ~FlacWriter() override {
        if (m_encoder) {
            m_api.finish(m_encoder);
            m_api.destroy(m_encoder);
        }
    }
    bool write(const float* interleaved, uint32_t frames) override {
        const size_t n = static_cast<size_t>(frames) * m_channels;
        m_scratch.resize(n);
        const double full = static_cast<double>((1u << (m_bits - 1)) - 1u);
        std::uniform_real_distribution<double> lsb(-0.5, 0.5);
        for (size_t i = 0; i < n; ++i) {
            double v = std::clamp(static_cast<double>(interleaved[i]), -1.0, 1.0) * full;
            if (m_bits <= 16)
                v += lsb(m_rng) + lsb(m_rng); // TPDF dither, as the WAV writer does at 16 bits
            m_scratch[i] =
                static_cast<int32_t>(std::clamp(std::lround(v), -static_cast<long>(full) - 1, static_cast<long>(full)));
        }
        return m_api.process(m_encoder, m_scratch.data(), frames) != 0;
    }
    bool finish() override {
        if (!m_encoder)
            return false;
        const bool ok = m_api.finish(m_encoder) != 0;
        m_api.destroy(m_encoder);
        m_encoder = nullptr;
        return ok;
    }

private:
    const FlacApi& m_api;
    void* m_encoder;
    uint32_t m_channels, m_bits;
    std::vector<int32_t> m_scratch;
    std::mt19937 m_rng;
};

class Mp3Writer final : public EncodedAudioWriter {
public:
    Mp3Writer(const LameApi& api, void* lame, FILE* file, uint32_t channels)
        : m_api(api), m_lame(lame), m_file(file), m_channels(channels) {}
    ~Mp3Writer() override {
        if (m_lame)
            m_api.close(m_lame);
        if (m_file)
            std::fclose(m_file);
    }
    bool write(const float* interleaved, uint32_t frames) override {
        // LAME's documented worst case: 1.25 * samples + 7200 bytes.
        m_out.resize(static_cast<size_t>(frames) * 5 / 4 + 7200);
        const int n =
            m_api.encode(m_lame, interleaved, static_cast<int>(frames), m_out.data(), static_cast<int>(m_out.size()));
        return n >= 0 && std::fwrite(m_out.data(), 1, static_cast<size_t>(n), m_file) == static_cast<size_t>(n);
    }
    bool finish() override {
        if (!m_lame)
            return false;
        m_out.resize(7200);
        const int n = m_api.flush(m_lame, m_out.data(), static_cast<int>(m_out.size()));
        bool ok = n >= 0 && std::fwrite(m_out.data(), 1, static_cast<size_t>(n), m_file) == static_cast<size_t>(n);
        m_api.close(m_lame);
        m_lame = nullptr;
        ok = std::fflush(m_file) == 0 && ok;
        ok = std::fclose(m_file) == 0 && ok;
        m_file = nullptr;
        (void)m_channels;
        return ok;
    }

private:
    const LameApi& m_api;
    void* m_lame;
    FILE* m_file;
    uint32_t m_channels;
    std::vector<unsigned char> m_out;
};

FILE* openForWrite(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    return _wfopen(wide.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

} // namespace

const EncoderAvailability& probeEncoders() {
    return libraries().availability;
}

const char* exportFormatExtension(ExportFormat format) {
    switch (format) {
    case ExportFormat::Flac:
        return "flac";
    case ExportFormat::Mp3:
        return "mp3";
    case ExportFormat::Wav:
    default:
        return "wav";
    }
}

std::unique_ptr<EncodedAudioWriter> EncodedAudioWriter::open(const std::string& path, const Settings& settings,
                                                             std::string& error) {
    const auto& libs = libraries();
    if (settings.channels == 0 || settings.channels > 2 || settings.sampleRate == 0) {
        error = "FLAC and MP3 export support mono or stereo at a known sample rate";
        return nullptr;
    }
    if (settings.format == ExportFormat::Flac) {
        if (!libs.availability.flac) {
            error = "FLAC export needs libFLAC, which was not found on this computer";
            return nullptr;
        }
        const uint32_t bits = settings.bitsPerSample <= 16 ? 16u : 24u;
        void* enc = libs.flac.create();
        if (!enc) {
            error = "the FLAC encoder could not be created";
            return nullptr;
        }
        libs.flac.channels(enc, settings.channels);
        libs.flac.bits(enc, bits);
        libs.flac.rate(enc, settings.sampleRate);
        libs.flac.level(enc, 5);
        // FLAC__STREAM_ENCODER_INIT_STATUS_OK is 0.
        if (libs.flac.initFile(enc, path.c_str(), nullptr, nullptr) != 0) {
            libs.flac.destroy(enc);
            error = "the FLAC file could not be created at " + path;
            return nullptr;
        }
        return std::make_unique<FlacWriter>(libs.flac, enc, settings.channels, bits);
    }
    if (settings.format == ExportFormat::Mp3) {
        if (!libs.availability.mp3) {
            error = "MP3 export needs libmp3lame (LAME), which was not found on this computer";
            return nullptr;
        }
        void* lame = libs.lame.init();
        if (!lame) {
            error = "the MP3 encoder could not be created";
            return nullptr;
        }
        libs.lame.inRate(lame, static_cast<int>(settings.sampleRate));
        libs.lame.outRate(lame, static_cast<int>(settings.sampleRate));
        libs.lame.channels(lame, static_cast<int>(settings.channels));
        libs.lame.bitrate(lame, static_cast<int>(settings.mp3Kbps));
        libs.lame.quality(lame, 2); // high quality
        if (libs.lame.initParams(lame) < 0) {
            libs.lame.close(lame);
            error = "the MP3 encoder rejected these settings (sample rate or bit rate)";
            return nullptr;
        }
        FILE* file = openForWrite(path);
        if (!file) {
            libs.lame.close(lame);
            error = "the MP3 file could not be created at " + path;
            return nullptr;
        }
        return std::make_unique<Mp3Writer>(libs.lame, lame, file, settings.channels);
    }
    error = "WAV export does not go through the encoder";
    return nullptr;
}

} // namespace Audio
} // namespace Aestra
