// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIRoutingMap.h"

#include "ChannelDisplayName.h"
#include "MixerViewModel.h"
#include "NUIThemeSystem.h"
#include "TrackColorPalette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace AestraUI {

namespace {
constexpr float kHeaderH = 48.0f;
constexpr float kListW = 232.0f;
constexpr float kListHeadH = 76.0f;
constexpr float kListRowH = 30.0f;
constexpr float kDetailH = 152.0f;
constexpr float kRailW = 44.0f;
constexpr float kRailInset = 40.0f;
constexpr float kNodeW = 184.0f;
constexpr float kNodeH = 36.0f;
constexpr float kColStride = 288.0f;
constexpr float kRowPitch = 56.0f;
constexpr float kOriginX = 40.0f;
constexpr float kOriginY = 64.0f;
constexpr float kPortHit = 8.0f;
constexpr uint32_t kNoOutput = 0xFFFFFFFFu;

float gainToDb(float g) { return g <= 0.0f ? -144.0f : 20.0f * std::log10(g); }

RoutingGraph::Role roleOf(const Aestra::ChannelViewModel& ch) {
    using R = Aestra::ChannelViewModel::Role;
    if (ch.role == R::Bus) return RoutingGraph::Role::Bus;
    if (ch.role == R::Return) return RoutingGraph::Role::Return;
    return RoutingGraph::Role::Track;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

NUIPoint bezierAt(NUIPoint a, NUIPoint b, float t) {
    const float dx = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
    const NUIPoint c1{a.x + dx, a.y}, c2{b.x - dx, b.y};
    const float u = 1.0f - t;
    return {u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
            u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y};
}
} // namespace

UIRoutingMap::UIRoutingMap() { cacheThemeColors(); }

void UIRoutingMap::setViewModel(Aestra::MixerViewModel* viewModel) {
    if (m_viewModel == viewModel) return;
    m_viewModel = viewModel;
    m_signature = 0;
    m_fitPending = true;
    repaint();
}

void UIRoutingMap::setShowAllTracks(bool all) {
    if (m_allTracks == all) return;
    m_allTracks = all;
    m_signature = 0;
    m_fitPending = true;
    if (m_viewModel) rebuildGraph();
    repaint();
}

int UIRoutingMap::columnOf(uint32_t channelId) const {
    const Node* n = nodeById(channelId);
    return n ? n->column : -1;
}

void UIRoutingMap::cacheThemeColors() {
    auto& tm = NUIThemeManager::getInstance();
    m_bg = tm.getColor("backgroundPrimary");
    m_panel = tm.getColor("backgroundSecondary");
    m_node = tm.getColor("surfaceTertiary");
    m_raised = tm.getColor("surfaceRaised");
    m_border = tm.getColor("border");
    m_text = tm.getColor("textPrimary");
    m_textSecondary = tm.getColor("textSecondary");
    m_textMuted = tm.getColor("textMuted");
    m_accent = tm.getColor("accentPrimary");
    m_live = tm.getColor("accentCyan");
    m_sidechain = tm.getColor("warning");
    m_refuse = tm.getColor("error");
}

// ---------------------------------------------------------------------------------------------------------------
// Model

size_t UIRoutingMap::routingSignature() const {
    if (!m_viewModel) return 0;
    size_t h = 1469598103934665603ull;
    auto mix = [&h](size_t v) { h = (h ^ v) * 1099511628211ull; };
    for (size_t i = 0; i < m_viewModel->getChannelCount(); ++i) {
        const auto* ch = m_viewModel->getChannelByIndex(i);
        if (!ch) continue;
        mix(ch->id);
        mix(ch->mainOutputId);
        mix(ch->masterSendEnabled ? 1 : 2);
        mix(static_cast<size_t>(ch->role));
        mix(static_cast<size_t>(ch->trackColorIndex + 7));
        mix(std::hash<std::string>{}(ch->name));
        mix(static_cast<size_t>(ch->fxCount));
        for (const auto& s : ch->sends) {
            mix(s.sendId);
            mix(s.targetId);
            mix(s.sidechainOnly ? 3 : 4);
            mix(s.postFader ? 5 : 6);
            mix(static_cast<size_t>(std::lround(gainToDb(s.gain) * 10.0f) + 2000));
        }
    }
    mix(m_allTracks ? 11 : 12);
    return h == 0 ? 1 : h;
}

void UIRoutingMap::rebuildGraph() {
    m_channels.clear();
    m_nodes.clear();
    m_edges.clear();
    m_direct.clear();
    if (!m_viewModel) return;

    std::unordered_map<uint32_t, const Aestra::ChannelViewModel*> byId;
    for (size_t i = 0; i < m_viewModel->getChannelCount(); ++i) {
        const auto* ch = m_viewModel->getChannelByIndex(i);
        if (!ch || ch->id == 0) continue;
        byId[ch->id] = ch;
        RoutingGraph::Channel c;
        c.id = ch->id;
        c.output = ch->mainOutputId == kNoOutput ? RoutingGraph::kMaster : ch->mainOutputId;
        c.outputAudible = c.output != RoutingGraph::kMaster || ch->masterSendEnabled;
        c.role = roleOf(*ch);
        for (const auto& s : ch->sends)
            c.sends.push_back({s.targetId == kNoOutput ? RoutingGraph::kMaster : s.targetId, s.sidechainOnly});
        m_channels.push_back(std::move(c));
    }

    m_plan = RoutingGraph::plan(m_channels, m_allTracks);
    for (const auto& p : m_plan.nodes) {
        const auto* ch = byId[p.id];
        Node n;
        n.id = p.id;
        n.role = roleOf(*ch);
        n.label = channelDisplayName(ch->id, ch->name);
        n.color = paletteIndexToARGB(ch->trackColorIndex);
        n.column = p.column;
        n.x = static_cast<float>(p.column) * kColStride;
        n.y = p.row * kRowPitch;
        n.muted = ch->muted;
        n.soloed = ch->soloed;
        n.peakDb = std::max(ch->smoothedPeakL, ch->smoothedPeakR);
        m_nodes.push_back(n);

        const uint32_t out = ch->mainOutputId == kNoOutput ? RoutingGraph::kMaster : ch->mainOutputId;
        if (out != RoutingGraph::kMaster || ch->masterSendEnabled) m_edges.push_back({p.id, out, Edge::Output});
        for (const auto& s : ch->sends) {
            Edge e;
            e.source = p.id;
            e.target = s.targetId == kNoOutput ? RoutingGraph::kMaster : s.targetId;
            e.type = s.sidechainOnly ? Edge::Sidechain : Edge::Send;
            e.levelDb = gainToDb(s.gain);
            e.postFader = s.postFader;
            e.sendId = s.sendId;
            m_edges.push_back(e);
        }
    }
    for (uint32_t id : m_plan.direct) {
        const auto* ch = byId[id];
        m_direct.push_back({id, channelDisplayName(ch->id, ch->name), paletteIndexToARGB(ch->trackColorIndex)});
    }
    if (m_selectedId != 0 && !byId.count(m_selectedId)) m_selectedId = 0;
    m_trace = m_selectedId ? RoutingGraph::trace(m_channels, m_selectedId) : RoutingGraph::Trace{};
    m_lit.assign(m_nodes.size(), false);
    m_hoverNode = m_hoverEdge = m_hoverDirect = -1;
}

void UIRoutingMap::selectChannel(uint32_t id) {
    m_selectedId = id;
    m_trace = id ? RoutingGraph::trace(m_channels, id) : RoutingGraph::Trace{};
    repaint();
}

void UIRoutingMap::onUpdate(double deltaTime) {
    if (!m_viewModel) return;

    const int32_t vmSel = m_viewModel->getSelectedChannelId();
    const size_t sig = routingSignature();
    if (sig != m_signature) {
        m_signature = sig;
        rebuildGraph();
        repaint();
    }
    if (vmSel != m_lastVmSelection) {
        m_lastVmSelection = vmSel;
        selectChannel(vmSel > 0 ? static_cast<uint32_t>(vmSel) : 0);
    }
    refreshLiveState();

    if (m_signalFlow) {
        bool any = false;
        for (bool l : m_lit) any |= l;
        if (any) {
            m_flowPhase = std::fmod(m_flowPhase + static_cast<float>(deltaTime) * 0.8f, 1.0f);
            repaint();
        }
    }
    if (m_fitPending && !getBounds().isEmpty()) {
        fitToView();
        repaint();
    }
}

void UIRoutingMap::refreshLiveState() {
    if (!m_viewModel) return;
    bool changed = false;
    if (m_lit.size() != m_nodes.size()) m_lit.assign(m_nodes.size(), false);
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        auto& n = m_nodes[i];
        const auto* ch = m_viewModel->getChannelById(n.id);
        if (!ch) continue;
        n.peakDb = std::max(ch->smoothedPeakL, ch->smoothedPeakR);
        const bool lit = n.peakDb > -60.0f && !ch->muted;
        if (lit != m_lit[i] || n.muted != ch->muted || n.soloed != ch->soloed) changed = true;
        m_lit[i] = lit;
        n.muted = ch->muted;
        n.soloed = ch->soloed;
    }
    if (const auto* m = m_viewModel->getMaster()) {
        if (std::abs(m->smoothedPeakL - m_masterPeakL) > 0.5f || std::abs(m->smoothedPeakR - m_masterPeakR) > 0.5f) {
            m_masterPeakL = m->smoothedPeakL;
            m_masterPeakR = m->smoothedPeakR;
            changed = true;
        }
    }
    if (changed) repaint();
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry and camera

UIRoutingMap::Geometry UIRoutingMap::geometry() const {
    const NUIRect b = getBounds();
    Geometry g;
    g.header = {b.x, b.y, b.width, kHeaderH};
    const bool list = !m_allTracks && !m_direct.empty();
    g.list = {b.x, b.y + kHeaderH, list ? kListW : 0.0f, std::max(0.0f, b.height - kHeaderH)};
    const float cx = g.list.right();
    const float detailH = (m_selectedId != 0 && !m_patching) ? kDetailH : 0.0f;
    const float fullH = std::max(0.0f, b.bottom() - g.header.bottom());
    g.detail = {cx, b.bottom() - detailH, std::max(0.0f, b.right() - cx), detailH};
    g.canvas = {cx, g.header.bottom(), std::max(0.0f, b.right() - cx), std::max(0.0f, fullH - detailH)};
    g.rail = {g.canvas.right() - kRailInset - kRailW, g.canvas.y + 16.0f, kRailW,
              std::max(40.0f, g.canvas.height - 16.0f - 24.0f)};
    return g;
}

void UIRoutingMap::fitToView() {
    m_fitPending = false;
    const Geometry g = geometry();
    m_cameraX = kOriginX;
    m_cameraY = kOriginY;
    m_zoom = 1.0f;
    if (m_nodes.empty()) return;
    float maxX = 0.0f, maxY = 0.0f;
    for (const auto& n : m_nodes) {
        maxX = std::max(maxX, n.x + kNodeW);
        maxY = std::max(maxY, n.y + kNodeH);
    }
    const float availW = g.rail.x - g.canvas.x - kOriginX - 64.0f;
    const float availH = g.canvas.height - kOriginY - 48.0f;
    // Never below 65 %: names stay readable and the rest pans.
    if (availW > 0 && availH > 0) m_zoom = std::clamp(std::min({1.0f, availW / maxX, availH / maxY}), 0.65f, 1.0f);
}

void UIRoutingMap::clampCamera() {
    const Geometry g = geometry();
    float maxX = kNodeW, maxY = kNodeH;
    for (const auto& n : m_nodes) {
        maxX = std::max(maxX, n.x + kNodeW);
        maxY = std::max(maxY, n.y + kNodeH);
    }
    // Keep at least 80 px of the graph on screen in each direction.
    m_cameraX = std::clamp(m_cameraX, 80.0f - maxX * m_zoom, std::max(80.0f, g.canvas.width - 80.0f));
    m_cameraY = std::clamp(m_cameraY, 80.0f - maxY * m_zoom, std::max(80.0f, g.canvas.height - 80.0f));
}

void UIRoutingMap::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    m_fitPending = true;
}

