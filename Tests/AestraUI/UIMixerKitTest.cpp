// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-K1 / V81-M1 kit pieces behind the new mixer strip (FD-28): the one shared dB scale, the draggable
// parameter bar (the strip's pan, the inspector's trim and width), the footer's gain and peak, the send list's
// level text, and the fader's overlay mode (cap across the meters, on the same scale as the meters).

#include "MixerScale.h"
#include "UIMixerFader.h"
#include "UIMixerFooter.h"
#include "UIMixerSendList.h"
#include "UIParamBar.h"

#include <cmath>
#include <iostream>
#include <chrono>
#include <string>
#include <thread>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

NUIMouseEvent mouse(NUIMouseEventType type, float x, float y, NUIMouseButton button, bool pressed, bool released) {
    NUIMouseEvent e;
    e.type = type;
    e.position = {x, y};
    e.button = button;
    e.pressed = pressed;
    e.released = released;
    return e;
}
// Two presses within 400 ms read as a double click; separate gestures wait it out.
void pauseBetweenGestures() { std::this_thread::sleep_for(std::chrono::milliseconds(450)); }
NUIMouseEvent down(float x, float y) { return mouse(NUIMouseEventType::Down, x, y, NUIMouseButton::Left, true, false); }
NUIMouseEvent up(float x, float y) { return mouse(NUIMouseEventType::Up, x, y, NUIMouseButton::Left, false, true); }
NUIMouseEvent move(float x, float y) { return mouse(NUIMouseEventType::Move, x, y, NUIMouseButton::None, false, false); }

} // namespace

