// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §1.2: the timeline's selection exit.
//
// Owner reports: "Left-clicking outside the zone does nothing; you have to right-click." Nothing
// ever cleared the ruler zone. And "I cannot highlight in the grid": the marquee only existed
// behind the Multi-Select tool.
//
// Owner ruling (2026-09-23): a drag on empty grid marquees by default; one left click on empty
// grid, or Esc, clears BOTH the clip selection and the ruler zone.
//
// The ruler zone here is made the way a user makes it: a right-drag in the ruler, through
// onMouseEvent. Clip hit-rects are only filled while rendering, so the band's clip hit-test
// is not reachable here; its release rules are pinned headless in TimelineMarqueeTest.

#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackManagerUIMath.h"

#include "NUIThemeSystem.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

AestraUI::NUIMouseEvent mouse(AestraUI::NUIMouseEventType type, AestraUI::NUIMouseButton button, float x, float y,
                              AestraUI::NUIModifiers modifiers = AestraUI::NUIModifiers::None) {
    AestraUI::NUIMouseEvent e;
    e.type = type;
    e.button = button;
    e.position = {x, y};
    e.pressed = type == AestraUI::NUIMouseEventType::Down;
    e.released = type == AestraUI::NUIMouseEventType::Up;
    e.modifiers = modifiers;
    return e;
}

struct Fixture {
    std::shared_ptr<TrackManager> trackManager = std::make_shared<TrackManager>();
    std::shared_ptr<TrackManagerUI> ui = std::make_shared<TrackManagerUI>(trackManager);
    float gridX = 0.0f;

    Fixture() {
        ui->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 700.0f));
        const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        gridX = controls + kTimelineGridInsetX;
    }

    // A right-drag in the ruler: the ruler zone, as a user makes it.
    void makeRulerZone() {
        const float y = kTimelineMinimapHeight + 6.0f;
        using T = AestraUI::NUIMouseEventType;
        const auto right = AestraUI::NUIMouseButton::Right;
        ui->onMouseEvent(mouse(T::Down, right, gridX + 100.0f, y));
        ui->onMouseEvent(mouse(T::Move, right, gridX + 400.0f, y));
        ui->onMouseEvent(mouse(T::Up, right, gridX + 400.0f, y));
    }

    AestraUI::NUIMouseEvent gridPress(AestraUI::NUIModifiers modifiers = AestraUI::NUIModifiers::None) const {
        return mouse(AestraUI::NUIMouseEventType::Down, AestraUI::NUIMouseButton::Left, gridX + 250.0f,
                     kTimelineTimeBandHeight + 20.0f, modifiers);
    }
};

// The report: a left click on empty grid is the exit. It clears the zone, and is a click, not a band.
void testClickOnEmptyGridClearsTheRulerZone() {
    Fixture f;
    f.makeRulerZone();
    check(f.ui->hasRulerSelection(), "a right-drag in the ruler makes a zone (so the next check is not vacuous)");

    const auto press = f.gridPress();
    check(f.ui->beginGridMarquee(press), "a plain left press on empty grid starts the default marquee");
    check(!f.ui->hasRulerSelection(), "and clears the ruler zone");
    check(f.ui->isMarqueeActive(), "the marquee owns the gesture from the press");

    auto release = press;
    release.type = AestraUI::NUIMouseEventType::Up;
    release.pressed = false;
    release.released = true;
    f.ui->onMouseEvent(release);
    check(!f.ui->isMarqueeActive(), "the release ends it");
    check(f.ui->getSelectedTracks().empty(), "a click on the grid never selects a lane (the header rail owns that)");
}

// Shift/Ctrl extend a selection, so they must not start by wiping it.
void testModifiedPressKeepsTheZone() {
    Fixture f;
    f.makeRulerZone();
    check(f.ui->beginGridMarquee(f.gridPress(AestraUI::NUIModifiers::Shift)), "Shift+press on grid starts a band");
    check(f.ui->hasRulerSelection(), "Shift+press keeps the ruler zone: it adds, it does not replace");
}

// The default marquee belongs to the Select tool; the other tools keep their own grid gestures.
void testOtherToolsDoNotStartTheGridMarquee() {
    Fixture f;
    f.makeRulerZone();
    f.ui->setCurrentTool(PlaylistTool::Split);
    check(!f.ui->beginGridMarquee(f.gridPress()), "the Split tool's grid press is not a marquee");
    check(f.ui->hasRulerSelection(), "and it leaves the zone alone");
}

// Esc is the other exit. It used to require a clip or lane selection, so a lone zone survived it.
void testEscClearsALoneRulerZone() {
    Fixture f;
    f.makeRulerZone();
    check(f.ui->hasRulerSelection(), "zone made");
    AestraUI::NUIKeyEvent esc;
    esc.keyCode = AestraUI::NUIKeyCode::Escape;
    esc.pressed = true;
    check(f.ui->onKeyEvent(esc), "Esc with only a ruler zone is handled");
    check(!f.ui->hasRulerSelection(), "Esc clears the ruler zone");
}

} // namespace

int main() {
    testClickOnEmptyGridClearsTheRulerZone();
    testModifiedPressKeepsTheZone();
    testOtherToolsDoNotStartTheGridMarquee();
    testEscClearsALoneRulerZone();
    if (g_failures == 0) {
        std::cout << "Timeline grid selection tests passed\n";
        return 0;
    }
    std::cout << g_failures << " timeline grid selection test(s) failed\n";
    return 1;
}