const UIRoutingMap::Node* UIRoutingMap::nodeById(uint32_t id) const {
    for (const auto& n : m_nodes)
        if (n.id == id) return &n;
    return nullptr;
}

NUIRect UIRoutingMap::nodeRect(const Node& n) const {
    const Geometry g = geometry();
    return {g.canvas.x + m_cameraX + n.x * m_zoom, g.canvas.y + m_cameraY + n.y * m_zoom, kNodeW * m_zoom,
            kNodeH * m_zoom};
}

NUIPoint UIRoutingMap::outputPort(const Node& n) const {
    const NUIRect r = nodeRect(n);
    return {r.right(), r.y + r.height * 0.5f};
}

NUIPoint UIRoutingMap::inputPort(const Node& n) const {
    const NUIRect r = nodeRect(n);
    return {r.x, r.y + r.height * 0.5f};
}

NUIPoint UIRoutingMap::sidechainPort(const Node& n) const {
    const NUIRect r = nodeRect(n);
    return {r.x + 25.0f * m_zoom, r.y};
}

float UIRoutingMap::railPortY(float y) const {
    const Geometry g = geometry();
    return std::clamp(y, g.rail.y + 12.0f, g.rail.bottom() - 12.0f);
}

int UIRoutingMap::hitNode(const NUIPoint& p) const {
    if (!geometry().canvas.contains(p)) return -1;
    for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i)
        if (nodeRect(m_nodes[i]).contains(p)) return i;
    return -1;
}

