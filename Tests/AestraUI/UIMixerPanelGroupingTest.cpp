// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-28 point 1 on the real panel: strips are laid out grouped by where they go, each group under a header,
// a bus closing the group it feeds, and a route change regroups without being asked twice.
// Real TrackManager, MixerViewModel and UIMixerPanel; no pixels.

#include "MixerViewModel.h"
#include "UIMixerPanel.h"
#include "UIMixerStrip.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v)
        out += (out.empty() ? "" : " | ") + s;
    return out;
}

} // namespace

int main() {
    auto tm = std::make_shared<Audio::TrackManager>();
    auto* pad = tm->addChannel("Pad");
    auto* kick = tm->addChannel("Kick");
    auto* snare = tm->addChannel("Snare");
    auto* drumBus = tm->addChannel("Drum Bus");
    auto* lead = tm->addChannel("Lead");
    kick->setMainOutputId(drumBus->getChannelId());
    snare->setMainOutputId(drumBus->getChannelId());

    tm->buildAndShareSlotMap();
    auto vm = std::make_shared<MixerViewModel>();
    vm->syncFromEngine(*tm, *tm->getChannelSlotMapShared());

    AestraUI::UIMixerPanel panel(vm, tm);
    panel.setBounds({0, 0, 1700, 600});
    panel.onResize(1700, 600);

    std::cout << "[1] strips are shown grouped by output\n";
    {
        const auto ids = panel.displayedChannelIds();
        const std::vector<uint32_t> want{kick->getChannelId(), snare->getChannelId(), drumBus->getChannelId(),
                                         pad->getChannelId(), lead->getChannelId()};
        check(ids == want, "feeders, then the bus that closes them, then the master's channels");
        check(join(panel.groupHeaderLabels()) == "Drum Bus | Master",
              "two headers: '" + join(panel.groupHeaderLabels()) + "'");
    }

    std::cout << "[2] the strips sit left to right in that order, with a gap between groups\n";
    {
        const auto* sKick = panel.stripForChannel(kick->getChannelId());
        const auto* sSnare = panel.stripForChannel(snare->getChannelId());
        const auto* sBus = panel.stripForChannel(drumBus->getChannelId());
        const auto* sPad = panel.stripForChannel(pad->getChannelId());
        check(sKick && sSnare && sBus && sPad, "all strips exist");
        if (sKick && sSnare && sBus && sPad) {
            check(sKick->getBounds().x < sSnare->getBounds().x && sSnare->getBounds().x < sBus->getBounds().x &&
                      sBus->getBounds().x < sPad->getBounds().x,
                  "left to right: Kick, Snare, Drum Bus, Pad");
            const float within = sSnare->getBounds().x - sKick->getBounds().x;
            const float across = sPad->getBounds().x - sBus->getBounds().x;
            check(across > within, "the step between groups is wider than inside one group");
            check(sKick->getBounds().y > 0.0f, "strips start below the header band");
        }
    }

    std::cout << "[3] a route change regroups\n";
    {
        pad->setMainOutputId(drumBus->getChannelId());
        vm->syncFromEngine(*tm, *tm->getChannelSlotMapShared());
        panel.onUpdate(0.016);
        const auto ids = panel.displayedChannelIds();
        check(!ids.empty() && std::find(ids.begin(), ids.end(), pad->getChannelId()) < std::find(ids.begin(), ids.end(), drumBus->getChannelId()),
              "Pad now sits before the Drum Bus it feeds");
    }

    std::cout << "[4] a project with no buses shows no headers\n";
    {
        auto flat = std::make_shared<Audio::TrackManager>();
        flat->addChannel("A");
        flat->addChannel("B");
        flat->buildAndShareSlotMap();
        auto flatVm = std::make_shared<MixerViewModel>();
        flatVm->syncFromEngine(*flat, *flat->getChannelSlotMapShared());
        AestraUI::UIMixerPanel flatPanel(flatVm, flat);
        flatPanel.setBounds({0, 0, 1700, 600});
        flatPanel.onResize(1700, 600);
        check(flatPanel.groupHeaderLabels().empty(), "no headers and no band");
        check(flatPanel.displayedChannelIds().size() == 2, "both strips shown");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
