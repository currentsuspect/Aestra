// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "NUIContextMenu.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
class MixerViewModel;
struct ChannelViewModel;
}

namespace AestraUI {

/**
 * @brief Routing map visualizer — minimap and full panel modes.
 *
 * v1: read-only visualization. Auto-layout. Nodes are not draggable.
 */
class UIRoutingMap : public NUIComponent {
public:
    enum class Mode { Minimap, FullPanel };

    explicit UIRoutingMap(Mode mode = Mode::Minimap);

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    void setViewModel(Aestra::MixerViewModel* viewModel);

    Mode getMode() const { return m_mode; }
    void setMode(Mode mode);

    /// Callback fired on double-click (minimap → full panel transition)
    void setOnDoubleClick(std::function<void()> callback) { m_onDoubleClick = std::move(callback); }

    /// Callback fired when a node is clicked (track selection)
    void setOnNodeSelected(std::function<void(uint32_t channelId)> callback) { m_onNodeSelected = std::move(callback); }

    /// Callback fired from full panel collapse button
    void setOnCollapse(std::function<void()> callback) { m_onCollapse = std::move(callback); }

    /// Callback fired when user drags from a track output port and drops on another node
    /// to reroute that track's main output. Args: (sourceChannelId, newTargetId)
    void setOnRerouteMain(std::function<void(uint32_t, uint32_t)> cb) { m_onRerouteMain = std::move(cb); }

    /// Callback fired when user drags from a track input port and drops on another node
    /// to create a new send. Args: (sourceChannelId, targetId, sidechainOnly)
    void setOnAddSend(std::function<void(uint32_t, uint32_t, bool)> cb) { m_onAddSend = std::move(cb); }

    /// Callback fired on right-click of a node to toggle mute.
    void setOnNodeMuteToggle(std::function<void(uint32_t)> cb) { m_onNodeMuteToggle = std::move(cb); }

    /// Callback fired on shift-click of a node to toggle solo.
    void setOnNodeSoloToggle(std::function<void(uint32_t)> cb) { m_onNodeSoloToggle = std::move(cb); }

    /// Callback fired when a selected send edge is deleted (e.g. via Delete key).
    void setOnRemoveSend(std::function<void(uint32_t channelId, int sendIndex)> cb) { m_onRemoveSend = std::move(cb); }

    /// Callback fired when user edits a send level directly (e.g. right-click edge).
    void setOnEditSendLevel(std::function<void(uint32_t channelId, int sendIndex, float newDb)> cb) { m_onEditSendLevel = std::move(cb); }

    /// Refresh live state (mute/solo/levels) from the bound view model. Cheap; call per frame.
    void refreshLiveState();

private:
    Mode m_mode;
    Aestra::MixerViewModel* m_viewModel{nullptr};

    struct Node {
        uint32_t id{0};          // 0 = master, otherwise channel ID
        enum Type { Track, Master, Send } type{Track};
        std::string label;
        uint32_t color{0xFF808080};
        int insertCount{0};

        // Live engine state (snapshotted from ChannelViewModel each rebuild/update)
        bool muted{false};
        bool soloed{false};
        float peakDb{-144.0f};   // Smoothed peak level (dB)

        // Routing warning for this channel
        bool hasRoutingWarning{false};

        // Insert names for mini dots (up to 4)
        std::vector<std::string> insertNames;

        // Layout (local to the canvas)
        float x{0}, y{0};
        float w{80}, h{20};

        // Port positions (local to node)
        float inputX{0}, inputY{0};
        float outputX{0}, outputY{0};

        bool hovered{false};
    };

    struct Edge {
        uint32_t sourceNodeId{0};
        uint32_t targetNodeId{0};
        enum Type { MainPath, SendPath, SidechainPath } type{MainPath};
        float sendLevelDb{0.0f};
        int sendIndex{-1}; // index into ChannelViewModel::sends (for send/sidechain edges)
        bool hovered{false};
    };

    std::vector<Node> m_nodes;
    std::vector<Edge> m_edges;

    // Full-panel camera
    float m_cameraX{0};
    float m_cameraY{0};
    float m_zoom{1.0f};
    float m_targetZoom{1.0f};
    bool m_panning{false};
    bool m_middlePanning{false};
    bool m_fitPending{true};
    NUIPoint m_panStartMouse;
    float m_panStartCameraX{0};
    float m_panStartCameraY{0};
    NUIPoint m_middlePanStartMouse;
    float m_middlePanStartCameraX{0};
    float m_middlePanStartCameraY{0};
    NUIPoint m_zoomAnchorScreen{0, 0};
    bool m_zoomAnchorActive{false};

    // Minimap last-rendered transform (for accurate hit-testing)
    float m_minimapScale{1.0f};
    float m_minimapOffsetX{0.0f};
    float m_minimapOffsetY{0.0f};
    float m_minimapCenterX{0.0f};
    float m_minimapCenterY{0.0f};

    // World-space bounds of laid-out graph (for fit-to-view)
    float m_worldMinX{0}, m_worldMinY{0}, m_worldMaxX{0}, m_worldMaxY{0};

    // Hover / interaction
    int m_hoveredNodeIdx{-1};
    int m_hoveredEdgeIdx{-1};
    long long m_lastClickTimeMs{0};

