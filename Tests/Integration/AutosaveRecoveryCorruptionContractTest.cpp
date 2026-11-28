// © 2026 Aestra Studios — All Rights Reserved.
// Persistence contracts for interrupted autosaves and corrupt recovery candidates.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/TrackManager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
}

void writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "could not open test input file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(output.good(), "could not write test input file");
}

std::string minimalProject(int version) {
    return std::string("{\"version\":") + std::to_string(version) +
           ",\"tempo\":120,\"playhead\":0,\"sources\":[],\"patterns\":[],\"mixerChannels\":[],\"lanes\":[],"
           "\"arsenal\":{\"nextId\":1,\"units\":[]}}";
}

std::shared_ptr<Aestra::Audio::TrackManager> makeSentinelManager() {
    auto manager = std::make_shared<Aestra::Audio::TrackManager>();
    auto& playlist = manager->getPlaylistModel();
    playlist.setBPM(137.0);
    playlist.createLane("Sentinel lane");
    return manager;
}

void requireSentinelIntact(const std::shared_ptr<Aestra::Audio::TrackManager>& manager) {
    auto& playlist = manager->getPlaylistModel();
    require(playlist.getBPM() == 137.0, "rejected recovery candidate changed the existing tempo");
    const auto laneIds = playlist.getLaneIDs();
    require(laneIds.size() == 1, "rejected recovery candidate changed the existing lane count");
    const auto* lane = playlist.getLane(laneIds.front());
    require(lane != nullptr && lane->name == "Sentinel lane",
            "rejected recovery candidate partially loaded over the existing lane");
}

void requireDiagnostic(const std::filesystem::path& path,
                       const std::string& contents,
                       const std::string& expectedDiagnostic,
                       const char* caseName,
                       const std::shared_ptr<Aestra::Audio::TrackManager>& manager) {
    writeFile(path, contents);
    const auto result = ProjectSerializer::load(path.string(), manager);
    require(!result.ok, "corrupt recovery candidate unexpectedly loaded");
    if (result.errorMessage != expectedDiagnostic) {
        std::cerr << "[FAIL] " << caseName << " diagnostic mismatch\n"
                  << "  expected: " << expectedDiagnostic << '\n'
                  << "  actual:   " << result.errorMessage << '\n';
        std::exit(1);
    }
    requireSentinelIntact(manager);
    std::cout << "[PASS] " << caseName << ": " << result.errorMessage << '\n';
}

#if !defined(_WIN32)
void testKilledAtomicWrite(const std::filesystem::path& target, const std::string& committedContents) {
    namespace fs = std::filesystem;
    fs::path temporary = target;
    temporary += ".tmp";

    require(ProjectSerializer::writeAtomically(target.string(), committedContents),
            "could not establish the last committed autosave");
    require(::mkfifo(temporary.c_str(), 0600) == 0, "could not create temporary FIFO for interrupted writer");

    const std::string inFlightContents(8 * 1024 * 1024, 'x');
    const pid_t child = ::fork();
    require(child >= 0, "could not fork the interrupted autosave writer");
    if (child == 0) {
        const bool committed = ProjectSerializer::writeAtomically(target.string(), inFlightContents);
        _exit(committed ? 0 : 1);
    }

    // The blocking FIFO open rendezvous with the writer, so the read end cannot
    // observe a no-writer hangup before the first bytes arrive.
    const int reader = ::open(temporary.c_str(), O_RDONLY);
    if (reader < 0) {
        ::kill(child, SIGKILL);
        int ignoredStatus = 0;
        ::waitpid(child, &ignoredStatus, 0);
        fs::remove(temporary);
        require(false, "could not open temporary FIFO reader");
    }

    pollfd descriptor{};
    descriptor.fd = reader;
    descriptor.events = POLLIN;
    int pollResult = 0;
    for (int attempt = 0; attempt < 100 && pollResult == 0; ++attempt)
        pollResult = ::poll(&descriptor, 1, 50);
    if (pollResult <= 0 || (descriptor.revents & POLLIN) == 0) {
        ::kill(child, SIGKILL);
        int ignoredStatus = 0;
        ::waitpid(child, &ignoredStatus, 0);
        ::close(reader);
        fs::remove(temporary);
        require(false, "atomic writer did not begin writing its temporary file");
    }

    char firstByte = 0;
    if (::read(reader, &firstByte, 1) != 1) {
        ::kill(child, SIGKILL);
        int ignoredStatus = 0;
        ::waitpid(child, &ignoredStatus, 0);
        ::close(reader);
        fs::remove(temporary);
        require(false, "could not observe bytes from the in-flight write");
    }
    require(::kill(child, SIGKILL) == 0, "could not kill the in-flight autosave writer");
    int childStatus = 0;
    require(::waitpid(child, &childStatus, 0) == child, "could not reap the interrupted autosave writer");
    require(WIFSIGNALED(childStatus) && WTERMSIG(childStatus) == SIGKILL,
            "autosave writer did not exit from the requested mid-write kill");
    ::close(reader);

    require(fs::exists(temporary), "interrupted writer should leave its uncommitted temporary entry");
    require(fs::is_fifo(temporary), "interrupted writer temporary entry should still be the FIFO");
    fs::remove(temporary);

    std::ifstream committed(target, std::ios::binary);
    const std::string persisted((std::istreambuf_iterator<char>(committed)), std::istreambuf_iterator<char>());
    require(persisted == committedContents, "killed write replaced the last committed autosave");

    auto manager = std::make_shared<Aestra::Audio::TrackManager>();
    const auto loaded = ProjectSerializer::load(target.string(), manager);
    require(loaded.ok, "last committed autosave did not load after the writer was killed");
    std::cout << "[PASS] kill during atomic autosave write preserved and loaded the last committed .aes\n";
}
#endif

} // namespace

