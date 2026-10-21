// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Getting an update onto the disk: the download resumes, is accepted only when its size and SHA-256 match the signed feed,
// never sends the founder key anywhere but the update host, and the unpacked package is checked before anything could be
// installed from it. The fake host serves a fixed 100,000-byte package and can misbehave in the ways real ones do.

#include "UpdateInstall.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

        writeFile(d.path / "notatar.tar.gz", "this is not an archive");
        check(!extractUpdatePackage((d.path / "notatar.tar.gz").string(), (d.path / "staged5").string(), "Aestra", &err) && !fs::exists(d.path / "staged5"), "garbage is refused");

        // An archive that names a path above the staging folder must not write there.
        writeFile(src / "up" / "Aestra", "x");
        check(run("tar -czf '" + (d.path / "up.tar.gz").string() + "' --transform='s,^\\./,../escaped-,' -C '" + (src / "up").string() + "' ./Aestra"), "(test setup) tarball with an escaping name");
        extractUpdatePackage((d.path / "up.tar.gz").string(), (d.path / "staged6").string(), "Aestra", &err);
        check(!fs::exists(d.path / "escaped-Aestra"), "nothing was written above the staging folder");
    }
#endif

    if (g_failures == 0) std::cout << "update install: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
