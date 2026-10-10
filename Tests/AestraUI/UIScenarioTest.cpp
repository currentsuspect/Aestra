// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-G1: UI headless scenarios, modelled on the audio side's
// Tests/headless_scenarios.json. Each scenario in Tests/ui_scenarios.json builds a
// real surface (today: the timeline, a real TrackManagerUI over a real
// TrackManager), drives it with real mouse events through the canonical dispatch
// entry point, and checks what a user would see: where a clip ended up, whether
// the gesture is one undo step, whether layout raised a problem at a window size.
//
// A scenario is data, so a regression report becomes a JSON entry, not a new test
// program. The runner is strict: an unknown surface, step or expectation key fails
// the scenario, because a check that silently does not run reports success.

#include "../Support/NullRenderer.h"
#include "AutomationLaneEditing.h"
#include "AestraJSON.h"
#include "Layout/NUILayoutExplain.h"
#include "NUIComponent.h"
#include "NUIThemeSystem.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackManagerUIMath.h"
#include "TrackUIComponent.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef AESTRA_UI_SCENARIO_FILE
#error "AESTRA_UI_SCENARIO_FILE must name Tests/ui_scenarios.json"
#endif

using Aestra::JSON;
using namespace Aestra::Audio;

namespace {

/** @brief One scenario's run: failures are collected with the step that raised them. */
struct Run {
    std::string name;
    std::vector<std::string> failures;
    int step = -1;

