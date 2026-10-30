// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/OutOfProcessPluginInstance.h"

#include "AestraLog.h"
#include "SharedAudioBlock.h"

#include <cstdio>
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
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#elif !defined(_WIN32)
#include <cerrno>
#include <ctime>
#include <semaphore.h>
#endif

namespace Aestra {
namespace Audio {

// Wakes the worker the moment process() hands it a block, instead of the worker finding it on its next 1 ms nap
// (V82-H7 follow-up). post() is one atomic and, only when the worker sleeps, one wake syscall: safe from the render thread.
class WorkerWake {
public:
    WorkerWake() {
#ifdef _WIN32
        m_sem = CreateSemaphoreA(nullptr, 0, 1, nullptr);
#elif defined(__APPLE__)
        m_sem = dispatch_semaphore_create(0);
#else
        m_ok = sem_init(&m_sem, 0, 0) == 0;
#endif
    }
    ~WorkerWake() {
#ifdef _WIN32
        if (m_sem)
            CloseHandle(m_sem);
#elif defined(__APPLE__)
        if (m_sem)
            dispatch_release(m_sem);
#else
        if (m_ok)
            sem_destroy(&m_sem);
#endif
    }
    WorkerWake(const WorkerWake&) = delete;
    WorkerWake& operator=(const WorkerWake&) = delete;

