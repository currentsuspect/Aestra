// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>

namespace Aestra {

/**
 * @brief `AestraHeadless --check`: render a project the way Export does and say
 *        whether it is right, without a person listening.
 *
 *   AestraHeadless --check song.aes                     # pass/fail + JSON summary
 *   AestraHeadless --check song.aes --out song.wav      # keep the render
 *   AestraHeadless --check song.aes --expect song.golden.json
 *   AestraHeadless --check song.aes --write-expect song.golden.json
 *
 * Exit codes: 0 the render passed every check, 1 it rendered but failed one,
 * 2 it could not be checked (unreadable project, failed render, bad golden).
 */
struct HeadlessCheckOptions {
    std::string projectPath;
    std::string outPath;          ///< keep the float WAV here; empty = temp file, deleted
    std::string reportPath;       ///< also write the JSON report here
    std::string expectPath;       ///< compare against this golden
    std::string writeExpectPath;  ///< write a golden from this render
    uint32_t sampleRate = 48000;
    bool allowSilence = false;    ///< a silent render is a failure unless allowed
    bool allowMissing = false;    ///< missing assets/plugins are a failure unless allowed
};

int runHeadlessCheck(const HeadlessCheckOptions& options);

} // namespace Aestra