    void fail(const std::string& what) {
        failures.push_back((step >= 0 ? "step " + std::to_string(step) + ": " : std::string("setup: ")) + what);
    }
};

std::string keysOf(const JSON& object) {
    std::string out;
    for (const auto& [key, _] : object.asObject()) out += (out.empty() ? "" : ", ") + key;
    return out;
}

// Rejects keys a scenario author may have misspelled: the check they meant would not run.
bool onlyKeys(Run& run, const JSON& object, const std::set<std::string>& allowed, const std::string& where) {
    bool ok = true;
    for (const auto& [key, _] : object.asObject()) {
        if (!allowed.count(key)) {
            run.fail(where + ": unknown key \"" + key + "\"");
            ok = false;
        }
    }
    return ok;
}

bool parseTarget(Run& run, const JSON& name, AutomationTarget& out) {
    const std::string n = name.asString();
    if (n == "volume") {
        out = AutomationTarget::Volume;
    } else if (n == "pan") {
        out = AutomationTarget::Pan;
    } else {
        run.fail("unknown automation target \"" + n + "\" (volume, pan)");
        return false;
    }
    return true;
}

/** @brief The timeline surface: a real TrackManagerUI in a root, as the app window holds it. */
class TimelineSurface {
public:
    bool build(Run& run, const JSON& setup) {
        if (!onlyKeys(run, setup, {"size", "lanes", "clips", "automation", "mode", "snap"}, "setup")) return false;
        trackManager_ = std::make_shared<TrackManager>();
        trackManager_->setCommandSink([](const AudioQueueCommand&) { return true; });
        auto& playlist = trackManager_->getPlaylistModel();
        auto& patterns = trackManager_->getPatternManager();

        const int laneCount = setup.has("lanes") ? static_cast<int>(setup["lanes"].asNumber()) : 1;
        for (int i = 0; i < laneCount; ++i) {
            lanes_.push_back(playlist.createLane("Lane " + std::to_string(i + 1)));
        }
        if (setup.has("clips")) {
            for (const auto& c : setup["clips"].asArray()) {
                if (!onlyKeys(run, c, {"lane", "start", "length"}, "setup.clips")) return false;
                const int lane = static_cast<int>(c["lane"].asNumber());
                if (lane < 0 || lane >= laneCount) {
                    run.fail("setup.clips: lane " + std::to_string(lane) + " does not exist");
                    return false;
                }
                const double length = c.has("length") ? c["length"].asNumber() : 4.0;
                const PatternID pattern =
                    patterns.createMidiPattern("Clip " + std::to_string(clips_.size() + 1), length, MidiPayload{});
                const ClipInstanceID id = playlist.addClipFromPattern(lanes_[lane], pattern, c["start"].asNumber(), length);
                if (!id.isValid()) {
                    run.fail("setup.clips: could not place clip " + std::to_string(clips_.size()));
                    return false;
                }
                clips_.push_back(id);
            }
        }

        if (setup.has("automation")) {
            // Curves as a project would hold them: one per target on a lane.
            for (const auto& a : setup["automation"].asArray()) {
                if (!onlyKeys(run, a, {"lane", "target", "points"}, "setup.automation")) return false;
                AutomationTarget target;
                if (!parseTarget(run, a["target"], target)) return false;
                auto* lane = playlist.getLane(lanes_.at(static_cast<size_t>(a["lane"].asNumber())));
                AutomationCurve curve = makeAutomationCurve(target);
                for (const auto& p : a["points"].asArray()) {
                    curve.addPoint(p.asArray()[0].asNumber(), static_cast<float>(p.asArray()[1].asNumber()), 24000.0);
                }
                lane->automationCurves.push_back(std::move(curve));
            }
        }

        manager_ = std::make_shared<TrackManagerUI>(trackManager_);
        root_ = std::make_shared<AestraUI::NUIComponent>();
        root_->addChild(manager_);
        float w = 1400.0f, h = 700.0f;
        if (setup.has("size")) {
            w = static_cast<float>(setup["size"].asArray()[0].asNumber());
            h = static_cast<float>(setup["size"].asArray()[1].asNumber());
        }
        resize(w, h);
        manager_->refreshTracks();
        if (setup.has("snap")) {
            // The timeline's snap grid, as the toolbar sets it.
            const std::string snap = setup["snap"].asString();
            const std::map<std::string, AestraUI::SnapGrid> grids{{"bar", AestraUI::SnapGrid::Bar},
                                                                  {"beat", AestraUI::SnapGrid::Beat},
                                                                  {"half", AestraUI::SnapGrid::Half},
                                                                  {"quarter", AestraUI::SnapGrid::Quarter},
                                                                  {"none", AestraUI::SnapGrid::None}};
            const auto it = grids.find(snap);
            if (it == grids.end()) {
                run.fail("setup.snap: unknown grid \"" + snap + "\" (bar, beat, half, quarter, none)");
                return false;
            }
            manager_->setSnapSetting(it->second);
        }
        if (setup.has("mode")) {
            const std::string mode = setup["mode"].asString();
            if (mode == "automation") {
                manager_->setPlaylistMode(PlaylistMode::Automation);
            } else if (mode != "clips") {
                run.fail("setup.mode: unknown mode \"" + mode + "\" (clips, automation)");
                return false;
            }
        }
        paint();
        return true;
    }

    void resize(float w, float h) {
        root_->setBounds(AestraUI::NUIRect(0.0f, 0.0f, w, h));
        manager_->setBounds(AestraUI::NUIRect(0.0f, 0.0f, w, h));
        paint();
    }

    // A frame: the paint pass also publishes clip geometry that hit-testing reads.
    void paint() {
        Aestra::Testing::NullRenderer renderer;
        root_->onRender(renderer);
        for (const auto& lane : laneComponents()) lane->renderStatic(renderer);
    }

    std::vector<std::shared_ptr<TrackUIComponent>> laneComponents() const {
        std::vector<std::shared_ptr<TrackUIComponent>> out;
        collectLanes(*manager_, out);
        return out;
    }

    std::shared_ptr<TrackUIComponent> laneComponentFor(int lane) const {
        for (const auto& c : laneComponents()) {
            if (c->getLaneId() == lanes_.at(lane)) return c;
        }
        return nullptr;
    }

