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
#include <vector>

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
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}

bool hexDecodeRaw(const std::string& input, std::vector<uint8_t>& output) {
    if ((input.size() % 2) != 0) {
        return false;
    }
    output.resize(input.size() / 2);
    for (size_t i = 0; i < output.size(); ++i) {
        const int hi = hexValue(input[i * 2]);
        const int lo = hexValue(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        output[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
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

#ifdef _WIN32
// Owns one Win32 HANDLE. start() juggles five of them across four exit paths, and
// every future early return would otherwise have to remember the right subset of
// CloseHandle calls by hand.
class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : m_handle(handle) {}
    ~ScopedHandle() { reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    void reset(HANDLE handle = nullptr) {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }

    HANDLE get() const { return m_handle; }
    HANDLE* put() { return &m_handle; }

    // Hand ownership to a long-lived member once the call has fully succeeded.
    HANDLE release() {
        HANDLE handle = m_handle;
        m_handle = nullptr;
        return handle;
    }

    explicit operator bool() const { return m_handle && m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = nullptr;
};
#endif

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

    // True once a command or a response was truncated part-way through a line.
    // The newline framing cannot be recovered from that: the next read returns the
    // tail of the abandoned response, so every later request/response pair is
    // mismatched. Callers must fail the instance instead of continuing to talk.
    //
    // KNOWN GAP: this catches a reply cut off mid-line, not one that arrives
    // complete but late. A command that times out with nothing read leaves the
    // channel usable, and if the child answers afterwards that reply is returned to
    // whichever command asks next — the same mismatch, undetectable from the byte
    // stream alone. Closing it needs a sequence tag per command so a stale reply can
    // be recognised and discarded instead of the channel being thrown away; that is
    // a protocol change, deliberately not bundled with this fix.
    bool framingLost() const { return m_framingLost; }

private:
    // Matches the POSIX and Windows runaway-line caps; a line this long means the
    // child is malfunctioning, not that a reply is merely large.
    static constexpr size_t kMaxLineBytes = 16ull * 1024ull * 1024ull;

    std::string m_executablePath;
    bool m_framingLost = false;
#ifdef _WIN32
    HANDLE m_process = nullptr;   // child process
    HANDLE m_stdinWrite = nullptr;  // our end of the child's stdin
    HANDLE m_stdoutRead = nullptr;  // our end of the child's stdout
    // Bytes read past the current line. A bulk read can cross a response boundary,
    // so the remainder is kept here rather than thrown away.
    std::string m_readCarry;
#else
    pid_t m_pid = -1;
    int m_stdinFd = -1;
    int m_stdoutFd = -1;
#endif
};

bool PluginHostProcess::start() {
#ifdef _WIN32
    // Windows previously returned false here, which meant plugins were never
    // sandboxed on Windows at all: the caller fell back to in-process hosting, so
    // any plugin fault took Aestra down with it. Linux/macOS had fork() isolation
    // the whole time.
    if (m_process) {
        return false; // already started
    }

    // Same pre-exec check the POSIX path performs. AESTRA_PLUGIN_HOST_PATH is
    // attacker-influencable, so refuse anything that is not an absolute path to a
    // regular file before handing it to CreateProcess (SEC-FIX parity).
    std::error_code ec;
    const std::filesystem::path exe(m_executablePath);
    if (!exe.is_absolute() || !std::filesystem::is_regular_file(exe, ec)) {
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // Every handle below is scoped: on any failure they close themselves, and the
    // two we keep are released into members only once CreateProcess has succeeded.
    ScopedHandle childStdinRead;
    ScopedHandle childStdoutWrite;
    ScopedHandle stdinWrite;
    ScopedHandle stdoutRead;
    if (!CreatePipe(childStdinRead.put(), stdinWrite.put(), &sa, 0)) {
        return false;
    }
    if (!CreatePipe(stdoutRead.put(), childStdoutWrite.put(), &sa, 0)) {
        return false;
    }

    // Our ends must NOT be inheritable. If they are, the child holds a duplicate
    // of the write end of its own stdout and the pipe never reports EOF when the
    // child dies — readLine() would then block until its timeout on every crash
    // instead of noticing immediately.
    SetHandleInformation(stdinWrite.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead.get(), HANDLE_FLAG_INHERIT, 0);

    // STARTF_USESTDHANDLES makes all three std handles significant, so stderr
    // cannot just be whatever GetStdHandle returns. Two cases break it: a GUI
    // process has no console and gets back NULL or INVALID_HANDLE_VALUE, and even a
    // valid console handle is not necessarily inheritable — CreateProcess passes
    // only inheritable handles. Either way the child would be left with an
    // unusable stderr, losing exactly the diagnostics that matter most when an
    // untrusted plugin dies inside the helper. Duplicating it inheritable keeps
    // them; a NULL here (no console to forward to) is the documented way to say
    // "no stderr", which is very different from a bogus handle.
    ScopedHandle childStderr;
    const HANDLE parentStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (parentStderr && parentStderr != INVALID_HANDLE_VALUE) {
        if (!DuplicateHandle(GetCurrentProcess(), parentStderr, GetCurrentProcess(), childStderr.put(), 0,
                             TRUE, // inheritable, or CreateProcess will not pass it on
                             DUPLICATE_SAME_ACCESS)) {
            childStderr.reset();
        }
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead.get();
    si.hStdOutput = childStdoutWrite.get();
    si.hStdError = childStderr.get();

    // CreateProcess mutates lpCommandLine, so it cannot be a string literal or a
    // c_str(). The path is quoted because plugin host paths routinely sit under
    // "Program Files".
    std::string commandLine = "\"" + m_executablePath + "\" --stdio";
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessA(m_executablePath.c_str(), mutableCommandLine.data(), nullptr, nullptr,
                                        TRUE, // inherit the pipe ends marked inheritable above
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!created) {
        return false;
    }

    CloseHandle(pi.hThread);
    // The child holds its own duplicates of the pipe ends now, so our copies of
    // those must go — the stdout write end especially, or the pipe never reports EOF
    // when the child dies. ScopedHandle closes them on the way out of this function;
    // only the two ends we keep talking on are released into members.
    m_process = pi.hProcess;
    m_stdinWrite = stdinWrite.release();
    m_stdoutRead = stdoutRead.release();
    return true;
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
#ifdef _WIN32
    // Ask first, then insist. Closing stdin is itself a shutdown signal: the child
    // sees EOF on its read loop even if it ignored EXIT.
    if (m_stdinWrite) {
        sendLine("EXIT");
        CloseHandle(m_stdinWrite);
        m_stdinWrite = nullptr;
    }
    if (m_stdoutRead) {
        CloseHandle(m_stdoutRead);
        m_stdoutRead = nullptr;
    }
    if (m_process) {
        // Same 100ms grace as POSIX before escalating. A wedged plugin must not be
        // able to hold shutdown open indefinitely, which is the whole point of
        // hosting it out of process.
        if (WaitForSingleObject(m_process, 100) != WAIT_OBJECT_0) {
            TerminateProcess(m_process, 1);
            // TerminateProcess only *requests* termination; it returns before the
            // process object is signalled. That normally completes in microseconds,
            // but a child blocked in an uninterruptible kernel call — a wedged
            // driver, a hung network-share read reached from inside a plugin DLL —
            // can stay unsignalled indefinitely, and untrusted third-party plugin
            // code is exactly the population that produces those states. stop()
            // runs from ~PluginHostProcess via shutdown(), so an unbounded wait
            // here would block project close and application exit: the failure mode
            // out-of-process hosting exists to prevent. Abandoning the handle leaks
            // it, which is the better trade — the process is unrecoverable anyway.
            constexpr DWORD kTerminateWaitMs = 2000;
            if (WaitForSingleObject(m_process, kTerminateWaitMs) != WAIT_OBJECT_0) {
                Log::warning("[PluginHost] helper process did not terminate; abandoning it to keep shutdown bounded");
            }
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }
#else
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
    if (!m_process) {
        return false;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_process, &exitCode)) {
        return false;
    }
    // STILL_ACTIVE is 259, which a process may also legitimately exit with. Confirm
    // against the process handle rather than trusting the code alone, or a plugin
    // that exits 259 would look alive forever and never be restarted.
    if (exitCode == STILL_ACTIVE) {
        return WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
    }
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
    if (!m_stdinWrite || m_framingLost || !isRunning()) {
        return false;
    }
    std::string framed = line;
    framed.push_back('\n');
    const char* data = framed.data();
    DWORD remaining = static_cast<DWORD>(framed.size());
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(m_stdinWrite, data, remaining, &written, nullptr) || written == 0) {
            // A write that stopped part-way leaves half a command in the pipe, and
            // the child would splice the next command onto its tail. Same
            // unrecoverable framing loss as a truncated response.
            if (remaining < framed.size()) {
                m_framingLost = true;
            }
            return false;
        }
        data += written;
        remaining -= written;
    }
    return true;
#else
    if (m_stdinFd < 0 || m_framingLost || !isRunning()) {
        return false;
    }
    std::string framed = line;
    framed.push_back('\n');
    const char* data = framed.data();
    size_t remaining = framed.size();
    while (remaining > 0) {
        const ssize_t written = write(m_stdinFd, data, remaining);
        if (written <= 0) {
            // See the Windows branch: a half-written command cannot be recovered
            // from, because the child splices the next one onto its tail.
            if (remaining < framed.size()) {
                m_framingLost = true;
            }
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
    if (!m_stdoutRead || m_framingLost) {
        return false;
    }
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        // Drained before touching the pipe: a bulk read can cross a response
        // boundary, so the newline this call needs may already be in hand.
        const size_t newline = m_readCarry.find('\n');
        if (newline != std::string::npos) {
            line.assign(m_readCarry, 0, newline);
            m_readCarry.erase(0, newline + 1);
            return true;
        }
        if (m_readCarry.size() > kMaxLineBytes) {
            m_framingLost = true;
            return false; // same runaway-line cap as the POSIX path
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        // Anonymous pipes cannot be opened for overlapped I/O, so there is no
        // WaitForSingleObject-able handle and no way to give ReadFile a timeout.
        // PeekNamedPipe (which does work on anonymous pipes) supplies the
        // "would block?" answer that O_NONBLOCK gives the POSIX path. Without the
        // peek, ReadFile would block past the caller's deadline and a hung child
        // would hang the caller with it.
        DWORD available = 0;
        if (!PeekNamedPipe(m_stdoutRead, nullptr, 0, nullptr, &available, nullptr)) {
            break; // broken pipe: child is gone
        }
        if (available == 0) {
            if (!isRunning()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Take everything the peek reported in one call. Reading a byte at a time
        // costs two pipe operations per byte, where the POSIX path pays one.
        char buffer[4096];
        const DWORD wanted = static_cast<DWORD>(std::min<size_t>(available, sizeof(buffer)));
        DWORD read = 0;
        if (!ReadFile(m_stdoutRead, buffer, wanted, &read, nullptr) || read == 0) {
            break;
        }
        m_readCarry.append(buffer, read);
    }

    // A clean timeout consumed nothing, so the channel stays usable. Bytes left
    // without a newline mean this response was abandoned part-way through: the
    // caller gives up and issues its next command, and the tail still sitting here
    // would come back as that command's reply. Framing is unrecoverable then.
    if (!m_readCarry.empty()) {
        m_framingLost = true;
    }
    return false;
#else
    if (m_stdoutFd < 0 || m_framingLost) {
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
            if (line.size() > kMaxLineBytes) {
                m_framingLost = true;
                return false;
            }
        } else {
            if (!isRunning()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // Bytes already taken from the pipe are gone and the rest of this response is
    // still queued — see the Windows branch for why that cannot be recovered from.
    if (!line.empty()) {
        m_framingLost = true;
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
    if (!sendCommand(command.str(), &response, std::chrono::seconds(30)) || response.find("OK") != 0) {
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
    m_pendingMidiData.assign(MidiBuffer::kMaxEvents * 8, 0);
    m_workerMidiData.assign(MidiBuffer::kMaxEvents * 8, 0);
    m_pendingMidiBytes.store(0, std::memory_order_release);
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
    if (midiOutput) {
        midiOutput->clear();
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
        size_t midiBytes = 0;
        if (midiInput) {
            const size_t maxEvents = m_pendingMidiData.size() / 8;
            const size_t eventCount = std::min(midiInput->getEventCount(), maxEvents);
            for (size_t i = 0; i < eventCount; ++i) {
                const auto& event = midiInput->getEvent(i);
                if (event.size != 3 || midiBytes + 8 > m_pendingMidiData.size()) {
                    continue;
                }
                const uint32_t sampleOffset = std::min<uint32_t>(event.sampleOffset, numFrames > 0 ? numFrames - 1 : 0);
                std::memcpy(m_pendingMidiData.data() + midiBytes, &sampleOffset, sizeof(sampleOffset));
                m_pendingMidiData[midiBytes + 4] = event.size;
                std::memcpy(m_pendingMidiData.data() + midiBytes + 5, event.data, 3);
                midiBytes += 8;
            }
        }
        m_pendingMidiBytes.store(midiBytes, std::memory_order_release);
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
    // Non-blocking by design: enqueue and return. No IPC, lock, or allocation
    // here. The worker thread forwards the change to the child in order (#238).
    // The queue is single-producer (see the header) — call from one thread. A
    // full queue drops the newest change and bumps a diagnostic counter
    // (parameterDropCount) rather than blocking; the first drop is logged so the
    // loss is observable instead of silent.
    if (!m_paramQueue.push(ParamChange{id, value})) {
        if (m_paramDrops.fetch_add(1, std::memory_order_relaxed) == 0) {
            Log::warning("[PluginHost] parameter queue full for " + m_info.name +
                         "; dropping changes (raise kParamQueueCapacity if this recurs)");
        }
    }
}

void OutOfProcessPluginInstance::drainParamQueueToChild() {
    // Worker-thread only. Each change is a single SETPARAM round-trip on the IPC
    // channel this thread owns. Bounded per pass (kMaxParamDrainPerPass) so a big
    // burst against a slow child cannot starve PROCESS; the remainder drains on
    // the next worker iteration, in order.
    ParamChange change;
    for (size_t drained = 0; drained < kMaxParamDrainPerPass && m_paramQueue.pop(change); ++drained) {
        std::ostringstream command;
        command << "SETPARAM " << change.id << " " << change.value;
        std::string response;
        if (!sendCommand(command.str(), &response) || response.find("OK") != 0) {
            // A dead/unresponsive child fails the whole instance safely; a plugin
            // that rejects one parameter (ERR) is logged but does not crash us.
            if (!m_process || !m_process->isRunning()) {
                markCrashed();
                return;
            }
            Log::warning("[PluginHost] SETPARAM rejected for " + m_info.name + " (id=" +
                         std::to_string(change.id) + ")");
        }
    }
}

std::string OutOfProcessPluginInstance::getParameterDisplay(uint32_t id) const {
    (void)id;
    return {};
}

std::vector<uint8_t> OutOfProcessPluginInstance::saveState() const {
    if (!m_process || !m_process->isRunning()) {
        return {};
    }
    std::string response;
    auto* self = const_cast<OutOfProcessPluginInstance*>(this);
    if (!self->sendCommand("SAVESTATE", &response, std::chrono::seconds(5))) {
        return {};
    }
    constexpr const char* kPrefix = "OK ";
    if (response.compare(0, 3, kPrefix) != 0) {
        return {};
    }
    std::vector<uint8_t> state;
    if (!hexDecodeRaw(response.substr(3), state)) {
        return {};
    }
    return state;
}

bool OutOfProcessPluginInstance::loadState(const std::vector<uint8_t>& state) {
    if (state.empty()) {
        return true;
    }
    if (!m_process || !m_process->isRunning()) {
        return false;
    }
    std::ostringstream command;
    command << "LOADSTATE " << hexEncodeBytes(state.data(), state.size());
    std::string response;
    return sendCommand(command.str(), &response, std::chrono::seconds(5)) && response.find("OK") == 0;
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

IPluginInstance::WatchdogStats OutOfProcessPluginInstance::getWatchdogStats() const {
    WatchdogStats stats;
    stats.maxExecutionTimeNs = static_cast<double>(m_watchdogMaxExecutionTimeNs.load(std::memory_order_relaxed));
    stats.avgExecutionTimeNs = static_cast<double>(m_watchdogAvgExecutionTimeNs.load(std::memory_order_relaxed));
    stats.violationCount = m_watchdogViolationCount.load(std::memory_order_relaxed);
    stats.isBypassed = m_watchdogBypassed.load(std::memory_order_acquire);
    return stats;
}

bool OutOfProcessPluginInstance::isBypassedByWatchdog() const {
    return m_watchdogBypassed.load(std::memory_order_acquire);
}

void OutOfProcessPluginInstance::resetWatchdog() {
    m_watchdogMaxExecutionTimeNs.store(0, std::memory_order_release);
    m_watchdogAvgExecutionTimeNs.store(0, std::memory_order_release);
    m_watchdogViolationCount.store(0, std::memory_order_release);
    m_watchdogBypassed.store(false, std::memory_order_release);
    if (m_process && m_process->isRunning()) {
        m_crashed.store(false, std::memory_order_release);
    }
}

bool OutOfProcessPluginInstance::sendCommand(const std::string& command, std::string* response,
                                             std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(m_ipcMutex);
    if (!m_process || !m_process->isRunning()) {
        return false;
    }
    // A truncated command or response desynchronizes every later request/response
    // pair on this channel, so it cannot be reused: the next reply read would be
    // the tail of the abandoned one. Failing the instance here is what keeps that
    // contained — saveState() and loadState() would otherwise hand their callers a
    // plausible-looking failure with no sign that the transport itself is broken,
    // and drainParamQueueToChild() would keep issuing commands into the desync
    // because the child is still alive. Checked at this choke point so every
    // caller, present and future, inherits it.
    if (!m_process->sendLine(command)) {
        if (m_process->framingLost()) {
            markCrashed();
        }
        return false;
    }
    std::string localResponse;
    if (!m_process->readLine(localResponse, timeout)) {
        if (m_process->framingLost()) {
            markCrashed();
        }
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

        // Forward any queued parameter changes to the child first, so a change
        // requested before this block takes effect for it. Runs every iteration
        // (even with no audio pending) so knob moves apply while transport is
        // stopped too (#238).
        drainParamQueueToChild();
        if (m_crashed.load(std::memory_order_acquire)) {
            continue;
        }

        uint8_t pendingReady = 2;
        if (!m_pendingState.compare_exchange_strong(pendingReady, 3, std::memory_order_acq_rel)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const uint32_t frames = m_pendingFrames.load(std::memory_order_acquire);
        const size_t midiBytes = m_pendingMidiBytes.load(std::memory_order_acquire);
        const size_t sampleCount = static_cast<size_t>(frames) * m_transportChannels;
        if (sampleCount <= m_workerInput.size()) {
            std::copy_n(m_pendingInput.data(), sampleCount, m_workerInput.data());
        }
        if (midiBytes <= m_workerMidiData.size()) {
            std::copy_n(m_pendingMidiData.data(), midiBytes, m_workerMidiData.data());
        }
        m_pendingState.store(0, std::memory_order_release);

        if (sampleCount > m_workerInput.size() || midiBytes > m_workerMidiData.size() ||
            !processBlockInHelper(m_workerInput, m_transportChannels, frames, m_workerMidiData, midiBytes,
                                  m_workerOutput)) {
            markCrashed();
            continue;
        }

        uint8_t readyEmpty = 0;
        if (m_readyState.compare_exchange_strong(readyEmpty, 1, std::memory_order_acq_rel)) {
            const size_t outputSamples = std::min(sampleCount, m_workerOutput.size());
            std::copy_n(m_workerOutput.data(), outputSamples, m_readyOutput.data());
            m_readyFrames.store(frames, std::memory_order_release);
            m_readyState.store(2, std::memory_order_release);
            // Published last, so an observer that sees this advance is guaranteed
            // to find the result readable rather than half-written.
            m_processedBlocks.fetch_add(1, std::memory_order_acq_rel);
        }
    }
}

bool OutOfProcessPluginInstance::processBlockInHelper(const std::vector<float>& input, uint32_t channels,
                                                      uint32_t frames, const std::vector<uint8_t>& midiData,
                                                      size_t midiBytes, std::vector<float>& output) {
    const size_t sampleCount = static_cast<size_t>(channels) * frames;
    if (sampleCount > input.size()) {
        return false;
    }
    std::ostringstream command;
    if (midiBytes > 0) {
        command << "PROCESSMIDI " << channels << " " << frames << " "
                << hexEncodeBytes(input.data(), sampleCount * sizeof(float)) << " "
                << hexEncodeBytes(midiData.data(), midiBytes);
    } else {
        command << "PROCESS " << channels << " " << frames << " "
                << hexEncodeBytes(input.data(), sampleCount * sizeof(float));
    }
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
    m_watchdogBypassed.store(true, std::memory_order_release);
    m_watchdogViolationCount.fetch_add(1, std::memory_order_acq_rel);
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
