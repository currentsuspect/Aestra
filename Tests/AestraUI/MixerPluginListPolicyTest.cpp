// © 2026 Aestra Studios — All Rights Reserved.
// MixerPluginListPolicyTest — mixer plugin dropdown catalog contract.
//
// The dropdown's "smart enough to know what goes on a mixer" rule is the
// policy's: mixer inserts are audio EFFECTS (instruments, MIDI effects and
// analyzers attach elsewhere), grouping comes from the plugin's own category,
// unknown categories land in OTHER, ordering is deterministic, and duplicate
// IDs are dropped. Header-only policy, no widget, no audio-library link.

#include "Helpers/MixerPluginListPolicy.h"

#include <iostream>

using Aestra::Components::MixerPluginEntry;
using Aestra::Components::MixerPluginGroup;

namespace {

int g_failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "  FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

MixerPluginEntry effect(const std::string& id, const std::string& name, const std::string& category) {
    return {id, name, category, "Effect"};
}

const MixerPluginGroup* findGroup(const std::vector<MixerPluginGroup>& groups, const std::string& label) {
    for (const auto& group : groups) {
        if (group.label == label) {
            return &group;
        }
    }
    return nullptr;
}

bool containsId(const MixerPluginGroup* group, const std::string& id) {
    if (!group) {
        return false;
    }
    for (const auto& entry : group->entries) {
        if (entry.id == id) {
            return true;
        }
    }
    return false;
}

void testOnlyEffectsSurvive() {
    std::cout << "  [1/5] Instruments, MIDI effects and analyzers are excluded... ";
    const int before = g_failures;
    std::vector<MixerPluginEntry> entries = {
        {"com.test.comp", "Comp", "Dynamics", "Effect"}, {"com.test.sampler", "Sampler", "Instrument", "Instrument"},
        {"com.test.arp", "Arp", "MidiEffect", "MIDI"},   {"com.test.scope", "Scope", "Analyzer", "Analyzer"},
        {"", "unnamed", "Dynamics", "Effect"}, // no ID: not insertable
    };
    const auto groups = Aestra::Components::groupForMixerDropdown(std::move(entries));
    check(groups.size() == 1, "only the Dynamics group survives");
    check(!findGroup(groups, "DYNAMICS")->entries.empty(), "the effect is kept");
    check(!containsId(findGroup(groups, "DYNAMICS"), "com.test.sampler"), "instrument excluded");
    check(!containsId(findGroup(groups, "DYNAMICS"), "com.test.arp"), "MIDI effect excluded");
    check(!containsId(findGroup(groups, "DYNAMICS"), "com.test.scope"), "analyzer excluded");
    check(!containsId(findGroup(groups, "DYNAMICS"), ""), "ID-less entries excluded");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testKnownCategoryMapping() {
    std::cout << "  [2/5] Known categories map to stable groups... ";
    const int before = g_failures;
    std::vector<MixerPluginEntry> entries = {
        {"eq", "Aestra EQ", "Equalizer", "Effect"},
        {"verb", "Aestra Verb", "Reverb", "Effect"},
        {"delay", "Aestra Delay", "Delay", "Effect"},
        {"comp", "Aestra Comp", "Dynamics", "Effect"},
        {"transient", "Aestra Transient", "Dynamics", "Effect"},
        {"limit", "Aestra Limit", "Dynamics", "Effect"},
    };
    const auto groups = Aestra::Components::groupForMixerDropdown(std::move(entries));
    check(groups.size() == 3, "three groups for three categories present");
    check(containsId(findGroup(groups, "DYNAMICS"), "transient"), "new internal plugin appears without UI change");
    check(findGroup(groups, "DYNAMICS")->icon == "chart-bar", "DYNAMICS icon");
    check(findGroup(groups, "TIME")->icon == "clock", "TIME icon");
    check(findGroup(groups, "SPECTRAL")->icon == "wave-sine", "SPECTRAL icon");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testUnknownCategoryLandsInOther() {
    std::cout << "  [3/5] Unknown categories land in OTHER... ";
    const int before = g_failures;
    std::vector<MixerPluginEntry> entries = {
        {"vst.weird", "WeirdVerb 9000", "Space Modulator", "Effect"},
        {"lfo", "Aestra LFO", "Modulation", "Effect"},
    };
    const auto groups = Aestra::Components::groupForMixerDropdown(std::move(entries));
    check(groups.size() == 1 && groups.front().label == "OTHER", "single OTHER group");
    check(containsId(&groups.front(), "vst.weird") && containsId(&groups.front(), "lfo"),
          "unknown-category effects still reachable");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testDeterministicOrder() {
    std::cout << "  [4/5] Ordering is deterministic regardless of scan order... ";
    const int before = g_failures;
    std::vector<MixerPluginEntry> scanOrder = {
        {"z", "Zeta", "Dynamics", "Effect"},
        {"a", "Alpha", "Delay", "Effect"},
        {"m", "Mid", "Dynamics", "Effect"},
    };
    std::vector<MixerPluginEntry> reversed = {scanOrder[2], scanOrder[1], scanOrder[0]};
    const auto groupsA = Aestra::Components::groupForMixerDropdown(scanOrder);
    const auto groupsB = Aestra::Components::groupForMixerDropdown(reversed);
    check(groupsA.size() == groupsB.size(), "same group count");
    check(groupsA.front().label == groupsB.front().label, "stable group order");
    check(groupsA.front().entries.front().name == groupsB.front().entries.front().name,
          "alphabetical within group regardless of input order");
    check(groupsA.front().entries.front().name == "Mid", "Mid sorts before Zeta");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

void testDuplicateIdsDropped() {
    std::cout << "  [5/5] Duplicate plugin IDs are dropped (first wins)... ";
    const int before = g_failures;
    std::vector<MixerPluginEntry> entries = {
        {"dup", "First", "Delay", "Effect"},
        {"dup", "Second", "Delay", "Effect"},
    };
    const auto groups = Aestra::Components::groupForMixerDropdown(std::move(entries));
    check(findGroup(groups, "TIME")->entries.size() == 1, "one row per plugin ID");
    check(findGroup(groups, "TIME")->entries.front().name == "First", "first registration wins");
    if (g_failures == before) {
        std::cout << "PASSED\n";
    }
}

} // namespace

int main() {
    std::cout << "MixerPluginListPolicyTest\n";
    testOnlyEffectsSurvive();
    testKnownCategoryMapping();
    testUnknownCategoryLandsInOther();
    testDeterministicOrder();
    testDuplicateIdsDropped();
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All mixer plugin list policy tests passed\n";
    return 0;
}