    // A point named the way a user thinks of it: on a clip, or at a beat on a lane.
    bool resolvePoint(Run& run, const JSON& at, AestraUI::NUIPoint& out) {
        if (!onlyKeys(run, at, {"clip", "fraction", "lane", "beat", "value", "target", "x", "y"}, "at")) return false;
        if (at.has("x") && at.has("y")) {
            out = {static_cast<float>(at["x"].asNumber()), static_cast<float>(at["y"].asNumber())};
            return true;
        }
        if (at.has("clip")) {
            const size_t i = static_cast<size_t>(at["clip"].asNumber());
            if (i >= clips_.size()) {
                run.fail("at.clip " + std::to_string(i) + " does not exist");
                return false;
            }
            const auto* clip = trackManager_->getPlaylistModel().getClip(clips_[i]);
            const int lane = laneIndexOf(trackManager_->getPlaylistModel().findClipLane(clips_[i]));
            if (!clip || lane < 0) {
                run.fail("at.clip " + std::to_string(i) + " is not on any lane");
                return false;
            }
            const double fraction = at.has("fraction") ? at["fraction"].asNumber() : 0.5;
            return pointAt(run, lane, clip->startBeat + clip->durationBeats * fraction, out);
        }
        if (at.has("lane") && at.has("beat")) {
            if (!pointAt(run, static_cast<int>(at["lane"].asNumber()), at["beat"].asNumber(), out)) return false;
            if (at.has("value")) {
                // An automation value, drawn on its target's range in the lane row.
                AutomationTarget target = AutomationTarget::Volume;
                if (at.has("target") && !parseTarget(run, at["target"], target)) return false;
                const auto b = laneComponentFor(static_cast<int>(at["lane"].asNumber()))->getBounds();
                out.y = b.y + (1.0f - static_cast<float>(automationHeightOfValue(target, at["value"].asNumber()))) *
                                  b.height;
            }
            return true;
        }
        run.fail("at: needs {clip}, {lane, beat} or {x, y}");
        return false;
    }

    bool pointAt(Run& run, int lane, double beat, AestraUI::NUIPoint& out) {
        if (lane < 0 || lane >= static_cast<int>(lanes_.size())) {
            run.fail("lane " + std::to_string(lane) + " does not exist");
            return false;
        }
        const auto component = laneComponentFor(lane);
        if (!component || !component->isVisible()) {
            run.fail("lane " + std::to_string(lane) + " has no visible row to point at");
            return false;
        }
        const auto b = component->getBounds();
        const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        const float gridX = manager_->getBounds().x + controls + kTimelineGridInsetX;
        out = {gridX + timelineBeatToGridOffset(beat, manager_->getHorizontalScroll(), manager_->getHorizontalZoom()),
               b.y + b.height * 0.5f};
        return true;
    }

    void mouse(AestraUI::NUIMouseEventType type, const AestraUI::NUIPoint& at, AestraUI::NUIMouseButton button,
               AestraUI::NUIModifiers modifiers) {
        // Exactly what NUIPlatformBridge sends: motion carries no button, a press
        // or release carries its button and pressed/released.
        AestraUI::NUIMouseEvent e;
        e.type = type;
        e.position = at;
        e.button = type == AestraUI::NUIMouseEventType::Move ? AestraUI::NUIMouseButton::None : button;
        e.modifiers = modifiers;
        e.pressed = type == AestraUI::NUIMouseEventType::Down;
        e.released = type == AestraUI::NUIMouseEventType::Up;
        const bool handled = AestraUI::NUIComponent::dispatchMouseEvent(root_.get(), e);
        if (std::getenv("AESTRA_UI_SCENARIO_DEBUG")) {
            // Set AESTRA_UI_SCENARIO_DEBUG=1 to see where a gesture went when writing a scenario.
            std::cout << "  mouse " << static_cast<int>(type) << " at " << at.x << ',' << at.y
                      << (handled ? " handled" : " NOT handled") << '\n';
            for (const auto& lane : laneComponents()) {
                const auto lb = lane->getBounds();
                std::cout << "    lane " << laneIndexOf(lane->getLaneId()) << " @" << static_cast<const void*>(lane.get()) << " row " << lb.x << ',' << lb.y << ' '
                          << lb.width << 'x' << lb.height << (lane->getSelectedClipId().isValid() ? " [clip selected]" : "")
                          << (lane->isTrimming() ? " [trimming]" : "");
                for (const auto& [id, cb] : lane->getAllClipBounds()) {
                    std::cout << "  clip " << cb.x << ',' << cb.y << ' ' << cb.width << 'x' << cb.height;
                }
                std::cout << '\n';
            }
        }
        paint();
    }