    // Drag-to-reroute state (full panel only)
    bool m_draggingConnection{false};
    int m_dragSourceNodeIdx{-1};
    NUIPoint m_dragCurrentPos{0, 0};

    // Drag-to-reposition state (full panel only)
    bool m_draggingNode{false};
    int m_dragNodeIdx{-1};
    NUIPoint m_dragNodeStartMouse{0, 0};
    NUIPoint m_dragNodeStartPos{0, 0};

    // Drag-to-add-send state (full panel only)
    bool m_draggingSend{false};
    int m_dragSendSourceIdx{-1};
    NUIPoint m_dragSendCurrentPos{0, 0};

    // Send-type confirmation menu (shown on quick-send drop)
    std::shared_ptr<AestraUI::NUIContextMenu> m_sendTypeMenu;
    bool m_sendTypeMenuPending{false};
    uint32_t m_pendingSendSourceId{0};
    uint32_t m_pendingSendTargetId{0};

    // Floating inspector panel (full panel only)
    bool m_inspectorVisible{false};
    int m_inspectorNodeIdx{-1};
    NUIRect m_inspectorCloseRect{0, 0, 0, 0};
    bool m_inspectorCloseHovered{false};
    NUIRect m_inspectorPanelRect{0, 0, 0, 0};

    // Hover-to-trace state
    std::vector<bool> m_traceUpstreamMask; // per-node
    std::vector<bool> m_traceDownstreamMask; // per-node
    std::vector<bool> m_traceEdgeMask; // per-edge

    // Right-click node context menu
    std::shared_ptr<AestraUI::NUIContextMenu> m_nodeContextMenu;

    // Search bar state
    bool m_searchActive{false};
    std::string m_searchQuery;
    NUIRect m_searchRect{0, 0, 0, 0};
    bool m_searchHovered{false};
    bool m_searchFocused{false};
    std::vector<int> m_searchMatches;       // indices into m_nodes
    int m_searchHoveredMatch{-1};           // dropdown item index (-1 = none)
    NUIRect m_searchDropdownRects[5];       // hit rects for up to 5 suggestions
    float m_searchCaretTimer{0.0f};
    bool m_searchCaretVisible{true};

    // Edge right-click context menu
    std::shared_ptr<AestraUI::NUIContextMenu> m_edgeContextMenu;

    // Animation / live signal state
    float m_livePulsePhase{0.0f};
    bool m_anySoloed{false};

    // Dirty flag for graph rebuild
    bool m_graphDirty{true};
    uint32_t m_lastChannelCount{0};
    uint32_t m_lastSelectedId{0xFFFFFFFFu};

    // Title-bar buttons (full panel only)
    NUIRect m_collapseButtonRect{0, 0, 0, 0};
    bool m_collapseHovered{false};
    NUIRect m_fitButtonRect{0, 0, 0, 0};
    bool m_fitHovered{false};
    NUIRect m_resetButtonRect{0, 0, 0, 0};
    bool m_resetHovered{false};

    // Port hover state (full panel only)
    enum PortType { NoPort, InputPort, OutputPort };
    PortType m_hoveredPortType{NoPort};
    int m_hoveredPortNodeIdx{-1};

    // Edge selection state (full panel only)
    int m_selectedEdgeIdx{-1};

    // Cached colors
    NUIColor m_bg;
    NUIColor m_bgSecondary;
    NUIColor m_bgTertiary;
    NUIColor m_border;
    NUIColor m_borderSecondary;
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_textInfo;
    NUIColor m_accent;
    NUIColor m_warning;

    void cacheThemeColors();
    void rebuildGraph();
    void rebuildFocusedGraph(const Aestra::ChannelViewModel* selected);
    void autoLayout();
    void computeWorldBounds();
    void fitToView();
    void renderMinimap(NUIRenderer& renderer);
    void renderFullPanel(NUIRenderer& renderer);

    int hitTestNode(const NUIPoint& p) const;
    int hitTestEdge(const NUIPoint& p) const;
    int hitTestOutputPort(const NUIPoint& p) const;

    void drawNode(NUIRenderer& renderer, const Node& node, float scale);
    void drawEdge(NUIRenderer& renderer, const Edge& edge, float scale);
    void drawBezier(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b, float thickness, const NUIColor& color, bool dashed);
    void drawDotGrid(NUIRenderer& renderer);

    void drawLivePulse(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b, float peakDb);
    void drawSendLevelLabel(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b, float sendLevelDb);
    void drawInsertDots(NUIRenderer& renderer, const Node& node, float nx, float ny, float nw, float nh);
    NUIColor resolveInsertColor(const std::string& name) const;

    int hitTestInputPort(const NUIPoint& p) const;
    void renderMiniOverview(NUIRenderer& renderer);
    void renderInspector(NUIRenderer& renderer);
    void recomputeSearchMatches();

    std::function<void()> m_onDoubleClick;
    std::function<void(uint32_t)> m_onNodeSelected;
    std::function<void()> m_onCollapse;
    std::function<void(uint32_t, uint32_t)> m_onRerouteMain;
    std::function<void(uint32_t, uint32_t, bool)> m_onAddSend;
    std::function<void(uint32_t)> m_onNodeMuteToggle;
    std::function<void(uint32_t)> m_onNodeSoloToggle;
    std::function<void(uint32_t, int)> m_onRemoveSend;
    std::function<void(uint32_t, int, float)> m_onEditSendLevel;
};

} // namespace AestraUI
