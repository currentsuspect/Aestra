// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Getting an update onto the disk: the download resumes, is accepted only when its size and SHA-256 match the signed feed,
// never sends the founder key anywhere but the update host, and the unpacked package is checked before anything could be
// installed from it. The fake host serves a fixed 100,000-byte package and can misbehave in the ways real ones do.

#include "UpdateInstall.h"
#include "UpdateSession.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::License;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "failed: " << what << "\n";
        ++g_failures;
    }
}

const char* kHash = "cd2df694e424bc7968cc37f47751019e5ca0cd1bdf2e479ea537c3a1c32ee1aa";
constexpr size_t kSize = 100000;

std::string payload() {
    std::string p(kSize, '\0');
    for (size_t i = 0; i < kSize; ++i) p[i] = static_cast<char>(i % 251);
    return p;
}

struct FakeHost : IHttpTransport {
    std::string body = payload();
    int status = 200;            // what to answer when serving (200/206 are chosen by honouring the range)
    int forceStatus = 0;         // answer this status with no body (401, 404, 416 ...)
    bool ignoreRange = false;    // always send the whole file with 200
    size_t cutAfter = 0;         // stop after this many bytes this call (an interrupted transfer); 0 = never
    size_t cancelAfter = 0;      // ask the progress callback to cancel after this many bytes
    std::vector<DownloadRequest> seen;
    HttpResponse send(const HttpRequest&) override { return {}; }
    DownloadResult download(const DownloadRequest& r, const DownloadProgress& progress) override {
        seen.push_back(r);
        DownloadResult result;
        if (forceStatus) {
            result.status = forceStatus;
            return result;
        }
        size_t from = ignoreRange ? 0 : static_cast<size_t>(r.resumeFrom);
        result.status = from > 0 ? 206 : 200;
        std::ofstream out(r.destPath, std::ios::binary | (from > 0 ? std::ios::app : std::ios::trunc));
        size_t sent = 0;
        while (from + sent < body.size()) {
            const size_t n = std::min<size_t>(10000, body.size() - from - sent);
            out.write(body.data() + from + sent, static_cast<std::streamsize>(n));
            sent += n;
            if (progress && !progress(from + sent, body.size())) {
                result.cancelled = true;
                break;
            }
            if (cancelAfter && sent >= cancelAfter) {
                if (progress) progress(from + sent, body.size());
                result.cancelled = true;
                break;
            }
            if (cutAfter && sent >= cutAfter) {
                result.error = "the connection dropped";
                break;
            }
        }
        out.close();
        std::error_code ec;
        result.bytesOnDisk = fs::exists(r.destPath, ec) ? fs::file_size(r.destPath, ec) : 0;
        return result;
    }
};

UpdateConfig config(const std::string& channel = "early") {
    UpdateConfig c;
    c.channel = channel;
    c.founderKey = "aef_testkey0123456789";
    c.currentVersion = "0.8.1";
    c.platform = "windows-x64";
    return c;
}

UpdateManifest manifest() {
    UpdateManifest m;
    m.channel = "early";
    m.platform = "windows-x64";
    m.version = "0.8.2";
    m.url = "https://update.aestra.studio/early/windows-x64/Aestra-0.8.2.zip";
    m.sha256 = kHash;
    m.size = kSize;
    return m;
}

