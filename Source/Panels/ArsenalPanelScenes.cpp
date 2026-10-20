// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V82-AR1 (FD-52): the panel's side of the pattern menu. The menu and its rules are ArsenalSceneMenu; this is what
// the panel does once a verb has changed the pattern list. Its own file because ArsenalPanel.cpp is size-pinned.
#include "ArsenalPanel.h"

#include "PatternBrowserPanel.h"

#include <functional>
#include <string>

namespace Aestra {
namespace Audio {

void ArsenalPanel::showSceneMenu(uint64_t patternId, const AestraUI::NUIPoint& at) {
    if (!m_trackManager) return;
    ArsenalSceneMenu::Request request;
    request.tracks = m_trackManager.get();
    request.owner = this;
    request.id = patternId;
    request.activeId = m_activePatternID.value;
    request.at = at;
    request.chip = m_scenes.chipRectOf(patternId);
    request.scenes = m_scenes.scenes();
    request.changed = [this](uint64_t select) { sceneMenuChanged(select); };
    m_sceneMenu.show(request);
}

void ArsenalPanel::reconcilePatterns() {
    if (!m_trackManager) return;
    auto& patterns = m_trackManager->getPatternManager();
    size_t signature = 0;
    uint64_t firstMidi = 0;
    patterns.forEachPattern([&](const PatternSource& p) {
        signature += std::hash<std::string>{}(p.name) * 31u + static_cast<size_t>(p.id.value); // order-free: the store is unordered
        if (p.isMidi() && (firstMidi == 0 || p.id.value < firstMidi)) firstMidi = p.id.value;
    });
    if (!patterns.getPattern(m_activePatternID) && firstMidi != 0) {
        // The pattern on show is gone (undoing a duplicate, a delete elsewhere): show the first one rather than nothing.
        m_activePatternID = PatternID{firstMidi};
        m_trackManager->preparePatternForArsenal(m_activePatternID);
        if (m_onActivePatternChanged) m_onActivePatternChanged(m_activePatternID);
    }
    if (signature != m_patternSignature) { // renamed, added or removed: Ctrl+Z reaches here without the browser knowing
        m_patternSignature = signature;
        if (m_patternBrowser) m_patternBrowser->refreshPatterns();
        repaint();
    }
}

void ArsenalPanel::sceneMenuChanged(uint64_t select) {
    if (m_patternBrowser) m_patternBrowser->refreshPatterns();
    // After Duplicate the copy becomes active (as in the browser); after deleting the active pattern its neighbour does.
    if (select != 0) setActivePattern(PatternID{select});
    refreshUnits();
    repaint();
}

} // namespace Audio
} // namespace Aestra
