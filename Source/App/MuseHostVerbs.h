// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// AestraContent lives in the GLOBAL namespace, not in Aestra — the namespace
// block above its declaration in AestraContent.h closes before the class. A
// forward declaration inside namespace Aestra silently creates a second,
// unrelated type and makes the name ambiguous wherever both are visible.
class AestraContent;

namespace Aestra {

namespace Audio { class MuseService; }

/**
 * @brief Register the application's capabilities into a MuseService.
 *
 * This is the application side of the seam introduced in #612. MuseService lives
 * in AestraAudio and cannot reach the UI layer — the dependency deliberately runs
 * the other way — so the app hands it callables at startup and MuseService never
 * learns what an AestraContent is.
 *
 * A verb registered here is an APPLICATION CAPABILITY, not a UI gesture:
 * view.open takes the name of a workspace, not the coordinates of a button.
 * The first survives a UI redesign; the second would make Muse an accessibility
 * macro system welded to today's widget tree.
 *
 * Every verb here is main-thread affine. The socket server executes requests from
 * the frame pump, so that is satisfied in the app and correctly refused in
 * headless processes, which have no UI thread to satisfy it with.
 *
 * @param content borrowed, must outlive the service.
 */
void registerMuseHostVerbs(Audio::MuseService& service, ::AestraContent& content);

} // namespace Aestra
