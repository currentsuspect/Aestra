// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Panels/AuditionBounceTile.h"

#include <functional>
#include <string>
#include <utility>

namespace Aestra {

/// What the Audition hub needs from the app, handed over once: the open project, where references are kept, and
/// the two things the hub can ask for (a reference to import, the timeline bounced).
struct AuditionHooks {
    std::function<std::pair<std::string, std::string>()> project;  ///< name, path of the open project
    std::string libraryFile;                                       ///< where the imported references are listed
    std::function<void()> importReference;                         ///< pick a file, then call addAuditionReference
    std::function<BounceOutcome()> bounceNow;                      ///< bounce the open version; ok, or why not
};

} // namespace Aestra
