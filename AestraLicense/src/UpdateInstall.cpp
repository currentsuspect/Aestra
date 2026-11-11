// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateInstall.h"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace Aestra {
namespace License {

namespace {

constexpr uint64_t kMaxPackageFiles = 20000;
constexpr uint64_t kMaxStagedBytes = 4ull * 1024 * 1024 * 1024;

std::string lowerHex(const unsigned char* bytes, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0F]);
    }
    return out;
}

uint64_t sizeOrZero(const fs::path& p) {
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

bool safeFileName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name[0] == '.' || name.find("..") != std::string::npos) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '-'; });
}

DownloadOutcome fail(DownloadStatus status, std::string detail) {
    DownloadOutcome o;
    o.status = status;
    o.detail = std::move(detail);
    return o;
}

// Run a program with arguments (no shell) and wait. False if it could not be started.
bool runProgram(const std::vector<std::string>& args, int* exitCode) {
#ifdef _WIN32
    std::string line;
    for (const auto& a : args) line += (line.empty() ? "\"" : " \"") + a + "\"";
    std::wstring wide(line.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
    if (n <= 0) return false;
    wide.resize(static_cast<size_t>(n));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, wide.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *exitCode = static_cast<int>(code);
    return true;
#else
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    // #1267: no PATH lookup and none of the app's environment. args[0] is an absolute, root-owned program
    // (tarProgram), and it runs with only what it needs: GNU tar reads TAR_OPTIONS on every run, and a hostile one
    // (--to-command, -I) would execute code while unpacking a signed update.
    std::vector<std::string> envStore = {"PATH=/usr/bin:/bin", "LC_ALL=C"};
    if (const char* tmp = std::getenv("TMPDIR")) envStore.push_back(std::string("TMPDIR=") + tmp);
    std::vector<char*> envp;
    for (auto& e : envStore) envp.push_back(e.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), envp.data()) != 0) return false;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return true;
#endif
}

std::string tarProgram() {
#ifdef _WIN32
    // The tar that ships with Windows 10 and later (bsdtar) reads .zip as well as .tar.gz.
    char root[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("SystemRoot", root, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::string(root) + "\\System32\\tar.exe" : std::string("tar.exe");
#else
    // #1267: an absolute tar that only root can change, never whichever "tar" comes first on PATH.
    for (const char* candidate : {"/usr/bin/tar", "/bin/tar"}) {
        struct stat st {};
        if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 && (st.st_mode & S_IXUSR) != 0 &&
            (st.st_mode & (S_IWGRP | S_IWOTH)) == 0)
            return candidate;
    }
    return {};
#endif
}

} // namespace

std::string sha256FileHex(const std::string& path, std::string* error) {
    if (sodium_init() < 0) {
        if (error) *error = "the crypto library did not start";
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return {};
    }
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto got = in.gcount();
        if (got > 0) crypto_hash_sha256_update(&state, reinterpret_cast<const unsigned char*>(buf.data()), static_cast<unsigned long long>(got));
    }
    unsigned char out[crypto_hash_sha256_BYTES];
    crypto_hash_sha256_final(&state, out);
    return lowerHex(out, sizeof out);
}

std::string updatePackageFileName(const UpdateManifest& manifest) {
    const size_t slash = manifest.url.find_last_of('/');
    std::string name = slash == std::string::npos ? std::string() : manifest.url.substr(slash + 1);
    const size_t q = name.find_first_of("?#");
    if (q != std::string::npos) name.resize(q);
    return safeFileName(name) ? name : std::string();
}