int UIRoutingMap::hitOutputPort(const NUIPoint& p) const {
    if (!geometry().canvas.contains(p)) return -1;
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        const NUIPoint o = outputPort(m_nodes[i]);
        if (std::abs(p.x - o.x) <= kPortHit && std::abs(p.y - o.y) <= kPortHit) return i;
    }
    return -1;
}

int UIRoutingMap::hitDirect(const NUIPoint& p) const {
    const Geometry g = geometry();
    if (g.list.width <= 0 || !g.list.contains(p)) return -1;
    const float y0 = g.list.y + kListHeadH - m_listScroll;
    if (p.y < g.list.y + kListHeadH) return -1;
    const int i = static_cast<int>(std::floor((p.y - y0) / kListRowH));
    return (i >= 0 && i < static_cast<int>(m_direct.size())) ? i : -1;
}

int UIRoutingMap::hitEdge(const NUIPoint& p) const {
    const Geometry g = geometry();
    if (!g.canvas.contains(p)) return -1;
    for (int i = 0; i < static_cast<int>(m_edges.size()); ++i) {
        const Edge& e = m_edges[i];
        if (e.type == Edge::Output) continue; // only sends carry a menu
        const Node* s = nodeById(e.source);
        const Node* t = nodeById(e.target);
        if (!s || !t) continue;
        const NUIPoint a = outputPort(*s);
        const NUIPoint b = e.type == Edge::Sidechain ? sidechainPort(*t) : inputPort(*t);
        for (int k = 0; k <= 24; ++k) {
            const NUIPoint q = bezierAt(a, b, static_cast<float>(k) / 24.0f);
            if (std::abs(q.x - p.x) < 5.0f && std::abs(q.y - p.y) < 5.0f) return i;
        }
    }
    return -1;
}