    int laneIndexOf(const PlaylistLaneID& id) const {
        for (size_t i = 0; i < lanes_.size(); ++i) {
            if (lanes_[i] == id) return static_cast<int>(i);
        }
        return -1;
    }

    TrackManager& model() { return *trackManager_; }
    TrackManagerUI& ui() { return *manager_; }
    const std::vector<ClipInstanceID>& clips() const { return clips_; }
    const std::vector<PlaylistLaneID>& lanes() const { return lanes_; }

private:
    static void collectLanes(const AestraUI::NUIComponent& node, std::vector<std::shared_ptr<TrackUIComponent>>& out) {
        for (const auto& child : node.getChildren()) {
            if (!child) continue;
            if (auto lane = std::dynamic_pointer_cast<TrackUIComponent>(child)) out.push_back(lane);
            collectLanes(*child, out);
        }
    }

    std::shared_ptr<TrackManager> trackManager_;
    std::shared_ptr<AestraUI::NUIComponent> root_;
    std::shared_ptr<TrackManagerUI> manager_;
    std::vector<PlaylistLaneID> lanes_;
    std::vector<ClipInstanceID> clips_;
};

size_t undoDepth(TrackManager& m) { return m.getCommandHistory().getUndoStack().size(); }

void checkExpect(Run& run, TimelineSurface& s, const JSON& expect, size_t undoBaseline) {
    if (!onlyKeys(run, expect, {"clip", "undoDepth", "layoutProblems", "lanesInside", "automation", "editedTarget"},
                  "expect"))
        return;
    if (expect.has("automation")) {
        const JSON& a = expect["automation"];
        AutomationTarget target;
        if (onlyKeys(run, a, {"lane", "target", "points", "selected"}, "expect.automation") &&
            parseTarget(run, a["target"], target)) {
            const int laneIndex = static_cast<int>(a["lane"].asNumber());
            const auto* lane = s.model().getPlaylistModel().getLane(s.lanes().at(static_cast<size_t>(laneIndex)));
            const int ci = lane ? automationCurveIndexFor(lane->automationCurves, target) : -1;
            const auto& want = a["points"].asArray();
            std::ostringstream got;
            bool same = ci >= 0;
            if (ci >= 0) {
                const auto& pts = lane->automationCurves[static_cast<size_t>(ci)].getPoints();
                same = pts.size() == want.size();
                for (size_t i = 0; i < pts.size(); ++i) {
                    got << " [" << pts[i].beat << ", " << pts[i].value << "]";
                    if (same && (std::fabs(pts[i].beat - want[i].asArray()[0].asNumber()) > 1e-3 ||
                                 std::fabs(pts[i].value - want[i].asArray()[1].asNumber()) > 1e-3)) {
                        same = false;
                    }
                }
            }
            if (ci >= 0 && a.has("selected")) {
                // Indices of the selected points, in order (V8-A4).
                std::vector<int> selected;
                const auto& pts = lane->automationCurves[static_cast<size_t>(ci)].getPoints();
                for (size_t i = 0; i < pts.size(); ++i) {
                    if (pts[i].selected) selected.push_back(static_cast<int>(i));
                }
                std::vector<int> wantSelected;
                for (const auto& i : a["selected"].asArray()) wantSelected.push_back(static_cast<int>(i.asNumber()));
                if (selected != wantSelected) {
                    std::string got;
                    for (int i : selected) got += " " + std::to_string(i);
                    run.fail("selected points are [" + got + " ], expected " + a["selected"].toString(0));
                }
            }
            if (!a.has("points")) same = ci >= 0;
            if (!same) {
                run.fail("lane " + std::to_string(laneIndex) + " " + a["target"].asString() + " curve is" +
                         (ci < 0 ? std::string(" missing") : got.str()) + ", expected " + a["points"].toString(0));
            }
        }
    }
    if (expect.has("editedTarget")) {
        const JSON& e = expect["editedTarget"];
        AutomationTarget target;
        if (onlyKeys(run, e, {"lane", "target"}, "expect.editedTarget") && parseTarget(run, e["target"], target)) {
            const auto lane = s.laneComponentFor(static_cast<int>(e["lane"].asNumber()));
            if (!lane || lane->getEditedAutomationTarget() != target) {
                run.fail("lane " + std::to_string(static_cast<int>(e["lane"].asNumber())) + " does not edit " +
                         e["target"].asString());
            }
        }
    }
    if (expect.has("clip")) {
        const JSON& c = expect["clip"];
        if (!onlyKeys(run, c, {"index", "start", "lane"}, "expect.clip")) return;
        const size_t i = static_cast<size_t>(c["index"].asNumber());
        if (i >= s.clips().size()) {
            run.fail("expect.clip.index " + std::to_string(i) + " does not exist");
            return;
        }
        const auto* clip = s.model().getPlaylistModel().getClip(s.clips()[i]);
        if (!clip) {
            run.fail("clip " + std::to_string(i) + " no longer exists");
            return;
        }
        if (c.has("start") && std::fabs(clip->startBeat - c["start"].asNumber()) > 1e-6) {
            std::ostringstream m;
            m << "clip " << i << " starts at beat " << clip->startBeat << ", expected " << c["start"].asNumber();
            run.fail(m.str());
        }
        if (c.has("lane")) {
            const int lane = s.laneIndexOf(s.model().getPlaylistModel().findClipLane(s.clips()[i]));
            if (lane != static_cast<int>(c["lane"].asNumber())) {
                run.fail("clip " + std::to_string(i) + " is on lane " + std::to_string(lane) + ", expected " +
                         std::to_string(static_cast<int>(c["lane"].asNumber())));
            }
        }
    }
    if (expect.has("undoDepth")) {
        const size_t depth = undoDepth(s.model()) - undoBaseline;
        if (depth != static_cast<size_t>(expect["undoDepth"].asNumber())) {
            run.fail("undo depth is " + std::to_string(depth) + ", expected " +
                     std::to_string(static_cast<int>(expect["undoDepth"].asNumber())));
        }
    }
    if (expect.has("layoutProblems")) {
        const auto* recorder = AestraUI::Layout::layoutRecorderFor("timeline");
        if (!recorder) {
            run.fail("layoutProblems: the timeline layout recorder is off, so nothing was checked");
        } else {
            const auto problems = recorder->problems();
            if (problems.size() != static_cast<size_t>(expect["layoutProblems"].asNumber())) {
                std::string list;
                for (const auto& p : problems) list += " [" + p.step + ": " + p.what + "]";
                run.fail(std::to_string(problems.size()) + " layout problem(s):" + list);
            }
        }
    }
    if (expect.has("lanesInside") && expect["lanesInside"].asBool()) {
        const auto area = s.ui().getBounds();
        int visible = 0;
        for (const auto& lane : s.laneComponents()) {
            if (!lane->isVisible()) continue;
            ++visible;
            const auto b = lane->getBounds();
            if (!(b.width > 0.0f && b.height > 0.0f) || b.x < area.x - 0.5f || b.right() > area.right() + 0.5f) {
                std::ostringstream m;
                m << "a lane row is outside the timeline: " << b.x << ',' << b.y << ' ' << b.width << 'x' << b.height
                  << " in " << area.width << 'x' << area.height;
                run.fail(m.str());
            }
        }
        if (visible == 0) run.fail("lanesInside: no lane row is visible, so nothing was checked");
    }
}

AestraUI::NUIModifiers modifiersOf(const JSON& step) {
    int mods = 0;
    if (step.has("mods")) {
        for (const auto& m : step["mods"].asArray()) {
            const std::string name = m.asString();
            if (name == "shift") mods |= static_cast<int>(AestraUI::NUIModifiers::Shift);
            if (name == "ctrl") mods |= static_cast<int>(AestraUI::NUIModifiers::Ctrl);
            if (name == "alt") mods |= static_cast<int>(AestraUI::NUIModifiers::Alt);
        }
    }
    return static_cast<AestraUI::NUIModifiers>(mods);
}

Run runTimeline(const JSON& scenario) {
    Run run{scenario["name"].asString(), {}, -1};
    TimelineSurface s;
    if (!s.build(run, scenario.has("setup") ? scenario["setup"] : JSON::object())) return run;
    const size_t undoBaseline = undoDepth(s.model());

    const auto& steps = scenario["steps"].asArray();
    for (size_t i = 0; i < steps.size(); ++i) {
        run.step = static_cast<int>(i);
        const JSON& step = steps[i];
        if (step.has("expect")) {
            if (onlyKeys(run, step, {"expect"}, "step")) checkExpect(run, s, step["expect"], undoBaseline);
        } else if (step.has("resize")) {
            if (!onlyKeys(run, step, {"resize"}, "step")) continue;
            s.resize(static_cast<float>(step["resize"].asArray()[0].asNumber()),
                     static_cast<float>(step["resize"].asArray()[1].asNumber()));
        } else if (step.has("mouse")) {
            if (!onlyKeys(run, step, {"mouse", "at", "button", "mods"}, "step")) continue;
            AestraUI::NUIPoint at;
            if (!s.resolvePoint(run, step["at"], at)) continue;
            const std::string kind = step["mouse"].asString();
            const auto button = step.has("button") && step["button"].asString() == "right"
                                    ? AestraUI::NUIMouseButton::Right
                                    : AestraUI::NUIMouseButton::Left;
            using T = AestraUI::NUIMouseEventType;
            if (kind == "down") {
                s.mouse(T::Down, at, button, modifiersOf(step));
            } else if (kind == "move") {
                s.mouse(T::Move, at, button, modifiersOf(step));
            } else if (kind == "up") {
                s.mouse(T::Up, at, button, modifiersOf(step));
            } else {
                run.fail("mouse: unknown kind \"" + kind + "\"");
            }
        } else if (step.has("undo")) {
            if (!onlyKeys(run, step, {"undo"}, "step")) continue;
            for (int n = 0; n < static_cast<int>(step["undo"].asNumber()); ++n) {
                if (!s.model().getCommandHistory().undo()) run.fail("undo: nothing to undo");
            }
            s.paint();
        } else {
            run.fail("unknown step {" + keysOf(step) + "}");
        }
    }
    return run;
}

} // namespace

