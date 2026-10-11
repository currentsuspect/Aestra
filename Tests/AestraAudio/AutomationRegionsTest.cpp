// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AutomationRegionsTest — V8-A7. An automation clip moves, copies, splits and deletes as one object.
//   1. the rules (implicit region, overlap refusal, overwrite, no move before 0, split cuts a segment);
//   2. splitting never changes what the curve plays, whatever its tension;
//   3. each edit is one undo step through the lane (EditAutomationCurvesCommand), and undo restores it;
//   4. clips survive save and load, a project without clips saves as before, and hostile data is dropped.

#include "Commands/CommandHistory.h"
#include "Commands/EditAutomationCurvesCommand.h"
#include "Core/AutomationRegions.h"
#include "Models/TrackManager.h"
#include "AutomationRegionJson.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace Aestra;
using namespace Aestra::Audio;
namespace R = Aestra::Audio::AutomationRegions;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

AutomationCurve makeCurve(std::initializer_list<std::pair<double, float>> pts, float tension = 0.0f) {
    AutomationCurve c("vol", AutomationTarget::Volume);
    for (auto [b, v] : pts)
        c.addPoint(b, v, 24000.0, tension);
    return c;
}

bool sameAudio(const AutomationCurve& a, const AutomationCurve& b, double from, double to) {
    for (double beat = from; beat <= to; beat += 0.125)
        if (std::fabs(a.getValueAtBeat(beat) - b.getValueAtBeat(beat)) > 1e-6f)
            return false;
    return true;
}
} // namespace

