// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-A6: automation tension shapes a segment (AutomationCurve.h), and the v3->v4 project
// migration (ProjectMigrations.h) resets pre-v4 tension so an old project's audio does not
// change on load. Header-only: links nothing.

#include "../../AestraAudio/include/Core/AutomationCurve.h"
#include "../../Source/Core/ProjectMigrations.h"

#include <cmath>
#include <iostream>
#include <string>

using Aestra::Audio::AutomationCurve;
using Aestra::Audio::AutomationTarget;
using Aestra::Audio::automationTensionShape;

namespace {
int g_failures = 0;
void check(bool c, const std::string& what) {
    if (!c) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

AutomationCurve rising(float tension) {
    AutomationCurve c("Volume", AutomationTarget::Volume);
    c.addPoint(0.0, 0.0f, 1.0, tension);
    c.addPoint(4.0, 1.0f, 1.0);
    return c;
}

void testShape() {
    for (double t : {0.0, 0.1, 0.37, 0.5, 0.9, 1.0}) {
        check(near(automationTensionShape(t, 0.0f), t), "tension 0 is exactly linear");
    }
    for (float k : {-1.0f, -0.4f, 0.3f, 1.0f}) {
        check(near(automationTensionShape(0.0, k), 0.0) && near(automationTensionShape(1.0, k), 1.0),
              "every tension is exact at both ends of a segment");
        double prev = 0.0;
        bool monotonic = true;
        for (int i = 1; i <= 100; ++i) {
            const double v = automationTensionShape(i / 100.0, k);
            monotonic = monotonic && v >= prev;
            prev = v;
        }
        check(monotonic, "every tension is monotonic, so a segment never overshoots");
        check(near(automationTensionShape(0.3, k), 1.0 - automationTensionShape(0.7, -k), 1e-12),
              "opposite tensions mirror each other");
    }
    check(automationTensionShape(0.5, 0.5f) > 0.5, "positive tension arrives early");
    check(automationTensionShape(0.5, -0.5f) < 0.5, "negative tension arrives late");
    check(near(automationTensionShape(0.5, 3.0f), automationTensionShape(0.5, 1.0f)), "tension clamps at +1");
    check(near(automationTensionShape(0.5, -3.0f), automationTensionShape(0.5, -1.0f)), "and at -1");
}

void testCurveUsesTheSegmentStartTension() {
    check(near(rising(0.0f).getValueAtBeat(1.0), 0.25, 1e-6), "a linear segment is linear in beats");
    check(rising(0.6f).getValueAtBeat(2.0) > 0.5f, "the starting point's positive tension bends the segment up");
    check(rising(-0.6f).getValueAtBeat(2.0) < 0.5f, "and negative tension bends it down");
    check(near(rising(0.6f).getValueAtBeat(4.0), 1.0, 1e-6), "the segment still lands on its end value");
    AutomationCurve fresh("Volume", AutomationTarget::Volume);
    fresh.addPoint(0.0, 0.0f, 1.0);
    check(fresh.points[0].curve == 0.0f, "a new point defaults to linear");
}

Aestra::JSON projectWithTensions(std::initializer_list<double> tensions) {
    Aestra::JSON points = Aestra::JSON::array();
    double beat = 0.0;
    for (double c : tensions) {
        Aestra::JSON p = Aestra::JSON::object();
        p.set("b", Aestra::JSON(beat));
        p.set("v", Aestra::JSON(0.5));
        p.set("c", Aestra::JSON(c));
        points.push(p);
        beat += 1.0;
    }
    Aestra::JSON curve = Aestra::JSON::object();
    curve.set("points", points);
    Aestra::JSON automation = Aestra::JSON::array();
    automation.push(curve);
    Aestra::JSON lane = Aestra::JSON::object();
    lane.set("automation", automation);
    Aestra::JSON lanes = Aestra::JSON::array();
    lanes.push(lane);
    Aestra::JSON root = Aestra::JSON::object();
    root.set("version", Aestra::JSON(3.0));
    root.set("lanes", lanes);
    return root;
}

void testMigration() {
    using Aestra::MigrationOutcome;
    using Aestra::ProjectMigrations;
    Aestra::JSON old = projectWithTensions({0.5, 0.25, -0.75});
    const auto result = ProjectMigrations::runMigrations(old, 3, 4);
    check(result.outcome == MigrationOutcome::Transformed, "pre-v4 tension makes v3->v4 a transformation");
    const Aestra::JSON& pts = old["lanes"][0]["automation"][0]["points"];
    check(pts[0]["c"].asNumber() == 0.0 && pts[1]["c"].asNumber() == 0.0 && pts[2]["c"].asNumber() == 0.0,
          "every stored tension becomes linear");
    check(pts[1]["b"].asNumber() == 1.0 && pts[1]["v"].asNumber() == 0.5, "beats and values are untouched");

    Aestra::JSON linear = projectWithTensions({0.0, 0.0});
    check(ProjectMigrations::runMigrations(linear, 3, 4).outcome != MigrationOutcome::Transformed,
          "a project whose tensions are already linear is not transformed");
    Aestra::JSON empty = Aestra::JSON::object();
    empty.set("version", Aestra::JSON(3.0));
    check(ProjectMigrations::runMigrations(empty, 3, 4).outcome != MigrationOutcome::Transformed,
          "a project with no lanes is not transformed");
}

void testMigratedAudioIsUnchanged() {
    // What pre-v4 evaluation played (linear whatever the stored tension) is what the
    // migrated curve plays now.
    AutomationCurve migrated("Volume", AutomationTarget::Volume);
    migrated.addPoint(0.0, 0.2f, 1.0, 0.0f); // was 0.5, migrated
    migrated.addPoint(4.0, 0.9f, 1.0, 0.0f);
    for (double beat = 0.0; beat <= 4.0; beat += 0.25) {
        const double oldLinear = 0.2 + (beat / 4.0) * (0.9 - 0.2);
        check(near(migrated.getValueAtBeat(beat), oldLinear, 1e-6), "a migrated curve plays what it always played");
    }
}
} // namespace

int main() {
    testShape();
    testCurveUsesTheSegmentStartTension();
    testMigration();
    testMigratedAudioIsUnchanged();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "AutomationTensionTest: all checks passed\n";
    return 0;
}
