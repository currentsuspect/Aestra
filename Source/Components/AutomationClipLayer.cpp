// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AutomationClipLayer.h"

#include "AutomationLaneEditing.h"
#include "Models/TrackManager.h"
#include "NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {

namespace R = AutomationRegions;

/// What the user sees as the curve's clips: the stored ones, or one implicit clip over all its points.
std::vector<AutomationRegion> visibleRegions(const AutomationCurve& curve) {
    if (!curve.regions.empty())
        return curve.regions;
    if (curve.points.empty())
        return {};
    return {{1, curve.points.front().beat, curve.points.back().beat}};
}

float screenX(double beat, const AutomationClipLayer::Geometry& g) {
    return g.gridStartX + static_cast<float>(beat) * g.pixelsPerBeat - g.scrollOffset;
}

double beatAt(float x, const AutomationClipLayer::Geometry& g) {
    return std::max(0.0, static_cast<double>(x - g.gridStartX + g.scrollOffset) / g.pixelsPerBeat);
}

AestraUI::NUIRect headerRect(const AutomationRegion& r, const AutomationClipLayer::Geometry& g) {
    const float x0 = screenX(r.startBeat, g) - 3.0f;
    const float x1 = std::max(screenX(r.endBeat, g) + 3.0f, x0 + 8.0f);
    return {x0, g.bounds.y, x1 - x0, AutomationClipLayer::kHeaderHeight};
}

/// The tab at a clip's start that moves it: just right of the first point, so it never covers it.
AestraUI::NUIRect gripRect(const AutomationRegion& r, const AutomationClipLayer::Geometry& g) {
    return {screenX(r.startBeat, g) + 6.0f, g.bounds.y, 16.0f, AutomationClipLayer::kHeaderHeight};
}

/// A point within reach of the pointer is point editing's, whatever else is under it.
bool pointNear(const PlaylistLane& lane, const AestraUI::NUIPoint& p, const AutomationClipLayer::Geometry& g) {
    for (const auto& curve : lane.automationCurves)
        for (const auto& pt : curve.points) {
            const float x = screenX(pt.beat, g);
            const float y = g.bounds.y + (1.0f - static_cast<float>(automationHeightOfValue(
                                                     curve.getAutomationTarget(), pt.value))) * g.bounds.height;
            if (std::hypot(x - p.x, y - p.y) <= 12.0f)
                return true;
        }
    return false;
}

} // namespace

AutomationClipLayer::~AutomationClipLayer() {
    closeMenu();
}

void AutomationClipLayer::closeMenu() {
    if (m_menu) {
        if (auto* parent = m_menu->getParent())
            parent->removeChild(m_menu);
        m_menu.reset();
    }
}

void AutomationClipLayer::render(AestraUI::NUIRenderer& renderer, const PlaylistLane& lane, int editedCurve,
                                 const Geometry& g) const {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    // V82-T1: regions take the track's colour, like its curve; an unset (white/grey) lane colour keeps the accent.
    const uint32_t argb = lane.colorRGBA;
    const auto r8 = static_cast<uint8_t>(argb >> 16), g8 = static_cast<uint8_t>(argb >> 8), b8 = static_cast<uint8_t>(argb);
    const auto accent = (r8 == g8 && g8 == b8) ? theme.getColor("accentCyan")
                                               : AestraUI::NUIColor(r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, 1.0f);
    // Everything is kept inside the lane (right of the row's header): a clip that starts off-screen to the left, or
    // at beat 0, used to draw its box across the track header.
    const AestraUI::NUIRect laneArea{g.gridStartX, g.bounds.y, std::max(0.0f, g.bounds.right() - g.gridStartX), g.bounds.height};
    for (size_t c = 0; c < lane.automationCurves.size(); ++c) {
        const bool edited = static_cast<int>(c) == editedCurve;
        for (const auto& region : visibleRegions(lane.automationCurves[c])) {
            const bool selected = static_cast<int>(c) == m_selectedCurve && region.id == m_selectedRegion;
            const AestraUI::NUIRect head = AestraUI::intersectRects(headerRect(region, g), laneArea);
            if (head.isEmpty()) continue; // scrolled out of view
            const AestraUI::NUIRect body(head.x, g.bounds.y, head.width, g.bounds.height);
            renderer.fillRect(body, accent.withAlpha(edited ? 0.07f : 0.03f));
            renderer.fillRect(head, accent.withAlpha(selected ? 0.40f : (edited ? 0.20f : 0.10f)));
            const AestraUI::NUIRect grip = gripRect(region, g);
            if (laneArea.contains({grip.x, grip.y}) && laneArea.contains({grip.right(), grip.y})) {
                renderer.fillRect(grip, accent.withAlpha(selected ? 0.90f : (edited ? 0.60f : 0.35f)));
                for (int dot = 0; dot < 3; ++dot) // three dots: a grip
                    renderer.fillRect({grip.x + 4.0f + 4.0f * static_cast<float>(dot), grip.y + 5.0f, 2.0f, 2.0f},
                                      theme.getColor("background").withAlpha(0.7f));
            }
            renderer.strokeRect(body, selected ? 1.5f : 1.0f,
                                selected ? theme.getColor("primary") : accent.withAlpha(edited ? 0.55f : 0.25f));
        }
    }
}