int main() {
    std::cout << "[1] the shared dB scale\n";
    {
        check(MixerScale::norm(MixerScale::kFloorDb) == 0.0f && MixerScale::norm(MixerScale::kTopDb) == 1.0f,
              "-60 dB is the bottom, +6 dB the top");
        check(MixerScale::norm(-120.0f) == 0.0f && MixerScale::norm(20.0f) == 1.0f, "outside the range clamps");
        bool monotone = true;
        float last = -1.0f;
        for (float db = -60.0f; db <= 6.0f; db += 0.5f) {
            const float n = MixerScale::norm(db);
            monotone = monotone && n > last - 1e-6f;
            last = n;
        }
        check(monotone, "louder is always higher");
        bool roundTrip = true;
        for (float db = -59.0f; db <= 5.5f; db += 0.7f)
            roundTrip = roundTrip && std::fabs(MixerScale::db(MixerScale::norm(db)) - db) < 0.02f;
        check(roundTrip, "norm() and db() are inverses");
        check(MixerScale::norm(0.0f) > MixerScale::norm(-6.0f) + 0.1f, "the 6 dB below 0 take real height (mixing happens there)");
    }

    std::cout << "[2] the parameter bar drags, commits and resets\n";
    {
        UIParamBar bar(UIParamBar::Look::Row);
        bar.setBounds({100, 100, 200, 22});
        bar.setLabel("Trim");
        bar.setRange(-24.0f, 24.0f, 0.0f);
        float live = 99.0f, committed = 99.0f;
        int commits = 0;
        bar.onValueChanged = [&](float v) { live = v; };
        bar.onValueCommitted = [&](float v) { committed = v; ++commits; };
        bar.onMouseEvent(down(200, 111));
        check(bar.isDragging(), "a press starts a drag");
        check(std::fabs(bar.value()) < 1e-4f, "and does not move the value (no jump on click)");
        bar.onMouseEvent(move(230, 111));
        check(bar.value() > 0.0f && std::fabs(live - bar.value()) < 1e-5f, "dragging right raises it, live");
        const float afterRight = bar.value();
        bar.onMouseEvent(move(170, 111));
        check(bar.value() < afterRight, "dragging left lowers it");
        bar.onMouseEvent(up(170, 111));
        check(commits == 1 && std::fabs(committed - bar.value()) < 1e-5f, "release commits once");
        bar.setValue(10.0f);
        check(std::fabs(bar.value() - 10.0f) < 1e-5f, "the model can set it when idle");
        pauseBetweenGestures();
        bar.onMouseEvent(down(200, 111));
        bar.setValue(-5.0f);
        check(std::fabs(bar.value() - 10.0f) < 1e-5f, "but not under the user's finger");
        bar.onMouseEvent(up(200, 111));

        pauseBetweenGestures();
        bar.setValue(7.0f);
        bar.onMouseEvent(down(150, 111));
        bar.onMouseEvent(up(150, 111));
        bar.onMouseEvent(down(150, 111));
        check(std::fabs(bar.value()) < 1e-5f, "a double click resets to the default");
        bar.onMouseEvent(up(150, 111));

        bar.setValue(100.0f);
        check(bar.value() <= 24.0f, "values clamp to the range");
        bar.setValue(std::nanf(""));
        check(std::isfinite(bar.value()), "a NaN from the model is ignored");
    }

    std::cout << "[3] footer: gain on the left, peak on the right\n";
    {
        check(UIMixerFooter::formatDb(-4.5f) == "\xE2\x88\x92" "4.5", "negative gain keeps its minus sign");
        check(UIMixerFooter::formatDb(0.0f) == "0.0" && UIMixerFooter::formatDb(0.01f) == "0.0", "zero reads 0.0");
        check(UIMixerFooter::formatDb(-90.0f) == "\xE2\x88\x92\xE2\x88\x9E", "the floor reads minus infinity");
        UIMixerFooter footer;
        footer.setBounds({0, 0, 76, 18});
        footer.setGainDb(-65.0f);
        check(footer.gainText() == "\xE2\x88\x92\xE2\x88\x9E", "a fader pulled to its floor reads minus infinity");
        footer.setPeakDb(-1.2f, false);
        check(!footer.clipped() && footer.peakText() == "\xE2\x88\x92" "1.2", "peak text");
        footer.setPeakDb(0.8f, true);
        check(footer.clipped(), "a clip is flagged (it draws as a red block)");
        bool reset = false;
        footer.onResetPeakRequested = [&] { reset = true; };
        footer.onMouseEvent(down(10, 9));
        check(!reset, "a click on the gain side does nothing");
        footer.onMouseEvent(down(60, 9));
        check(reset, "a click on the peak asks for a reset");
    }

    std::cout << "[4] send list level text\n";
    {
        check(UIMixerSendList::formatLevel(0.0f) == "0", "unity reads 0");
        check(UIMixerSendList::formatLevel(-18.2f) == "\xE2\x88\x92" "18", "above 10 dB it drops the decimal");
        check(UIMixerSendList::formatLevel(-6.5f) == "\xE2\x88\x92" "6.5", "below 10 dB it keeps one");
        check(UIMixerSendList::formatLevel(-120.0f) == "\xE2\x88\x92\xE2\x88\x9E", "silence reads minus infinity");
        check(UIMixerSendList::heightForRows(0) == 0.0f, "no sends ask for no height");
        check(UIMixerSendList::heightForRows(9) == UIMixerSendList::heightForRows(UIMixerSendList::kMaxRows), "the list caps its rows");
        UIMixerSendList list;
        list.setBounds({0, 0, 76, UIMixerSendList::heightForRows(2)});
        std::vector<UIMixerSendList::Row> rows(3);
        rows[0].name = "Reverb";
        rows[1].name = "Delay";
        rows[2].name = "Bus";
        list.setRows(rows);
        check(list.rows().size() == 3 && list.visibleRowCapacity() == 2, "three sends in room for two fold the rest");
    }

    std::cout << "[5] the overlay fader shares the meters' scale\n";
    {
        UIMixerFader fader;
        fader.setRangeDb(-90.0f, 6.0f);
        fader.setDefaultDb(0.0f);
        fader.setOverlay(true, 26.0f);
        fader.setBounds({100, 100, 14, 400}); // track runs from y=109 to y=491
        fader.setValueDb(0.0f);
        float seen = 99.0f;
        fader.onValueChanged = [&](float db) { seen = db; };
        // A click at the height 0 dB occupies on the shared scale lands on 0 dB.
        const float trackTop = 109.0f, trackBottom = 491.0f;
        const float y0 = trackBottom - MixerScale::norm(0.0f) * (trackBottom - trackTop);
        fader.setValueDb(-30.0f);
        fader.onMouseEvent(down(107, y0));
        check(std::fabs(fader.getValueDb()) < 0.3f, "clicking where the scale puts 0 dB sets 0 dB (" + std::to_string(fader.getValueDb()) + ")");
        fader.onMouseEvent(up(107, y0));
        pauseBetweenGestures();
        const float yMinus12 = trackBottom - MixerScale::norm(-12.0f) * (trackBottom - trackTop);
        fader.onMouseEvent(down(107, yMinus12));
        check(std::fabs(fader.getValueDb() - (-12.0f)) < 0.3f, "and -12 dB lands on -12 dB");
        fader.onMouseEvent(up(107, yMinus12));
        pauseBetweenGestures();
        fader.onMouseEvent(down(107, trackBottom + 8.0f));
        check(fader.getValueDb() <= -60.0f, "the very bottom is minus infinity");
        fader.onMouseEvent(up(107, trackBottom + 8.0f));
        (void)seen;
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