int main() {
    // Layout problems are read from the timeline's recorder, which is off unless traced.
#ifdef _WIN32
    _putenv_s("AESTRA_LAYOUT_TRACE", "timeline");
#else
    setenv("AESTRA_LAYOUT_TRACE", "timeline", 1);
#endif

    std::ifstream in(AESTRA_UI_SCENARIO_FILE, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    bool consumedAll = false;
    const JSON root = JSON::parseStrict(text.str(), consumedAll);
    if (!in || !consumedAll || !root.isObject() || !root.has("version") || root["version"].asNumber() != 1.0 ||
        !root.has("scenarios")) {
        std::cout << "[FAIL] " << AESTRA_UI_SCENARIO_FILE << " is not a version-1 UI scenario file\n";
        return 1;
    }

    int failed = 0;
    int ran = 0;
    for (const auto& scenario : root["scenarios"].asArray()) {
        const std::string name = scenario.has("name") ? scenario["name"].asString() : "(unnamed)";
        const std::string surface = scenario.has("surface") ? scenario["surface"].asString() : "";
        Run run{name, {}, -1};
        if (surface == "timeline") {
            run = runTimeline(scenario);
        } else {
            run.fail("unknown surface \"" + surface + "\"");
        }
        ++ran;
        if (run.failures.empty()) {
            std::cout << "[PASS] " << name << '\n';
        } else {
            ++failed;
            std::cout << "[FAIL] " << name << '\n';
            for (const auto& f : run.failures) std::cout << "         " << f << '\n';
        }
    }
    if (ran == 0) {
        std::cout << "[FAIL] the scenario file has no scenarios\n";
        return 1;
    }
    std::cout << ran - failed << '/' << ran << " UI scenario(s) passed\n";
    return failed == 0 ? 0 : 1;
}
