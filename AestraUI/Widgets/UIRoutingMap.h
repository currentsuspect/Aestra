// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "NUIRenderer.h"
#include "RoutingGraphLayout.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
class MixerViewModel;
struct ChannelViewModel;
} // namespace Aestra

namespace AestraUI {

/**
 * @brief The routing graph (V81-R1, FD-25): opt-in, exceptions only.
 *
 * Opened from the mixer inspector's "Show in routing graph", it draws only what a producer could not assume:
 * custom outputs, sends and sidechains. Channels that simply go to Master are listed on the left, not drawn.
 * Columns follow signal depth and Master is a rail on the right. The graph is still when idle; ports light
 * with level, and wires move only when "Signal flow" is on. Selecting a channel traces its chain and opens a
 * detail strip (output, sends, inserts). Dragging an output patches it: drop to route, Alt to send, Shift to
 * sidechain; channels that would loop refuse the drop and say why.
 */
class UIRoutingMap : public NUIComponent {
public:
    UIRoutingMap();

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override {
        cacheThemeColors();
        NUIComponent::onThemeChanged(theme);
    }
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    void setViewModel(Aestra::MixerViewModel* viewModel);

    /// What the graph currently holds, for tests: drawn channel nodes (Master is the rail, not a node) and
    /// drawn wires (outputs, sends, sidechains; an output to Master is a wire to the rail).
    size_t nodeCount() const { return m_nodes.size(); }
    size_t edgeCount() const { return m_edges.size(); }
    /// Channels listed as "Direct to Master" instead of drawn.
    size_t directCount() const { return m_direct.size(); }
    /// Column of a drawn channel, -1 when it is listed or unknown.
    int columnOf(uint32_t channelId) const;
    /// Draw every channel ("All tracks") instead of custom routes only.
    void setShowAllTracks(bool all);
    bool showsAllTracks() const { return m_allTracks; }

    /// Callback fired when a node is clicked (track selection)
    void setOnNodeSelected(std::function<void(uint32_t channelId)> callback) { m_onNodeSelected = std::move(callback); }

    /// Callback fired by the close button and Escape.
    void setOnCollapse(std::function<void()> callback) { m_onCollapse = std::move(callback); }

    /// Callback fired when an output is dropped on another channel or the Master rail. Args: (source, target)
    void setOnRerouteMain(std::function<void(uint32_t, uint32_t)> cb) { m_onRerouteMain = std::move(cb); }

    /// Callback fired when an output is dropped with Alt (send) or Shift (sidechain). Args: (source, target, sidechain)
    void setOnAddSend(std::function<void(uint32_t, uint32_t, bool)> cb) { m_onAddSend = std::move(cb); }

    /// Callback fired from a node's menu to toggle mute.
    void setOnNodeMuteToggle(std::function<void(uint32_t)> cb) { m_onNodeMuteToggle = std::move(cb); }

    /// Callback fired on shift-click of a node to toggle solo.
    void setOnNodeSoloToggle(std::function<void(uint32_t)> cb) { m_onNodeSoloToggle = std::move(cb); }

    /// Callback fired when a send is deleted (wire or send-row menu).
    void setOnRemoveSend(std::function<void(uint32_t channelId, uint64_t sendId)> cb) { m_onRemoveSend = std::move(cb); }

    /// Callback fired when a send level is set from a menu.
    void setOnEditSendLevel(std::function<void(uint32_t channelId, uint64_t sendId, float newDb)> cb) {
        m_onEditSendLevel = std::move(cb);
    }

    /// Refresh live state (mute/solo/levels) from the bound view model. Cheap; call per frame.
    void refreshLiveState();

private:
    Aestra::MixerViewModel* m_viewModel{nullptr};

    struct Node {
        uint32_t id{0};
        RoutingGraph::Role role{RoutingGraph::Role::Track};
        std::string label;
        uint32_t color{0xFF808080};
        int column{0};
        float x{0}, y{0}; // world, top-left
        bool muted{false};
        bool soloed{false};
        float peakDb{-144.0f};
    };

    struct Edge {
        uint32_t source{0};
        uint32_t target{0}; // RoutingGraph::kMaster = the rail
        enum Type { Output, Send, Sidechain } type{Output};
        float levelDb{0.0f};
        bool postFader{true};
        uint64_t sendId{0}; // 0 = the main output
    };

    struct DirectRow {
        uint32_t id{0};
        std::string label;
        uint32_t color{0xFF808080};
    };

    /// Screen regions, derived from bounds and state each time they are needed.
    struct Geometry {
        NUIRect header, list, canvas, rail, detail;
    };

    std::vector<RoutingGraph::Channel> m_channels; // the routing as the layout sees it
    std::vector<Node> m_nodes;
    std::vector<Edge> m_edges;
    std::vector<DirectRow> m_direct;
    RoutingGraph::Plan m_plan;
    size_t m_signature{0};
    bool m_allTracks{false};
    bool m_signalFlow{false};
    float m_flowPhase{0.0f};