AutomationClipLayer::Hit AutomationClipLayer::headerAt(const PlaylistLane& lane, const AestraUI::NUIPoint& p,
                                                       const Geometry& g, bool gripOnly) const {
    if (pointNear(lane, p, g))
        return {};
    // Regions of different curves may share a header strip; the one drawn last (highest curve index) is on top.
    for (int c = static_cast<int>(lane.automationCurves.size()) - 1; c >= 0; --c) {
        for (const auto& region : visibleRegions(lane.automationCurves[static_cast<size_t>(c)]))
            if ((gripOnly ? gripRect(region, g) : headerRect(region, g)).contains(p))
                return {c, region.id};
    }
    return {};
}

void AutomationClipLayer::commit(TrackManager& tm, PlaylistLaneID laneId, std::vector<AutomationCurve> before,
                                 const char* name, const Hooks& hooks) {
    auto* lane = tm.getPlaylistModel().getLane(laneId);
    if (!lane)
        return;
    for (auto& curve : lane->automationCurves)
        R::retighten(curve);
    if (!automationCurvesEqual(before, lane->automationCurves)) {
        tm.getCommandHistory().pushAndExecute(std::make_shared<EditAutomationCurvesCommand>(
            tm, laneId, std::move(before), lane->automationCurves, name));
        tm.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
        tm.markModified();
    }
    if (hooks.changed)
        hooks.changed();
}

bool AutomationClipLayer::onMouse(const AestraUI::NUIMouseEvent& event, TrackManager& tm, PlaylistLaneID laneId,
                                  const Geometry& g, const Hooks& hooks, int& editedCurve) {
    auto* lane = tm.getPlaylistModel().getLane(laneId);
    if (!lane)
        return false;
    const auto snap = [&](double b) { return hooks.snapBeat ? hooks.snapBeat(b) : b; };

    if (m_dragging) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_dragging = false;
            if (hooks.captureMouse)
                hooks.captureMouse(false);
            // The live edit is already in the lane; the release only records it.
            auto before = std::move(m_before);
            m_before.clear();
            if (m_copy) {
                // The copy becomes the selection: the newest region of the dragged curve.
                const auto& curve = lane->automationCurves[static_cast<size_t>(m_dragHit.curve)];
                if (!curve.regions.empty() && curve.regions.size() > before[static_cast<size_t>(m_dragHit.curve)].regions.size())
                    m_selectedRegion = R::nextId(curve) - 1;
            }
            commit(tm, laneId, std::move(before), m_copy ? "Copy Automation Clip" : "Move Automation Clip", hooks);
            return true;
        }
        if (!event.pressed && !event.released) {
            // Dragging: edit a copy of the curves from the press, and keep the last placement that was allowed.
            const double delta = snap(m_origStart + beatAt(event.position.x, g) - m_grabBeat) - m_origStart;
            auto trial = m_before;
            auto& curve = trial[static_cast<size_t>(m_dragHit.curve)];
            const bool ok = m_copy ? (std::fabs(delta) < R::kEps || R::copy(curve, m_dragHit.region, delta).has_value())
                                   : R::move(curve, m_dragHit.region, delta);
            if (ok) {
                lane->automationCurves = std::move(trial);
                if (hooks.changed)
                    hooks.changed();
            }
            return true;
        }
        return true;
    }

    if (!event.pressed)
        return false;
    const bool right = event.button == AestraUI::NUIMouseButton::Right;
    const Hit hit = headerAt(*lane, event.position, g, /*gripOnly=*/!right);
    if (hit.curve < 0) {
        // A right-click on empty lane space (no point under it, no clip header) offers to create a clip there.
        if (right && g.bounds.contains(event.position) && !pointNear(*lane, event.position, g)) {
            showCreateMenu(tm, laneId, beatAt(event.position.x, g), event.position, g, hooks);
            return true;
        }
        return false;
    }

    m_selectedCurve = hit.curve;
    m_selectedRegion = hit.region;
    editedCurve = hit.curve;
    if (event.button == AestraUI::NUIMouseButton::Right) {
        showMenu(tm, laneId, hit, beatAt(event.position.x, g), event.position, hooks);
        return true;
    }
    if (event.button != AestraUI::NUIMouseButton::Left)
        return false;

    const auto& curve = lane->automationCurves[static_cast<size_t>(hit.curve)];
    const auto regions = visibleRegions(curve);
    const auto it = std::find_if(regions.begin(), regions.end(), [&](const auto& r) { return r.id == hit.region; });
    m_dragging = true;
    m_copy = (event.modifiers & AestraUI::NUIModifiers::Ctrl) != 0;
    m_dragHit = hit;
    m_grabBeat = beatAt(event.position.x, g);
    m_origStart = it != regions.end() ? it->startBeat : 0.0;
    m_before = lane->automationCurves;
    if (hooks.captureMouse)
        hooks.captureMouse(true);
    if (hooks.changed)
        hooks.changed();
    return true;
}