bool UIRoutingMap::overRail(const NUIPoint& p) const {
    const NUIRect r = geometry().rail;
    return NUIRect{r.x - 12.0f, r.y, r.width + 24.0f, r.height}.contains(p);
}

bool UIRoutingMap::refuses(uint32_t id) const {
    return std::find(m_patchRefused.begin(), m_patchRefused.end(), id) != m_patchRefused.end();
}

// ---------------------------------------------------------------------------------------------------------------
// Patching

void UIRoutingMap::beginPatch(uint32_t source, const NUIPoint& from, const NUIPoint& mouse) {
    m_patching = true;
    m_patchSource = source;
    m_patchStartScreen = from;
    m_patchMouse = mouse;
    m_patchRefused.clear();
    if (m_viewModel) {
        // Anything not offered as a destination would loop (the view model asks the engine's routing authority).
        std::unordered_set<uint32_t> ok;
        for (const auto& d : m_viewModel->getAvailableDestinations(source)) ok.insert(d.id);
        for (const auto& c : m_channels)
            if (c.id != source && !ok.count(c.id)) m_patchRefused.push_back(c.id);
    }
    repaint();
}

uint32_t UIRoutingMap::patchTarget(const NUIPoint& mouse, bool& isMaster) const {
    isMaster = overRail(mouse);
    if (isMaster) return RoutingGraph::kMaster;
    const int i = hitNode(mouse);
    if (i < 0 || m_nodes[i].id == m_patchSource) return kNoOutput;
    return m_nodes[i].id;
}