DownloadOutcome downloadUpdatePackage(IHttpTransport& transport, const UpdateConfig& config, const UpdateManifest& manifest,
                                      const std::string& directory, const DownloadProgress& progress) {
    // Nothing is asked of the network until the request is one we would send: the founder key only to the update host,
    // only over https, and only for the early channel.
    const std::string host = httpsHostOf(config.baseUrl);
    if (host.empty() || httpsHostOf(manifest.url) != host) return fail(DownloadStatus::BadConfig, "the download is not on the update host");
    if (config.channel == "early" && config.founderKey.empty()) return fail(DownloadStatus::BadConfig, "the early channel needs a founder key");
    const std::string name = updatePackageFileName(manifest);
    if (name.empty()) return fail(DownloadStatus::BadConfig, "the package name is not safe");
    if (manifest.size == 0 || manifest.sha256.size() != 64) return fail(DownloadStatus::BadConfig, "the feed gave no size or hash");

    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) return fail(DownloadStatus::DiskError, "cannot create " + directory);
    const fs::path finalPath = fs::path(directory) / name;
    const fs::path partPath = fs::path(directory) / (name + ".part");

    // Already here and right? Then there is nothing to fetch.
    if (sizeOrZero(finalPath) == manifest.size && sha256FileHex(finalPath.string()) == manifest.sha256) {
        DownloadOutcome ok;
        ok.status = DownloadStatus::Ok;
        ok.path = finalPath.string();
        return ok;
    }
    fs::remove(finalPath, ec); // a stale or damaged earlier copy

    // Room for the package, and for the folder it unpacks into.
    const auto space = fs::space(directory, ec);
    if (!ec && space.available < manifest.size * 2) return fail(DownloadStatus::DiskError, "not enough free disk space for the update");

    DownloadRequest request;
    request.url = manifest.url;
    request.destPath = partPath.string();
    request.headers["Accept"] = "application/octet-stream";
    if (config.channel == "early") request.headers["Authorization"] = "Bearer " + config.founderKey;

    for (int attempt = 0; attempt < 2; ++attempt) {
        uint64_t resume = sizeOrZero(partPath);
        if (resume >= manifest.size) { // a part that is already full-size (or bigger) is not a start to build on
            fs::remove(partPath, ec);
            resume = 0;
        }
        request.resumeFrom = resume;
        const DownloadResult got = transport.download(request, progress);

        if (got.cancelled) return fail(DownloadStatus::Cancelled, "download cancelled");
        if (got.status == 401 || got.status == 403) return fail(DownloadStatus::Unauthorized, "the update host refused the key");
        if (got.status == 416 && resume > 0) { // the range was beyond what the server has: start over once
            fs::remove(partPath, ec);
            continue;
        }
        if (got.status != 200 && got.status != 206) {
            return fail(DownloadStatus::NetworkError, got.status == 0 ? "could not reach the update host: " + got.error
                                                                      : "update host answered HTTP " + std::to_string(got.status));
        }
        if (!got.error.empty() && sizeOrZero(partPath) < manifest.size) return fail(DownloadStatus::NetworkError, got.error);
        break;
    }

    const uint64_t size = sizeOrZero(partPath);
    if (size < manifest.size) return fail(DownloadStatus::NetworkError, "the download stopped early; it will resume");
    if (size > manifest.size) {
        fs::remove(partPath, ec);
        return fail(DownloadStatus::SizeMismatch, "the download is not the size the update promised");
    }
    std::string hashError;
    if (sha256FileHex(partPath.string(), &hashError) != manifest.sha256) {
        fs::remove(partPath, ec);
        return fail(DownloadStatus::HashMismatch, hashError.empty() ? "the download is not the file the update promised" : hashError);
    }
    fs::rename(partPath, finalPath, ec);
    if (ec) return fail(DownloadStatus::DiskError, "cannot finish the download file");

    DownloadOutcome ok;
    ok.status = DownloadStatus::Ok;
    ok.path = finalPath.string();
    return ok;
}

