// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/OutOfProcessPluginInstance.h"

#include "AestraLog.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Aestra {
namespace Audio {

namespace {

std::string hexEncode(const std::string& input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 2);
    for (unsigned char c : input) {
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string hexEncodeBytes(const void* data, size_t size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

bool hexDecodeBytes(const std::string& input, std::vector<float>& output) {
    if ((input.size() % 2) != 0) {
        return false;
    }
    const size_t byteCount = input.size() / 2;
    if ((byteCount % sizeof(float)) != 0) {
        return false;
    }
    output.resize(byteCount / sizeof(float));
    auto* bytes = reinterpret_cast<unsigned char*>(output.data());
    for (size_t i = 0; i < byteCount; ++i) {
        const int hi = hexValue(input[i * 2]);
        const int lo = hexValue(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        bytes[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

std::string formatToken(PluginFormat format) {
    switch (format) {
    case PluginFormat::VST3:
        return "vst3";
    case PluginFormat::CLAP:
        return "clap";
    case PluginFormat::Internal:
        return "internal";
    }
    return "unknown";
}

std::string defaultHostPath() {
    if (const char* env = std::getenv("AESTRA_PLUGIN_HOST_PATH")) {
        if (*env != '\0') {
            return env;
        }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return (std::filesystem::path(exePath).parent_path() / "AestraPluginHost.exe").string();
    }
#else
    char exePath[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        return (std::filesystem::path(exePath).parent_path() / "AestraPluginHost").string();
    }
#endif
    return "AestraPluginHost";
}

} // namespace

class PluginHostProcess {
public:
    explicit PluginHostProcess(std::string executablePath) : m_executablePath(std::move(executablePath)) {}
    ~PluginHostProcess() { stop(); }

    bool start();
    void stop();
    bool isRunning();
    bool sendLine(const std::string& line);
    bool readLine(std::string& line, std::chrono::milliseconds timeout);

private:
    std::string m_executablePath;
#ifndef _WIN32
    pid_t m_pid = -1;
    int m_stdinFd = -1;
    int m_stdoutFd = -1;
#endif
};

bool PluginHostProcess::start() {
#ifdef _WIN32
    return false;
#else
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
        return false;
    }

    m_pid = fork();
    if (m_pid < 0) {
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        return false;
    }

    if (m_pid == 0) {
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        // [SEC-FIX] Validate path is absolute and a regular file before exec to prevent
        // arbitrary code execution via AESTRA_PLUGIN_HOST_PATH environment variable.
        std::error_code ec;
        std::filesystem::path p(m_executablePath);
        if (!p.is_absolute() || !std::filesystem::is_regular_file(p, ec)) {
            _exit(127);
        }
        execl(m_executablePath.c_str(), m_executablePath.c_str(), "--stdio", static_cast<char*>(nullptr));
        _exit(127);
    }

    close(inPipe[0]);
    close(outPipe[1]);
    m_stdinFd = inPipe[1];
    m_stdoutFd = outPipe[0];
    fcntl(m_stdoutFd, F_SETFL, fcntl(m_stdoutFd, F_GETFL, 0) | O_NONBLOCK);
    return true;
#endif
}

void PluginHostProcess::stop() {
#ifndef _WIN32
    if (m_stdinFd >= 0) {
        sendLine("EXIT");
        close(m_stdinFd);
        m_stdinFd = -1;
    }
    if (m_stdoutFd >= 0) {
        close(m_stdoutFd);
        m_stdoutFd = -1;
    }
    if (m_pid > 0) {
        int status = 0;
        for (int i = 0; i < 10; ++i) {
            if (waitpid(m_pid, &status, WNOHANG) == m_pid) {
                m_pid = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        kill(m_pid, SIGTERM);
        waitpid(m_pid, &status, 0);
        m_pid = -1;
    }
#endif
}

bool PluginHostProcess::isRunning() {
#ifdef _WIN32
    return false;
#else
    if (m_pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(m_pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    if (result == m_pid) {
        m_pid = -1;
    }
    return false;
#endif
}

bool PluginHostProcess::sendLine(const std::string& line) {
#ifdef _WIN32
    (void)line;
    return false;
#else
    if (m_stdinFd < 0 || !isRunning()) {
        return false;
    }
    std::string framed = line;
    framed.push_back('\n');
    const char* data = framed.data();
    size_t remaining = framed.size();
    while (remaining > 0) {
        const ssize_t written = write(m_stdinFd, data, remaining);
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
#endif
}

bool PluginHostProcess::readLine(std::string& line, std::chrono::milliseconds timeout) {
#ifdef _WIN32
    (void)line;
    (void)timeout;
    return false;
#else
    if (m_stdoutFd < 0) {
        return false;
    }
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        char ch = '\0';
        const ssize_t n = read(m_stdoutFd, &ch, 1);
        if (n == 1) {
            if (ch == '\n') {
                return true;
            }
            line.push_back(ch);
            if (line.size() > 8192) {
                return false;
            }
        } else {
            if (!isRunning()) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return false;
#endif
}

OutOfProcessPluginInstance::OutOfProcessPluginInstance(PluginInfo info, std::string hostExecutablePath)
    : m_info(std::move(info)),
      m_hostExecutablePath(hostExecutablePath.empty() ? defaultHostPath() : std::move(hostExecutablePath)) {}

OutOfProcessPluginInstance::~OutOfProcessPluginInstance() {
    shutdown();
}

bool OutOfProcessPluginInstance::load() {
    if (m_loaded.load(std::memory_order_acquire)) {
        return true;
    }

    m_process = std::make_unique<PluginHostProcess>(m_hostExecutablePath);
    if (!m_process->start()) {
        markCrashed();
        Log::error("[PluginHost] Failed to start helper process: " + m_hostExecutablePath);
        return false;
    }

    std::ostringstream command;
    command << "LOAD " << formatToken(m_info.format) << " " << hexEncode(m_info.id) << " "
            << hexEncode(m_info.path.string());
    std::string response;
    if (!sendCommand(command.str(), &response) || response.find("OK") != 0) {
        markCrashed();
        Log::error("[PluginHost] Helper failed to load plugin: " + m_info.name);
        return false;
    }

    m_loaded.store(true, std::memory_order_release);
    return true;
}

bool OutOfProcessPluginInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    if (!load()) {
        return false;
    }
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    const size_t capacity = static_cast<size_t>(std::max<uint32_t>(1, maxBlockSize)) * m_transportChannels;
    m_pendingInput.assign(capacity, 0.0f);
    m_workerInput.assign(capacity, 0.0f);
    m_workerOutput.assign(capacity, 0.0f);
    m_readyOutput.assign(capacity, 0.0f);
    m_pendingFrames.store(0, std::memory_order_release);
    m_readyFrames.store(0, std::memory_order_release);
    m_pendingState.store(0, std::memory_order_release);
    m_readyState.store(0, std::memory_order_release);

    std::ostringstream command;
    command << "INIT " << sampleRate << " " << maxBlockSize;
    std::string response;
    if (!sendCommand(command.str(), &response) || response.find("OK") != 0) {
        markCrashed();
        return false;
    }
    startWorker();
    return true;
}

void OutOfProcessPluginInstance::shutdown() {
    m_active.store(false, std::memory_order_release);
    stopWorker();
    if (m_process) {
        sendCommand("SHUTDOWN", nullptr);
        m_process.reset();
    }
    m_loaded.store(false, std::memory_order_release);
}

void OutOfProcessPluginInstance::activate() {
    std::string response;
    if (sendCommand("ACTIVATE", &response) && response.find("OK") == 0) {
        m_active.store(true, std::memory_order_release);
    } else {
        markCrashed();
    }
}

void OutOfProcessPluginInstance::deactivate() {
    sendCommand("DEACTIVATE", nullptr);
    m_active.store(false, std::memory_order_release);
}

void OutOfProcessPluginInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                                         uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* midiInput,
                                         MidiBuffer* midiOutput) {
    (void)midiInput;
    if (midiOutput) {
        midiOutput->clear();
    }

    if (!m_process || !m_process->isRunning()) {
        markCrashed();
    }

    if (m_crashed.load(std::memory_order_acquire) || m_maxBlockSize == 0 || numFrames > m_maxBlockSize) {
        passThrough(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
        return;
    }

    uint8_t readyEmpty = 2;
    bool usedReadyOutput = false;
    if (m_readyState.compare_exchange_strong(readyEmpty, 3, std::memory_order_acq_rel)) {
        const uint32_t frames = std::min<uint32_t>(m_readyFrames.load(std::memory_order_acquire), numFrames);
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (!outputs || !outputs[ch]) {
                continue;
            }
            if (ch < m_transportChannels) {
                for (uint32_t frame = 0; frame < frames; ++frame) {
                    outputs[ch][frame] = m_readyOutput[static_cast<size_t>(frame) * m_transportChannels + ch];
                }
                if (frames < numFrames) {
                    std::memset(outputs[ch] + frames, 0, static_cast<size_t>(numFrames - frames) * sizeof(float));
                }
            } else {
                std::memset(outputs[ch], 0, static_cast<size_t>(numFrames) * sizeof(float));
            }
        }
        m_readyState.store(0, std::memory_order_release);
        usedReadyOutput = true;
    }

    uint8_t pendingEmpty = 0;
    if (m_pendingState.compare_exchange_strong(pendingEmpty, 1, std::memory_order_acq_rel)) {
        for (uint32_t frame = 0; frame < numFrames; ++frame) {
            for (uint32_t ch = 0; ch < m_transportChannels; ++ch) {
                const size_t index = static_cast<size_t>(frame) * m_transportChannels + ch;
                m_pendingInput[index] = (inputs && ch < numInputChannels && inputs[ch]) ? inputs[ch][frame] : 0.0f;
            }
        }
        m_pendingFrames.store(numFrames, std::memory_order_release);
        m_pendingState.store(2, std::memory_order_release);
    }

    if (!usedReadyOutput) {
        passThrough(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
    }
}

float OutOfProcessPluginInstance::getParameter(uint32_t id) const {
    (void)id;
    return 0.0f;
}

void OutOfProcessPluginInstance::setParameter(uint32_t id, float value) {
    (void)id;
    (void)value;
}

std::string OutOfProcessPluginInstance::getParameterDisplay(uint32_t id) const {
    (void)id;
    return {};
}

bool OutOfProcessPluginInstance::loadState(const std::vector<uint8_t>& state) {
    return state.empty();
}

bool OutOfProcessPluginInstance::openEditor(void* parentWindow) {
    (void)parentWindow;
    return false;
}

bool OutOfProcessPluginInstance::resizeEditor(int width, int height) {
    (void)width;
    (void)height;
    return false;
}

void OutOfProcessPluginInstance::resetWatchdog() {
    m_watchdogStats = WatchdogStats{};
    if (m_process && m_process->isRunning()) {
        m_crashed.store(false, std::memory_order_release);
    }
}

bool OutOfProcessPluginInstance::sendCommand(const std::string& command, std::string* response) {
    std::lock_guard<std::mutex> lock(m_ipcMutex);
    if (!m_process || !m_process->isRunning()) {
        return false;
    }
    if (!m_process->sendLine(command)) {
        return false;
    }
    std::string localResponse;
    if (!m_process->readLine(localResponse, std::chrono::milliseconds(500))) {
        return false;
    }
    if (response) {
        *response = std::move(localResponse);
    }
    return true;
}

void OutOfProcessPluginInstance::startWorker() {
    if (m_workerRunning.load(std::memory_order_acquire)) {
        return;
    }
    m_workerStop.store(false, std::memory_order_release);
    m_workerThread = std::thread([this] { workerLoop(); });
    m_workerRunning.store(true, std::memory_order_release);
}

void OutOfProcessPluginInstance::stopWorker() {
    m_workerStop.store(true, std::memory_order_release);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_workerRunning.store(false, std::memory_order_release);
}

void OutOfProcessPluginInstance::workerLoop() {
    while (!m_workerStop.load(std::memory_order_acquire)) {
        if (m_crashed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        uint8_t pendingReady = 2;
        if (!m_pendingState.compare_exchange_strong(pendingReady, 3, std::memory_order_acq_rel)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const uint32_t frames = m_pendingFrames.load(std::memory_order_acquire);
        const size_t sampleCount = static_cast<size_t>(frames) * m_transportChannels;
        if (sampleCount <= m_workerInput.size()) {
            std::copy_n(m_pendingInput.data(), sampleCount, m_workerInput.data());
        }
        m_pendingState.store(0, std::memory_order_release);

        if (sampleCount > m_workerInput.size() ||
            !processBlockInHelper(m_workerInput, m_transportChannels, frames, m_workerOutput)) {
            markCrashed();
            continue;
        }

        uint8_t readyEmpty = 0;
        if (m_readyState.compare_exchange_strong(readyEmpty, 1, std::memory_order_acq_rel)) {
            const size_t outputSamples = std::min(sampleCount, m_workerOutput.size());
            std::copy_n(m_workerOutput.data(), outputSamples, m_readyOutput.data());
            m_readyFrames.store(frames, std::memory_order_release);
            m_readyState.store(2, std::memory_order_release);
        }
    }
}

bool OutOfProcessPluginInstance::processBlockInHelper(const std::vector<float>& input, uint32_t channels,
                                                      uint32_t frames, std::vector<float>& output) {
    const size_t sampleCount = static_cast<size_t>(channels) * frames;
    if (sampleCount > input.size()) {
        return false;
    }
    std::ostringstream command;
    command << "PROCESS " << channels << " " << frames << " "
            << hexEncodeBytes(input.data(), sampleCount * sizeof(float));
    std::string response;
    if (!sendCommand(command.str(), &response)) {
        return false;
    }
    constexpr const char* kPrefix = "OK ";
    if (response.compare(0, 3, kPrefix) != 0) {
        return false;
    }
    return hexDecodeBytes(response.substr(3), output) && output.size() >= sampleCount;
}

void OutOfProcessPluginInstance::markCrashed() {
    m_crashed.store(true, std::memory_order_release);
    m_active.store(false, std::memory_order_release);
    m_watchdogStats.isBypassed = true;
    m_watchdogStats.violationCount++;
}

void OutOfProcessPluginInstance::passThrough(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                                             uint32_t numOutputChannels, uint32_t numFrames) const {
    for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
        if (!outputs || !outputs[ch]) {
            continue;
        }
        if (inputs && ch < numInputChannels && inputs[ch]) {
            if (outputs[ch] != inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], static_cast<size_t>(numFrames) * sizeof(float));
            }
        } else {
            std::memset(outputs[ch], 0, static_cast<size_t>(numFrames) * sizeof(float));
        }
    }
}

} // namespace Audio
} // namespace Aestra