void UIRoutingMap::finishPatch(const NUIPoint& mouse) {
    bool isMaster = false;
    const uint32_t target = patchTarget(mouse, isMaster);
    const uint32_t source = m_patchSource;
    const bool alt = (static_cast<int>(m_patchMods) & static_cast<int>(NUIModifiers::Alt)) != 0;
    const bool shift = (static_cast<int>(m_patchMods) & static_cast<int>(NUIModifiers::Shift)) != 0;
    m_patching = false;
    m_patchSource = 0;
    if (target != kNoOutput && !(target != RoutingGraph::kMaster && refuses(target))) {
        if (isMaster || (!alt && !shift)) {
            if (m_onRerouteMain) m_onRerouteMain(source, target);
        } else if (m_onAddSend) {
            m_onAddSend(source, target, shift);
        }
        m_signature = 0; // rebuild from the new routing next frame
    }
    m_patchRefused.clear();
    repaint();
}

// ---------------------------------------------------------------------------------------------------------------
// Menus

void UIRoutingMap::showMenu(const NUIPoint& at) {
    NUIComponent* root = getParent();
    while (root && root->getParent()) root = root->getParent();
    if (root) root->addChild(m_menu);
    m_menu->showAt(static_cast<int>(at.x), static_cast<int>(at.y));
}

void UIRoutingMap::openOutputMenu(const NUIPoint& at, uint32_t channelId) {
    if (!m_viewModel) return;
    m_menu = std::make_shared<NUIContextMenu>();
    m_menu->setCloseOnSelection(true);
    const auto* ch = m_viewModel->getChannelById(channelId);
    const uint32_t current = ch ? (ch->mainOutputId == kNoOutput ? 0 : ch->mainOutputId) : 0;
    auto add = [&](uint32_t target, const std::string& name) {
        m_menu->addItem((target == current ? "\xE2\x80\xA2 " : "   ") + name, [this, channelId, target]() {
            if (m_onRerouteMain) m_onRerouteMain(channelId, target);
            m_signature = 0;
        });
    };
    for (const auto& d : m_viewModel->getAvailableDestinations(channelId))
        add(d.id, d.id == RoutingGraph::kMaster ? std::string("Master") : channelDisplayName(d.id, d.name));
    showMenu(at);
}

void UIRoutingMap::openAddSendMenu(const NUIPoint& at, uint32_t channelId) {
    if (!m_viewModel) return;
    m_menu = std::make_shared<NUIContextMenu>();
    m_menu->setCloseOnSelection(true);
    auto sidechain = std::make_shared<NUIContextMenu>();
    for (const auto& d : m_viewModel->getAvailableDestinations(channelId)) {
        if (d.id == RoutingGraph::kMaster) continue;
        const uint32_t target = d.id;
        const std::string name = channelDisplayName(d.id, d.name);
        m_menu->addItem(name, [this, channelId, target]() {
            if (m_onAddSend) m_onAddSend(channelId, target, false);
            m_signature = 0;
        });
        sidechain->addItem(name, [this, channelId, target]() {
            if (m_onAddSend) m_onAddSend(channelId, target, true);
            m_signature = 0;
        });
    }
    m_menu->addSeparator();
    m_menu->addSubmenu("Sidechain into", sidechain);
    showMenu(at);
}

void UIRoutingMap::openSendMenu(const NUIPoint& at, uint32_t channelId, uint64_t sendId) {
    m_menu = std::make_shared<NUIContextMenu>();
    m_menu->setCloseOnSelection(true);
    auto level = std::make_shared<NUIContextMenu>();
    static const float kLevels[] = {-144.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f};
    static const char* kNames[] = {"-inf dB", "-24 dB", "-12 dB", "-6 dB", "0 dB", "+6 dB"};
    for (size_t i = 0; i < 6; ++i) {
        const float db = kLevels[i];
        level->addItem(kNames[i], [this, channelId, sendId, db]() {
            if (m_onEditSendLevel) m_onEditSendLevel(channelId, sendId, db);
            m_signature = 0;
        });
    }
    m_menu->addSubmenu("Set level", level);
    m_menu->addSeparator();
    m_menu->addItem("Delete send", [this, channelId, sendId]() {
        if (m_onRemoveSend) m_onRemoveSend(channelId, sendId);
        m_signature = 0;
    });
    showMenu(at);
}