bool extractUpdatePackage(const std::string& packagePath, const std::string& stagingDir, const std::string& executableName,
                          std::string* error) {
    auto bad = [&](const std::string& why) {
        std::error_code ec;
        fs::remove_all(stagingDir, ec); // never leave half a version behind
        if (error) *error = why;
        return false;
    };
    std::error_code ec;
    fs::remove_all(stagingDir, ec);
    fs::create_directories(stagingDir, ec);
    if (ec) return bad("cannot create the staging folder");

    int code = 0;
    const std::string tar = tarProgram();
    if (tar.empty()) return bad("no trusted tar (/usr/bin/tar or /bin/tar, owned by root) to unpack the update");
    if (!runProgram({tar, "-xf", packagePath, "-C", stagingDir}, &code)) return bad("cannot start tar to unpack the update");
    if (code != 0) return bad("the update package could not be unpacked");

    // Nothing may have landed outside the folder, and nothing may be a link: a link in a package can point anywhere.
    const fs::path root = fs::weakly_canonical(stagingDir, ec);
    uint64_t files = 0, bytes = 0;
    for (auto it = fs::recursive_directory_iterator(stagingDir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        if (it->is_symlink(ec)) return bad("the update package contains a link, which is not allowed");
        const fs::path canonical = fs::weakly_canonical(p, ec);
        if (canonical.string().compare(0, root.string().size(), root.string()) != 0) return bad("the update package reaches outside its folder");
        if (it->is_regular_file(ec)) {
            ++files;
            bytes += sizeOrZero(p);
        }
        if (files > kMaxPackageFiles || bytes > kMaxStagedBytes) return bad("the update package is larger than any real build");
    }

    // A package wrapped in one folder (Aestra-0.8.2-linux-x64/...) is the same package: lift its contents up.
    {
        std::vector<fs::path> top;
        for (const auto& e : fs::directory_iterator(stagingDir, ec)) top.push_back(e.path());
        if (top.size() == 1 && fs::is_directory(top[0], ec)) {
            const fs::path wrapper = top[0];
            const fs::path hold = fs::path(stagingDir).parent_path() / (fs::path(stagingDir).filename().string() + ".lift");
            fs::remove_all(hold, ec);
            fs::rename(wrapper, hold, ec);
            if (ec) return bad("cannot unwrap the update package");
            fs::remove_all(stagingDir, ec);
            fs::rename(hold, stagingDir, ec);
            if (ec) return bad("cannot unwrap the update package");
        }
    }

    if (!fs::is_regular_file(fs::path(stagingDir) / executableName, ec)) return bad("the update package has no " + executableName);
    return true;
}

long currentProcessId() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

std::string currentExecutablePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
    return fs::path(std::wstring(buf, n)).string();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    return fs::weakly_canonical(buf, ec).string();
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
#endif
}

bool installLocationWritable(const std::string& appDir, std::string* why) {
    auto canWrite = [](const fs::path& dir) {
        std::error_code ec;
        const fs::path probe = dir / ".aestra-write-test";
        {
            std::ofstream out(probe);
            if (!out) return false;
        }
        fs::remove(probe, ec);
        return true;
    };
    const fs::path dir(appDir);
    if (!canWrite(dir) || !canWrite(dir.parent_path())) {
        if (why) *why = "Aestra is installed somewhere that needs administrator rights to change (" + appDir + "). Move it to a folder you own, or install the update yourself.";
        return false;
    }
    return true;
}

bool launchDetached(const std::string& program, const std::vector<std::string>& args, std::string* error) {
#ifdef _WIN32
    std::string line = "\"" + program + "\"";
    for (const auto& a : args) line += " \"" + a + "\"";
    std::wstring wide(line.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
    if (n <= 0) {
        if (error) *error = "bad program path";
        return false;
    }
    wide.resize(static_cast<size_t>(n));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, wide.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (error) *error = "could not start " + program;
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        if (error) *error = "could not start " + program;
        return false;
    }
    if (pid == 0) {
        setsid(); // its own session: closing this one does not take it down
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
        for (int fd = 3; fd < 256; ++fd) close(fd); // nothing of ours leaks into it
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    return true;
#endif
}

bool launchUpdater(const UpdateLayout& layout, const std::string& resultFile, const std::string& fromVersion,
                   const std::string& toVersion, long pid, std::string* error) {
    std::error_code ec;
    const fs::path source = fs::path(layout.appDir) / layout.updaterName;
    if (!fs::is_regular_file(source, ec)) {
        if (error) *error = "this install has no " + layout.updaterName;
        return false;
    }
    const fs::path home = fs::temp_directory_path(ec) / ("aestra-updater-" + std::to_string(pid));
    fs::remove_all(home, ec);
    fs::create_directories(home, ec);
    const fs::path copy = home / layout.updaterName;
    fs::copy_file(source, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) *error = "could not copy the updater: " + ec.message();
        return false;
    }
#ifndef _WIN32
    fs::permissions(copy, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    return launchDetached(copy.string(),
                          {"--pid", std::to_string(pid), "--app-dir", layout.appDir, "--staged", layout.stagedDir(), "--backup",
                           layout.backupDir(), "--exe", layout.executableName, "--result", resultFile, "--from", fromVersion, "--to", toVersion},
                          error);
}

} // namespace License
} // namespace Aestra