int main() {
    std::cout << "[0] create\n";
    {
        AutomationCurve empty("vol", AutomationTarget::Volume);
        const auto id = R::create(empty, 4.0, 8.0, 0.5f);
        check(id && empty.regions.size() == 1 && empty.regions[0].startBeat == 4.0 && empty.regions[0].endBeat == 8.0,
              "a clip is created over the span on an empty curve");
        check(empty.points.size() == 2 && empty.points[0].beat == 4.0 && empty.points[1].beat == 8.0 &&
                  empty.points[0].value == 0.5f && empty.points[1].value == 0.5f,
              "holding two points at the given value (a flat line)");
        check(sameAudio(empty, empty, 4.0, 8.0) && std::fabs(empty.getValueAtBeat(6.0) - 0.5f) < 1e-6f, "that plays flat");

        check(!R::create(empty, 6.0, 10.0, 0.9f) && empty.regions.size() == 1 && empty.points.size() == 2,
              "creating on top of another clip is refused and changes nothing");
        check(!R::create(empty, 8.0, 12.0, 0.9f), "touching another clip's end counts as on top of it (a clip never abuts)");
        check(!R::create(empty, -1.0, 3.0, 0.5f), "starting before beat 0 is refused");
        check(!R::create(empty, 2.0, 2.0, 0.5f), "an empty span is refused");
        check(R::create(empty, 12.5, 16.5, 0.25f).has_value() && empty.regions.size() == 2, "a clear spot takes a second clip");

        // A curve with loose points and no stored clips has one implicit clip over them: it counts.
        auto loose = makeCurve({{0, 0.2f}, {2, 0.8f}, {4, 0.4f}});
        check(!R::create(loose, 3.0, 7.0, 0.5f), "an implicit clip over existing points blocks it too");
        check(loose.points.size() == 3, "and the points are untouched");
        check(R::create(loose, 6.0, 10.0, 0.5f).has_value() && loose.regions.size() == 2, "beside it is fine (the implicit clip becomes stored)");
    }

    std::cout << "[1] rules\n";
    {
        auto c = makeCurve({{0, 0.2f}, {2, 0.8f}, {4, 0.4f}});
        check(c.regions.empty(), "a curve starts with no stored regions");
        check(R::move(c, 1, 8.0), "moving the implicit region works");
        check(c.regions.size() == 1 && c.regions[0].id == 1, "and stores it");
        check(c.regions[0].startBeat == 8.0 && c.regions[0].endBeat == 12.0, "span moved by 8 beats");
        check(c.points.size() == 3 && c.points[0].beat == 8.0 && c.points[2].beat == 12.0, "its points moved with it");
        check(!R::move(c, 1, -9.0), "moving before beat 0 is refused");
        check(c.regions[0].startBeat == 8.0, "and changes nothing");

        auto copied = R::copy(c, 1, 8.0);
        check(copied && c.regions.size() == 2, "a copy is a second region");
        check(c.points.size() == 6, "with its own points");
        check(!R::copy(c, 1, 8.0), "copying onto another region is refused");
        check(!R::copy(c, 1, 2.0), "copying onto itself is refused");
        check(!R::move(c, 1, 8.0), "moving onto another region is refused");

        // A loose point under the destination is overwritten.
        c.addPoint(30.0, 0.9f, 24000.0);
        R::materialize(c);
        check(R::copy(c, 1, 22.0).has_value(), "copy to beat 30 lands on a loose point");
        bool loose = false;
        for (const auto& p : c.points)
            loose |= (std::fabs(p.beat - 30.0) < 1e-9 && std::fabs(p.value - 0.9f) < 1e-6f);
        check(!loose, "the loose point under it was replaced");
    }

    std::cout << "[2] splitting changes nothing you hear\n";
    for (float tension : {0.0f, 0.6f, -0.6f}) {
        auto c = makeCurve({{0, 0.1f}, {4, 0.9f}, {8, 0.3f}}, tension);
        const auto before = c;
        const auto right = R::split(c, 1, 2.0);
        check(right && c.regions.size() == 2, "split at beat 2 gives two regions (tension " + std::to_string(tension) + ")");
        check(c.points.size() == 3, "no point is inserted");
        check(c.regions[0].endBeat == 0.0 && c.regions[1].startBeat == 4.0, "the cut falls between the two points around beat 2");
        check(sameAudio(before, c, 0.0, 10.0), "the curve plays the same");
        check(!R::split(c, c.regions[0].id, 0.0), "no cut at the very start");
    }
    {
        auto c = makeCurve({{0, 0.1f}, {4, 0.9f}, {8, 0.3f}});
        auto right = R::split(c, 1, 4.0);
        check(right && c.regions[0].endBeat == 0.0 && c.regions[1].startBeat == 4.0, "a cut on a point's beat goes before it");
        auto d = makeCurve({{0, 0.1f}});
        check(!R::split(d, 1, 1.0), "a one-point region cannot be split");
    }

    std::cout << "[3] delete, and tidying after point edits\n";
    {
        auto c = makeCurve({{0, 0.1f}, {4, 0.9f}, {8, 0.3f}});
        R::split(c, 1, 5.0);
        check(R::remove(c, c.regions[1].id), "delete the right part");
        check(c.regions.size() == 1 && c.points.size() == 2, "its points go with it");
        c.points[1].beat = 3.0; // dragged in
        R::retighten(c);
        check(c.regions[0].endBeat == 3.0, "regions follow their points after an edit");
        c.points.clear();
        R::retighten(c);
        check(c.regions.empty(), "a region with no points is dropped");
    }

    std::cout << "[4] one undo step through the lane\n";
    {
        TrackManager tm;
        auto& pm = tm.getPlaylistModel();
        const auto laneId = pm.createLane("A");
        pm.getLane(laneId)->automationCurves = {makeCurve({{0, 0.2f}, {2, 0.8f}, {4, 0.4f}})};
        const auto before = pm.getLane(laneId)->automationCurves;
        auto after = before;
        R::move(after[0], 1, 4.0);
        pm.getLane(laneId)->automationCurves = after; // applied, as the gesture does
        tm.getCommandHistory().pushAndExecute(
            std::make_shared<EditAutomationCurvesCommand>(tm, laneId, before, after, "Move Automation Clip"));
        check(pm.getLane(laneId)->automationCurves[0].points[0].beat == 4.0, "moved");
        tm.getCommandHistory().undo();
        check(automationCurvesEqual(pm.getLane(laneId)->automationCurves, before), "undo restores points and regions exactly");
        tm.getCommandHistory().redo();
        check(pm.getLane(laneId)->automationCurves[0].regions.size() == 1, "redo moves it again");
    }

    std::cout << "[5] project format\n";
    {
        auto plain = makeCurve({{0, 0.2f}, {4, 0.4f}});
        JSON pj = JSON::object();
        writeAutomationRegions(pj, plain);
        check(!pj.has("regions"), "a curve without clips writes no key: such projects save as before");

        auto c = makeCurve({{0, 0.2f}, {2, 0.8f}, {4, 0.4f}, {10, 0.5f}, {12, 0.6f}});
        R::split(c, 1, 5.0);
        JSON cj = JSON::object();
        writeAutomationRegions(cj, c);
        AutomationCurve back("vol", AutomationTarget::Volume);
        readAutomationRegions(JSON::parse(cj.toString()), back);
        check(automationRegionsEqual(back.regions, c.regions) && back.regions.size() == 2, "regions survive write and read");

        const char* hostile = R"({"regions":[
            {"id":"1","start":0,"end":4},
            {"id":"1","start":6,"end":8},
            {"id":"-2","start":6,"end":8},
            {"id":"3","start":3,"end":5},
            {"id":"4","start":9,"end":8},
            {"id":"5","start":-1,"end":2},
            {"id":"6","start":20,"end":30},
            {"id":7,"start":40,"end":50},
            {"id":"0","start":60,"end":70}]})";
        AutomationCurve h("vol", AutomationTarget::Volume);
        readAutomationRegions(JSON::parse(hostile), h);
        check(h.regions.size() == 2 && h.regions[0].id == 1 && h.regions[1].id == 6,
              "duplicate, malformed, overlapping, reversed, negative and zero-id regions are dropped (kept " +
                  std::to_string(h.regions.size()) + ")");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