    // Camera: screen = canvas.origin + camera + world * zoom
    float m_cameraX{40.0f};
    float m_cameraY{64.0f};
    float m_zoom{1.0f};
    bool m_fitPending{true};
    bool m_panning{false};
    NUIPoint m_panStart{0, 0};
    float m_panCamX{0}, m_panCamY{0};
    float m_listScroll{0.0f};

    // Selection and hover
    uint32_t m_selectedId{0}; // 0 = nothing selected (Master is never selected)
    int32_t m_lastVmSelection{-2};
    int m_hoverNode{-1};
    int m_hoverDirect{-1};
    int m_hoverEdge{-1};
    int m_hoverButton{-1}; // header button index, -1 = none
    RoutingGraph::Trace m_trace;

    // Patching: dragging an output
    bool m_patching{false};
    uint32_t m_patchSource{0};
    NUIPoint m_patchStartScreen{0, 0}; // where the wire leaves (an output port or a list row)
    NUIPoint m_patchMouse{0, 0};
    NUIModifiers m_patchMods{NUIModifiers::None};
    std::vector<uint32_t> m_patchRefused; // channels that would loop
    bool m_pressOnNode{false};
    int m_pressNode{-1};
    NUIPoint m_pressPos{0, 0};

    // Search
    bool m_searchOpen{false};
    std::string m_search;

    // Detail strip hit rects (rebuilt each render)
    NUIRect m_outputBox{0, 0, 0, 0};
    NUIRect m_addSendRect{0, 0, 0, 0};
    std::vector<std::pair<NUIRect, uint64_t>> m_sendRows;
    // Header hit rects (rebuilt each render): segment Custom, segment All, Signal flow, search, close
    NUIRect m_buttons[5]{};

    std::shared_ptr<NUIContextMenu> m_menu;

    // Live state that decides whether a frame needs painting
    std::vector<bool> m_lit;
    float m_masterPeakL{-144.0f}, m_masterPeakR{-144.0f};

    // Cached colors
    NUIColor m_bg, m_panel, m_node, m_raised, m_border, m_text, m_textSecondary, m_textMuted;
    NUIColor m_accent, m_live, m_sidechain, m_refuse;

    void cacheThemeColors();
    size_t routingSignature() const;
    void rebuildGraph();
    void fitToView();
    void clampCamera();
    Geometry geometry() const;
    void showMenu(const NUIPoint& at);
    void selectChannel(uint32_t id);

    // World/screen
    NUIRect nodeRect(const Node& n) const;
    NUIPoint outputPort(const Node& n) const;
    NUIPoint inputPort(const Node& n) const;
    NUIPoint sidechainPort(const Node& n) const;
    float railPortY(float y) const;
    const Node* nodeById(uint32_t id) const;

    int hitNode(const NUIPoint& p) const;
    int hitOutputPort(const NUIPoint& p) const;
    int hitDirect(const NUIPoint& p) const;
    int hitEdge(const NUIPoint& p) const;
    bool overRail(const NUIPoint& p) const;
    bool refuses(uint32_t id) const;

    // Patching
    void beginPatch(uint32_t source, const NUIPoint& from, const NUIPoint& mouse);
    void finishPatch(const NUIPoint& mouse);
    uint32_t patchTarget(const NUIPoint& mouse, bool& isMaster) const;

    // Menus
    void openOutputMenu(const NUIPoint& at, uint32_t channelId);
    void openAddSendMenu(const NUIPoint& at, uint32_t channelId);
    void openSendMenu(const NUIPoint& at, uint32_t channelId, uint64_t sendId);
    void openNodeMenu(const NUIPoint& at, uint32_t channelId);

    // Drawing (UIRoutingMapDraw.cpp)
    void drawHeader(NUIRenderer& r, const Geometry& g);
    void drawDirectList(NUIRenderer& r, const Geometry& g);
    void drawColumnHeadings(NUIRenderer& r, const Geometry& g);
    void drawEdges(NUIRenderer& r, const Geometry& g);
    void drawNodes(NUIRenderer& r, const Geometry& g);
    void drawExpanded(NUIRenderer& r, const Node& n);
    void drawRail(NUIRenderer& r, const Geometry& g);
    void drawPatch(NUIRenderer& r, const Geometry& g);
    void drawDetail(NUIRenderer& r, const Geometry& g);
    void drawLegend(NUIRenderer& r, const Geometry& g);
    void drawEmpty(NUIRenderer& r, const Geometry& g);
    void drawWire(NUIRenderer& r, NUIPoint a, NUIPoint b, float thickness, const NUIColor& c, bool dashed,
                  bool vertical = false);

    std::function<void(uint32_t)> m_onNodeSelected;
    std::function<void()> m_onCollapse;
    std::function<void(uint32_t, uint32_t)> m_onRerouteMain;
    std::function<void(uint32_t, uint32_t, bool)> m_onAddSend;
    std::function<void(uint32_t)> m_onNodeMuteToggle;
    std::function<void(uint32_t)> m_onNodeSoloToggle;
    std::function<void(uint32_t, uint64_t)> m_onRemoveSend;
    std::function<void(uint32_t, uint64_t, float)> m_onEditSendLevel;
};

} // namespace AestraUI