int main() {
    const Aestra::Tests::ScopedTempDirectory tempDirScope{"AutosaveRecoveryCorruptionContract"};
    const auto& tempDir = tempDirScope.path();
    const int currentVersion = ProjectSerializer::PROJECT_VERSION_CURRENT;
#if !defined(_WIN32)
    testKilledAtomicWrite(tempDir / "committed-autosave.aes", minimalProject(currentVersion));
#else
    std::cout << "[SKIP] kill-during-write contract requires POSIX process and FIFO support\n";
#endif

    const auto manager = makeSentinelManager();
    const auto truncatedPath = tempDir / "truncated.aes";
    const auto emptyPath = tempDir / "empty.aes";
    const auto futurePath = tempDir / "future-version.aes";
    const std::string envelopeDiagnostic = "Invalid project file: project must be a single JSON object";
    requireDiagnostic(truncatedPath,
                      "{\"version\":1,\"tempo\":120,\"playhead\":0,\"sources\":[],\"patterns\":[],\"lanes\":[",
                      envelopeDiagnostic, "truncated .aes", manager);
    requireDiagnostic(emptyPath, "", envelopeDiagnostic, "empty .aes", manager);
    const int futureVersion = currentVersion + 1;
    const std::string futureDiagnostic =
        "Project file version " + std::to_string(futureVersion) + " is newer than this version of AESTRA (" +
        std::to_string(currentVersion) + "). Please update AESTRA to open this project.";
    const std::string futureProject =
        std::string("{\"version\":") + std::to_string(futureVersion) +
        ",\"tempo\":88,\"playhead\":9,\"sources\":[],\"patterns\":[],\"mixerChannels\":[],\"lanes\":[],"
        "\"futureVersionField\":{\"newSchemaData\":true}}";
    requireDiagnostic(futurePath, futureProject, futureDiagnostic, "future-version fields", manager);

    const auto validPath = tempDir / "valid-recovery.aes";
    writeFile(validPath, minimalProject(currentVersion));
    const auto fallback = ProjectSerializer::loadFirstValid(
        {truncatedPath.string(), emptyPath.string(), futurePath.string(), validPath.string()}, manager, validPath.string());
    require(fallback.result.ok, "recovery did not select the first structurally valid candidate");
    require(fallback.loadedPath == validPath.string(), "recovery selected the wrong valid candidate");
    std::cout << "[PASS] recovery reported and skipped corrupt candidates before loading the valid candidate\n";

    std::cout << "All autosave recovery corruption contracts passed.\n";
    return 0;
}
