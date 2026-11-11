// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ProjectDamageSweepTest — "never lose a session": a damaged project file is
// reported, never hangs or crashes the load, and never stops recovery.
//
// Saves a real project, then damages it about two thousand ways (one byte
// overwritten, one byte deleted, the file cut short) and loads every version.
// Each load must return quickly; a load may call itself Verified only if it
// really is the saved project; and the recovery path (damaged file first,
// backup second) must always end with a usable session.
//
// Regression: one bad byte inside a JSON array made the parser loop forever,
// appending empty values until the machine ran out of memory (#1243). The
// ctest TIMEOUT on this test is part of it.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "  PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

constexpr double kTempo = 128.0;
constexpr double kPlayhead = 3.5;

std::shared_ptr<TrackManager> makeProject() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(48000.0);
    tm->getPlaylistModel().setBPM(kTempo);
    for (const char* name : {"Drums", "Bass", "Vocal"}) {
        const PlaylistLaneID lane = tm->getPlaylistModel().createLane(name);
        MixerChannel* channel = tm->addChannel(name);
        if (channel) {
            tm->createTrack(lane, name, channel->getChannelId());
        }
    }
    return tm;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

struct Tally {
    int loads = 0;
    int rejected = 0;
    int mismatch = 0;
    int unchecked = 0;
    int verified = 0;
    int verifiedButDifferent = 0;
    int recoveryFailed = 0;
    double slowestMs = 0.0;
};

void loadDamaged(const std::string& damaged, const std::filesystem::path& dir, const std::string& original,
                 const std::filesystem::path& backup, Tally& tally) {
    const auto path = dir / "damaged.aes";
    writeFile(path, damaged);

    auto tm = std::make_shared<TrackManager>();
    const auto start = std::chrono::steady_clock::now();
    const auto result = ProjectSerializer::load(path.string(), tm);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    tally.slowestMs = std::max(tally.slowestMs, ms);
    ++tally.loads;

    if (!result.ok) {
        ++tally.rejected;
    } else if (result.integrity == ProjectSerializer::LoadIntegrity::Mismatch) {
        ++tally.mismatch;
    } else if (result.integrity == ProjectSerializer::LoadIntegrity::Unchecked) {
        ++tally.unchecked;
    } else {
        ++tally.verified;
        // Verified must mean "this is the saved project": damage that only
        // touched whitespace is fine, anything else is a false all-clear.
        const auto again = ProjectSerializer::serialize(tm, result.tempo, result.playhead);
        if (!again.ok || again.contents != original) {
            ++tally.verifiedButDifferent;
        }
    }

    // Recovery: damaged file first, backup second, as the app offers them.
    auto recovered = std::make_shared<TrackManager>();
    const auto pick = ProjectSerializer::loadFirstValid({path.string(), backup.string()}, recovered, dir.string());
    if (!pick.result.ok) {
        ++tally.recoveryFailed;
    }
}

} // namespace

int main() {
    Aestra::Tests::ScopedTempDirectory temp("project-damage-sweep");
    const auto dir = temp.path();

    auto project = makeProject();
    const auto good = dir / "song.aes";
    const auto backup = dir / "song.aes.bak";
    check(ProjectSerializer::save(good.string(), project, kTempo, kPlayhead), "the project saves");
    std::filesystem::copy_file(good, backup, std::filesystem::copy_options::overwrite_existing);
    const std::string bytes = readFile(good);
    const auto original = ProjectSerializer::serialize(project, kTempo, kPlayhead);
    check(bytes.size() > 500 && original.ok,
          "saved file is a real project (" + std::to_string(bytes.size()) + " bytes)");

    {
        auto tm = std::make_shared<TrackManager>();
        const auto clean = ProjectSerializer::load(good.string(), tm);
        check(clean.ok && clean.integrity == ProjectSerializer::LoadIntegrity::Verified,
              "the undamaged file loads Verified");
    }

    Tally tally;
    const size_t n = bytes.size();
    // Overwrite one byte with characters JSON cannot start a value with, or
    // with structure characters in the wrong place.
    for (const char junk : {'x', '}', ']', ',', '"', '\0'}) {
        for (size_t at = 0; at < n; at += 11) {
            std::string damaged = bytes;
            damaged[at] = junk;
            loadDamaged(damaged, dir, original.contents, backup, tally);
        }
    }
    for (size_t at = 0; at < n; at += 7) {
        std::string damaged = bytes;
        damaged.erase(at, 1);
        loadDamaged(damaged, dir, original.contents, backup, tally);
    }
    for (size_t len = 0; len < n; len += std::max<size_t>(1, n / 40)) {
        loadDamaged(bytes.substr(0, len), dir, original.contents, backup, tally);
    }

    std::cout << "  " << tally.loads << " damaged loads: " << tally.rejected << " rejected, " << tally.mismatch
              << " checksum mismatch, " << tally.unchecked << " unchecked, " << tally.verified << " verified; slowest "
              << tally.slowestMs << " ms\n";

    check(tally.loads > 1000, "the sweep ran (" + std::to_string(tally.loads) + " loads)");
    check(tally.slowestMs < 2000.0, "every damaged load returns within 2 s");
    check(tally.verifiedButDifferent == 0, "no damaged file is reported Verified with different content (" +
                                               std::to_string(tally.verifiedButDifferent) + ")");
    check(tally.recoveryFailed == 0, "with a backup beside it, every damaged file still ends in a usable session (" +
                                         std::to_string(tally.recoveryFailed) + " failed)");

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "All project damage checks passed\n";
    return EXIT_SUCCESS;
}