struct Dir {
    fs::path path;
    Dir() {
        path = fs::temp_directory_path() / ("aestra-install-test-" + std::to_string(std::rand()) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string slurpFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

} // namespace

int main() {
    // The hash function itself, against the standard test vector.
    {
        Dir d;
        writeFile(d.path / "abc", "abc");
        check(sha256FileHex((d.path / "abc").string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of 'abc'");
        std::string err;
        check(sha256FileHex((d.path / "missing").string(), &err).empty() && !err.empty(), "an unreadable file has no hash and says why");
    }

    // The file name comes from the URL and must be plain.
    {
        UpdateManifest m = manifest();
        check(updatePackageFileName(m) == "Aestra-0.8.2.zip", "name from the URL");
        m.url = "https://update.aestra.studio/early/windows-x64/..";
        check(updatePackageFileName(m).empty(), "a traversing name is refused");
        m.url = "https://update.aestra.studio/early/windows-x64/../../x.zip";
        check(updatePackageFileName(m) == "x.zip", "only the last part of the URL ever names a file, so a dotted path cannot place one elsewhere");
        m.url = "https://update.aestra.studio/early/windows-x64/.hidden.zip";
        check(updatePackageFileName(m).empty(), "a hidden name is refused");
        m.url = "https://update.aestra.studio/early/windows-x64/a%2e%2e.zip";
        check(updatePackageFileName(m).empty(), "an encoded name is refused");
        m.url = "https://update.aestra.studio/early/windows-x64/Aestra-0.8.2.zip?token=1";
        check(updatePackageFileName(m) == "Aestra-0.8.2.zip", "a query string is not part of the name");
    }

    // A whole download: right file, right place, key sent as asked.
    {
        Dir d;
        FakeHost host;
        std::vector<uint64_t> seen;
        const auto out = downloadUpdatePackage(host, config(), manifest(), d.path.string(), [&](uint64_t done, uint64_t) { seen.push_back(done); return true; });
        check(out.status == DownloadStatus::Ok && fs::exists(out.path), "a good download succeeds");
        check(host.seen.size() == 1 && host.seen[0].resumeFrom == 0, "from the start");
        check(host.seen[0].headers["Authorization"] == "Bearer aef_testkey0123456789", "early sends the founder key");
        check(!seen.empty() && seen.back() == kSize, "progress reaches the full size");
        check(!fs::exists(out.path + ".part"), "no part file is left");
        // Asking again finds it already there and right: no request.
        FakeHost again;
        const auto second = downloadUpdatePackage(again, config(), manifest(), d.path.string());
        check(second.status == DownloadStatus::Ok && again.seen.empty(), "a package already downloaded and verified is not fetched again");
    }
    {
        Dir d;
        FakeHost host;
        UpdateManifest m = manifest();
        m.url = "https://update.aestra.studio/stable/windows-x64/Aestra-0.8.2.zip";
        downloadUpdatePackage(host, config("stable"), m, d.path.string());
        check(host.seen.size() == 1 && host.seen[0].headers.count("Authorization") == 0, "stable never sends the founder key");
    }

    // Requests that must not be made.
    {
        Dir d;
        FakeHost host;
        check(downloadUpdatePackage(host, config("early"), [&] { auto m = manifest(); m.url = "https://evil.example/early/windows-x64/Aestra-0.8.2.zip"; return m; }(), d.path.string()).status == DownloadStatus::BadConfig,
              "a download on another host is refused");
        UpdateConfig noKey = config("early");
        noKey.founderKey.clear();
        check(downloadUpdatePackage(host, noKey, manifest(), d.path.string()).status == DownloadStatus::BadConfig, "early without a key asks nothing");
        UpdateConfig http = config();
        http.baseUrl = "http://update.aestra.studio";
        check(downloadUpdatePackage(host, http, manifest(), d.path.string()).status == DownloadStatus::BadConfig, "a plain-http host asks nothing");
        auto bad = manifest();
        bad.url = "https://update.aestra.studio/early/windows-x64/%2e%2e";
        check(downloadUpdatePackage(host, config(), bad, d.path.string()).status == DownloadStatus::BadConfig, "an unsafe name asks nothing");
        check(host.seen.empty(), "none of those reached the transport");
    }

    // The file is not what was promised.
    {
        Dir d;
        FakeHost host;
        host.body[500] ^= 1; // one flipped byte, same size
        const auto out = downloadUpdatePackage(host, config(), manifest(), d.path.string());
        check(out.status == DownloadStatus::HashMismatch, "one changed byte is caught by the hash");
        check(!fs::exists(d.path / "Aestra-0.8.2.zip") && !fs::exists(d.path / "Aestra-0.8.2.zip.part"), "and nothing is kept");
    }
    {
        Dir d;
        FakeHost host;
        host.body += "extra";
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::SizeMismatch, "a longer file is caught by the size");
        check(!fs::exists(d.path / "Aestra-0.8.2.zip.part"), "and removed");
    }

    // Server answers.
    {
        Dir d;
        FakeHost host;
        host.forceStatus = 401;
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::Unauthorized, "401 is Unauthorized");
        host.forceStatus = 404;
        const auto out = downloadUpdatePackage(host, config(), manifest(), d.path.string());
        check(out.status == DownloadStatus::NetworkError && out.detail.find("404") != std::string::npos, "404 is an error that names the code");
    }

    // Interrupted, then resumed from where it stopped.
    {
        Dir d;
        FakeHost host;
        host.cutAfter = 30000;
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::NetworkError, "a dropped connection is a network error");
        const uint64_t partial = fs::file_size(d.path / "Aestra-0.8.2.zip.part");
        check(partial >= 30000 && partial < kSize, "the part file is kept");
        host.cutAfter = 0;
        const auto out = downloadUpdatePackage(host, config(), manifest(), d.path.string());
        check(out.status == DownloadStatus::Ok, "the next try finishes it");
        check(host.seen.back().resumeFrom == partial, "asking only for the rest");
    }
    // Cancelled: the part stays, and resumes.
    {
        Dir d;
        FakeHost host;
        host.cancelAfter = 20000;
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::Cancelled, "cancel is reported as cancel");
        check(fs::exists(d.path / "Aestra-0.8.2.zip.part"), "the part stays");
        host.cancelAfter = 0;
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::Ok, "and the next try completes");
    }
    // A server that ignores the range sends everything again: the file must not be the part plus the whole.
    {
        Dir d;
        FakeHost host;
        writeFile(d.path / "Aestra-0.8.2.zip.part", host.body.substr(0, 40000));
        host.ignoreRange = true;
        const auto out = downloadUpdatePackage(host, config(), manifest(), d.path.string());
        check(out.status == DownloadStatus::Ok, "a server that ignores Range still gives a correct file");
    }
    // A part that is already full size (or bigger) is not trusted as a start.
    {
        Dir d;
        FakeHost host;
        writeFile(d.path / "Aestra-0.8.2.zip.part", std::string(kSize + 10, 'x'));
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::Ok && host.seen[0].resumeFrom == 0, "an oversized part is thrown away");
    }
    // 416: the range was beyond the file; start over once.
    {
        Dir d;
        struct Host416 : FakeHost {
            int calls = 0;
            DownloadResult download(const DownloadRequest& r, const DownloadProgress& p) override {
                if (calls++ == 0) {
                    seen.push_back(r);
                    DownloadResult res;
                    res.status = 416;
                    return res;
                }
                return FakeHost::download(r, p);
            }
        } host;
        writeFile(d.path / "Aestra-0.8.2.zip.part", host.body.substr(0, 40000));
        check(downloadUpdatePackage(host, config(), manifest(), d.path.string()).status == DownloadStatus::Ok && host.seen.back().resumeFrom == 0, "416 restarts from zero once and succeeds");
    }