// One bar (4/4) of automation, flat at the channel's current level so nothing jumps when it is created.
void AutomationClipLayer::showCreateMenu(TrackManager& tm, PlaylistLaneID laneId, double beat,
                                         const AestraUI::NUIPoint& at, const Geometry& g, const Hooks& hooks) {
    closeMenu();
    m_menu = std::make_shared<AestraUI::NUIContextMenu>();
    TrackManager* manager = &tm;
    const AutomationTarget target = g.editedTarget;
    const double start = hooks.snapBeat ? hooks.snapBeat(std::max(0.0, beat)) : std::max(0.0, beat);

    const uint32_t channelId = [&]() {
        const uint32_t resolved = tm.resolveLaneChannelId(laneId);
        return resolved != 0 ? resolved : static_cast<uint32_t>(MASTER_MIXER_CHANNEL_ID);
    }();
    const auto* channel = tm.getChannelById(channelId);
    const float level = target == AutomationTarget::Pan ? (channel ? channel->getPan() : 0.0f)
                                                         : (channel ? channel->getVolume() : 1.0f);
    const auto build = [target, channelId, level, start](std::vector<AutomationCurve>& curves) -> bool {
        int index = automationCurveIndexFor(curves, target);
        if (index < 0) {
            AutomationCurve curve = makeAutomationCurve(target);
            curve.mixerChannelId = channelId;
            curves.push_back(std::move(curve));
            index = static_cast<int>(curves.size()) - 1;
        }
        return R::create(curves[static_cast<size_t>(index)], start, start + 4.0, level).has_value();
    };

    // Refused spots (on another clip) show the item disabled instead of doing nothing silently.
    bool allowed = false;
    if (const auto* lane = tm.getPlaylistModel().getLane(laneId)) {
        auto trial = lane->automationCurves;
        allowed = build(trial);
    }
    auto item = std::make_shared<AestraUI::NUIContextMenuItem>("Create Automation Clip");
    item->setEnabled(allowed);
    item->setOnClick([manager, laneId, hooks, this, build]() {
        auto* lane = manager->getPlaylistModel().getLane(laneId);
        if (!lane)
            return;
        auto before = lane->automationCurves;
        if (!build(lane->automationCurves)) {
            lane->automationCurves = std::move(before);
            return;
        }
        commit(*manager, laneId, std::move(before), "Create Automation Clip", hooks);
    });
    m_menu->addItem(item);

    AestraUI::NUIComponent* root = &m_owner;
    while (root->getParent())
        root = root->getParent();
    root->addChild(m_menu);
    m_menu->setOwner(m_owner.weak_from_this());
    m_menu->showAt(at);
    root->repaint();
}

void AutomationClipLayer::showMenu(TrackManager& tm, PlaylistLaneID laneId, Hit hit, double beat,
                                   const AestraUI::NUIPoint& at, const Hooks& hooks) {
    closeMenu();
    m_menu = std::make_shared<AestraUI::NUIContextMenu>();
    TrackManager* manager = &tm;

    // Each action edits the lane's curve, then records ONE undo step for it.
    const auto edit = [manager, laneId, hit, hooks, this](const char* name, auto&& op) {
        auto* lane = manager->getPlaylistModel().getLane(laneId);
        if (!lane || hit.curve < 0 || hit.curve >= static_cast<int>(lane->automationCurves.size()))
            return;
        auto before = lane->automationCurves;
        op(lane->automationCurves[static_cast<size_t>(hit.curve)]);
        commit(*manager, laneId, std::move(before), name, hooks);
    };

    m_menu->addItem("Duplicate Clip", [edit, hit]() {
        edit("Duplicate Automation Clip", [&](AutomationCurve& curve) {
            R::materialize(curve);
            const auto* region = R::find(curve, hit.region);
            if (!region)
                return;
            const double length = std::max(region->endBeat - region->startBeat, 0.25);
            // The first free slot to the right, a clip-length at a time.
            for (int step = 1; step <= 64; ++step)
                if (R::copy(curve, hit.region, length * step).has_value())
                    return;
        });
    });
    m_menu->addItem("Split Here", [edit, hit, beat]() {
        edit("Split Automation Clip", [&](AutomationCurve& curve) { R::split(curve, hit.region, beat); });
    });
    m_menu->addSeparator();
    m_menu->addItem("Delete Clip", [edit, hit, this]() {
        edit("Delete Automation Clip", [&](AutomationCurve& curve) { R::remove(curve, hit.region); });
        m_selectedCurve = -1;
        m_selectedRegion = 0;
    });

    AestraUI::NUIComponent* root = &m_owner;
    while (root->getParent())
        root = root->getParent();
    root->addChild(m_menu);
    m_menu->setOwner(m_owner.weak_from_this());
    m_menu->showAt(at);
    root->repaint();
}

} // namespace Audio
} // namespace Aestra