void UIRoutingMap::openNodeMenu(const NUIPoint& at, uint32_t channelId) {
    m_menu = std::make_shared<NUIContextMenu>();
    m_menu->setCloseOnSelection(true);
    m_menu->addItem("Solo", [this, channelId]() {
        if (m_onNodeSoloToggle) m_onNodeSoloToggle(channelId);
    });
    m_menu->addItem("Mute", [this, channelId]() {
        if (m_onNodeMuteToggle) m_onNodeMuteToggle(channelId);
    });
    m_menu->addSeparator();
    m_menu->addItem("Route output to...", [this, channelId, at]() { openOutputMenu(at, channelId); });
    m_menu->addItem("Add a send...", [this, channelId, at]() { openAddSendMenu(at, channelId); });
    showMenu(at);
}

// ---------------------------------------------------------------------------------------------------------------
// Input

bool UIRoutingMap::onMouseEvent(const NUIMouseEvent& event) {
    const NUIPoint& p = event.position;
    if (!getBounds().contains(p) && !m_patching && !m_panning) return false;
    const Geometry g = geometry();
    const bool left = event.button == NUIMouseButton::Left;

    if (event.type == NUIMouseEventType::Move || event.pressed) {
        const int hn = hitNode(p);
        const int hd = hitDirect(p);
        const int he = hn < 0 ? hitEdge(p) : -1;
        int hb = -1;
        for (int i = 0; i < 5; ++i)
            if (m_buttons[i].contains(p)) hb = i;
        if (hn != m_hoverNode || hd != m_hoverDirect || he != m_hoverEdge || hb != m_hoverButton) {
            m_hoverNode = hn;
            m_hoverDirect = hd;
            m_hoverEdge = he;
            m_hoverButton = hb;
            repaint();
        }
    }

    if (event.type == NUIMouseEventType::Move) {
        if (m_patching) {
            m_patchMouse = p;
            m_patchMods = event.modifiers;
            repaint();
            return true;
        }
        if (m_pressNode >= 0 && !m_pressOnNode) {
            // A press on a Direct row turns into a patch once it moves.
            if (std::abs(p.x - m_pressPos.x) + std::abs(p.y - m_pressPos.y) > 5.0f) {
                const auto& row = m_direct[static_cast<size_t>(m_pressNode)];
                const float y = g.list.y + kListHeadH - m_listScroll + m_pressNode * kListRowH + kListRowH * 0.5f;
                m_pressNode = -1;
                beginPatch(row.id, {g.list.right() - 8.0f, y}, p);
                m_patchMods = event.modifiers;
            }
            return true;
        }
        if (m_panning) {
            m_cameraX = m_panCamX + (p.x - m_panStart.x);
            m_cameraY = m_panCamY + (p.y - m_panStart.y);
            clampCamera();
            repaint();
            return true;
        }
        return false;
    }

    if (event.type == NUIMouseEventType::Scroll) {
        if (g.list.contains(p)) {
            const float content = static_cast<float>(m_direct.size()) * kListRowH;
            const float view = g.list.height - kListHeadH;
            m_listScroll = std::clamp(m_listScroll - event.wheelDelta * 30.0f, 0.0f, std::max(0.0f, content - view));
            repaint();
            return true;
        }
        if (g.canvas.contains(p)) {
            const float old = m_zoom;
            m_zoom = std::clamp(m_zoom * (1.0f + event.wheelDelta * 0.08f), 0.35f, 2.0f);
            const float wx = (p.x - g.canvas.x - m_cameraX) / old;
            const float wy = (p.y - g.canvas.y - m_cameraY) / old;
            m_cameraX = p.x - g.canvas.x - wx * m_zoom;
            m_cameraY = p.y - g.canvas.y - wy * m_zoom;
            clampCamera();
            repaint();
            return true;
        }
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Right) {
        if (m_patching) {
            m_patching = false;
            m_patchRefused.clear();
            repaint();
            return true;
        }
        for (const auto& [rect, sendId] : m_sendRows)
            if (rect.contains(p)) {
                openSendMenu(p, m_selectedId, sendId);
                return true;
            }
        if (m_hoverNode >= 0) {
            openNodeMenu(p, m_nodes[m_hoverNode].id);
            return true;
        }
        if (m_hoverEdge >= 0) {
            const Edge& e = m_edges[m_hoverEdge];
            openSendMenu(p, e.source, e.sendId);
            return true;
        }
        return true;
    }

    if (event.pressed && left) {
        switch (m_hoverButton) {
        case 0: setShowAllTracks(false); return true;
        case 1: setShowAllTracks(true); return true;
        case 2: m_signalFlow = !m_signalFlow; repaint(); return true;
        case 3:
            m_searchOpen = !m_searchOpen;
            m_search.clear();
            if (m_searchOpen) setFocused(true);
            repaint();
            return true;
        case 4:
            if (m_onCollapse) m_onCollapse();
            return true;
        default: break;
        }
        if (g.header.contains(p)) return true;
        if (g.detail.height > 0 && g.detail.contains(p)) {
            if (m_outputBox.contains(p)) openOutputMenu({m_outputBox.x, m_outputBox.bottom()}, m_selectedId);
            else if (m_addSendRect.contains(p)) openAddSendMenu({m_addSendRect.x, m_addSendRect.bottom()}, m_selectedId);
            else
                for (const auto& [rect, sendId] : m_sendRows)
                    if (rect.contains(p)) openSendMenu(p, m_selectedId, sendId);
            return true;
        }
        if (m_hoverDirect >= 0) {
            m_pressNode = m_hoverDirect;
            m_pressOnNode = false;
            m_pressPos = p;
            return true;
        }
        const int port = hitOutputPort(p);
        if (port >= 0) {
            beginPatch(m_nodes[port].id, outputPort(m_nodes[port]), p);
            m_patchMods = event.modifiers;
            return true;
        }
        if (m_hoverNode >= 0) {
            if ((static_cast<int>(event.modifiers) & static_cast<int>(NUIModifiers::Shift)) != 0) {
                if (m_onNodeSoloToggle) m_onNodeSoloToggle(m_nodes[m_hoverNode].id);
                return true;
            }
            m_pressNode = m_hoverNode;
            m_pressOnNode = true;
            m_pressPos = p;
            return true;
        }
        if (g.canvas.contains(p)) {
            m_panning = true;
            m_panStart = p;
            m_panCamX = m_cameraX;
            m_panCamY = m_cameraY;
            return true;
        }
        return true;
    }

    if (event.released && left) {
        if (m_patching) {
            m_patchMods = event.modifiers;
            finishPatch(p);
            return true;
        }
        if (m_pressNode >= 0) {
            const uint32_t id = m_pressOnNode ? m_nodes[static_cast<size_t>(m_pressNode)].id
                                              : m_direct[static_cast<size_t>(m_pressNode)].id;
            m_pressNode = -1;
            selectChannel(id);
            if (m_onNodeSelected) m_onNodeSelected(id);
            return true;
        }
        if (m_panning) {
            m_panning = false;
            if (std::abs(p.x - m_panStart.x) + std::abs(p.y - m_panStart.y) < 4.0f) selectChannel(0);
            return true;
        }
    }
    return getBounds().contains(p);
}

