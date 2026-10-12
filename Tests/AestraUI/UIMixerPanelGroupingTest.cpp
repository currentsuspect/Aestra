// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-28 point 1 on the real panel: strips are laid out grouped by where they go, each group under a header,
// a bus closing the group it feeds, and a route change regroups without being asked twice.
// Real TrackManager, MixerViewModel and UIMixerPanel; no pixels.

#include "MixerViewModel.h"
#include "UIMixerPanel.h"
#include "UIMixerInspector.h"
#include "UIMixerStrip.h"

#include "Core/ChannelSlotMap.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
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

    std::cout << "[5] trim and width are edited from the inspector now (they left the strips)\n";
    {
        auto* inspector = panel.getInspector();
        check(inspector != nullptr && inspector->onTrimEdited && inspector->onWidthEdited, "the panel wires both edits");
        if (inspector && inspector->onTrimEdited && inspector->onWidthEdited) {
            const uint32_t id = kick->getChannelId();
            check(!tm->getCommandHistory().canUndo(), "nothing to undo yet");
            inspector->onTrimEdited(id, 6.0f, false);
            check(!tm->getCommandHistory().canUndo(), "dragging trim is live and records no undo step");
            check(std::fabs(vm->getChannelById(id)->trimDb - 6.0f) < 1e-4f, "the view model follows the drag");
            inspector->onTrimEdited(id, 6.0f, true);
            check(tm->getCommandHistory().canUndo() && std::fabs(kick->getTrimDb() - 6.0f) < 1e-4f,
                  "release commits one undo step and sets the channel's trim");
            tm->getCommandHistory().undo();
            check(std::fabs(kick->getTrimDb()) < 1e-4f, "undo puts the trim back");
            inspector->onWidthEdited(id, 1.5f);
            check(std::fabs(kick->getWidth() - 1.5f) < 1e-4f, "width is applied to the channel");
        }
    }

    std::cout << "[6] density sets the strip width and how much a strip shows\n";
    {
        int prefSaves = 0;
        panel.onViewPreferencesChanged = [&] { ++prefSaves; };
        const uint32_t id = kick->getChannelId();
        check(panel.density() == AestraUI::MixerDensity::Normal, "Normal is the default");
        const auto* strip = panel.stripForChannel(id);
        check(strip && std::fabs(strip->getBounds().width - 84.0f) < 0.5f, "a Normal strip is 84 px wide");
        panel.setDensity(AestraUI::MixerDensity::Wide);
        check(std::fabs(panel.stripForChannel(id)->getBounds().width - 120.0f) < 0.5f, "Wide is 120 px");
        panel.setDensity(AestraUI::MixerDensity::Narrow);
        const auto* narrow = panel.stripForChannel(id);
        check(std::fabs(narrow->getBounds().width - 44.0f) < 0.5f, "Narrow is 44 px");
        check(!narrow->insertList()->isVisible() && !narrow->panBar()->isVisible() && !narrow->sendList()->isVisible(),
              "Narrow drops the inserts, sends and pan");
        check(narrow->outputChip()->isCompact(), "and the output shrinks to its colour");
        check(narrow->meterBarsRect().height > 100.0f, "the meters keep their height");
        check(prefSaves == 2, "each change asks for the preference to be saved");
        panel.setDensity(AestraUI::MixerDensity::Normal);
        check(panel.stripForChannel(id)->insertList()->isVisible(), "going back to Normal brings them back");
        panel.onViewPreferencesChanged = nullptr; // it captured this block's counter
    }

    std::cout << "[7] Group by output can be switched off\n";
    {
        panel.setGroupByOutput(false);
        check(panel.groupHeaderLabels().empty(), "no headers when flat");
        const auto flat = panel.displayedChannelIds();
        const std::vector<uint32_t> projectOrder{pad->getChannelId(), kick->getChannelId(), snare->getChannelId(),
                                                 drumBus->getChannelId(), lead->getChannelId()};
        check(flat == projectOrder, "strips follow the project's own channel order");
        panel.setGroupByOutput(true);
        check(!panel.groupHeaderLabels().empty(), "switching it back regroups");
    }

    std::cout << "[8] the toolbar's buttons reach the panel\n";
    {
        const auto* tb = panel.toolbar();
        check(tb != nullptr, "the panel has a toolbar");
        if (tb) {
            const AestraUI::NUIRect narrowRect = tb->densityRect(AestraUI::MixerDensity::Narrow);
            AestraUI::NUIMouseEvent click;
            click.type = AestraUI::NUIMouseEventType::Down;
            click.button = AestraUI::NUIMouseButton::Left;
            click.pressed = true;
            click.position = {narrowRect.x + narrowRect.width * 0.5f, narrowRect.y + narrowRect.height * 0.5f};
            panel.onMouseEvent(click);
            check(panel.density() == AestraUI::MixerDensity::Narrow, "clicking Narrow switches the density");
            panel.setDensity(AestraUI::MixerDensity::Normal);

            vm->getChannelById(kick->getChannelId())->peakSinceResetDb = -3.0f;
            const AestraUI::NUIRect resetRect = tb->resetRect();
            click.position = {resetRect.x + resetRect.width * 0.5f, resetRect.y + resetRect.height * 0.5f};
            panel.onMouseEvent(click);
            check(vm->getChannelById(kick->getChannelId())->peakSinceResetDb <= MixerMath::DB_MIN + 0.01f,
                  "Reset peaks clears every channel's peak");
        }
    }

    std::cout << "[9] the navigator appears only when channels overflow\n";
    {
        check(panel.navigator() && !panel.navigator()->isVisible(), "5 channels fit: no navigator");
        auto big = std::make_shared<Audio::TrackManager>();
        for (int i = 0; i < 24; ++i)
            big->addChannel("C" + std::to_string(i));
        big->buildAndShareSlotMap();
        auto bigVm = std::make_shared<MixerViewModel>();
        bigVm->syncFromEngine(*big, *big->getChannelSlotMapShared());
        AestraUI::UIMixerPanel bigPanel(bigVm, big);
        bigPanel.setBounds({0, 0, 1700, 600});
        bigPanel.onResize(1700, 600);
        bigPanel.onUpdate(0.016);
        const auto* nav = bigPanel.navigator();
        check(nav && nav->isVisible(), "24 Normal strips overflow the panel: the navigator shows");
        check(nav && nav->ticks().size() == 24, "one tick per channel");
        const float yWith = bigPanel.stripForChannel(bigVm->getChannelByIndex(0)->id)->getBounds().y;
        bigPanel.setDensity(AestraUI::MixerDensity::Narrow);
        bigPanel.onUpdate(0.016);
        check(!bigPanel.navigator()->isVisible(), "Narrow fits them all (24 x 50 px): it goes away");
        const float yWithout = bigPanel.stripForChannel(bigVm->getChannelByIndex(0)->id)->getBounds().y;
        check(yWithout < yWith, "and the strips move up into the room it used");
        bigPanel.setDensity(AestraUI::MixerDensity::Normal);
        bigPanel.onUpdate(0.016);
        float scrolled = -1.0f;
        AestraUI::NUIMouseEvent click;
        click.type = AestraUI::NUIMouseEventType::Down;
        click.button = AestraUI::NUIMouseButton::Left;
        click.pressed = true;
        const auto tick = bigPanel.navigator()->tickRect(20);
        click.position = {tick.x + tick.width * 0.5f, tick.y + tick.height * 0.5f};
        bigPanel.onMouseEvent(click);
        for (int i = 0; i < 80; ++i)
            bigPanel.onUpdate(0.016);
        scrolled = bigPanel.stripForChannel(bigVm->getChannelByIndex(20)->id)->getBounds().x;
        check(scrolled > 0.0f && scrolled < 1700.0f, "clicking a tick scrolls that channel into view (x=" + std::to_string(scrolled) + ")");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
