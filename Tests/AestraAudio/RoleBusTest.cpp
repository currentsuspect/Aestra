// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// "Create a Drum Bus" (Connected Understanding, owner brief 16 Nov 2026): sounds that share a role can be offered
// one bus; the offer is a deterministic rule over roles and routing; creating it is one undoable step; members keep
// their own channels; nothing is offered when there is nothing to group (RoleBus.h).

#include "Commands/CreateRoleBusCommand.h"
#include "Models/RoleBus.h"
#include "Models/TrackManager.h"

#include <cstdio>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool c, const std::string& m) { if (!c) { std::printf("[FAIL] %s\n", m.c_str()); ++g_failures; } }

constexpr uint32_t kMaster = 0xFFFFFFFFu;

UnitID addUnit(TrackManager& tm, const std::string& name, bool ownChannel) {
    auto& units = tm.getUnitManager();
    const UnitID id = units.createUnit(name, UnitType::Sampler);
    if (ownChannel) {
        auto* channel = tm.addChannel(name + " ch");
        units.setUnitMixerChannel(id, channel->getChannelId());
    }
    return id;
}
uint32_t mainOutOf(TrackManager& tm, UnitID id) {
    const uint32_t ch = tm.getUnitManager().getUnitMixerChannel(id);
    const auto* c = tm.getChannelById(ch);
    return c ? c->getMainOutputId() : 0;
}
} // namespace

int main() {
    // Three drums with channels of their own, one vocal: the offer is the three, as "Drum Bus".
    {
        TrackManager tm;
        const UnitID kick = addUnit(tm, "Kick 01", true);
        const UnitID snare = addUnit(tm, "Snare 01", true);
        const UnitID hat = addUnit(tm, "hh - huge", true);
        const UnitID vox = addUnit(tm, "Vocal Lead", true);
        const RoleBusPlan plan = planRoleBus(tm, RoleCategory::Drums);
        check(plan.worthOffering() && plan.members.size() == 3 && plan.name == "Drum Bus", "three drums are offered a Drum Bus");
        check(!planRoleBus(tm, RoleCategory::Vocals).worthOffering(), "one vocal is not enough for a bus");
        check(!planRoleBus(tm, RoleCategory::Unknown).worthOffering(), "unknown is never offered");

        const size_t channelsBefore = tm.getChannelCount();
        CreateRoleBusCommand cmd(tm, plan);
        cmd.execute();
        const uint32_t bus = cmd.busChannelId();
        check(bus != 0 && tm.getChannelCount() == channelsBefore + 1, "creating it adds one channel");
        check(tm.getChannelById(bus) && tm.getChannelById(bus)->getName() == "Drum Bus", "named Drum Bus");
        check(mainOutOf(tm, kick) == bus && mainOutOf(tm, snare) == bus && mainOutOf(tm, hat) == bus, "the three drum channels go into the bus");
        check(mainOutOf(tm, vox) == kMaster, "the vocal is untouched");
        check(tm.getUnitManager().getUnitMixerChannel(kick) != bus, "each drum keeps its own channel (and its processing)");
        check(!planRoleBus(tm, RoleCategory::Drums).worthOffering(), "once grouped, the offer goes away");

        cmd.undo();
        check(tm.getChannelCount() == channelsBefore, "one undo takes the bus away");
        check(mainOutOf(tm, kick) == kMaster && mainOutOf(tm, snare) == kMaster && mainOutOf(tm, hat) == kMaster, "and the drums go back to Master");
        check(planRoleBus(tm, RoleCategory::Drums).worthOffering(), "so the offer returns");

        cmd.redo();
        check(cmd.busChannelId() != 0 && mainOutOf(tm, kick) == cmd.busChannelId(), "redo builds it again");
        cmd.undo();
    }

    // Units going straight to Master have no channel to route: they are routed to the bus themselves.
    {
        TrackManager tm;
        const UnitID a = addUnit(tm, "Kick 1", false);
        const UnitID b = addUnit(tm, "Snare 1", false);
        const RoleBusPlan plan = planRoleBus(tm, RoleCategory::Drums);
        check(plan.members.size() == 2, "two drums on Master are offered a bus");
        CreateRoleBusCommand cmd(tm, plan);
        cmd.execute();
        check(tm.getUnitManager().getUnitMixerChannel(a) == cmd.busChannelId() && tm.getUnitManager().getUnitMixerChannel(b) == cmd.busChannelId(),
              "they are routed into it");
        cmd.undo();
        check(tm.getUnitManager().getUnitMixerChannel(a) == MASTER_MIXER_CHANNEL_ID, "and back to Master on undo");
    }

    // Two sounds on one channel count once; a sound already in a group is left alone.
    {
        TrackManager tm;
        auto* shared = tm.addChannel("Shared");
        auto& units = tm.getUnitManager();
        const UnitID a = units.createUnit("Kick 1", UnitType::Sampler);
        const UnitID b = units.createUnit("Snare 1", UnitType::Sampler);
        units.setUnitMixerChannel(a, shared->getChannelId());
        units.setUnitMixerChannel(b, shared->getChannelId());
        check(planRoleBus(tm, RoleCategory::Drums).members.size() == 1, "two drums on one channel are one member (nothing to group)");

        const UnitID c = addUnit(tm, "Clap 1", true);
        const UnitID d = addUnit(tm, "Hat 1", true);
        const RoleBusPlan plan = planRoleBus(tm, RoleCategory::Drums);
        check(plan.members.size() == 3, "the shared channel and the two own channels are three members");
        CreateRoleBusCommand cmd(tm, plan);
        cmd.execute();
        const UnitID e = addUnit(tm, "Tom 1", true);
        (void)c; (void)d; (void)e;
        const RoleBusPlan again = planRoleBus(tm, RoleCategory::Drums);
        check(again.members.size() == 1, "a sound added after the bus is offered alone: one member, not worth a second bus");
        cmd.undo();
    }

    // A name that says nothing is no member; the user's own choice is.
    {
        TrackManager tm;
        const UnitID a = addUnit(tm, "Audio 17", true);
        const UnitID b = addUnit(tm, "Audio 18", true);
        check(planRoleBus(tm, RoleCategory::Drums).members.empty(), "\"Audio 17\" and \"Audio 18\" are not drums");
        tm.getUnitManager().setUnitRole(a, userRole(RoleCategory::Drums));
        tm.getUnitManager().setUnitRole(b, userRole(RoleCategory::Drums));
        check(planRoleBus(tm, RoleCategory::Drums).worthOffering(), "once the user says drums they are");
    }

    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("RoleBusTest: all checks passed\n");
    return 0;
}
