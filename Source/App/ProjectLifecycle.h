// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Project lifecycle: the operations behind File > New / Open / Save, plus the
// Muse host verbs that expose them.
//
// Two reasons this is its own file rather than more of AestraApp.cpp:
//
//   * AestraApp.cpp is pinned at its exact line count by
//     Tests/Guards/file_size_baseline.txt, so a verb that needs the app's
//     private save/load path cannot be added there. A member function may be
//     defined in any translation unit, so the verbs live here and AestraApp.cpp
//     only loses the inline menu lambdas they replaced.
//   * The menu items and the verbs must run the SAME code. "New Project" as an
//     inline lambda was a dozen lines of stop / cleanup / reset / re-arm
//     autosave; a second copy for the agent would drift from the first the first
//     time either changed. Both now call AestraApp::createNewProject().
//
// A verb here is an application capability, not a UI gesture: `project.save`
// takes a path, not "click Save As and type into the dialog". An agent cannot
// drive a native file chooser, and a verb that needed one would be refused
// headlessly for a reason that has nothing to do with the request.

#include <memory>
#include <string>

class AestraContent;

/**
 * @brief Point a track manager's recording at the project that owns it.
 *
 * Global, like AestraApp and AestraContent, because it was already a free
 * function in AestraApp.cpp — it was file-local only because that file has no
 * other translation unit. Declared here so the project-lifecycle code can reach
 * it; defined in ProjectLifecycle.cpp.
 */
void syncRecordingProjectPath(const std::shared_ptr<AestraContent>& content,
                              const std::string& projectPath);