bool UIRoutingMap::onKeyEvent(const NUIKeyEvent& event) {
    if (m_patching) {
        // Holding or letting go of Alt / Shift changes what the drop will do; show it at once.
        m_patchMods = event.modifiers;
        repaint();
    }
    if (!event.pressed) return false;

    if (m_searchOpen) {
        if (event.keyCode == NUIKeyCode::Escape) {
            m_searchOpen = false;
            m_search.clear();
            repaint();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Backspace) {
            if (!m_search.empty()) m_search.pop_back();
            repaint();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Enter) {
            const std::string q = lower(m_search);
            for (const auto& n : m_nodes)
                if (!q.empty() && lower(n.label).find(q) != std::string::npos) {
                    selectChannel(n.id);
                    if (m_onNodeSelected) m_onNodeSelected(n.id);
                    break;
                }
            m_searchOpen = false;
            m_search.clear();
            repaint();
            return true;
        }
        if (event.character >= 32 && event.character < 127) {
            m_search.push_back(event.character);
            repaint();
            return true;
        }
    }

    if (event.keyCode == NUIKeyCode::Escape) {
        if (m_patching) {
            m_patching = false;
            m_patchRefused.clear();
            repaint();
            return true;
        }
        if (m_selectedId != 0) {
            selectChannel(0);
            return true;
        }
        if (m_onCollapse) m_onCollapse();
        return true;
    }
    if (event.keyCode == NUIKeyCode::F) {
        m_fitPending = true;
        repaint();
        return true;
    }
    return false;
}

} // namespace AestraUI