    void post() noexcept {
#ifdef _WIN32
        if (m_sem)
            ReleaseSemaphore(m_sem, 1, nullptr); // at most one pending wake: a full count just fails
#elif defined(__APPLE__)
        if (m_sem)
            dispatch_semaphore_signal(m_sem);
#else
        if (m_ok)
            sem_post(&m_sem);
#endif
    }
    /// Sleeps until post() or the timeout. Spurious wakes are fine: the worker re-checks its state.
    void wait(std::chrono::microseconds timeout) noexcept {
#ifdef _WIN32
        if (m_sem) {
            WaitForSingleObject(m_sem, static_cast<DWORD>(std::max<long long>(1, timeout.count() / 1000)));
            return;
        }
#elif defined(__APPLE__)
        if (m_sem) {
            dispatch_semaphore_wait(m_sem, dispatch_time(DISPATCH_TIME_NOW, timeout.count() * 1000));
            return;
        }
#else
        if (m_ok) {
            timespec ts{};
            clock_gettime(CLOCK_REALTIME, &ts);
            const long long ns = ts.tv_nsec + static_cast<long long>(timeout.count()) * 1000;
            ts.tv_sec += static_cast<time_t>(ns / 1000000000);
            ts.tv_nsec = static_cast<long>(ns % 1000000000);
            while (sem_timedwait(&m_sem, &ts) != 0 && errno == EINTR) {
            }
            return;
        }
#endif
        std::this_thread::sleep_for(timeout);
    }

private:
#ifdef _WIN32
    HANDLE m_sem = nullptr;
#elif defined(__APPLE__)
    dispatch_semaphore_t m_sem = nullptr;
#else
    sem_t m_sem{};
    bool m_ok = false;
#endif
};

namespace {

#ifndef _WIN32
// Contain the fork child before exec (#710): descriptors the parent holds
// (project files, sockets, devices) are inherited across fork by default and
// would be reachable from untrusted plugin code. Only 0/1/2 survive.
//
// Only async-signal-safe work is allowed here — fork in a multithreaded
// process may hold malloc's lock. close_range is one syscall; the fallback is
// close() in a bounded loop. opendir/readdir would allocate and can deadlock,
// so the obvious /proc/self/fd iteration is out.
void closeUnrelatedDescriptors() noexcept {
// Doubly nested, not &&: invoking the function-like __GLIBC_PREREQ while it
// is undefined is a hard error on Clang, and short-circuit does not save it —
// expansion runs before evaluation. Each level keeps undefined macros out of
// reach: macOS skips at __linux__, musl (no __GLIBC__) takes the loop below.
#if defined(__linux__)
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
    if (::close_range(3, ~0u, 0) == 0) {
        return;
    }
#endif
#endif
#endif
    struct rlimit rl{};
    rlim_t lim = 1024;
    if (::getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        lim = rl.rlim_cur;
    }
    for (rlim_t fd = 3; fd < lim; ++fd) {
        ::close(static_cast<int>(fd));
    }
}
#endif

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

// The frame terminator is '\n' alone, so a '\r' before it is never payload.
//
// The helper now sets its stdout to binary mode, but this stays as the transport's
// own guarantee: without it, a child whose stdout is in Windows text mode silently
// appends '\r' to every reply. Prefix checks like `response.find("OK") == 0` cannot
// see that, while PROCESS and SAVESTATE hex-decode the remainder of the line and
// reject it for having an odd character count — so audio and plugin state break
// while load and activate look perfectly healthy.
void stripTrailingCR(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
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
    ScopedHandle m_process;     // child process
    ScopedHandle m_stdinWrite;  // our end of the child's stdin
    ScopedHandle m_stdoutRead;  // our end of the child's stdout
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

    // bInheritHandles=TRUE below is all-or-nothing: it hands the child EVERY
    // inheritable handle Aestra currently holds, not just the three std handles
    // named here. For a process whose entire purpose is to contain untrusted
    // third-party plugin code, that is a containment hole — an inheritable file or
    // socket handle opened anywhere else in the application (a project being
    // written, an audio device, a network connection) becomes reachable from inside
    // a plugin, and which handles happen to be inheritable is not something this
    // call can see. PROC_THREAD_ATTRIBUTE_HANDLE_LIST restricts inheritance to
    // exactly the handles listed, so the child gets its pipes and nothing else.
    //
    // Every handle in the list must itself be inheritable or CreateProcess fails
    // with ERROR_INVALID_PARAMETER, which is why childStderr is only included when
    // the duplication above actually produced one.
    HANDLE inheritList[3] = {childStdinRead.get(), childStdoutWrite.get(), nullptr};
    DWORD inheritCount = 2;
    if (childStderr) {
        inheritList[inheritCount++] = childStderr.get();
    }

    SIZE_T attributeSize = 0;
    // Deliberately unchecked: this call is documented to fail with
    // ERROR_INSUFFICIENT_BUFFER while returning the size, which is the only reason
    // to make it.
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    if (attributeSize == 0) {
        return false;
    }
    // Element type is a pointer, not char: the list stores pointers internally, and a
    // char buffer only guarantees char alignment. That is latent UB everywhere and an
    // actual fault on strict-alignment targets such as Windows on ARM64.
    std::vector<void*> attributeBuffer((attributeSize + sizeof(void*) - 1) / sizeof(void*));
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeSize)) {
        return false;
    }
    if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList,
                                   inheritCount * sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributeList);
        return false;
    }

    STARTUPINFOEXA si{};
    // Must be sizeof(STARTUPINFOEXA), not sizeof(STARTUPINFOA): the extended struct
    // is how CreateProcess knows lpAttributeList is present and readable.
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = childStdinRead.get();
    si.StartupInfo.hStdOutput = childStdoutWrite.get();
    si.StartupInfo.hStdError = childStderr.get();
    si.lpAttributeList = attributeList;

    // CreateProcess mutates lpCommandLine, so it cannot be a string literal or a
    // c_str(). The path is quoted because plugin host paths routinely sit under
    // "Program Files".
    std::string commandLine = "\"" + m_executablePath + "\" --stdio";
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL created =
        CreateProcessA(m_executablePath.c_str(), mutableCommandLine.data(), nullptr, nullptr,
                       TRUE, // narrowed to inheritList by the attribute list above
                       CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
    // The list is only consulted during process creation, so it can go immediately —
    // and must, on both outcomes.
    DeleteProcThreadAttributeList(attributeList);
    if (!created) {
        return false;
    }

    CloseHandle(pi.hThread);
    // The child holds its own duplicates of the pipe ends now, so our copies of
    // those must go — the stdout write end especially, or the pipe never reports EOF
    // when the child dies. ScopedHandle closes them on the way out of this function;
    // only the two ends we keep talking on are moved into members.
    m_process.reset(pi.hProcess);
    m_stdinWrite.reset(stdinWrite.release());
    m_stdoutRead.reset(stdoutRead.release());
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
        closeUnrelatedDescriptors();
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
        m_stdinWrite.reset(); // closing stdin is itself the second signal
    }
    m_stdoutRead.reset();
    if (m_process) {
        // Same 100ms grace as POSIX before escalating. A wedged plugin must not be
        // able to hold shutdown open indefinitely, which is the whole point of
        // hosting it out of process.
        if (WaitForSingleObject(m_process.get(), 100) != WAIT_OBJECT_0) {
            TerminateProcess(m_process.get(), 1);
            // TerminateProcess only *requests* termination; it returns before the
            // process object is signalled. That normally completes in microseconds,
            // but a child blocked in an uninterruptible kernel call — a wedged
            // driver, a hung network-share read reached from inside a plugin DLL —
            // can stay unsignalled indefinitely, and untrusted third-party plugin
            // code is exactly the population that produces those states. stop()
            // runs from ~PluginHostProcess via shutdown(), so an unbounded wait
            // here would block project close and application exit: the failure mode
            // out-of-process hosting exists to prevent. We stop waiting and close our
            // handle instead; the child stays in whatever state it is stuck in, which
            // costs nothing we can recover anyway.
            constexpr DWORD kTerminateWaitMs = 2000;
            if (WaitForSingleObject(m_process.get(), kTerminateWaitMs) != WAIT_OBJECT_0) {
                Log::warning("[PluginHost] helper process did not terminate; abandoning it to keep shutdown bounded");
            }
        }
        m_process.reset();
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
    if (!GetExitCodeProcess(m_process.get(), &exitCode)) {
        return false;
    }
    // STILL_ACTIVE is 259, which a process may also legitimately exit with. Confirm
    // against the process handle rather than trusting the code alone, or a plugin
    // that exits 259 would look alive forever and never be restarted.
    if (exitCode == STILL_ACTIVE) {
        return WaitForSingleObject(m_process.get(), 0) == WAIT_TIMEOUT;
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
        if (!WriteFile(m_stdinWrite.get(), data, remaining, &written, nullptr) || written == 0) {
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
    const auto waitStart = std::chrono::steady_clock::now();
    const auto deadline = waitStart + timeout;
    for (;;) {
        // Drained before touching the pipe: a bulk read can cross a response
        // boundary, so the newline this call needs may already be in hand.
        const size_t newline = m_readCarry.find('\n');
        if (newline != std::string::npos) {
            line.assign(m_readCarry, 0, newline);
            m_readCarry.erase(0, newline + 1);
            stripTrailingCR(line);
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
        if (!PeekNamedPipe(m_stdoutRead.get(), nullptr, 0, nullptr, &available, nullptr)) {
            break; // broken pipe: child is gone
        }
        if (available == 0) {
            if (!isRunning()) {
                break;
            }
            // A block's "OK" usually lands within a couple of milliseconds, and a Windows sleep can last a whole
            // 15.6 ms tick, longer than the block. So yield for the first 3 ms, then nap (V82-H7 follow-up).
            if (std::chrono::steady_clock::now() < waitStart + std::chrono::milliseconds(3)) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            continue;
        }

        // Take everything the peek reported in one call. Reading a byte at a time
        // costs two pipe operations per byte, where the POSIX path pays one.
        char buffer[4096];
        const DWORD wanted = static_cast<DWORD>(std::min<size_t>(available, sizeof(buffer)));
        DWORD read = 0;
        if (!ReadFile(m_stdoutRead.get(), buffer, wanted, &read, nullptr) || read == 0) {
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
                stripTrailingCR(line);
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
            // Sleep until the reply's next byte is there rather than a fixed 1 ms: a block's "OK" back from the
            // helper used to wait out the rest of a nap every time (V82-H7 follow-up). Capped so a dead child is still noticed.
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            pollfd pfd{m_stdoutFd, POLLIN, 0};
            ::poll(&pfd, 1, static_cast<int>(std::clamp<long long>(left.count() + 1, 1, 50)));
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
    // Room for a chunk in flight plus two callbacks either side (see process()).
    m_inFifo.reset(static_cast<size_t>(std::max<uint32_t>(1, maxBlockSize)) * 3, m_transportChannels);
    m_outFifo.reset(static_cast<size_t>(std::max<uint32_t>(1, maxBlockSize)) * 3, m_transportChannels);
    m_midiQueue.clear();
    m_inFifoEnd = 0;
    m_chunkFrames.store(0, std::memory_order_release);

    std::ostringstream command;
    command << "INIT " << sampleRate << " " << maxBlockSize;
    std::string response;
    if (!sendCommand(command.str(), &response) || response.find("OK") != 0) {
        markCrashed();
        return false;
    }
    refreshPluginLatency(); // VST3 and CLAP both know it once set up; activation can change it, so activate() asks again
    setupSharedBlock();
    startWorker();
    return true;
}

void OutOfProcessPluginInstance::setupSharedBlock() {
    m_shm.reset();
    // AESTRA_PLUGIN_SHM=0 keeps the hex transport: a way out if a system's shared memory misbehaves, and how the
    // tests still cover that path.
    if (const char* env = std::getenv("AESTRA_PLUGIN_SHM"); env && std::strcmp(env, "0") == 0) {
        return;
    }
    static std::atomic<unsigned long> counter{0};
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    const auto stamp = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = SharedAudioBlock::uniqueName(pid, counter.fetch_add(1), stamp);
    auto block = std::make_unique<SharedAudioBlock>();
    const size_t maxSamples = m_workerInput.size();
    const size_t maxMidiBytes = m_workerMidiData.size();
    if (!block->create(name, maxSamples, maxMidiBytes)) {
        Log::warning("[PluginHost] No shared audio block; " + m_info.name + " runs on the slower hex transport");
        return;
    }
    std::ostringstream command;
    command << "SHM " << hexEncode(name) << " " << maxSamples << " " << maxMidiBytes;
    std::string response;
    const bool mapped = sendCommand(command.str(), &response) && response == "OK SHM";
    block->unlinkName(); // mapped or not, the name has done its job
    if (!mapped) {
        Log::warning("[PluginHost] Helper could not map the shared audio block; " + m_info.name +
                     " runs on the slower hex transport");
        return;
    }
    m_shm = std::move(block);
}

void OutOfProcessPluginInstance::refreshPluginLatency() {
    std::string response;
    uint32_t latency = 0;
    if (sendCommand("LATENCY", &response) && response.rfind("OK LATENCY ", 0) == 0) {
        try {
            // A hostile or broken helper must not be able to claim an absurd delay: more than ten seconds at 192 kHz
            // is not a latency, it is garbage, and PDC would delay every other track by it.
            const unsigned long parsed = std::stoul(response.substr(11));
            latency = static_cast<uint32_t>(std::min<unsigned long>(parsed, 1920000UL));
        } catch (const std::exception&) {
            latency = 0;
        }
    }
    m_pluginLatency.store(latency, std::memory_order_release);
}

void OutOfProcessPluginInstance::shutdown() {
    m_active.store(false, std::memory_order_release);
    m_editorOpen.store(false, std::memory_order_release); // the helper closes its window with it
    stopWorker();
    if (m_process) {
        sendCommand("SHUTDOWN", nullptr);
        m_process.reset();
    }
    m_shm.reset();
    m_loaded.store(false, std::memory_order_release);
}

void OutOfProcessPluginInstance::activate() {
    std::string response;
    if (sendCommand("ACTIVATE", &response) && response.find("OK") == 0) {
        m_active.store(true, std::memory_order_release);
        refreshPluginLatency();
    } else {
        markCrashed();
    }
}

void OutOfProcessPluginInstance::deactivate() {
    sendCommand("DEACTIVATE", nullptr);
    m_active.store(false, std::memory_order_release);
}

// process() — the audio-thread side of the hand-over — lives in OutOfProcessPluginInstanceRt.cpp.
void OutOfProcessPluginInstance::wakeWorker() noexcept {
    if (m_wake) {
        m_wake->post();
    }
}

namespace {
bool hexToText(const std::string& hex, std::string& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return true;
}
} // namespace

std::vector<PluginParameter> OutOfProcessPluginInstance::getParameters() const {
    std::lock_guard<std::mutex> lock(m_paramListMutex);
    if (m_paramListValid) return m_paramList;
    if (!m_loaded.load(std::memory_order_acquire) || m_crashed.load(std::memory_order_acquire)) return {};

    std::string response;
    auto* self = const_cast<OutOfProcessPluginInstance*>(this);
    if (!self->sendCommand("PARAMS", &response, std::chrono::seconds(10)) || response.rfind("OK ", 0) != 0) return {};
    std::istringstream in(response.substr(3));
    size_t count = 0;
    in >> count;
    std::vector<PluginParameter> list;
    list.reserve(std::min<size_t>(count, 100000));
    std::string token;
    while (in >> token && list.size() < 100000) {
        // id:flags:steps:default:name:short:unit
        std::vector<std::string> field;
        size_t start = 0;
        for (size_t at; (at = token.find(':', start)) != std::string::npos; start = at + 1)
            field.push_back(token.substr(start, at - start));
        field.push_back(token.substr(start));
        if (field.size() != 7) continue;
        PluginParameter p;
        try {
            p.id = static_cast<uint32_t>(std::stoul(field[0]));
            const int flags = std::stoi(field[1]);
            p.isAutomatable = (flags & 1) != 0;
            p.isBypass = (flags & 2) != 0;
            p.isReadOnly = (flags & 4) != 0;
            p.stepCount = static_cast<uint32_t>(std::stoul(field[2]));
            p.defaultValue = std::stof(field[3]);
        } catch (const std::exception&) {
            continue;
        }
        if (!hexToText(field[4], p.name) || !hexToText(field[5], p.shortName) || !hexToText(field[6], p.unit)) continue;
        list.push_back(std::move(p));
    }
    m_paramList = std::move(list);
    m_paramListValid = true;
    return m_paramList;
}

float OutOfProcessPluginInstance::getParameter(uint32_t id) const {
    if (!m_loaded.load(std::memory_order_acquire) || m_crashed.load(std::memory_order_acquire)) return 0.0f;
    std::string response;
    auto* self = const_cast<OutOfProcessPluginInstance*>(this);
    if (!self->sendCommand("GETPARAM " + std::to_string(id), &response) || response.rfind("OK GETPARAM ", 0) != 0)
        return 0.0f;
    return std::strtof(response.c_str() + 12, nullptr);
}

bool OutOfProcessPluginInstance::supportsRealtimeAutomation() const noexcept {
    return m_info.format == PluginFormat::VST3 || m_info.format == PluginFormat::CLAP;
}

void OutOfProcessPluginInstance::applyAutomation(uint32_t id, float normalizedValue) noexcept {
    // Render thread: no lock, no allocation, no IPC. Unchanged values are not sent again.
    for (auto& r : m_recentAutomation)
        if (r.valid && r.id == id) {
            if (r.value == normalizedValue) return;
            r.value = normalizedValue;
            m_autoQueue.push(ParamChange{id, normalizedValue}); // a full ring loses a step the next block repeats
            return;
        }
    RecentAutomation& slot = m_recentAutomation[m_recentAutomationNext++ % m_recentAutomation.size()];
    slot = {id, normalizedValue, true};
    m_autoQueue.push(ParamChange{id, normalizedValue});
}

std::vector<OutOfProcessPluginInstance::ParameterEdit> OutOfProcessPluginInstance::takeEditedParameters() {
    std::vector<ParameterEdit> edits;
    if (!m_editorOpen.load(std::memory_order_acquire)) return edits; // edits only come from the window
    std::string response;
    if (!sendCommand("EDITS", &response) || response.rfind("OK EDITS", 0) != 0) return edits;
    std::istringstream in(response.substr(8));
    std::string token;
    while (in >> token) {
        const size_t colon = token.find(':');
        if (colon == std::string::npos) continue;
        ParameterEdit e;
        e.id = static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 10));
        e.value = std::strtof(token.c_str() + colon + 1, nullptr);
        edits.push_back(e);
    }
    return edits;
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
    for (size_t drained = 0;
         drained < kMaxParamDrainPerPass && (m_autoQueue.pop(change) || m_paramQueue.pop(change)); ++drained) {
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
    if (!m_loaded.load(std::memory_order_acquire) || m_crashed.load(std::memory_order_acquire)) return {};
    std::string response;
    auto* self = const_cast<OutOfProcessPluginInstance*>(this);
    if (!self->sendCommand("GETPARAM " + std::to_string(id), &response) || response.rfind("OK GETPARAM ", 0) != 0)
        return {};
    const size_t space = response.find(' ', 12);
    std::string text;
    if (space == std::string::npos || !hexToText(response.substr(space + 1), text)) return {};
    return text;
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

bool OutOfProcessPluginInstance::hasEditor() const {
#if defined(__linux__) || defined(_WIN32)
    return (m_info.format == PluginFormat::VST3 || m_info.format == PluginFormat::CLAP) &&
           m_loaded.load(std::memory_order_acquire) && !m_crashed.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool OutOfProcessPluginInstance::openEditor(void* parentWindow) {
    (void)parentWindow; // the helper makes the window; there is nothing of the app's to embed into
    if (!hasEditor()) return false;
    std::string response;
    if (!sendCommand("EDITOR OPEN " + hexEncode(m_info.name), &response, std::chrono::seconds(15))) return false;
    int width = 0, height = 0;
    if (std::sscanf(response.c_str(), "OK EDITOR %d %d", &width, &height) != 2) return false;
    m_editorWidth.store(width, std::memory_order_relaxed);
    m_editorHeight.store(height, std::memory_order_relaxed);
    m_editorOpen.store(true, std::memory_order_release);
    return true;
}

void OutOfProcessPluginInstance::closeEditor() {
    if (!m_editorOpen.exchange(false, std::memory_order_acq_rel)) return;
    sendCommand("EDITOR CLOSE", nullptr, std::chrono::seconds(5));
}

std::pair<int, int> OutOfProcessPluginInstance::getEditorSize() const {
    return {m_editorWidth.load(std::memory_order_relaxed), m_editorHeight.load(std::memory_order_relaxed)};
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
    // A live child is not sufficient: a wedged-but-running child whose channel lost
    // framing satisfies isRunning(), and un-crashing there would take process() off
    // the passthrough path and onto a transport that cannot answer correctly. The
    // next sendCommand() would re-crash it, so this is a narrow window rather than a
    // lasting wrong state — but the invariant is "once framing is lost, stop
    // talking", and this function is a caller like any other.
    if (m_process && m_process->isRunning() && !m_process->framingLost()) {
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
    if (!m_wake) {
        m_wake = std::make_unique<WorkerWake>();
    }
    m_workerThread = std::thread([this] {
        raiseWorkerPriority();
        workerLoop();
    });
    m_workerRunning.store(true, std::memory_order_release);
}

void OutOfProcessPluginInstance::stopWorker() {
    m_workerStop.store(true, std::memory_order_release);
    if (m_wake) {
        m_wake->post();
    }
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
            // process() posts the moment it hands a block over; the timeout keeps knob moves flowing while stopped.
            m_wake->wait(std::chrono::milliseconds(2));
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
        m_workerTransport = m_pendingTransport;
        m_pendingState.store(0, std::memory_order_release);

        if (sampleCount > m_workerInput.size() || midiBytes > m_workerMidiData.size() ||
            !processBlockInHelper(m_workerInput, m_transportChannels, frames, m_workerMidiData, midiBytes,
                                  m_workerTransport, m_workerOutput)) {
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
            const uint64_t blocks = m_processedBlocks.fetch_add(1, std::memory_order_acq_rel) + 1;
            // A plugin may only know its own latency once it has processed a block (LSP's limiters report theirs
            // then), and it can change later: ask after the first block and every kLatencyCheckEveryBlocks after.
            if (blocks == 1 || blocks % kLatencyCheckEveryBlocks == 0) {
                refreshPluginLatency();
            }
        }
    }
}

void OutOfProcessPluginInstance::raiseWorkerPriority() {
    // The worker carries every block to the helper and back, so it runs just below the audio thread
    // (RtAudioDriver: SCHED_FIFO mid-range). Best effort: without rtprio it stays a normal thread.
#ifdef _WIN32
    MMCSS::setProAudio();
#elif defined(__linux__)
    sched_param param{};
    param.sched_priority = (sched_get_priority_max(SCHED_FIFO) + sched_get_priority_min(SCHED_FIFO)) / 2 - 1;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
}

bool OutOfProcessPluginInstance::processBlockInHelper(const std::vector<float>& input, uint32_t channels,
                                                      uint32_t frames, const std::vector<uint8_t>& midiData,
                                                      size_t midiBytes, const HostTransport& transport,
                                                      std::vector<float>& output) {
    const size_t sampleCount = static_cast<size_t>(channels) * frames;
    if (sampleCount > input.size()) {
        return false;
    }
    if (m_shm && sampleCount <= m_shm->maxSamples() && midiBytes <= m_shm->maxMidiBytes()) {
        // The samples go through the shared block; the pipe carries only the sizes and the "OK" (V82-H7 follow-up).
        std::copy_n(input.data(), sampleCount, m_shm->input());
        if (midiBytes > 0) {
            std::copy_n(midiData.data(), midiBytes, m_shm->midi());
        }
        const std::string command = "PROCESSSHM " + std::to_string(channels) + " " + std::to_string(frames) + " " +
                                    std::to_string(midiBytes) + transport.toWire();
        std::string response;
        if (!sendCommand(command, &response) || response != "OK") {
            return false;
        }
        if (output.size() < sampleCount) {
            output.resize(sampleCount);
        }
        std::copy_n(m_shm->output(), sampleCount, output.data());
        return true;
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
    command << transport.toWire();
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