#ifndef _WIN32
    // Unpacking (uses the system tar; the Windows path is checked by the Windows build's own run).
    {
        auto run = [](const std::string& cmd) { return std::system(cmd.c_str()) == 0; };
        Dir d;
        const fs::path src = d.path / "src";
        writeFile(src / "wrapped" / "Aestra", "#!/bin/sh\nexit 0\n");
        writeFile(src / "wrapped" / "AestraAssets" / "a.txt", "asset");
        check(run("tar -czf '" + (d.path / "wrapped.tar.gz").string() + "' -C '" + src.string() + "' wrapped"), "(test setup) made a wrapped tarball");
        std::string err;
        check(extractUpdatePackage((d.path / "wrapped.tar.gz").string(), (d.path / "staged1").string(), "Aestra", &err), "a wrapped package unpacks: " + err);
        check(fs::exists(d.path / "staged1" / "Aestra") && fs::exists(d.path / "staged1" / "AestraAssets" / "a.txt"), "and the wrapping folder is gone");

        writeFile(src / "flat" / "Aestra", "x");
        check(run("tar -czf '" + (d.path / "flat.tar.gz").string() + "' -C '" + (src / "flat").string() + "' ."), "(test setup) flat tarball");
        check(extractUpdatePackage((d.path / "flat.tar.gz").string(), (d.path / "staged2").string(), "Aestra", &err) && fs::exists(d.path / "staged2" / "Aestra"), "a package that is not wrapped unpacks too");

        writeFile(src / "nomain" / "readme.txt", "x");
        check(run("tar -czf '" + (d.path / "nomain.tar.gz").string() + "' -C '" + (src / "nomain").string() + "' ."), "(test setup) tarball without the program");
        check(!extractUpdatePackage((d.path / "nomain.tar.gz").string(), (d.path / "staged3").string(), "Aestra", &err) && !fs::exists(d.path / "staged3"), "no program inside: refused, nothing left staged");

        writeFile(src / "linked" / "Aestra", "x");
        std::error_code ec;
        fs::create_symlink("/etc/passwd", src / "linked" / "evil", ec);
        check(run("tar -czf '" + (d.path / "linked.tar.gz").string() + "' -C '" + (src / "linked").string() + "' ."), "(test setup) tarball with a symlink");
        check(!extractUpdatePackage((d.path / "linked.tar.gz").string(), (d.path / "staged4").string(), "Aestra", &err) && err.find("link") != std::string::npos && !fs::exists(d.path / "staged4"),
              "a symlink in the package is refused");

        // #1267: the environment must not choose or steer the tar that unpacks a signed update.
        {
            const fs::path marker = d.path / "tar-options-ran";
            const fs::path hook = d.path / "hook.sh";
            writeFile(hook, "#!/bin/sh\ntouch '" + marker.string() + "'\ncat >/dev/null\n");
            fs::permissions(hook, fs::perms::owner_all, fs::perm_options::replace);
            setenv("TAR_OPTIONS", ("--to-command=" + hook.string()).c_str(), 1);
            const bool ok = extractUpdatePackage((d.path / "wrapped.tar.gz").string(), (d.path / "staged6").string(), "Aestra", &err);
            unsetenv("TAR_OPTIONS");
            check(ok && fs::exists(d.path / "staged6" / "Aestra") && !fs::exists(marker),
                  "a hostile TAR_OPTIONS in the environment does not run or reach the unpacking tar: " + err);

            const fs::path fakeBin = d.path / "fakebin";
            const fs::path marker2 = d.path / "fake-tar-ran";
            writeFile(fakeBin / "tar", "#!/bin/sh\ntouch '" + marker2.string() + "'\nexit 0\n");
            fs::permissions(fakeBin / "tar", fs::perms::owner_all, fs::perm_options::replace);
            const std::string oldPath = std::getenv("PATH") ? std::getenv("PATH") : "";
            setenv("PATH", (fakeBin.string() + ":" + oldPath).c_str(), 1);
            const bool ok2 = extractUpdatePackage((d.path / "wrapped.tar.gz").string(), (d.path / "staged7").string(), "Aestra", &err);
            setenv("PATH", oldPath.c_str(), 1);
            check(ok2 && fs::exists(d.path / "staged7" / "Aestra") && !fs::exists(marker2),
                  "a tar earlier on PATH is never the one that runs: " + err);
        }

        writeFile(d.path / "notatar.tar.gz", "this is not an archive");
        check(!extractUpdatePackage((d.path / "notatar.tar.gz").string(), (d.path / "staged5").string(), "Aestra", &err) && !fs::exists(d.path / "staged5"), "garbage is refused");

        // An archive that names a path above the staging folder must not write there. (Needs GNU tar's --transform to make
        // such an archive; macOS's bsdtar cannot, so the case runs where it can.)
        writeFile(src / "up" / "Aestra", "x");
        if (run("tar --version 2>/dev/null | grep -q 'GNU tar'")) check(run("tar -czf '" + (d.path / "up.tar.gz").string() + "' --transform='s,^\\./,../escaped-,' -C '" + (src / "up").string() + "' ./Aestra"), "(test setup) tarball with an escaping name");
        if (run("tar --version 2>/dev/null | grep -q 'GNU tar'")) {
            extractUpdatePackage((d.path / "up.tar.gz").string(), (d.path / "staged6").string(), "Aestra", &err);
            check(!fs::exists(d.path / "escaped-Aestra"), "nothing was written above the staging folder");
        }
    }
