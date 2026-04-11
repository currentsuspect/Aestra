// © 2026 Aestra Studios — All Rights Reserved.
// SEC-001: malformed lane color values must not crash project load

#include "ProjectSerializer.h"
#include "TrackManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Aestra::Audio::TrackManager;

namespace {
/**
 * @brief Builds a minimal project JSON containing a single lane with the specified color.
 *
 * The provided color string is inserted verbatim into the lane's "color" field; no validation or normalization is performed.
 *
 * @param color The string to place into the lane's "color" property (e.g., a hex color or malformed input).
 * @return std::string JSON text representing the minimal project with one lane containing the given color.
 */
std::string buildProjectJson(const std::string& color) {
    return "{\n"
           "  \"version\": 1,\n"
           "  \"tempo\": 120.0,\n"
           "  \"playhead\": 0.0,\n"
           "  \"lanes\": [\n"
           "    {\n"
           "      \"name\": \"Track 1\",\n"
           "      \"color\": \"" + color + "\",\n"
           "      \"volume\": 1.0,\n"
           "      \"pan\": 0.0,\n"
           "      \"mute\": false,\n"
           "      \"solo\": false,\n"
           "      \"clips\": []\n"
           "    }\n"
           "  ]\n"
           "}\n";
}
} /**
 * @brief Runs a security regression test that verifies ProjectSerializer safely handles malformed lane color values.
 *
 * The test writes temporary project files containing a single lane whose `"color"` field is set to several malformed
 * strings, attempts to load each project into a TrackManager, and validates that loading neither throws nor leaves
 * the playlist in an invalid state. For each case the test checks that exactly one lane is loaded and that the lane's
 * color falls back to white (0xFFFFFFFF). Results are printed to stdout.
 *
 * Side effects: creates a temporary directory under the system temp path, writes `.aes` files, and removes the
 * directory tree before exiting.
 *
 * @return int `0` if all malformed color cases were handled safely (loaded with white fallback), `1` if any case
 * failed or caused an exception.
 */

int main() {
    std::cout << "=== SEC-001: ProjectSerializer malformed lane color handling ===" << std::endl;

    const std::vector<std::string> cases = {
        "not_a_number",
        "99999999999999999999999",
        "abc123xyz",
        "",
        "  ",
        "-1",
    };

    const fs::path tempDir = fs::temp_directory_path() / "aestra_sec001";
    fs::create_directories(tempDir);

    bool allHandled = true;
    for (size_t i = 0; i < cases.size(); ++i) {
        const fs::path projectPath = tempDir / ("case_" + std::to_string(i) + ".aes");
        std::ofstream out(projectPath);
        out << buildProjectJson(cases[i]);
        out.close();

        auto trackManager = std::make_shared<TrackManager>();
        bool threw = false;
        ProjectSerializer::LoadResult result;

        try {
            result = ProjectSerializer::load(projectPath.string(), trackManager);
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" threw: " << e.what() << std::endl;
            threw = true;
            allHandled = false;
        } catch (...) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" threw unknown exception" << std::endl;
            threw = true;
            allHandled = false;
        }

        if (threw) {
            continue;
        }
        if (!result.ok) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" failed to load: " << result.errorMessage << std::endl;
            allHandled = false;
            continue;
        }

        auto& playlist = trackManager->getPlaylistModel();
        if (playlist.getLaneCount() != 1) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" did not load one lane" << std::endl;
            allHandled = false;
            continue;
        }

        const auto laneIds = playlist.getLaneIDs();
        if (laneIds.empty()) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" produced no lane IDs" << std::endl;
            allHandled = false;
            continue;
        }
        auto* loadedLane = playlist.getLane(laneIds.front());
        if (!loadedLane || loadedLane->colorRGBA != 0xFFFFFFFFu) {
            std::cout << "  [FAIL] \"" << cases[i] << "\" did not fall back to white" << std::endl;
            allHandled = false;
            continue;
        }

        std::cout << "  [PASS] \"" << cases[i] << "\" loaded safely with fallback color" << std::endl;
    }

    fs::remove_all(tempDir);

    if (!allHandled) {
        std::cout << "\n[FAIL] Malformed lane colors still escape the real project load path." << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] Real project load path handles malformed lane colors safely." << std::endl;
    return 0;
}
