// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-H4/H5: the road a parameter value takes to a VST3 processor. A value set on the controller alone is never heard,
// so the host queues it and hands it to process() as IParameterChanges; the plugin window's own edits take the same
// road; and what the processor reports comes back for the controller to show.

#include "Plugin/Vst3ParamBridge.h"

#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    // A newer value for the same parameter replaces the waiting one; distinct parameters keep their order.
    {
        ParamBridge bridge;
        bridge.push(7, 0.1);
        bridge.push(9, 0.5);
        bridge.push(7, 0.9);
        check(bridge.pendingCount() == 2, "same parameter coalesces");
        ParamChange out[8];
        const size_t n = bridge.drain(out, 8);
        check(n == 2 && out[0].id == 7 && out[0].value == 0.9 && out[1].id == 9, "drain returns latest values in order");
        check(bridge.drain(out, 8) == 0, "drained values are gone");
    }

    // More waiting than one block can carry: the rest stay for the next block, nothing is lost.
    {
        ParamBridge bridge;
        for (uint32_t i = 0; i < 5; ++i) bridge.push(i, i * 0.1);
        ParamChange out[3];
        check(bridge.drain(out, 3) == 3 && out[2].id == 2, "a full block takes its share");
        check(bridge.drain(out, 3) == 2 && out[0].id == 3 && out[1].id == 4, "the remainder comes next block");
    }

    // The IParameterChanges handed to process() carries exactly the drained values, at sample 0.
    {
        HostParameterChanges changes;
        const ParamChange values[2] = {{11, 0.25}, {12, 0.75}};
        changes.fill(values, 2);
        check(changes.getParameterCount() == 2, "two queues");
        auto* q = changes.getParameterData(1);
        Steinberg::int32 offset = -1;
        Steinberg::Vst::ParamValue value = 0;
        check(q && q->getParameterId() == 12 && q->getPointCount() == 1 && q->getPoint(0, offset, value) == Steinberg::kResultOk &&
                  offset == 0 && value == 0.75,
              "the queue holds the value at offset 0");
        check(changes.getParameterData(2) == nullptr, "no queue past the count");
        changes.fill(values, 0);
        check(changes.getParameterCount() == 0, "refilling with nothing empties it");
        Steinberg::int32 index = 0;
        check(changes.addParameterData(5, index) == nullptr, "the plugin cannot add to the host's changes");
    }

    // What the processor reports: one queue per parameter, newest point kept, collected after the block.
    {
        OutputParameterChanges out;
        Steinberg::int32 index = -1;
        auto* a = out.addParameterData(21, index);
        check(a && index == 0, "first reported parameter gets a queue");
        a->addPoint(10, 0.3, index);
        auto* again = out.addParameterData(21, index);
        check(again == a && index == 0, "the same parameter reuses its queue");
        again->addPoint(40, 0.6, index);
        out.addParameterData(22, index)->addPoint(0, 0.1, index);
        ParamChange got[4];
        const size_t n = out.collect(got, 4);
        check(n == 2 && got[0].id == 21 && got[0].value == 0.6 && got[1].id == 22, "collect returns the latest per parameter");
        out.clear();
        check(out.getParameterCount() == 0, "clear starts the next block empty");
    }

    // Edits made in the plugin window reach the processor's queue and the listener.
    {
        ParamBridge bridge;
        std::vector<ParamChange> heard;
        HostComponentHandler handler(bridge, [&](uint32_t id, double v) { heard.push_back({id, v}); });
        handler.beginEdit(3);
        handler.performEdit(3, 0.42);
        handler.endEdit(3);
        ParamChange out[2];
        check(bridge.drain(out, 2) == 1 && out[0].id == 3 && out[0].value == 0.42, "a window edit is queued for the processor");
        check(heard.size() == 1 && heard[0].id == 3 && heard[0].value == 0.42, "and reported to the listener");

        handler.restartComponent(Steinberg::Vst::kParamValuesChanged | Steinberg::Vst::kLatencyChanged);
        check((handler.takeRestartFlags() & Steinberg::Vst::kParamValuesChanged) != 0, "restart flags are remembered");
        check(handler.takeRestartFlags() == 0, "and taken once");
    }

    if (g_failures == 0) std::cout << "Vst3ParamBridgeTest passed\n";
    return g_failures == 0 ? 0 : 1;
}