#endif


#ifndef _WIN32
    // The whole install, on its own thread: download, verify, unpack, Ready.
    {
        auto run = [](const std::string& cmd) { return std::system(cmd.c_str()) == 0; };
        Dir d;
        // A real package: a wrapped tarball with a program in it, whose size and hash the "feed" will promise.
        writeFile(d.path / "src" / "pkg" / "Aestra", "#!/bin/sh\nexit 0\n");
        writeFile(d.path / "src" / "pkg" / "AestraAssets" / "a.txt", "asset");
        check(run("tar -czf '" + (d.path / "pkg.tar.gz").string() + "' -C '" + (d.path / "src").string() + "' pkg"), "(test setup) made the package");
        std::string tarBytes;
        {
            std::ifstream in(d.path / "pkg.tar.gz", std::ios::binary);
            tarBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        UpdateManifest m = manifest();
        m.url = "https://update.aestra.studio/early/linux-x64/Aestra-0.8.2.tar.gz";
        m.size = tarBytes.size();
        m.sha256 = sha256FileHex((d.path / "pkg.tar.gz").string());

        UpdateLayout layout;
        layout.appDir = (d.path / "install" / "Aestra").string();
        layout.downloadDir = (d.path / "downloads").string();
        layout.executableName = "Aestra";
        layout.updaterName = "AestraUpdater";
        writeFile(fs::path(layout.appDir) / "Aestra", "old");

        auto waitUntil = [](UpdateSession& s, auto&& pred) {
            for (int i = 0; i < 500 && !pred(s.state()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        auto host = std::make_unique<FakeHost>();
        host->body = tarBytes;
        FakeHost* h = host.get();
        UpdateSession session(std::move(host));
        check(session.state().phase == InstallPhase::Idle, "a new session is idle");
        check(session.start(config(), m, layout), "start begins an install");
        waitUntil(session, [](const InstallState& st) { return st.phase == InstallPhase::Ready || st.phase == InstallPhase::Failed; });
        const InstallState done = session.state();
        check(done.phase == InstallPhase::Ready && done.done == done.total && done.total == tarBytes.size() && done.version == "0.8.2", "the install reaches Ready with full progress: " + done.message);
        check(fs::exists(fs::path(layout.stagedDir()) / "Aestra") && fs::exists(fs::path(layout.stagedDir()) / "AestraAssets" / "a.txt"), "the new version is staged beside the installed one");
        check(session.stagedDir() == layout.stagedDir(), "stagedDir names it");
        check(!fs::exists(fs::path(layout.downloadDir) / "Aestra-0.8.2.tar.gz"), "the package is removed once unpacked");
        check(slurpFile(fs::path(layout.appDir) / "Aestra") == "old", "the installed copy is untouched until the user restarts");
        const size_t calls = h->seen.size();
        check(session.start(config(), m, layout) && h->seen.size() == calls, "starting again for the same version, already Ready, does nothing");

        // A failure, then a retry that works.
        Dir d2;
        UpdateLayout layout2 = layout;
        layout2.appDir = (d2.path / "install" / "Aestra").string();
        layout2.downloadDir = (d2.path / "downloads").string();
        writeFile(fs::path(layout2.appDir) / "Aestra", "old");
        auto bad = std::make_unique<FakeHost>();
        bad->body = tarBytes;
        bad->body[10] ^= 1;
        FakeHost* badHost = bad.get();
        UpdateSession retry(std::move(bad));
        retry.start(config(), m, layout2);
        waitUntil(retry, [](const InstallState& st) { return st.phase == InstallPhase::Failed || st.phase == InstallPhase::Ready; });
        check(retry.state().phase == InstallPhase::Failed && retry.state().message.find("did not match") != std::string::npos, "a corrupted download fails with a plain sentence");
        check(!fs::exists(layout2.stagedDir()), "and nothing is staged");
        badHost->body = tarBytes;
        check(retry.start(config(), m, layout2), "start retries after a failure");
        waitUntil(retry, [](const InstallState& st) { return st.phase == InstallPhase::Ready || st.phase == InstallPhase::Failed; });
        check(retry.state().phase == InstallPhase::Ready, "and the retry succeeds");

        // Cancel, then resume.
        Dir d3;
        UpdateLayout layout3 = layout;
        layout3.appDir = (d3.path / "install" / "Aestra").string();
        layout3.downloadDir = (d3.path / "downloads").string();
        writeFile(fs::path(layout3.appDir) / "Aestra", "old");
        auto slow = std::make_unique<FakeHost>();
        slow->body = tarBytes;
        slow->cancelAfter = 1; // the fake cancels itself after its first chunk
        UpdateSession cancelled(std::move(slow));
        cancelled.start(config(), m, layout3);
        waitUntil(cancelled, [](const InstallState& st) { return st.phase != InstallPhase::Downloading; });
        check(cancelled.state().phase == InstallPhase::Cancelled, "a cancelled download says Cancelled");
        check(!cancelled.busy(), "and the session is free again");

        // An install location that cannot be changed is refused before anything is downloaded.
        Dir d4;
        UpdateLayout layout4 = layout;
        layout4.appDir = (d4.path / "locked" / "Aestra").string();
        layout4.downloadDir = (d4.path / "downloads").string();
        writeFile(fs::path(layout4.appDir) / "Aestra", "old");
        std::error_code ec;
        fs::permissions(d4.path / "locked", fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
        bool lockable = false;
        {
            std::ofstream probe(d4.path / "locked" / "probe");
            lockable = !probe;
        }
        if (lockable) {
            auto unused = std::make_unique<FakeHost>();
            FakeHost* unusedHost = unused.get();
            UpdateSession locked(std::move(unused));
            locked.start(config(), m, layout4);
            waitUntil(locked, [](const InstallState& st) { return st.phase != InstallPhase::Downloading; });
            check(locked.state().phase == InstallPhase::Failed && locked.state().message.find("administrator") != std::string::npos, "a read-only install says why in words a person can act on");
            check(unusedHost->seen.empty(), "and downloads nothing");
            fs::permissions(d4.path / "locked", fs::perms::owner_all, fs::perm_options::replace, ec);
        }

        // Handing over to the updater: it is copied away from the install, and gets everything it needs.
        Dir d5;
        UpdateLayout layout5 = layout;
        layout5.appDir = (d5.path / "install" / "Aestra").string();
        writeFile(fs::path(layout5.appDir) / "AestraUpdater", "#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\" > '" + (d5.path / "args.txt").string() + "'\n");
        fs::permissions(fs::path(layout5.appDir) / "AestraUpdater", fs::perms::owner_exec, fs::perm_options::add, ec);
        std::string launchError;
        check(launchUpdater(layout5, (d5.path / "result.json").string(), "0.8.1", "0.8.2", 4242, &launchError), "the updater is launched: " + launchError);
        for (int i = 0; i < 300 && !fs::exists(d5.path / "args.txt"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const std::string args = slurpFile(d5.path / "args.txt");
        check(args.find("aestra-updater-4242") != std::string::npos, "it runs from a temporary copy, not from the install");
        const std::vector<std::string> wanted = {"--pid\n4242", "--app-dir\n" + layout5.appDir, "--staged\n" + layout5.stagedDir(),
                                                 "--backup\n" + layout5.backupDir(), "--exe\nAestra", "--from\n0.8.1", "--to\n0.8.2"};
        for (const auto& w : wanted) check(args.find(w) != std::string::npos, "the updater is given " + w);
        UpdateLayout missing = layout5;
        missing.appDir = (d5.path / "nowhere").string();
        check(!launchUpdater(missing, "x", "a", "b", 1, &launchError) && launchError.find("AestraUpdater") != std::string::npos, "an install with no updater says so");
        std::error_code cleanup;
        fs::remove_all(fs::temp_directory_path() / "aestra-updater-4242", cleanup);
    }
#endif

    if (g_failures == 0) std::cout << "update install: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
