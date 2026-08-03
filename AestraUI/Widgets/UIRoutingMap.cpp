// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIRoutingMap.h"

#include "ChannelDisplayName.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include "MixerViewModel.h"
#include "TrackColorPalette.h"
#include "AestraLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace AestraUI {

namespace {
    constexpr float kTrackNodeW = 180.0f;
    constexpr float kTrackNodeH = 52.0f;
    constexpr float kMasterNodeW = 140.0f;
    constexpr float kMasterNodeH = 96.0f;
    constexpr float kMinimapTrackW = 96.0f;
    constexpr float kMinimapTrackH = 28.0f;
    constexpr float kMinimapMasterW = 72.0f;
    constexpr float kMinimapMasterH = 28.0f;
    constexpr float kTrackSpacing = 72.0f;
    constexpr float kMasterGap = 320.0f;
    constexpr float kPortRadius = 5.0f;
    constexpr float kMinimapPortRadius = 3.0f;
    constexpr float kMinSplitHeight = 32.0f;

    std::string fitLabel(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth)
    {
        if (renderer.measureText(text, fontSize).width <= maxWidth) return text;
        std::string clipped = text;
        const std::string ellipsis = "...";
        while (!clipped.empty() &&
               renderer.measureText(clipped + ellipsis, fontSize).width > maxWidth) {
            clipped.pop_back();
        }
        return clipped.empty() ? ellipsis : clipped + ellipsis;
    }

    float gainToDb(float linearGain) {
        if (linearGain <= 0.0f) return -144.0f;
        return 20.0f * std::log10(linearGain);
    }
}

UIRoutingMap::UIRoutingMap(Mode mode) : m_mode(mode) {
    cacheThemeColors();
}

void UIRoutingMap::setMode(Mode mode) {
    if (m_mode != mode) {
        m_mode = mode;
        m_graphDirty = true;
        m_zoom = 1.0f;
        m_targetZoom = 1.0f;
        m_cameraX = 0.0f;
        m_cameraY = 0.0f;
        repaint();
    }
}

void UIRoutingMap::setViewModel(Aestra::MixerViewModel* viewModel) {
    if (m_viewModel != viewModel) {
        m_viewModel = viewModel;
        m_graphDirty = true;
        repaint();
    }
}

void UIRoutingMap::cacheThemeColors() {
    auto& tm = NUIThemeManager::getInstance();
    m_bg = tm.getColor("backgroundPrimary");
    m_bgSecondary = tm.getColor("backgroundSecondary");
    m_bgTertiary = tm.getColor("elevatedPanel");
    m_border = tm.getColor("borderStrong");
    m_borderSecondary = tm.getColor("borderSubtle");
    m_text = tm.getColor("textPrimary");
    m_textSecondary = tm.getColor("textSecondary");
    m_textInfo = tm.getColor("info");
    m_accent = tm.getColor("accentPrimary");
    m_warning = tm.getColor("warning");
    m_error = tm.getColor("error");
}

void UIRoutingMap::rebuildGraph() {
    // Preserve existing node positions so user-dragged layouts survive rebuilds
    std::unordered_map<uint32_t, NUIPoint> savedPositions;
    for (const auto& n : m_nodes) {
        savedPositions[n.id] = {n.x, n.y};
    }

    m_nodes.clear();
    m_edges.clear();

    if (!m_viewModel) return;

    // Minimap: focused view (selected track + its destinations only)
    if (m_mode == Mode::Minimap) {
        int32_t selId = m_viewModel->getSelectedChannelId();
        const Aestra::ChannelViewModel* selected = nullptr;
        if (selId >= 0) {
            selected = m_viewModel->getChannelById(static_cast<uint32_t>(selId));
        }
        rebuildFocusedGraph(selected);
        autoLayout();
        computeWorldBounds();
        return;
    }

    size_t channelCount = m_viewModel->getChannelCount();
    const Aestra::ChannelViewModel* master = m_viewModel->getMaster();

    // Master node (id = 0)
    if (master) {
        Node masterNode;
        masterNode.id = 0;
        masterNode.type = Node::Master;
        masterNode.label = "MASTER";
        masterNode.color = 0xFF808080;
        masterNode.insertCount = 0;
        m_nodes.push_back(masterNode);
    }

    // Track nodes
    for (size_t i = 0; i < channelCount; ++i) {
        const auto* ch = m_viewModel->getChannelByIndex(i);
        if (!ch || ch->id == 0) continue; // skip master if it snuck in

        Node node;
        node.id = ch->id;
        node.type = Node::Track;
        node.label = channelDisplayName(ch->id, ch->name);
        node.color = paletteIndexToARGB(ch->trackColorIndex);
        node.insertCount = ch->fxCount;
        node.muted = ch->muted;
        node.soloed = ch->soloed;
        node.peakDb = std::max(ch->smoothedPeakL, ch->smoothedPeakR);
        if (m_viewModel) {
            node.hasRoutingWarning = !m_viewModel->getRoutingWarning(ch->id).empty();
        }
        for (size_t in = 0; in < ch->inserts.size() && node.insertNames.size() < 4; ++in) {
            if (!ch->inserts[in].isEmpty && !ch->inserts[in].name.empty()) {
                node.insertNames.push_back(ch->inserts[in].name);
            }
        }
        m_nodes.push_back(node);

        // Main path edge
        uint32_t destId = ch->mainOutputId;
        if (destId == 0xFFFFFFFFu) destId = 0;
        if (ch->masterSendEnabled || destId != 0) {
            Edge edge;
            edge.sourceNodeId = ch->id;
            edge.targetNodeId = destId;
            edge.type = Edge::MainPath;
            m_edges.push_back(edge);
        }

        // Send edges
        for (size_t s = 0; s < ch->sends.size(); ++s) {
            const auto& send = ch->sends[s];
            uint32_t sendTargetId = send.targetId;
            if (sendTargetId == 0xFFFFFFFFu) sendTargetId = 0;

            Edge edge;
            edge.sourceNodeId = ch->id;
            edge.targetNodeId = sendTargetId;
            edge.type = send.sidechainOnly ? Edge::SidechainPath : Edge::SendPath;
            edge.sendLevelDb = gainToDb(send.gain);
            edge.sendIndex = static_cast<int>(s);
            m_edges.push_back(edge);
        }
    }

    autoLayout();

    // Restore user-dragged positions for nodes that existed before
    bool hasNewNodes = false;
    for (auto& node : m_nodes) {
        auto it = savedPositions.find(node.id);
        if (it != savedPositions.end()) {
            node.x = it->second.x;
            node.y = it->second.y;
        } else {
            hasNewNodes = true; // at least one node didn't exist before
        }
    }

    computeWorldBounds();
    if (m_mode == Mode::FullPanel && hasNewNodes) {
        m_fitPending = true;
    }
}

void UIRoutingMap::rebuildFocusedGraph(const Aestra::ChannelViewModel* selected) {
    // Show only the selected track + master + send destinations.
    // No selection → just show master.
    const Aestra::ChannelViewModel* master = m_viewModel->getMaster();
    if (master) {
        Node masterNode;
        masterNode.id = 0;
        masterNode.type = Node::Master;
        masterNode.label = "MASTER";
        masterNode.color = 0xFF808080;
        m_nodes.push_back(masterNode);
    }

    if (!selected || selected->id == 0) {
        return;
    }

    Node node;
    node.id = selected->id;
    node.type = Node::Track;
    node.label = channelDisplayName(selected->id, selected->name);
    node.color = paletteIndexToARGB(selected->trackColorIndex);
    node.insertCount = selected->fxCount;
    node.muted = selected->muted;
    node.soloed = selected->soloed;
    node.peakDb = std::max(selected->smoothedPeakL, selected->smoothedPeakR);
    if (m_viewModel) {
        node.hasRoutingWarning = !m_viewModel->getRoutingWarning(selected->id).empty();
    }
    for (size_t in = 0; in < selected->inserts.size() && node.insertNames.size() < 4; ++in) {
        if (!selected->inserts[in].isEmpty && !selected->inserts[in].name.empty()) {
            node.insertNames.push_back(selected->inserts[in].name);
        }
    }
    m_nodes.push_back(node);

    // Main output edge
    uint32_t destId = selected->mainOutputId;
    if (destId == 0xFFFFFFFFu) destId = 0;
    if (selected->masterSendEnabled || destId != 0) {
        Edge edge;
        edge.sourceNodeId = selected->id;
        edge.targetNodeId = destId;
        edge.type = Edge::MainPath;
        m_edges.push_back(edge);
    }

    // Send destinations — pull the target nodes too so they're visible
    for (const auto& send : selected->sends) {
        uint32_t targetId = send.targetId;
        if (targetId == 0xFFFFFFFFu) targetId = 0;

        // Add target node if not already present
        bool exists = false;
        for (const auto& n : m_nodes)
            if (n.id == targetId) { exists = true; break; }

        if (!exists) {
            const auto* target = m_viewModel->getChannelById(targetId);
            if (target) {
                Node tn;
                tn.id = targetId;
                tn.type = (targetId == 0) ? Node::Master : Node::Track;
                tn.label = (targetId == 0)
                                ? std::string("MASTER")
                                : (target->name.empty() ? std::string("Bus") : target->name);
                tn.color = paletteIndexToARGB(target->trackColorIndex);
                tn.insertCount = target->fxCount;
                tn.muted = target->muted;
                tn.soloed = target->soloed;
                tn.peakDb = std::max(target->smoothedPeakL, target->smoothedPeakR);
                if (m_viewModel && targetId != 0) {
                    tn.hasRoutingWarning = !m_viewModel->getRoutingWarning(targetId).empty();
                }
                for (size_t in = 0; in < target->inserts.size() && tn.insertNames.size() < 4; ++in) {
                    if (!target->inserts[in].isEmpty && !target->inserts[in].name.empty()) {
                        tn.insertNames.push_back(target->inserts[in].name);
                    }
                }
                m_nodes.push_back(tn);
            }
        }

        Edge edge;
        edge.sourceNodeId = selected->id;
        edge.targetNodeId = targetId;
        edge.type = send.sidechainOnly ? Edge::SidechainPath : Edge::SendPath;
        edge.sendLevelDb = gainToDb(send.gain);
        m_edges.push_back(edge);
    }
}

void UIRoutingMap::computeWorldBounds() {
    if (m_nodes.empty()) {
        m_worldMinX = m_worldMinY = 0.0f;
        m_worldMaxX = m_worldMaxY = 1.0f;
        return;
    }
    m_worldMinX = m_nodes[0].x;
    m_worldMinY = m_nodes[0].y;
    m_worldMaxX = m_nodes[0].x + m_nodes[0].w;
    m_worldMaxY = m_nodes[0].y + m_nodes[0].h;
    for (const auto& n : m_nodes) {
        m_worldMinX = std::min(m_worldMinX, n.x);
        m_worldMinY = std::min(m_worldMinY, n.y);
        m_worldMaxX = std::max(m_worldMaxX, n.x + n.w);
        m_worldMaxY = std::max(m_worldMaxY, n.y + n.h);
    }
}

void UIRoutingMap::fitToView() {
    NUIRect bounds = getBounds();
    constexpr float kTitleBarH = 44.0f;
    constexpr float kPadX = 48.0f;
    constexpr float kPadY = 36.0f;
    float canvasW = std::max(1.0f, bounds.width - kPadX * 2.0f);
    float canvasH = std::max(1.0f, bounds.height - kTitleBarH - kPadY * 2.0f);
    float worldW = std::max(1.0f, m_worldMaxX - m_worldMinX);
    float worldH = std::max(1.0f, m_worldMaxY - m_worldMinY);
    float scaleX = canvasW / worldW;
    float scaleY = canvasH / worldH;
    m_zoom = std::clamp(std::min(scaleX, scaleY), 0.08f, 1.5f);
    m_targetZoom = m_zoom;

    // Render convention: screenX = bounds.x + worldX*zoom + cameraX
    //                    screenY = bounds.y + titleBar + worldY*zoom + cameraY
    float worldCenterX = (m_worldMinX + m_worldMaxX) * 0.5f;
    float worldCenterY = (m_worldMinY + m_worldMaxY) * 0.5f;
    m_cameraX = bounds.width * 0.5f - worldCenterX * m_zoom;
    m_cameraY = (bounds.height - kTitleBarH) * 0.5f - worldCenterY * m_zoom;
    m_fitPending = false;
}

void UIRoutingMap::clampCamera() {
    if (m_nodes.empty()) return;
    NUIRect bounds = getBounds();
    constexpr float kTitleBarH = 44.0f;
    float canvasW = std::max(1.0f, bounds.width);
    float canvasH = std::max(1.0f, bounds.height - kTitleBarH);

    // Allow the user to pan until the graph is almost off-screen, but keep
    // at least a 60 px "handle" visible so the canvas never feels lost.
    constexpr float kMargin = 60.0f;

    float minCamX = -m_worldMaxX * m_zoom + kMargin;
    float maxCamX = canvasW - m_worldMinX * m_zoom - kMargin;
    if (minCamX < maxCamX) {
        m_cameraX = std::clamp(m_cameraX, minCamX, maxCamX);
    }

    float minCamY = -m_worldMaxY * m_zoom + kMargin;
    float maxCamY = canvasH - m_worldMinY * m_zoom - kMargin;
    if (minCamY < maxCamY) {
        m_cameraY = std::clamp(m_cameraY, minCamY, maxCamY);
    }
}

void UIRoutingMap::autoLayout() {
    if (m_mode == Mode::Minimap) {
        // Compact representation: tracks left column, master right, square-ish layout
        // Adapts node spacing to track count so 20+ tracks remain readable.
        std::vector<Node*> tracks;
        Node* masterNode = nullptr;
        for (auto& n : m_nodes) {
            if (n.type == Node::Master) masterNode = &n;
            else tracks.push_back(&n);
        }

        // Use small uniform spacing; renderMinimap() rescales to fit bounds.
        float trackW = kMinimapTrackW;
        float trackH = kMinimapTrackH;
        float masterW = kMinimapMasterW;
        float masterH = kMinimapMasterH;
        float gap = 100.0f;
        float spacing = 6.0f;

        for (size_t i = 0; i < tracks.size(); ++i) {
            tracks[i]->x = 0.0f;
            tracks[i]->y = static_cast<float>(i) * (trackH + spacing);
            tracks[i]->w = trackW;
            tracks[i]->h = trackH;
            tracks[i]->outputX = trackW;
            tracks[i]->outputY = trackH * 0.5f;
            tracks[i]->inputX = 0.0f;
            tracks[i]->inputY = trackH * 0.5f;
        }

        if (masterNode) {
            masterNode->x = trackW + gap;
            // Center master vertically against track column
            float colHeight = tracks.empty() ? masterH
                                              : tracks.size() * trackH + (tracks.size() - 1) * spacing;
            masterNode->y = colHeight * 0.5f - masterH * 0.5f;
            masterNode->w = masterW;
            masterNode->h = masterH;
            masterNode->inputX = 0.0f;
            masterNode->inputY = masterH * 0.5f;
            masterNode->outputX = masterW;
            masterNode->outputY = masterH * 0.5f;
        }
    } else {
        // Full panel: flow-ordered columns instead of a rigid index grid.
        // Tracks that other tracks feed (send/bus targets) gravitate toward the
        // column nearest the master — horizontal position follows signal flow.
        // Partial columns are vertically centred against the tallest one, and
        // alternate columns get a half-row stagger so cross-column wires pass
        // between nodes instead of colliding head-on.
        std::vector<Node*> tracks;
        Node* masterNode = nullptr;
        for (auto& n : m_nodes) {
            if (n.type == Node::Master) masterNode = &n;
            else tracks.push_back(&n);
        }

        // Bus-like = target of at least one track-to-track edge (send or route).
        std::unordered_map<uint32_t, int> inDegree;
        for (const auto& e : m_edges) {
            if (e.targetNodeId != 0) ++inDegree[e.targetNodeId];
        }
        std::stable_sort(tracks.begin(), tracks.end(), [&](const Node* a, const Node* b) {
            const bool aBus = inDegree.find(a->id) != inDegree.end();
            const bool bBus = inDegree.find(b->id) != inDegree.end();
            return !aBus && bBus; // pure sources first, bus-like last (stable)
        });

        const int trackCount = static_cast<int>(tracks.size());
        // Cap rows per column so vertical extent stays reasonable
        constexpr int kMaxRowsPerColumn = 14;
        int numColumns = std::max(1, (trackCount + kMaxRowsPerColumn - 1) / kMaxRowsPerColumn);
        int rowsPerColumn = std::max(1, (trackCount + numColumns - 1) / numColumns);

        float trackW = kTrackNodeW;
        float trackH = kTrackNodeH;
        // Breathing room: a bit looser than the old grid in both axes
        float vSpacing = (numColumns > 1) ? 18.0f : kTrackSpacing;
        float colStride = trackW + 88.0f;
        const float rowStride = trackH + vSpacing;
        const float fullColH = rowsPerColumn * trackH + (rowsPerColumn - 1) * vSpacing;

        for (int i = 0; i < trackCount; ++i) {
            const int col = i / rowsPerColumn;
            const int row = i % rowsPerColumn;
            const int rowsInCol = std::min(rowsPerColumn, trackCount - col * rowsPerColumn);
            const float colH = rowsInCol * trackH + (rowsInCol - 1) * vSpacing;
            const float yBase = (fullColH - colH) * 0.5f;              // centre partial columns
            const float stagger = (col % 2 == 1) ? rowStride * 0.5f : 0.0f; // brick offset
            tracks[i]->x = static_cast<float>(col) * colStride;
            tracks[i]->y = yBase + stagger + static_cast<float>(row) * rowStride;
            tracks[i]->w = trackW;
            tracks[i]->h = trackH;
            tracks[i]->outputX = trackW;
            tracks[i]->outputY = trackH * 0.5f;
            tracks[i]->inputX = 0.0f;
            tracks[i]->inputY = trackH * 0.5f;
        }

        if (masterNode) {
            // Master sits to the right of the last column
            float lastColX = static_cast<float>(numColumns - 1) * colStride;
            masterNode->x = lastColX + trackW + 140.0f;
            masterNode->y = fullColH * 0.5f - kMasterNodeH * 0.5f;
            masterNode->w = kMasterNodeW;
            masterNode->h = kMasterNodeH;
            masterNode->inputX = 0.0f;
            masterNode->inputY = kMasterNodeH * 0.5f;
            masterNode->outputX = kMasterNodeW;
            masterNode->outputY = kMasterNodeH * 0.5f;
        }
    }
}

void UIRoutingMap::updateEdgeAttachments() {
    // Spread incoming wires along each target node's left edge instead of
    // terminating them all on a single pin — at 50 tracks the master turned
    // into a solid grey fan. Attachments are ordered by the source's vertical
    // position so wires arrive sorted and don't cross each other at the node.
    std::unordered_map<uint32_t, const Node*> byId;
    byId.reserve(m_nodes.size());
    for (const auto& n : m_nodes) byId[n.id] = &n;

    std::unordered_map<uint32_t, std::vector<size_t>> incoming;
    for (size_t i = 0; i < m_edges.size(); ++i) {
        incoming[m_edges[i].targetNodeId].push_back(i);
    }

    for (auto& [targetId, edgeIdxs] : incoming) {
        auto dstIt = byId.find(targetId);
        if (dstIt == byId.end()) continue;
        const Node* dst = dstIt->second;

        if (edgeIdxs.size() < 2) {
            for (size_t i : edgeIdxs) m_edges[i].dstPinYOffset = -1.0f;
            continue;
        }

        auto sourceY = [&](const Edge& e) {
            auto it = byId.find(e.sourceNodeId);
            return (it != byId.end()) ? it->second->y + it->second->outputY : 0.0f;
        };
        std::sort(edgeIdxs.begin(), edgeIdxs.end(), [&](size_t a, size_t b) {
            const float ya = sourceY(m_edges[a]);
            const float yb = sourceY(m_edges[b]);
            if (ya != yb) return ya < yb;
            return a < b; // stable tiebreak for equal heights
        });

        const float pad = dst->h * 0.16f;
        const float span = dst->h - pad * 2.0f;
        const float count = static_cast<float>(edgeIdxs.size());
        for (size_t k = 0; k < edgeIdxs.size(); ++k) {
            m_edges[edgeIdxs[k]].dstPinYOffset =
                pad + span * ((static_cast<float>(k) + 0.5f) / count);
        }
    }
}

float UIRoutingMap::edgeTargetY(const Edge& edge, const Node& dst) const {
    return dst.y + (edge.dstPinYOffset >= 0.0f ? edge.dstPinYOffset : dst.inputY);
}

void UIRoutingMap::onUpdate(double deltaTime) {
    if (!m_viewModel) return;

    // Animate live-wire pulse (cycles 0..1 at ~1.5 Hz base)
    m_livePulsePhase += static_cast<float>(deltaTime) * 1.5f;
    if (m_livePulsePhase > 1.0f) m_livePulsePhase -= 1.0f;

    // Search bar caret blink (~1 Hz)
    if (m_searchFocused) {
        m_searchCaretTimer += static_cast<float>(deltaTime);
        if (m_searchCaretTimer >= 0.5f) {
            m_searchCaretTimer -= 0.5f;
            m_searchCaretVisible = !m_searchCaretVisible;
            repaint();
        }
    } else {
        m_searchCaretTimer = 0.0f;
        m_searchCaretVisible = true;
    }

    // Cache global solo state for edge dimming
    m_anySoloed = false;
    for (const auto& node : m_nodes) {
        if (node.soloed) { m_anySoloed = true; break; }
    }

    // Rebuild graph if channel count or selection changed
    size_t channelCount = m_viewModel->getChannelCount();
    int32_t selectedId = m_viewModel->getSelectedChannelId();
    // Normalize -1 (no selection) to sentinel 0xFFFFFFFFu for stable comparison
    uint32_t selectedIdU = (selectedId < 0) ? 0xFFFFFFFFu : static_cast<uint32_t>(selectedId);
    if (m_graphDirty ||
        m_lastChannelCount != static_cast<uint32_t>(channelCount) ||
        m_lastSelectedId != selectedIdU) {
        rebuildGraph();
        m_graphDirty = false;
        m_lastChannelCount = static_cast<uint32_t>(channelCount);
        m_lastSelectedId = selectedIdU;
        repaint();
    } else {
        // Just refresh live meters / mute / solo each frame without rebuild
        refreshLiveState();
        repaint();
    }

    // Auto-fit on first show or after resize
    if (m_mode == Mode::FullPanel && m_fitPending && !m_nodes.empty()) {
        fitToView();
        repaint();
    }
}

void UIRoutingMap::refreshLiveState() {
    if (!m_viewModel) return;
    for (auto& node : m_nodes) {
        const auto* ch = m_viewModel->getChannelById(node.id);
        if (!ch) continue;
        node.muted = ch->muted;
        node.soloed = ch->soloed;
        node.peakDb = std::max(ch->smoothedPeakL, ch->smoothedPeakR);
        if (node.id != 0) {
            node.hasRoutingWarning = !m_viewModel->getRoutingWarning(node.id).empty();
        }
        node.insertNames.clear();
        for (size_t i = 0; i < ch->inserts.size() && node.insertNames.size() < 4; ++i) {
            if (!ch->inserts[i].isEmpty && !ch->inserts[i].name.empty()) {
                node.insertNames.push_back(ch->inserts[i].name);
            }
        }
    }
}

void UIRoutingMap::onRender(NUIRenderer& renderer) {
    // Recompute per frame: attachment order depends on node positions, which
    // change during drags. O(E log E) on ≤ a few hundred edges — negligible.
    updateEdgeAttachments();
    if (m_mode == Mode::Minimap) {
        renderMinimap(renderer);
    } else {
        renderFullPanel(renderer);
    }
}

void UIRoutingMap::renderMinimap(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    bool widgetHovered = isHovered();

    // Card background — slightly brighter on hover to signal interactivity
    NUIColor cardBg = m_bgSecondary.withAlpha(widgetHovered ? 1.0f : 0.85f);
    renderer.fillRoundedRect(bounds, 8.0f, cardBg);
    renderer.strokeRoundedRect(bounds, 8.0f, widgetHovered ? 1.0f : 0.5f,
                                widgetHovered ? m_accent.withAlpha(0.55f)
                                              : m_border.withAlpha(0.35f));

    // Reserve a small header strip for label + expand hint
    constexpr float kHeaderH = 20.0f;
    NUIRect canvasArea{bounds.x + 6.0f, bounds.y + kHeaderH,
                        bounds.width - 12.0f, bounds.height - kHeaderH - 6.0f};

    // Header label (left)
    renderer.drawText("ROUTING", {bounds.x + 10.0f, bounds.y + 5.0f}, 9.0f,
                      m_textSecondary.withAlpha(0.7f));

    // Expand hint (right) — pulses subtly on hover
    {
        std::string hint = widgetHovered ? "Expand" : "Open map";
        float hintW = renderer.measureText(hint, 9.0f).width;
        float alpha = widgetHovered ? 0.95f : 0.55f;
        const float labelRight = bounds.x + 10.0f + renderer.measureText("ROUTING", 9.0f).width + 12.0f;
        const float hintX = bounds.right() - hintW - 28.0f;
        if (hintX > labelRight) {
            renderer.drawText(hint, {hintX, bounds.y + 5.0f},
                              9.0f, m_accent.withAlpha(alpha));
        }
    }

    float scale = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    if (!m_nodes.empty()) {
        float minX = m_nodes[0].x, maxX = m_nodes[0].x + m_nodes[0].w;
        float minY = m_nodes[0].y, maxY = m_nodes[0].y + m_nodes[0].h;
        for (const auto& n : m_nodes) {
            minX = std::min(minX, n.x);
            maxX = std::max(maxX, n.x + n.w);
            minY = std::min(minY, n.y);
            maxY = std::max(maxY, n.y + n.h);
        }
        float contentW = std::max(1.0f, maxX - minX);
        float contentH = std::max(1.0f, maxY - minY);
        constexpr float kPad = 4.0f;
        float scaleX = (canvasArea.width - kPad * 2.0f) / contentW;
        float scaleY = (canvasArea.height - kPad * 2.0f) / contentH;
        scale = std::max(0.05f, std::min(scaleX, scaleY));
        centerX = (minX + maxX) * 0.5f;
        centerY = (minY + maxY) * 0.5f;
    }
    float offsetX = canvasArea.x + canvasArea.width * 0.5f;
    float offsetY = canvasArea.y + canvasArea.height * 0.5f;

    // Cache transform for hit-testing
    m_minimapScale = scale;
    m_minimapOffsetX = offsetX;
    m_minimapOffsetY = offsetY;
    m_minimapCenterX = centerX;
    m_minimapCenterY = centerY;

    auto worldToScreen = [&](float x, float y) -> NUIPoint {
        return {offsetX + (x - centerX) * scale, offsetY + (y - centerY) * scale};
    };

    // Edges
    for (const auto& edge : m_edges) {
        const Node* src = nullptr;
        const Node* dst = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == edge.sourceNodeId) src = &n;
            if (n.id == edge.targetNodeId) dst = &n;
        }
        if (!src || !dst) continue;

        NUIPoint a = worldToScreen(src->x + src->outputX, src->y + src->outputY);
        NUIPoint b = worldToScreen(dst->x + dst->inputX, edgeTargetY(edge, *dst));

        NUIColor color = m_textSecondary.withAlpha(0.5f);
        if (edge.type == Edge::SendPath) color = m_textInfo.withAlpha(0.5f);
        if (edge.type == Edge::SidechainPath) color = m_warning.withAlpha(0.55f);

        drawBezier(renderer, a, b, 1.0f, color, edge.type != Edge::MainPath);
    }

    // Nodes
    for (const auto& node : m_nodes) {
        NUIPoint topLeft = worldToScreen(node.x, node.y);
        float nx = topLeft.x;
        float ny = topLeft.y;
        float nw = node.w * scale;
        float nh = node.h * scale;

        NUIRect nodeRect(nx, ny, nw, nh);
        renderer.fillRoundedRect(nodeRect, 4.0f, m_bg.withAlpha(node.hovered ? 0.8f : 0.6f));
        renderer.strokeRoundedRect(nodeRect, 4.0f, 0.5f,
            node.hovered ? m_accent.withAlpha(0.8f) : m_border.withAlpha(0.3f));

        // Color strip
        NUIColor stripColor(((node.color >> 16) & 0xFF) / 255.0f,
                            ((node.color >> 8) & 0xFF) / 255.0f,
                            (node.color & 0xFF) / 255.0f,
                            ((node.color >> 24) & 0xFF) / 255.0f);
        renderer.fillRect({nx, ny, 3.0f, nh}, stripColor);

        // Label
        float fontSize = std::min(10.0f, nh * 0.45f);
        std::string label = fitLabel(renderer, node.label, fontSize, nw - 12.0f);
        renderer.drawTextCentered(label, {nx + 5.0f, ny, nw - 10.0f, nh},
                                  fontSize, m_text.withAlpha(0.92f));

        // Output port
        renderer.strokeCircle({nx + nw, ny + nh * 0.5f}, kMinimapPortRadius, 1.0f,
                              m_borderSecondary.withAlpha(0.6f));
    }
}

void UIRoutingMap::renderFullPanel(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    constexpr float kTitleBarH = 44.0f;
    constexpr float kCornerRadius = 12.0f;

    // === Chrome: flat rounded card ===
    // Solid dark card so the workspace doesn't bleed through. Flat — no fake
    // drop shadow, no depth gradient (matches the app's flat surface language).
    renderer.fillRoundedRect(bounds, kCornerRadius, m_bg);

    // === Title bar === charcoal (backgroundSecondary), matching the transport
    // bar and every docked panel's chrome — rounded top, squared flush bottom.
    NUIRect titleBar{bounds.x, bounds.y, bounds.width, kTitleBarH};
    renderer.fillRoundedRect(titleBar, kCornerRadius, m_bgSecondary);
    renderer.fillRect({bounds.x, bounds.y + kTitleBarH - kCornerRadius, bounds.width, kCornerRadius},
                      m_bgSecondary);
    renderer.drawLine({bounds.x, bounds.y + kTitleBarH - 0.5f},
                      {bounds.right(), bounds.y + kTitleBarH - 0.5f},
                      1.0f, m_border.withAlpha(0.5f));

    // Outer border (after the title-bar fill so the top edge stays visible)
    renderer.strokeRoundedRect(bounds, kCornerRadius, 1.0f, m_border.withAlpha(0.55f));

    // Title — 12px secondary, same treatment as the docked-panel title bars
    renderer.drawText("ROUTING MAP", {bounds.x + 14.0f, bounds.y + (kTitleBarH - 12.0f) * 0.5f},
                      12.0f, m_textSecondary);

    // Counts, dimmer, on the same line
    {
        size_t trackCount = 0;
        for (const auto& n : m_nodes)
            if (n.type == Node::Track) ++trackCount;
        std::string subtitle = std::to_string(trackCount) + (trackCount == 1 ? " track  ·  " : " tracks  ·  ") +
                               std::to_string(m_edges.size()) + (m_edges.size() == 1 ? " connection" : " connections");
        float titleW = renderer.measureText("ROUTING MAP", 12.0f).width;
        renderer.drawText(subtitle, {bounds.x + 14.0f + titleW + 12.0f, bounds.y + (kTitleBarH - 10.0f) * 0.5f},
                          10.0f, m_textSecondary.withAlpha(0.55f));
    }

    // Toolbar buttons: keep everything in one row so controls don't crowd the canvas.
    std::string fitBtnLabel = "Fit";
    float fitW = renderer.measureText(fitBtnLabel, 10.0f).width + 18.0f;
    m_fitButtonRect = NUIRect{bounds.right() - fitW - 14.0f, bounds.y + 11.0f, fitW, 22.0f};
    renderer.fillRoundedRect(m_fitButtonRect, 5.0f,
                             m_fitHovered ? m_accent.withAlpha(0.20f) : m_bg.withAlpha(0.34f));
    renderer.strokeRoundedRect(m_fitButtonRect, 5.0f, 1.0f,
                               m_fitHovered ? m_accent.withAlpha(0.58f) : m_border.withAlpha(0.30f));
    renderer.drawTextCentered(fitBtnLabel, m_fitButtonRect, 10.0f,
                              m_fitHovered ? m_accent.withAlpha(0.98f) : m_textSecondary.withAlpha(0.9f));

    std::string resetLabel = "Reset";
    float resetW = renderer.measureText(resetLabel, 10.0f).width + 18.0f;
    m_resetButtonRect = NUIRect{m_fitButtonRect.x - resetW - 8.0f, bounds.y + 11.0f, resetW, 22.0f};
    renderer.fillRoundedRect(m_resetButtonRect, 5.0f,
                             m_resetHovered ? m_accent.withAlpha(0.20f) : m_bg.withAlpha(0.34f));
    renderer.strokeRoundedRect(m_resetButtonRect, 5.0f, 1.0f,
                               m_resetHovered ? m_accent.withAlpha(0.58f) : m_border.withAlpha(0.30f));
    renderer.drawTextCentered(resetLabel, m_resetButtonRect, 10.0f,
                              m_resetHovered ? m_accent.withAlpha(0.98f) : m_textSecondary.withAlpha(0.9f));

    std::string collapseLabel = "Collapse";
    float collapseW = renderer.measureText(collapseLabel, 10.0f).width + 20.0f;
    m_collapseButtonRect = NUIRect{m_resetButtonRect.x - collapseW - 8.0f, bounds.y + 11.0f, collapseW, 22.0f};
    renderer.fillRoundedRect(m_collapseButtonRect, 5.0f,
                               m_collapseHovered ? m_accent.withAlpha(0.20f) : m_bg.withAlpha(0.34f));
    renderer.strokeRoundedRect(m_collapseButtonRect, 5.0f, 1.0f,
                               m_collapseHovered ? m_accent.withAlpha(0.58f) : m_border.withAlpha(0.30f));
    renderer.drawTextCentered(collapseLabel, m_collapseButtonRect, 10.0f,
                              m_collapseHovered ? m_accent.withAlpha(0.98f) : m_textSecondary.withAlpha(0.9f));

    // (The floating "Esc" hint is gone — Esc still closes the panel; a loose
    // keyboard hint in the chrome was clutter.)

    // Search bar in title bar
    {
        float searchX = bounds.x + bounds.width * 0.5f - 100.0f;
        float searchW = 200.0f;
        m_searchRect = NUIRect{searchX, bounds.y + 11.0f, searchW, 22.0f};
        NUIColor searchBg = m_searchFocused ? m_bgSecondary.withAlpha(0.68f) : m_bgSecondary.withAlpha(0.38f);
        renderer.fillRoundedRect(m_searchRect, 5.0f, searchBg);
        renderer.strokeRoundedRect(m_searchRect, 5.0f, 1.0f,
                                   m_searchFocused ? m_accent.withAlpha(0.52f) : m_border.withAlpha(0.26f));
        std::string searchDisplay = m_searchQuery.empty() ? "Search nodes..." : m_searchQuery;
        NUIColor searchTextCol = m_searchQuery.empty() ? m_textSecondary.withAlpha(0.45f) : m_text.withAlpha(0.85f);
        renderer.drawText(searchDisplay, {searchX + 8.0f, bounds.y + 14.0f}, 11.0f, searchTextCol);

        // Blinking caret when focused
        if (m_searchFocused && m_searchCaretVisible) {
            float textW = renderer.measureText(m_searchQuery, 11.0f).width;
            float caretX = searchX + 8.0f + textW + 1.0f;
            float caretY = bounds.y + 11.0f + (22.0f - 12.0f) * 0.5f;
            renderer.fillRoundedRect(NUIRect{caretX, caretY, 1.5f, 12.0f}, 1.0f, m_text.withAlpha(0.9f));
        }

        // Dropdown suggestions (rendered below search bar)
        if (m_searchFocused && !m_searchMatches.empty()) {
            float dropY = bounds.y + 11.0f + 22.0f + 2.0f;
            float itemH = 22.0f;
            float dropH = static_cast<float>(m_searchMatches.size()) * itemH + 4.0f;
            NUIRect dropRect{searchX, dropY, searchW, dropH};
            renderer.fillRoundedRect(dropRect, 5.0f, m_bgTertiary.withAlpha(0.98f));
            renderer.strokeRoundedRect(dropRect, 5.0f, 1.0f, m_border.withAlpha(0.35f));
            for (size_t i = 0; i < m_searchMatches.size() && i < 5; ++i) {
                int nodeIdx = m_searchMatches[i];
                const auto& matchNode = m_nodes[nodeIdx];
                float iy = dropY + 2.0f + static_cast<float>(i) * itemH;
                m_searchDropdownRects[i] = NUIRect{searchX, iy, searchW, itemH};
                if (static_cast<int>(i) == m_searchHoveredMatch) {
                    renderer.fillRoundedRect(m_searchDropdownRects[i], 4.0f, m_accent.withAlpha(0.18f));
                }
                // Color dot
                NUIColor dotColor(((matchNode.color >> 16) & 0xFF) / 255.0f,
                                  ((matchNode.color >> 8) & 0xFF) / 255.0f,
                                  (matchNode.color & 0xFF) / 255.0f,
                                  ((matchNode.color >> 24) & 0xFF) / 255.0f);
                renderer.fillCircle({searchX + 14.0f, iy + itemH * 0.5f}, 4.0f, dotColor);
                renderer.drawText(fitLabel(renderer, matchNode.label, 11.0f, searchW - 34.0f),
                                  {searchX + 24.0f, iy + 4.0f}, 11.0f, m_text.withAlpha(0.88f));
            }
            // Clear unused rects
            for (size_t i = m_searchMatches.size(); i < 5; ++i) {
                m_searchDropdownRects[i] = NUIRect{0, 0, 0, 0};
            }
        }
    }

    // (Edge-type legend now lives bottom-left over the canvas — see below,
    // after the canvas clip is cleared — so the title bar stays uncluttered.)

    // === Canvas area (clipped) ===
    NUIRect canvasRect{bounds.x + 1.0f, bounds.y + kTitleBarH + 1.0f,
                       bounds.width - 2.0f, bounds.height - kTitleBarH - 2.0f};
    renderer.setClipRect(canvasRect);

    // Dot grid inside canvas
    drawDotGrid(renderer);

    float canvasY = bounds.y + kTitleBarH;

    // Reduce edge density visual when there are many connections
    // Idle edges are wiring, not content: on dense graphs they fade to a whisper
    // so the map reads as nodes-first, and hover/trace/selection re-light the
    // paths that matter. The old floor (0.22) let 50 converging edges stack into
    // a solid grey fan at the master node.
    const float edgeAlphaScale = (m_edges.size() > 20)
                                     ? std::max(0.10f, 10.0f / static_cast<float>(m_edges.size()))
                                     : 1.0f;

    // Precompute hover-to-trace masks if hovering a node
    m_traceUpstreamMask.assign(m_nodes.size(), false);
    m_traceDownstreamMask.assign(m_nodes.size(), false);
    m_traceEdgeMask.assign(m_edges.size(), false);
    if (m_hoveredNodeIdx >= 0 && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
        // Build id->index map for trace lookups
        std::unordered_map<uint32_t, size_t> idToIdx;
        for (size_t i = 0; i < m_nodes.size(); ++i) idToIdx[m_nodes[i].id] = i;
        // Downstream BFS from hovered node
        std::vector<uint32_t> queue;
        queue.push_back(m_nodes[m_hoveredNodeIdx].id);
        size_t qhead = 0;
        while (qhead < queue.size()) {
            uint32_t curId = queue[qhead++];
            for (size_t e = 0; e < m_edges.size(); ++e) {
                if (m_edges[e].sourceNodeId == curId) {
                    m_traceEdgeMask[e] = true;
                    auto it = idToIdx.find(m_edges[e].targetNodeId);
                    if (it != idToIdx.end() && !m_traceDownstreamMask[it->second]) {
                        m_traceDownstreamMask[it->second] = true;
                        queue.push_back(it->first);
                    }
                }
            }
        }
        // Upstream BFS toward hovered node
        queue.clear(); qhead = 0;
        queue.push_back(m_nodes[m_hoveredNodeIdx].id);
        while (qhead < queue.size()) {
            uint32_t curId = queue[qhead++];
            for (size_t e = 0; e < m_edges.size(); ++e) {
                if (m_edges[e].targetNodeId == curId) {
                    m_traceEdgeMask[e] = true;
                    auto it = idToIdx.find(m_edges[e].sourceNodeId);
                    if (it != idToIdx.end() && !m_traceUpstreamMask[it->second]) {
                        m_traceUpstreamMask[it->second] = true;
                        queue.push_back(it->first);
                    }
                }
            }
        }
        m_traceDownstreamMask[m_hoveredNodeIdx] = true;
        m_traceUpstreamMask[m_hoveredNodeIdx] = true;
    }

    // Edges
    for (size_t ei = 0; ei < m_edges.size(); ++ei) {
        const auto& edge = m_edges[ei];
        const Node* src = nullptr;
        const Node* dst = nullptr;
        size_t srcIdx = 0, dstIdx = 0;
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            if (m_nodes[i].id == edge.sourceNodeId) { src = &m_nodes[i]; srcIdx = i; }
            if (m_nodes[i].id == edge.targetNodeId) { dst = &m_nodes[i]; dstIdx = i; }
        }
        if (!src || !dst) continue;

        float sx = bounds.x + (src->x + src->outputX) * m_zoom + m_cameraX;
        float sy = canvasY + (src->y + src->outputY) * m_zoom + m_cameraY;
        float dx_ = bounds.x + (dst->x + dst->inputX) * m_zoom + m_cameraX;
        float dy_ = canvasY + edgeTargetY(edge, *dst) * m_zoom + m_cameraY;

        // Mute/solo dimming logic
        bool soloSuppressed = m_anySoloed && !src->soloed && !dst->soloed;
        bool sourceMuted = src->muted;
        float dimFactor = 1.0f;
        if (sourceMuted) dimFactor *= 0.35f;
        if (soloSuppressed) dimFactor *= 0.25f;

        // Trace highlighting boost
        bool inTrace = m_traceEdgeMask[ei];
        bool inTraceNode = m_traceUpstreamMask[srcIdx] || m_traceDownstreamMask[srcIdx] ||
                           m_traceUpstreamMask[dstIdx] || m_traceDownstreamMask[dstIdx];

        const float baseA = edge.hovered ? 0.88f : 0.36f * edgeAlphaScale * dimFactor;
        NUIColor color = m_textSecondary.withAlpha(baseA);
        if (edge.type == Edge::SendPath) color = m_textInfo.withAlpha(edge.hovered ? 0.90f : 0.46f * edgeAlphaScale * dimFactor);
        if (edge.type == Edge::SidechainPath) color = m_warning.withAlpha(edge.hovered ? 0.90f : 0.52f * edgeAlphaScale * dimFactor);

        if (inTrace) {
            color = NUIColor(m_accent.r, m_accent.g, m_accent.b, 0.85f);
            if (edge.type == Edge::SendPath) color = NUIColor(m_textInfo.r, m_textInfo.g, m_textInfo.b, 0.85f);
            if (edge.type == Edge::SidechainPath) color = NUIColor(m_warning.r, m_warning.g, m_warning.b, 0.85f);
        }

        float thickness = edge.hovered ? 1.8f : 0.9f;
        if (inTrace) thickness = 2.2f;
        bool dashed = edge.type != Edge::MainPath;
        if (sourceMuted) dashed = true;

        drawBezier(renderer, {sx, sy}, {dx_, dy_}, thickness, color, dashed);

        // Live-wire pulse on MainPath edges with signal
        if (m_edges.size() <= 24 && edge.type == Edge::MainPath && src->peakDb > -60.0f && !src->muted && !soloSuppressed) {
            drawLivePulse(renderer, {sx, sy}, {dx_, dy_}, src->peakDb);
        }

        // Send-level label on SendPath / SidechainPath edges — only when hovered
        if ((edge.type == Edge::SendPath || edge.type == Edge::SidechainPath) && (edge.hovered || inTrace) && dimFactor > 0.2f) {
            drawSendLevelLabel(renderer, {sx, sy}, {dx_, dy_}, edge.sendLevelDb);
        }
    }

    // Selected edge highlight (drawn above edges but below nodes)
    if (m_selectedEdgeIdx >= 0 && m_selectedEdgeIdx < static_cast<int>(m_edges.size())) {
        const Edge& edge = m_edges[m_selectedEdgeIdx];
        const Node* src = nullptr;
        const Node* dst = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == edge.sourceNodeId) src = &n;
            if (n.id == edge.targetNodeId) dst = &n;
        }
        if (src && dst) {
            float sx = bounds.x + (src->x + src->outputX) * m_zoom + m_cameraX;
            float sy = canvasY + (src->y + src->outputY) * m_zoom + m_cameraY;
            float dx_ = bounds.x + (dst->x + dst->inputX) * m_zoom + m_cameraX;
            float dy_ = canvasY + edgeTargetY(edge, *dst) * m_zoom + m_cameraY;
            // Glow behind
            drawBezier(renderer, {sx, sy}, {dx_, dy_}, 6.0f, m_accent.withAlpha(0.22f), false);
            // Bright core
            drawBezier(renderer, {sx, sy}, {dx_, dy_}, 3.5f, m_accent.withAlpha(0.92f), false);
        }
    }

    // Nodes
    bool anyTraceActive = m_hoveredNodeIdx >= 0 && (!m_draggingConnection && !m_draggingNode && !m_draggingSend);
    for (size_t ni = 0; ni < m_nodes.size(); ++ni) {
        const auto& node = m_nodes[ni];
        drawNode(renderer, node, m_zoom);
        // Search highlight ring on matching nodes
        if (m_searchActive && !m_searchQuery.empty()) {
            std::string lowLabel, lowQuery;
            lowLabel.reserve(node.label.size());
            for (char c : node.label) lowLabel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            lowQuery.reserve(m_searchQuery.size());
            for (char c : m_searchQuery) lowQuery.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lowLabel.find(lowQuery) != std::string::npos) {
                float nx = bounds.x + node.x * m_zoom + m_cameraX;
                float ny = canvasY + node.y * m_zoom + m_cameraY;
                float nw = node.w * m_zoom;
                float nh = node.h * m_zoom;
                renderer.strokeRoundedRect({nx - 4.0f, ny - 4.0f, nw + 8.0f, nh + 8.0f}, 12.0f, 2.0f,
                                           m_accent.withAlpha(0.55f));
            }
        }
        // Trace dimming: non-trace nodes get a subtle dark overlay when trace is active
        if (anyTraceActive && ni != static_cast<size_t>(m_hoveredNodeIdx) &&
            !m_traceUpstreamMask[ni] && !m_traceDownstreamMask[ni]) {
            float nx = bounds.x + node.x * m_zoom + m_cameraX;
            float ny = canvasY + node.y * m_zoom + m_cameraY;
            float nw = node.w * m_zoom;
            float nh = node.h * m_zoom;
            renderer.fillRoundedRect({nx, ny, nw, nh}, 10.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.35f));
        }
    }

    // Highlight node being dragged
    if (m_mode == Mode::FullPanel && m_draggingNode && m_dragNodeIdx >= 0) {
        const Node& node = m_nodes[m_dragNodeIdx];
        float nx = bounds.x + node.x * m_zoom + m_cameraX;
        float ny = canvasY + node.y * m_zoom + m_cameraY;
        float nw = node.w * m_zoom;
        float nh = node.h * m_zoom;
        renderer.strokeRoundedRect({nx - 3.0f, ny - 3.0f, nw + 6.0f, nh + 6.0f}, 12.0f, 2.0f,
                                   m_accent.withAlpha(0.75f));
    }

    // Drag-to-reroute line (drawn above nodes)
    if (m_draggingConnection && m_dragSourceNodeIdx >= 0) {
        const Node& src = m_nodes[m_dragSourceNodeIdx];
        float sx = bounds.x + (src.x + src.outputX) * m_zoom + m_cameraX;
        float sy = canvasY + (src.y + src.outputY) * m_zoom + m_cameraY;
        drawBezier(renderer, {sx, sy}, m_dragCurrentPos, 2.0f, m_accent.withAlpha(0.92f), true);

        // Highlight valid drop target
        int targetIdx = hitTestNode(m_dragCurrentPos);
        if (targetIdx >= 0 && targetIdx != m_dragSourceNodeIdx) {
            const Node& tgt = m_nodes[targetIdx];
            float tx = bounds.x + tgt.x * m_zoom + m_cameraX;
            float ty = canvasY + tgt.y * m_zoom + m_cameraY;
            float tw = tgt.w * m_zoom;
            float th = tgt.h * m_zoom;
            renderer.strokeRoundedRect({tx, ty, tw, th}, 10.0f, 2.5f, m_accent.withAlpha(0.85f));
        }
    }

    // Drag-to-add-send line (drawn above nodes)
    if (m_draggingSend && m_dragSendSourceIdx >= 0) {
        const Node& src = m_nodes[m_dragSendSourceIdx];
        float sx = bounds.x + (src.x + src.inputX) * m_zoom + m_cameraX;
        float sy = canvasY + (src.y + src.inputY) * m_zoom + m_cameraY;
        drawBezier(renderer, {sx, sy}, m_dragSendCurrentPos, 2.0f, m_textInfo.withAlpha(0.92f), true);

        // Highlight valid drop target
        int targetIdx = hitTestNode(m_dragSendCurrentPos);
        if (targetIdx >= 0 && targetIdx != m_dragSendSourceIdx) {
            const Node& tgt = m_nodes[targetIdx];
            float tx = bounds.x + tgt.x * m_zoom + m_cameraX;
            float ty = canvasY + tgt.y * m_zoom + m_cameraY;
            float tw = tgt.w * m_zoom;
            float th = tgt.h * m_zoom;
            renderer.strokeRoundedRect({tx, ty, tw, th}, 10.0f, 2.5f, m_textInfo.withAlpha(0.85f));
        }
    }

    renderer.clearClipRect();

    // Edge-type legend — quiet chip in the canvas' bottom-left corner (the
    // standard node-editor spot), out of the title bar. Pill sized to content;
    // dot centres sit on the labels' optical middle.
    {
        constexpr float kLegFont = 9.0f;
        constexpr float kDotR = 3.0f;
        constexpr float kDotLabelGap = 6.0f;  // dot edge → label
        constexpr float kItemGap = 16.0f;     // label end → next dot
        constexpr float kPadX = 10.0f;
        constexpr float kPadY = 5.0f;

        struct LegendItem { const char* label; NUIColor color; bool hollow; };
        const LegendItem items[3] = {
            {"Route", m_textSecondary.withAlpha(0.70f), false},
            {"Send", m_textInfo.withAlpha(0.70f), true},
            {"Sidechain", m_warning.withAlpha(0.70f), true},
        };

        float labelW[3];
        float contentW = 0.0f;
        for (int i = 0; i < 3; ++i) {
            labelW[i] = renderer.measureText(items[i].label, kLegFont).width;
            contentW += kDotR * 2.0f + kDotLabelGap + labelW[i];
            if (i < 2) contentW += kItemGap;
        }

        const float pillH = kLegFont + kPadY * 2.0f + 2.0f;
        NUIRect legendBg{bounds.x + 14.0f, bounds.bottom() - pillH - 12.0f,
                         contentW + kPadX * 2.0f, pillH};
        renderer.fillRoundedRect(legendBg, 5.0f, m_bgTertiary.withAlpha(0.85f));
        renderer.strokeRoundedRect(legendBg, 5.0f, 1.0f, m_border.withAlpha(0.14f));

        const float textY = legendBg.y + kPadY;
        const float dotCY = textY + kLegFont * 0.55f; // optical middle of the label
        float cx = legendBg.x + kPadX;
        for (int i = 0; i < 3; ++i) {
            if (items[i].hollow) {
                renderer.strokeCircle({cx + kDotR, dotCY}, kDotR, 1.0f, items[i].color);
            } else {
                renderer.fillCircle({cx + kDotR, dotCY}, kDotR, items[i].color);
            }
            renderer.drawText(items[i].label, {cx + kDotR * 2.0f + kDotLabelGap, textY},
                              kLegFont, items[i].color.withAlpha(0.60f));
            cx += kDotR * 2.0f + kDotLabelGap + labelW[i] + kItemGap;
        }
    }

    // Left inspector panel
    renderInspector(renderer);

    // Tooltip for hovered edge (rendered after clip is cleared)
    if (m_hoveredEdgeIdx >= 0 && m_hoveredEdgeIdx < static_cast<int>(m_edges.size())) {
        const auto& edge = m_edges[m_hoveredEdgeIdx];
        if (edge.type != Edge::MainPath) {
            std::ostringstream tip;
            tip << "Send: " << std::fixed << std::setprecision(1) << edge.sendLevelDb << " dB";
            showRemoteTooltip(tip.str(), {bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f}, this);
        }
    } else {
        hideRemoteTooltip(this);
    }
}

void UIRoutingMap::drawNode(NUIRenderer& renderer, const Node& node, float scale) {
    NUIRect bounds = getBounds();
    constexpr float kFullTitleBarH = 44.0f;
    float canvasY = bounds.y + ((m_mode == Mode::FullPanel) ? kFullTitleBarH : 0.0f);
    float nx = bounds.x + node.x * scale + m_cameraX;
    float ny = canvasY + node.y * scale + m_cameraY;
    float nw = node.w * scale;
    float nh = node.h * scale;

    if (nx + nw < bounds.x || nx > bounds.x + bounds.width ||
        ny + nh < canvasY || ny > bounds.y + bounds.height) {
        return; // cull off-screen
    }

    NUIRect nodeRect(nx, ny, nw, nh);
    float radius = (m_mode == Mode::Minimap) ? 4.0f : 10.0f;

    NUIColor stripColor(((node.color >> 16) & 0xFF) / 255.0f,
                        ((node.color >> 8) & 0xFF) / 255.0f,
                        (node.color & 0xFF) / 255.0f,
                        ((node.color >> 24) & 0xFF) / 255.0f);

    if (m_mode == Mode::FullPanel) {
        // Flat node fill — no shadow, gradient, or inner highlight. Hover reads
        // as a single lighter fill; the master keeps its accent-tinted tone so
        // the destination stays visually distinct.
        NUIColor bg;
        if (node.type == Node::Master) {
            bg = node.hovered ? m_accent.withAlpha(0.28f) : m_accent.withAlpha(0.20f);
        } else {
            bg = node.hovered ? m_bgTertiary : m_bgSecondary;
        }
        renderer.fillRoundedRect(nodeRect, radius, bg);

        // Hover/selection/solo accent border
        bool isSelected = (m_viewModel &&
                           static_cast<int32_t>(node.id) == m_viewModel->getSelectedChannelId());
        NUIColor borderColor = m_border.withAlpha(0.30f);
        float borderThick = 1.0f;
        if (node.soloed) {
            borderColor = m_warning.withAlpha(0.9f);   // gold = soloed
            borderThick = 1.75f;
        } else if (isSelected) {
            borderColor = m_accent.withAlpha(0.85f);
            borderThick = 1.5f;
        } else if (node.hovered) {
            borderColor = m_accent.withAlpha(0.56f);
            borderThick = 1.25f;
        }
        renderer.strokeRoundedRect(nodeRect, radius, borderThick, borderColor);

        // Muted overlay: dim the entire node
        if (node.muted) {
            renderer.fillRoundedRect(nodeRect, radius, m_bg.withAlpha(0.72f));
        }

        if (node.type == Node::Master) {
            // Master: centered eyebrow + big title, no left color strip
            const float eyebrowFont = 9.5f;
            const float titleFont = 18.0f;
            const float subFont = 10.5f;
            const float spacing = 4.0f;
            const float stackH = eyebrowFont + spacing + titleFont + spacing + subFont;
            const float stackY = ny + (nh - stackH) * 0.5f;

            renderer.drawTextCentered("MAIN BUS",
                                      {nx, stackY, nw, eyebrowFont + 2.0f},
                                      eyebrowFont, m_accent.withAlpha(0.85f));
            renderer.drawTextCentered(node.label,
                                      {nx, stackY + eyebrowFont + spacing,
                                       nw, titleFont + 4.0f},
                                      titleFont, m_text.withAlpha(0.98f));
            renderer.drawTextCentered("Main output",
                                      {nx, stackY + eyebrowFont + spacing + titleFont + spacing,
                                       nw, subFont + 2.0f},
                                      subFont, m_textSecondary.withAlpha(0.72f));
        } else {
            // Track node: color strip + name + sub-label, left-aligned
            renderer.fillRoundedRect({nx, ny + 5.0f, 4.0f, nh - 10.0f}, 2.0f, stripColor.withAlpha(0.92f));

            const bool showName = nh >= 22.0f && nw > 50.0f;
            const bool showSubLabel = nh >= 40.0f && nw > 50.0f;
            const bool showMeter = nh >= 30.0f && nw > 60.0f;

            // Reserve right edge for M/S badges
            float rightPad = 8.0f;
            if (node.muted || node.soloed) rightPad += 20.0f;
            if (node.muted && node.soloed) rightPad += 18.0f;

            if (showName) {
                float nameFont = std::min(13.0f, std::max(10.0f, nh * 0.30f));
                std::string name = fitLabel(renderer, node.label, nameFont,
                                            nw - 14.0f - rightPad);
                float nameY = showSubLabel ? ny + 10.0f : ny + (nh - nameFont) * 0.5f;
                renderer.drawText(name, {nx + 14.0f, nameY}, nameFont,
                                  m_text.withAlpha(node.muted ? 0.55f : 0.96f));
            }

            if (showSubLabel) {
                std::string insertLabel = (node.insertCount == 0)
                                              ? "No inserts"
                                              : (std::to_string(node.insertCount) + " insert" +
                                                 (node.insertCount > 1 ? "s" : ""));
                renderer.drawText(insertLabel, {nx + 14.0f, ny + 28.0f}, 11.0f,
                                  m_textSecondary.withAlpha(node.muted ? 0.45f : 0.75f));
            }

            // M/S badges (top-right)
            float badgeX = nx + nw - 8.0f;
            if (node.soloed) {
                NUIRect bRect{badgeX - 16.0f, ny + 6.0f, 16.0f, 16.0f};
                renderer.fillRoundedRect(bRect, 4.0f, m_warning.withAlpha(0.85f));
                renderer.drawTextCentered("S", bRect, 10.0f, NUIColor::black());
                badgeX -= 20.0f;
            }
            if (node.muted) {
                NUIRect bRect{badgeX - 16.0f, ny + 6.0f, 16.0f, 16.0f};
                renderer.fillRoundedRect(bRect, 4.0f, m_error.withAlpha(0.85f));
                renderer.drawTextCentered("M", bRect, 10.0f, NUIColor::white());
            }

            // Routing warning badge (top-left, overlapping the color strip)
            if (node.hasRoutingWarning) {
                NUIRect wRect{nx + 6.0f, ny + 6.0f, 16.0f, 16.0f};
                renderer.fillRoundedRect(wRect, 4.0f, m_warning.withAlpha(0.9f));
                renderer.drawTextCentered("!", wRect, 11.0f, NUIColor::white());
            }

            // Insert-chain mini dots (left side, below the name/sub-label)
            if (nh >= 46.0f && nw > 50.0f && !node.insertNames.empty()) {
                drawInsertDots(renderer, node, nx, ny, nw, nh);
            }

            // Live peak meter (thin bar at bottom of node)
            if (showMeter && !node.muted) {
                float trackH_meter = 3.0f;
                float meterY = ny + nh - trackH_meter - 4.0f;
                NUIRect track{nx + 14.0f, meterY, nw - 24.0f, trackH_meter};
                renderer.fillRoundedRect(track, 1.5f, m_text.withAlpha(0.08f));

                // Map dB to 0..1: -60dB → 0, 0dB → 1
                float normalized = std::clamp((node.peakDb + 60.0f) / 60.0f, 0.0f, 1.0f);
                if (normalized > 0.0f) {
                    NUIRect fill{track.x, track.y, track.width * normalized, track.height};
                    NUIColor meterColor = stripColor.withAlpha(0.85f);
                    if (node.peakDb > -6.0f) {
                        // Hot: blend toward warning
                        meterColor = m_warning.withAlpha(0.95f);
                    }
                    renderer.fillRoundedRect(fill, 1.5f, meterColor);
                }
            }
        }

        // Ports
        float portR = std::max(3.5f, kPortRadius * scale);
        NUIColor portFill = m_bgTertiary;
        NUIColor portStroke = node.hovered ? m_accent.withAlpha(0.9f)
                                            : m_borderSecondary.withAlpha(0.85f);

        bool inHovered = (m_hoveredPortType == InputPort && m_hoveredPortNodeIdx == static_cast<int>(&node - m_nodes.data()));
        bool outHovered = (m_hoveredPortType == OutputPort && m_hoveredPortNodeIdx == static_cast<int>(&node - m_nodes.data()));

        // Input port (left side, all nodes)
        if (inHovered) {
            renderer.fillCircle({nx, ny + nh * 0.5f}, portR + 4.0f, m_accent.withAlpha(0.35f));
            renderer.fillCircle({nx, ny + nh * 0.5f}, portR + 2.0f, m_accent.withAlpha(0.55f));
        }
        renderer.fillCircle({nx, ny + nh * 0.5f}, portR, portFill);
        renderer.strokeCircle({nx, ny + nh * 0.5f}, portR, 1.5f, inHovered ? m_accent.withAlpha(0.98f) : portStroke);

        // Output port (right side, tracks only — master has no audio output here)
        if (node.type != Node::Master) {
            if (outHovered) {
                renderer.fillCircle({nx + nw, ny + nh * 0.5f}, portR + 4.0f, m_accent.withAlpha(0.35f));
                renderer.fillCircle({nx + nw, ny + nh * 0.5f}, portR + 2.0f, m_accent.withAlpha(0.55f));
            }
            renderer.fillCircle({nx + nw, ny + nh * 0.5f}, portR, portFill);
            renderer.strokeCircle({nx + nw, ny + nh * 0.5f}, portR, 1.5f, outHovered ? m_accent.withAlpha(0.98f) : portStroke);
        }
    } else {
        // === Minimap mode ===
        // Brighter overlay so nodes contrast against the dark card.
        NUIColor nodeFill = m_text.withAlpha(node.hovered ? 0.18f : 0.12f);
        renderer.fillRoundedRect(nodeRect, radius, nodeFill);

        // Border picks up solo/selection state too
        bool isSelected = (m_viewModel &&
                           static_cast<int32_t>(node.id) == m_viewModel->getSelectedChannelId());
        NUIColor strokeColor = m_text.withAlpha(0.22f);
        if (node.soloed) strokeColor = m_warning.withAlpha(0.95f);
        else if (isSelected) strokeColor = m_accent.withAlpha(0.85f);
        else if (node.hovered) strokeColor = m_accent.withAlpha(0.7f);
        renderer.strokeRoundedRect(nodeRect, radius, 1.0f, strokeColor);

        if (node.muted) {
            renderer.fillRoundedRect(nodeRect, radius,
                                      NUIColor(0.0f, 0.0f, 0.0f, 0.55f));
        }

        // Color strip (left edge) — skip for master so it's visually distinct
        if (node.type != Node::Master) {
            renderer.fillRect({nx, ny, 3.0f, nh}, stripColor);
        }

        // Label — auto-shrink font so master fits even in tight space
        if (nw > 18.0f) {
            float fontSize = std::min(10.0f, nh * 0.55f);
            // Shrink further if the label is wider than the node
            float labelW = renderer.measureText(node.label, fontSize).width;
            while (fontSize > 7.0f && labelW > nw - 10.0f) {
                fontSize -= 0.5f;
                labelW = renderer.measureText(node.label, fontSize).width;
            }
            if (fontSize >= 7.0f) {
                std::string label = fitLabel(renderer, node.label, fontSize, nw - 8.0f);
                float labelAlpha = node.muted ? 0.55f : 0.95f;
                if (node.type == Node::Master) {
                    // Master label is centered (no strip taking space)
                    renderer.drawTextCentered(label, nodeRect, fontSize,
                                              m_text.withAlpha(labelAlpha));
                } else {
                    renderer.drawText(label, {nx + 5.0f, ny + nh * 0.5f - fontSize * 0.4f},
                                      fontSize, m_text.withAlpha(labelAlpha));
                }
            }
        }
    }
}

// Note: edge rendering is handled inline in renderMinimap / renderFullPanel.
// The old drawEdge() stub has been removed.

void UIRoutingMap::drawBezier(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b,
                               float thickness, const NUIColor& color, bool dashed) {
    // Cubic bezier with a short horizontal lead/tail so the curve leaves the source
    // and enters the target perpendicular-ish, but heads toward target Y quickly
    // (so it avoids passing through intermediate same-row nodes).
    float dx = std::abs(b.x - a.x);
    float lead = std::min(dx * 0.35f, 40.0f * m_zoom + 20.0f);
    float cp1x = a.x + lead;
    float cp1y = a.y;
    float cp2x = b.x - lead;
    float cp2y = b.y;

    // Draw as polyline with adaptive subdivision
    constexpr int kSegments = 24;
    std::vector<NUIPoint> points;
    points.reserve(kSegments + 1);

    for (int i = 0; i <= kSegments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSegments);
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        float x = uuu * a.x + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + ttt * b.x;
        float y = uuu * a.y + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + ttt * b.y;
        points.push_back({x, y});
    }

    if (dashed) {
        // Draw dashed: 4px on, 4px off
        const float dashOn = 4.0f;
        const float dashOff = 4.0f;
        float accumulated = 0.0f;
        bool drawing = true;

        for (size_t i = 1; i < points.size(); ++i) {
            float segDx = points[i].x - points[i - 1].x;
            float segDy = points[i].y - points[i - 1].y;
            float segLen = std::sqrt(segDx * segDx + segDy * segDy);

            float segStart = accumulated;
            float segEnd = accumulated + segLen;
            float pos = segStart;

            while (pos < segEnd - 0.5f) {
                float dashPhase = std::fmod(pos, dashOn + dashOff);
                bool inDash = dashPhase < dashOn;

                float dashEnd = pos + (inDash ? (dashOn - dashPhase) : (dashOn + dashOff - dashPhase));
                dashEnd = std::min(dashEnd, segEnd);

                if (inDash) {
                    float t0 = (pos - segStart) / segLen;
                    float t1 = (dashEnd - segStart) / segLen;
                    NUIPoint p0{points[i - 1].x + segDx * t0, points[i - 1].y + segDy * t0};
                    NUIPoint p1{points[i - 1].x + segDx * t1, points[i - 1].y + segDy * t1};
                    renderer.drawLine(p0, p1, thickness, color);
                }

                pos = dashEnd;
            }

            accumulated = segEnd;
        }
    } else {
        renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
    }
}

void UIRoutingMap::drawDotGrid(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    constexpr float spacing = 24.0f;
    constexpr float dotRadius = 0.8f;
    NUIColor dotColor = m_border.withAlpha(0.055f);

    float startX = std::floor((bounds.x - m_cameraX) / spacing) * spacing + m_cameraX;
    float startY = std::floor((bounds.y - m_cameraY) / spacing) * spacing + m_cameraY;

    for (float y = startY; y < bounds.y + bounds.height; y += spacing) {
        for (float x = startX; x < bounds.x + bounds.width; x += spacing) {
            renderer.fillCircle({x, y}, dotRadius, dotColor);
        }
    }
}

int UIRoutingMap::hitTestNode(const NUIPoint& p) const {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        float nx, ny, nw, nh;
        if (m_mode == Mode::FullPanel) {
            NUIRect bounds = getBounds();
            float canvasY = bounds.y + 44.0f;
            nx = bounds.x + node.x * m_zoom + m_cameraX;
            ny = canvasY + node.y * m_zoom + m_cameraY;
            nw = node.w * m_zoom;
            nh = node.h * m_zoom;
        } else {
            nx = m_minimapOffsetX + (node.x - m_minimapCenterX) * m_minimapScale;
            ny = m_minimapOffsetY + (node.y - m_minimapCenterY) * m_minimapScale;
            nw = node.w * m_minimapScale;
            nh = node.h * m_minimapScale;
        }
        if (p.x >= nx && p.x <= nx + nw && p.y >= ny && p.y <= ny + nh) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int UIRoutingMap::hitTestEdge(const NUIPoint& p) const {
    // Simple distance-to-line-segment test on the bezier polyline
    constexpr float kHitDist = 6.0f;
    constexpr int kSegments = 12;

    for (size_t e = 0; e < m_edges.size(); ++e) {
        const auto& edge = m_edges[e];
        const Node* src = nullptr;
        const Node* dst = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == edge.sourceNodeId) src = &n;
            if (n.id == edge.targetNodeId) dst = &n;
        }
        if (!src || !dst) continue;

        float ax, ay, bx, by;
        if (m_mode == Mode::FullPanel) {
            NUIRect bounds = getBounds();
            float canvasY = bounds.y + 44.0f;
            ax = bounds.x + (src->x + src->outputX) * m_zoom + m_cameraX;
            ay = canvasY + (src->y + src->outputY) * m_zoom + m_cameraY;
            bx = bounds.x + (dst->x + dst->inputX) * m_zoom + m_cameraX;
            by = canvasY + edgeTargetY(edge, *dst) * m_zoom + m_cameraY;
        } else {
            ax = m_minimapOffsetX + (src->x + src->outputX - m_minimapCenterX) * m_minimapScale;
            ay = m_minimapOffsetY + (src->y + src->outputY - m_minimapCenterY) * m_minimapScale;
            bx = m_minimapOffsetX + (dst->x + dst->inputX - m_minimapCenterX) * m_minimapScale;
            by = m_minimapOffsetY + (edgeTargetY(edge, *dst) - m_minimapCenterY) * m_minimapScale;
        }

        float dx = std::abs(bx - ax);
        float cp1x = ax + dx * 0.5f;
        float cp1y = ay;
        float cp2x = bx - dx * 0.5f;
        float cp2y = by;

        for (int i = 0; i < kSegments; ++i) {
            float t0 = static_cast<float>(i) / kSegments;
            float t1 = static_cast<float>(i + 1) / kSegments;

            auto bezierPoint = [&](float t) -> NUIPoint {
                float u = 1.0f - t;
                float tt = t * t;
                float uu = u * u;
                float uuu = uu * u;
                float ttt = tt * t;
                float x = uuu * ax + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + ttt * bx;
                float y = uuu * ay + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + ttt * by;
                return {x, y};
            };

            NUIPoint a0 = bezierPoint(t0);
            NUIPoint b0 = bezierPoint(t1);

            // Distance from point p to line segment a0-b0
            float lx = b0.x - a0.x;
            float ly = b0.y - a0.y;
            float lenSq = lx * lx + ly * ly;
            float t = (lenSq > 0.0f) ? std::clamp(((p.x - a0.x) * lx + (p.y - a0.y) * ly) / lenSq, 0.0f, 1.0f) : 0.0f;
            float closestX = a0.x + t * lx;
            float closestY = a0.y + t * ly;
            float distSq = (p.x - closestX) * (p.x - closestX) + (p.y - closestY) * (p.y - closestY);
            if (distSq <= kHitDist * kHitDist) {
                return static_cast<int>(e);
            }
        }
    }
    return -1;
}

int UIRoutingMap::hitTestOutputPort(const NUIPoint& p) const {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        if (node.type == Node::Master) continue; // master has no output port
        float nx, ny, nw, nh;
        float scale;
        if (m_mode == Mode::FullPanel) {
            NUIRect bounds = getBounds();
            float canvasY = bounds.y + 44.0f;
            nx = bounds.x + node.x * m_zoom + m_cameraX;
            ny = canvasY + node.y * m_zoom + m_cameraY;
            nw = node.w * m_zoom;
            nh = node.h * m_zoom;
            scale = m_zoom;
        } else {
            nx = m_minimapOffsetX + (node.x - m_minimapCenterX) * m_minimapScale;
            ny = m_minimapOffsetY + (node.y - m_minimapCenterY) * m_minimapScale;
            nw = node.w * m_minimapScale;
            nh = node.h * m_minimapScale;
            scale = m_minimapScale;
        }
        float portCx = nx + nw;
        float portCy = ny + nh * 0.5f;
        float portR = std::max(3.5f, kPortRadius * scale);
        float dx = p.x - portCx;
        float dy = p.y - portCy;
        if (dx * dx + dy * dy <= portR * portR + 16.0f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void UIRoutingMap::onResize(int width, int height) {
    (void)width;
    (void)height;
    m_graphDirty = true;
    if (m_mode == Mode::FullPanel) m_fitPending = true;
}

bool UIRoutingMap::onMouseEvent(const NUIMouseEvent& event) {
    const NUIPoint& mouse = event.position;

    // In full-panel mode the bounds span the entire content area, so an early
    // bounds check prevents us from swallowing clicks meant for the title bar
    // or transport bar.  The minimap is already tightly clipped by its parent
    // layout, so we skip the guard there to avoid rejecting valid events when
    // bounds are briefly stale (e.g. before the first onRender).
    if (m_mode == Mode::FullPanel && !getBounds().contains(mouse)) {
        return false;
    }

    // Ensure hover state is current before handling presses / drags
    if (event.pressed || event.type == NUIMouseEventType::Move || event.type == NUIMouseEventType::Scroll) {
        int prevNode = m_hoveredNodeIdx;
        int prevEdge = m_hoveredEdgeIdx;
        m_hoveredNodeIdx = hitTestNode(mouse);
        m_hoveredEdgeIdx = (m_hoveredNodeIdx < 0) ? hitTestEdge(mouse) : -1;
        m_collapseHovered = (m_mode == Mode::FullPanel) && m_collapseButtonRect.contains(mouse);
        m_fitHovered = (m_mode == Mode::FullPanel) && m_fitButtonRect.contains(mouse);
        m_resetHovered = (m_mode == Mode::FullPanel) && m_resetButtonRect.contains(mouse);
        m_searchHovered = (m_mode == Mode::FullPanel) && m_searchRect.contains(mouse);
        bool overInspectorPanel = m_inspectorVisible && m_inspectorPanelRect.contains(mouse);
        m_inspectorCloseHovered = m_inspectorVisible && m_inspectorCloseRect.contains(mouse);
        // Port hover tracking (full panel only) — skip when over inspector or search
        m_hoveredPortType = NoPort;
        m_hoveredPortNodeIdx = -1;
        if (m_mode == Mode::FullPanel && m_hoveredNodeIdx < 0 && m_hoveredEdgeIdx < 0 &&
            !m_inspectorCloseHovered && !overInspectorPanel && !m_searchHovered) {
            int outPort = hitTestOutputPort(mouse);
            if (outPort >= 0) {
                m_hoveredPortType = OutputPort;
                m_hoveredPortNodeIdx = outPort;
            } else {
                int inPort = hitTestInputPort(mouse);
                if (inPort >= 0) {
                    m_hoveredPortType = InputPort;
                    m_hoveredPortNodeIdx = inPort;
                }
            }
        }
        for (auto& n : m_nodes) n.hovered = false;
        for (auto& e : m_edges) e.hovered = false;
        if (m_hoveredNodeIdx >= 0) {
            m_nodes[m_hoveredNodeIdx].hovered = true;
            setTooltip(m_nodes[m_hoveredNodeIdx].label +
                       "\n" + std::to_string(m_nodes[m_hoveredNodeIdx].insertCount) + " inserts");
        } else if (m_hoveredEdgeIdx >= 0) {
            m_edges[m_hoveredEdgeIdx].hovered = true;
            // Tooltip for edge type clarity
            const auto& edge = m_edges[m_hoveredEdgeIdx];
            if (edge.type == Edge::MainPath) {
                setTooltip("Main route");
            } else if (edge.type == Edge::SendPath) {
                setTooltip("Audio send\nRight-click to adjust level");
            } else if (edge.type == Edge::SidechainPath) {
                setTooltip("Sidechain send\nRight-click to adjust level");
            }
        } else {
            setTooltip("");
        }
        if (prevNode != m_hoveredNodeIdx || prevEdge != m_hoveredEdgeIdx) {
            repaint();
        }
    }

    if (m_mode == Mode::FullPanel) {
        // Search bar hit test
        if (event.pressed && event.button == NUIMouseButton::Left && m_searchRect.contains(mouse)) {
            m_searchFocused = true;
            setFocused(true); // Ensure routing map receives key events
            repaint();
            return true;
        }
        if (event.pressed && event.button == NUIMouseButton::Left && !m_searchRect.contains(mouse)) {
            bool overDropdown = false;
            for (size_t i = 0; i < m_searchMatches.size() && i < 5; ++i) {
                if (m_searchDropdownRects[i].contains(mouse)) { overDropdown = true; break; }
            }
            if (!overDropdown && !m_inspectorPanelRect.contains(mouse)) {
                m_searchFocused = false;
                m_searchMatches.clear();
                repaint();
            }
        }

        // Dropdown item click
        if (event.pressed && event.button == NUIMouseButton::Left) {
            for (size_t i = 0; i < m_searchMatches.size() && i < 5; ++i) {
                if (m_searchDropdownRects[i].contains(mouse)) {
                    int nodeIdx = m_searchMatches[i];
                    if (nodeIdx >= 0 && nodeIdx < static_cast<int>(m_nodes.size())) {
                        const auto& node = m_nodes[nodeIdx];
                        NUIRect bounds = getBounds();
                        float canvasY = bounds.y + 44.0f;
                        float targetX = bounds.x + node.x * m_zoom + m_cameraX;
                        float targetY = canvasY + node.y * m_zoom + m_cameraY;
                        float canvasCX = bounds.x + bounds.width * 0.5f;
                        float canvasCY = canvasY + (bounds.height - 44.0f) * 0.5f;
                        m_cameraX += (canvasCX - targetX);
                        m_cameraY += (canvasCY - targetY);
                    }
                    m_searchQuery.clear();
                    m_searchFocused = false;
                    m_searchMatches.clear();
                    repaint();
                    return true;
                }
            }
        }

        // Dropdown item hover tracking
        {
            int prevHover = m_searchHoveredMatch;
            m_searchHoveredMatch = -1;
            for (size_t i = 0; i < m_searchMatches.size() && i < 5; ++i) {
                if (m_searchDropdownRects[i].contains(mouse)) {
                    m_searchHoveredMatch = static_cast<int>(i);
                    break;
                }
            }
            if (prevHover != m_searchHoveredMatch) repaint();
        }

        // Button hit tests
        if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
            if (m_collapseButtonRect.contains(mouse)) {
                if (m_onCollapse) m_onCollapse();
                return true;
            }
            if (m_fitButtonRect.contains(mouse)) {
                m_fitPending = true;
                repaint();
                return true;
            }
            if (m_resetButtonRect.contains(mouse)) {
                m_zoom = 1.0f;
                m_targetZoom = 1.0f;
                m_cameraX = 0.0f;
                m_cameraY = 0.0f;
                m_zoomAnchorActive = false;
                m_fitPending = true;
                repaint();
                return true;
            }
        }

        // Drag-to-reroute: start on output port left-press
        if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode) {
            int portNode = hitTestOutputPort(mouse);
            if (portNode >= 0) {
                m_draggingConnection = true;
                m_dragSourceNodeIdx = portNode;
                m_dragCurrentPos = mouse;
                return true;
            }
        }

        // Drag-to-reposition: start on node body left-press (no modifier)
        if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
            bool shift = static_cast<int>(event.modifiers & NUIModifiers::Shift) != 0;
            if (m_hoveredNodeIdx >= 0) {
                if (shift) {
                    // Shift+click on node → toggle solo
                    uint32_t cid = m_nodes[m_hoveredNodeIdx].id;
                    if (m_onNodeSoloToggle) m_onNodeSoloToggle(cid);
                    return true;
                }
                m_draggingNode = true;
                m_dragNodeIdx = m_hoveredNodeIdx;
                m_dragNodeStartMouse = mouse;
                m_dragNodeStartPos = {m_nodes[m_hoveredNodeIdx].x, m_nodes[m_hoveredNodeIdx].y};
                return true;
            }
        }

        // Drag-to-add-send: start on input port left-press
        if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
            int portNode = hitTestInputPort(mouse);
            if (portNode >= 0 && m_nodes[portNode].type != Node::Master) {
                m_draggingSend = true;
                m_dragSendSourceIdx = portNode;
                m_dragSendCurrentPos = mouse;
                return true;
            }
        }

        // Dragging: update position (connection, node, or send)
        if (event.type == NUIMouseEventType::Move) {
            if (m_draggingConnection) {
                m_dragCurrentPos = mouse;
                repaint();
                return true;
            }
            if (m_draggingSend) {
                m_dragSendCurrentPos = mouse;
                repaint();
                return true;
            }
            if (m_draggingNode && m_dragNodeIdx >= 0) {
                float dx = (mouse.x - m_dragNodeStartMouse.x) / m_zoom;
                float dy = (mouse.y - m_dragNodeStartMouse.y) / m_zoom;
                m_nodes[m_dragNodeIdx].x = m_dragNodeStartPos.x + dx;
                m_nodes[m_dragNodeIdx].y = m_dragNodeStartPos.y + dy;
                repaint();
                return true;
            }
        }

        // Dragging: release -> drop (connection)
        if (!event.pressed && m_draggingConnection) {
            int targetIdx = hitTestNode(mouse);
            if (targetIdx >= 0 && targetIdx != m_dragSourceNodeIdx) {
                uint32_t sourceId = m_nodes[m_dragSourceNodeIdx].id;
                uint32_t targetId = m_nodes[targetIdx].id;
                if (m_onRerouteMain) m_onRerouteMain(sourceId, targetId);
                m_graphDirty = true; // Rebuild to show new routing
            }
            m_draggingConnection = false;
            m_dragSourceNodeIdx = -1;
            repaint();
            return true;
        }

        // Dragging: release -> drop (add send)
        if (!event.pressed && m_draggingSend) {
            int targetIdx = hitTestNode(mouse);
            if (targetIdx >= 0 && targetIdx != m_dragSendSourceIdx) {
                uint32_t sourceId = m_nodes[m_dragSendSourceIdx].id;
                uint32_t targetId = m_nodes[targetIdx].id;
                // Show send-type confirmation menu (Audio vs Sidechain)
                m_pendingSendSourceId = sourceId;
                m_pendingSendTargetId = targetId;
                m_sendTypeMenuPending = true;
                if (!m_sendTypeMenu) {
                    m_sendTypeMenu = std::make_shared<AestraUI::NUIContextMenu>();
                    m_sendTypeMenu->setCloseOnSelection(true);
                    m_sendTypeMenu->setOnHide([this]() {
                        m_sendTypeMenuPending = false;
                        m_pendingSendSourceId = 0;
                        m_pendingSendTargetId = 0;
                    });
                }
                m_sendTypeMenu->clear();
                m_sendTypeMenu->addItem("Audio Send", [this]() {
                    if (m_onAddSend) m_onAddSend(m_pendingSendSourceId, m_pendingSendTargetId, false);
                    m_graphDirty = true;
                    repaint();
                });
                m_sendTypeMenu->addItem("Sidechain Only", [this]() {
                    if (m_onAddSend) m_onAddSend(m_pendingSendSourceId, m_pendingSendTargetId, true);
                    m_graphDirty = true;
                    repaint();
                });
                // Attach to root and show at mouse position
                AestraUI::NUIComponent* root = this->getParent();
                while (root && root->getParent()) root = root->getParent();
                if (root) root->addChild(m_sendTypeMenu);
                m_sendTypeMenu->showAt(static_cast<int>(mouse.x), static_cast<int>(mouse.y));
            }
            m_draggingSend = false;
            m_dragSendSourceIdx = -1;
            repaint();
            return true;
        }

        // Dragging: release -> end (node). If barely moved, treat as click.
        if (!event.pressed && m_draggingNode) {
            float moveDist = std::abs(mouse.x - m_dragNodeStartMouse.x) +
                             std::abs(mouse.y - m_dragNodeStartMouse.y);
            if (moveDist < 8.0f && m_dragNodeIdx >= 0) {
                if (m_onNodeSelected) m_onNodeSelected(m_nodes[m_dragNodeIdx].id);
                // Inspector opens on double-click in full panel mode
                if (m_mode == Mode::FullPanel) {
                    auto now = std::chrono::steady_clock::now();
                    long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    bool isDouble = (nowMs - m_lastNodeClickTimeMs < 400) &&
                                    (m_lastNodeClickIdx == m_dragNodeIdx);
                    m_lastNodeClickTimeMs = nowMs;
                    m_lastNodeClickIdx = m_dragNodeIdx;
                    if (isDouble) {
                        m_inspectorVisible = true;
                        m_inspectorNodeIdx = m_dragNodeIdx;
                        m_inspectorScrollY = 0.0f;
                    }
                }
            } else if (m_dragNodeIdx >= 0) {
                // Grid snap to 20 px in world space
                constexpr float kGrid = 20.0f;
                m_nodes[m_dragNodeIdx].x = std::round(m_nodes[m_dragNodeIdx].x / kGrid) * kGrid;
                m_nodes[m_dragNodeIdx].y = std::round(m_nodes[m_dragNodeIdx].y / kGrid) * kGrid;
            }
            m_draggingNode = false;
            m_dragNodeIdx = -1;
            repaint();
            return true;
        }

        // Edge selection: left-click on an edge (when not on a node and not dragging)
        if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
            if (m_hoveredEdgeIdx >= 0 && m_hoveredNodeIdx < 0) {
                m_selectedEdgeIdx = m_hoveredEdgeIdx;
                repaint();
                return true;
            }
        }

        // Inspector close button
        if (event.pressed && event.button == NUIMouseButton::Left && m_inspectorVisible && m_inspectorCloseHovered) {
            m_inspectorVisible = false;
            m_inspectorNodeIdx = -1;
            m_inspectorScrollY = 0.0f;
            repaint();
            return true;
        }

        // Click on empty canvas while inspector is open → close inspector
        // (but not when clicking inside the inspector panel itself)
        bool overInspector = m_inspectorVisible && m_inspectorPanelRect.contains(mouse);
        if (event.pressed && event.button == NUIMouseButton::Left && m_inspectorVisible &&
            m_hoveredNodeIdx < 0 && m_hoveredEdgeIdx < 0 && !m_inspectorCloseHovered &&
            !m_collapseHovered && !m_fitHovered && !m_resetHovered && !overInspector && !m_searchHovered) {
            m_inspectorVisible = false;
            m_inspectorNodeIdx = -1;
            m_inspectorScrollY = 0.0f;
            repaint();
        }

        // Right-click context menu on node, or inline edge level edit
        if (event.pressed && event.button == NUIMouseButton::Right && !m_draggingConnection && !m_draggingNode && !m_draggingSend) {
            if (m_hoveredNodeIdx >= 0) {
                uint32_t cid = m_nodes[m_hoveredNodeIdx].id;
                if (!m_nodeContextMenu) {
                    m_nodeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
                    m_nodeContextMenu->setCloseOnSelection(true);
                }
                m_nodeContextMenu->clear();
                m_nodeContextMenu->addItem("Solo", [this, cid]() {
                    if (m_onNodeSoloToggle) m_onNodeSoloToggle(cid);
                });
                m_nodeContextMenu->addItem("Mute", [this, cid]() {
                    if (m_onNodeMuteToggle) m_onNodeMuteToggle(cid);
                });
                m_nodeContextMenu->addSeparator();
                m_nodeContextMenu->addItem("Inspector", [this]() {
                    if (m_hoveredNodeIdx >= 0) {
                        m_inspectorVisible = true;
                        m_inspectorNodeIdx = m_hoveredNodeIdx;
                        m_inspectorScrollY = 0.0f;
                        repaint();
                    }
                });
                AestraUI::NUIComponent* root = this->getParent();
                while (root && root->getParent()) root = root->getParent();
                if (root) root->addChild(m_nodeContextMenu);
                m_nodeContextMenu->showAt(static_cast<int>(mouse.x), static_cast<int>(mouse.y));
                return true;
            }
            if (m_hoveredEdgeIdx >= 0) {
                const Edge& edge = m_edges[m_hoveredEdgeIdx];
                if (edge.type != Edge::MainPath && edge.sendIndex >= 0) {
                    if (!m_edgeContextMenu) {
                        m_edgeContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
                        m_edgeContextMenu->setCloseOnSelection(true);
                    }
                    m_edgeContextMenu->clear();
                    uint32_t srcId = edge.sourceNodeId;
                    int sIdx = edge.sendIndex;
                    // Level submenu
                    auto levelMenu = std::make_shared<AestraUI::NUIContextMenu>();
                    static const float kLevelVals[] = {-144.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f};
                    static const char* kLevelLabels[] = {"-inf dB", "-24 dB", "-12 dB", "-6 dB", "0 dB", "+6 dB"};
                    for (size_t li = 0; li < 6; ++li) {
                        levelMenu->addItem(kLevelLabels[li], [this, srcId, sIdx, li]() {
                            if (m_onEditSendLevel) m_onEditSendLevel(srcId, sIdx, kLevelVals[li]);
                            m_graphDirty = true;
                            repaint();
                        });
                    }
                    m_edgeContextMenu->addSubmenu("Set Level", levelMenu);
                    m_edgeContextMenu->addSeparator();
                    m_edgeContextMenu->addItem("Delete Send", [this, srcId, sIdx]() {
                        if (m_onRemoveSend) m_onRemoveSend(srcId, sIdx);
                        m_graphDirty = true;
                        m_selectedEdgeIdx = -1;
                        repaint();
                    });
                    AestraUI::NUIComponent* root = this->getParent();
                    while (root && root->getParent()) root = root->getParent();
                    if (root) root->addChild(m_edgeContextMenu);
                    m_edgeContextMenu->showAt(static_cast<int>(mouse.x), static_cast<int>(mouse.y));
                } else if (edge.type == Edge::MainPath) {
                    // No action for main path edges
                }
                return true;
            }
        }

        // Cancel any drag or pan on right-click press
        if (event.pressed && event.button == NUIMouseButton::Right) {
            if (m_panning) {
                m_panning = false;
                repaint();
                return true;
            }
            if (m_middlePanning) {
                m_middlePanning = false;
                repaint();
                return true;
            }
            if (m_draggingConnection) {
                m_draggingConnection = false;
                m_dragSourceNodeIdx = -1;
                repaint();
                return true;
            }
            if (m_draggingSend) {
                m_draggingSend = false;
                m_dragSendSourceIdx = -1;
                repaint();
                return true;
            }
            if (m_draggingNode) {
                // Revert to start position
                if (m_dragNodeIdx >= 0) {
                    m_nodes[m_dragNodeIdx].x = m_dragNodeStartPos.x;
                    m_nodes[m_dragNodeIdx].y = m_dragNodeStartPos.y;
                }
                m_draggingNode = false;
                m_dragNodeIdx = -1;
                repaint();
                return true;
            }
        }

        // Pan start (left-click on empty canvas, or middle-click anywhere)
        bool overUI = m_searchHovered || m_collapseHovered || m_fitHovered || m_resetHovered ||
                      (m_inspectorVisible && m_inspectorPanelRect.contains(mouse)) ||
                      (m_inspectorVisible && m_inspectorCloseHovered);
        if (event.pressed && !m_draggingConnection && !m_draggingNode && !m_draggingSend && !overUI) {
            if (event.button == NUIMouseButton::Middle) {
                m_middlePanning = true;
                m_middlePanStartMouse = event.position;
                m_middlePanStartCameraX = m_cameraX;
                m_middlePanStartCameraY = m_cameraY;
                return true;
            }
            if (event.button == NUIMouseButton::Left && m_hoveredNodeIdx < 0 && m_hoveredEdgeIdx < 0) {
                m_panning = true;
                m_panStartMouse = event.position;
                m_panStartCameraX = m_cameraX;
                m_panStartCameraY = m_cameraY;
                return true;
            }
        }

        // Pan end (only on Up, not Move)
        if (event.released && m_panning && event.button == NUIMouseButton::Left) {
            m_panning = false;
            return true;
        }
        if (event.released && m_middlePanning && event.button == NUIMouseButton::Middle) {
            m_middlePanning = false;
            return true;
        }

        // Panning
        if (m_panning && event.type == NUIMouseEventType::Move) {
            float dx = event.position.x - m_panStartMouse.x;
            float dy = event.position.y - m_panStartMouse.y;
            m_cameraX = m_panStartCameraX + dx;
            m_cameraY = m_panStartCameraY + dy;
            clampCamera();
            repaint();
            return true;
        }
        if (m_middlePanning && event.type == NUIMouseEventType::Move) {
            float dx = event.position.x - m_middlePanStartMouse.x;
            float dy = event.position.y - m_middlePanStartMouse.y;
            m_cameraX = m_middlePanStartCameraX + dx;
            m_cameraY = m_middlePanStartCameraY + dy;
            clampCamera();
            repaint();
            return true;
        }

        // Inspector scroll (takes priority over canvas zoom)
        if (event.type == NUIMouseEventType::Scroll && m_inspectorVisible &&
            m_inspectorPanelRect.contains(mouse)) {
            m_inspectorScrollY -= event.wheelDelta * 20.0f;
            if (m_inspectorScrollY < 0.0f) m_inspectorScrollY = 0.0f;
            if (m_inspectorScrollY > 400.0f) m_inspectorScrollY = 400.0f;
            repaint();
            return true;
        }

        // Zoom (centered on mouse cursor, small wheelDelta-scaled steps)
        if (event.type == NUIMouseEventType::Scroll) {
            NUIRect bounds = getBounds();
            float canvasY = bounds.y + 44.0f;
            float oldZoom = m_zoom;
            float zoomDelta = event.wheelDelta * 0.025f;
            m_zoom = std::clamp(m_zoom * (1.0f + zoomDelta), 0.2f, 4.0f);
            // Keep the world point under the mouse at the same screen position
            float worldX = (mouse.x - bounds.x - m_cameraX) / oldZoom;
            float worldY = (mouse.y - canvasY - m_cameraY) / oldZoom;
            m_cameraX = mouse.x - bounds.x - worldX * m_zoom;
            m_cameraY = mouse.y - canvasY - worldY * m_zoom;
            clampCamera();
            repaint();
            return true;
        }
    }

    // Click / double-click (minimap only — full-panel clicks handled via drag-release above)
    if (event.pressed && event.button == NUIMouseButton::Left && !m_draggingConnection && !m_draggingNode) {
        auto now = std::chrono::steady_clock::now();
        long long nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        bool isDouble = event.doubleClick || (nowMs - m_lastClickTimeMs < 400);
        m_lastClickTimeMs = nowMs;

        if (isDouble && m_mode == Mode::Minimap) {
            if (m_onDoubleClick) m_onDoubleClick();
            return true;
        }

        if (m_hoveredNodeIdx >= 0 && m_mode == Mode::Minimap) {
            uint32_t channelId = m_nodes[m_hoveredNodeIdx].id;
            if (m_onNodeSelected) m_onNodeSelected(channelId);
            return true;
        }
    }

    if (event.type == NUIMouseEventType::DoubleClick && m_mode == Mode::Minimap) {
        if (m_onDoubleClick) m_onDoubleClick();
        return true;
    }

    return false;
}

namespace {
    std::string toLowerCopy(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }
}

void UIRoutingMap::recomputeSearchMatches() {
    m_searchMatches.clear();
    if (m_searchQuery.empty()) return;
    std::string q = toLowerCopy(m_searchQuery);
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        std::string label = toLowerCopy(m_nodes[i].label);
        if (label.find(q) != std::string::npos) {
            m_searchMatches.push_back(static_cast<int>(i));
            if (m_searchMatches.size() >= 5) break;
        }
    }
}

bool UIRoutingMap::onKeyEvent(const NUIKeyEvent& event) {
    if (!event.pressed) return false;

    // Search bar text input (when focused)
    if (m_searchFocused && m_mode == Mode::FullPanel) {
        if (event.keyCode == NUIKeyCode::Escape) {
            m_searchQuery.clear();
            m_searchFocused = false;
            m_searchActive = false;
            m_searchMatches.clear();
            repaint();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Backspace || event.keyCode == NUIKeyCode::Delete) {
            if (!m_searchQuery.empty()) {
                m_searchQuery.pop_back();
                m_searchActive = !m_searchQuery.empty();
                recomputeSearchMatches();
                repaint();
            }
            return true;
        }
        if (event.keyCode == NUIKeyCode::Enter) {
            m_searchActive = !m_searchQuery.empty();
            if (!m_searchMatches.empty()) {
                int nodeIdx = m_searchMatches[0];
                if (m_searchHoveredMatch >= 0 && m_searchHoveredMatch < static_cast<int>(m_searchMatches.size())) {
                    nodeIdx = m_searchMatches[m_searchHoveredMatch];
                }
                if (nodeIdx >= 0 && nodeIdx < static_cast<int>(m_nodes.size())) {
                    const auto& node = m_nodes[nodeIdx];
                    NUIRect bounds = getBounds();
                    float canvasY = bounds.y + 44.0f;
                    float targetX = bounds.x + node.x * m_zoom + m_cameraX;
                    float targetY = canvasY + node.y * m_zoom + m_cameraY;
                    float canvasCX = bounds.x + bounds.width * 0.5f;
                    float canvasCY = canvasY + (bounds.height - 44.0f) * 0.5f;
                    m_cameraX += (canvasCX - targetX);
                    m_cameraY += (canvasCY - targetY);
                }
            }
            m_searchQuery.clear();
            m_searchFocused = false;
            m_searchMatches.clear();
            repaint();
            return true;
        }
        if (event.keyCode == NUIKeyCode::Down) {
            if (!m_searchMatches.empty()) {
                m_searchHoveredMatch = (m_searchHoveredMatch + 1) % static_cast<int>(m_searchMatches.size());
                repaint();
            }
            return true;
        }
        if (event.keyCode == NUIKeyCode::Up) {
            if (!m_searchMatches.empty()) {
                int count = static_cast<int>(m_searchMatches.size());
                m_searchHoveredMatch = (m_searchHoveredMatch <= 0) ? count - 1 : m_searchHoveredMatch - 1;
                repaint();
            }
            return true;
        }
        if (event.character >= 32 && event.character < 127) {
            m_searchQuery.push_back(event.character);
            m_searchActive = true;
            m_searchHoveredMatch = 0;
            recomputeSearchMatches();
            repaint();
            return true;
        }
    }

    // Escape: close inspector first, then search, then let parent close the overlay
    if (event.keyCode == NUIKeyCode::Escape) {
        if (m_inspectorVisible) {
            m_inspectorVisible = false;
            m_inspectorNodeIdx = -1;
            m_inspectorScrollY = 0.0f;
            repaint();
            return true;
        }
        if (m_searchFocused) {
            m_searchQuery.clear();
            m_searchFocused = false;
            m_searchActive = false;
            m_searchMatches.clear();
            repaint();
            return true;
        }
        return false; // Let AestraContent close the routing map overlay
    }

    // F key: fit to view
    if (event.keyCode == NUIKeyCode::F && !m_searchFocused) {
        m_fitPending = true;
        repaint();
        return true;
    }

    if (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) {
        if (m_selectedEdgeIdx >= 0 && m_selectedEdgeIdx < static_cast<int>(m_edges.size())) {
            const Edge& edge = m_edges[m_selectedEdgeIdx];
            if (edge.type != Edge::MainPath && edge.sendIndex >= 0) {
                uint32_t sourceId = edge.sourceNodeId;
                int sendIdx = edge.sendIndex;
                if (m_onRemoveSend) {
                    m_onRemoveSend(sourceId, sendIdx);
                    m_graphDirty = true;
                }
            }
            m_selectedEdgeIdx = -1;
            repaint();
            return true;
        }
    }
    if (event.keyCode == NUIKeyCode::Escape) {
        if (m_searchFocused) {
            m_searchQuery.clear();
            m_searchFocused = false;
            m_searchActive = false;
            repaint();
            return true;
        }
        if (m_selectedEdgeIdx >= 0) {
            m_selectedEdgeIdx = -1;
            repaint();
            return true;
        }
        if (m_mode == Mode::FullPanel && m_onCollapse) {
            m_onCollapse();
            return true;
        }
    }
    return false;
}

void UIRoutingMap::drawLivePulse(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b, float peakDb) {
    // Number of pulses scales with signal level; louder = more dots
    float normalized = std::clamp((peakDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    int pulseCount = std::max(1, static_cast<int>(normalized * 4.0f));
    float speed = 0.4f + normalized * 1.2f; // faster when loud

    float dx = std::abs(b.x - a.x);
    float cp1x = a.x + dx * 0.5f;
    float cp1y = a.y;
    float cp2x = b.x - dx * 0.5f;
    float cp2y = b.y;

    for (int p = 0; p < pulseCount; ++p) {
        float phase = m_livePulsePhase * speed + static_cast<float>(p) / pulseCount;
        phase = phase - std::floor(phase);

        float t = phase;
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float x = uu * u * a.x + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + tt * t * b.x;
        float y = uu * u * a.y + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + tt * t * b.y;

        // Color shifts from accent (purple) toward warning (orange) as signal gets hot
        NUIColor pulseColor = m_accent;
        if (peakDb > -6.0f) {
            float hotness = std::clamp((peakDb + 6.0f) / 6.0f, 0.0f, 1.0f);
            pulseColor = NUIColor(
                m_accent.r + (m_warning.r - m_accent.r) * hotness,
                m_accent.g + (m_warning.g - m_accent.g) * hotness,
                m_accent.b + (m_warning.b - m_accent.b) * hotness,
                0.92f
            );
        }
        // Fade out near start/end of travel for smooth appearance
        float fade = 1.0f;
        if (t < 0.15f) fade = t / 0.15f;
        if (t > 0.85f) fade = (1.0f - t) / 0.15f;

        float radius = 2.5f + normalized * 2.0f;
        renderer.fillCircle({x, y}, radius, pulseColor.withAlpha(0.85f * fade));
    }
}

void UIRoutingMap::drawSendLevelLabel(NUIRenderer& renderer, const NUIPoint& a, const NUIPoint& b, float sendLevelDb) {
    float mx = (a.x + b.x) * 0.5f;
    float my = (a.y + b.y) * 0.5f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f dB", sendLevelDb);
    float fontSize = 9.0f;
    float textW = renderer.measureText(buf, fontSize).width;
    float textH = fontSize + 2.0f;
    NUIRect bg{mx - textW * 0.5f - 3.0f, my - textH * 0.5f, textW + 6.0f, textH};
    renderer.fillRoundedRect(bg, 3.0f, m_bgSecondary.withAlpha(0.85f));
    renderer.strokeRoundedRect(bg, 3.0f, 0.5f, m_borderSecondary.withAlpha(0.4f));
    renderer.drawTextCentered(buf, bg, fontSize, m_textSecondary.withAlpha(0.85f));
}

NUIColor UIRoutingMap::resolveInsertColor(const std::string& name) const {
    auto& tm = NUIThemeManager::getInstance();
    std::string low;
    low.reserve(name.size());
    for (char c : name) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (low.find("eq") != std::string::npos || low.find("filter") != std::string::npos)
        return tm.getColor("accentCyan").withAlpha(0.92f);
    if (low.find("comp") != std::string::npos || low.find("limit") != std::string::npos)
        return tm.getColor("accentAmber").withAlpha(0.92f);
    if (low.find("reverb") != std::string::npos || low.find("delay") != std::string::npos)
        return tm.getColor("info").withAlpha(0.92f);
    if (low.find("dist") != std::string::npos || low.find("sat") != std::string::npos || low.find("drive") != std::string::npos)
        return tm.getColor("accentRed").withAlpha(0.92f);
    if (low.find("chorus") != std::string::npos || low.find("flang") != std::string::npos || low.find("phaser") != std::string::npos)
        return tm.getColor("accentLime").withAlpha(0.92f);
    if (low.find("sampler") != std::string::npos || low.find("sample") != std::string::npos)
        return tm.getColor("warning").withAlpha(0.92f);
    return tm.getColor("accentPrimary").withAlpha(0.92f);
}

void UIRoutingMap::drawInsertDots(NUIRenderer& renderer, const Node& node, float nx, float ny, float nw, float nh) {
    (void)nw;
    float dotR = 3.0f;
    float gap = 6.0f;
    float startX = nx + 14.0f;
    float startY = ny + nh - dotR * 2.0f - 10.0f;
    for (size_t i = 0; i < node.insertNames.size(); ++i) {
        NUIColor col = resolveInsertColor(node.insertNames[i]);
        renderer.fillCircle({startX + i * (dotR * 2.0f + gap), startY}, dotR, col);
        renderer.strokeCircle({startX + i * (dotR * 2.0f + gap), startY}, dotR, 0.5f, m_text.withAlpha(0.25f));
    }
}

int UIRoutingMap::hitTestInputPort(const NUIPoint& p) const {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        float nx, ny, nh;
        float scale;
        if (m_mode == Mode::FullPanel) {
            NUIRect bounds = getBounds();
            float canvasY = bounds.y + 44.0f;
            nx = bounds.x + node.x * m_zoom + m_cameraX;
            ny = canvasY + node.y * m_zoom + m_cameraY;
            nh = node.h * m_zoom;
            scale = m_zoom;
        } else {
            nx = m_minimapOffsetX + (node.x - m_minimapCenterX) * m_minimapScale;
            ny = m_minimapOffsetY + (node.y - m_minimapCenterY) * m_minimapScale;
            nh = node.h * m_minimapScale;
            scale = m_minimapScale;
        }
        float portCx = nx;
        float portCy = ny + nh * 0.5f;
        float portR = std::max(3.5f, kPortRadius * scale);
        float dx = p.x - portCx;
        float dy = p.y - portCy;
        if (dx * dx + dy * dy <= portR * portR + 16.0f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void UIRoutingMap::renderMiniOverview(NUIRenderer& renderer) {
    if (m_mode != Mode::FullPanel) return;
    if (m_worldMaxX <= m_worldMinX || m_worldMaxY <= m_worldMinY) return;

    NUIRect bounds = getBounds();
    constexpr float kFullTitleBarH = 44.0f;
    float canvasY = bounds.y + kFullTitleBarH;

    // Inset dimensions
    float insetW = 140.0f;
    float insetH = 100.0f;
    float insetX = bounds.right() - insetW - 12.0f;
    float insetY = bounds.bottom() - insetH - 12.0f;

    // Avoid overlapping the collapse button area
    if (insetY < bounds.y + kFullTitleBarH + 30.0f) return;

    // Compute scale to fit world bounds into inset
    float worldW = m_worldMaxX - m_worldMinX + kTrackNodeW;
    float worldH = m_worldMaxY - m_worldMinY + kTrackNodeH;
    float scaleX = insetW / worldW;
    float scaleY = insetH / worldH;
    float scale = std::min(scaleX, scaleY) * 0.92f;

    float offsetX = insetX + insetW * 0.5f - (m_worldMinX + worldW * 0.5f) * scale;
    float offsetY = insetY + insetH * 0.5f - (m_worldMinY + worldH * 0.5f) * scale;

    // Background
    renderer.fillRoundedRect({insetX, insetY, insetW, insetH}, 6.0f, m_bgSecondary.withAlpha(0.92f));
    renderer.strokeRoundedRect({insetX, insetY, insetW, insetH}, 6.0f, 1.0f, m_border.withAlpha(0.35f));

    // Draw all edges as thin faint lines
    for (const auto& edge : m_edges) {
        const Node* src = nullptr;
        const Node* dst = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == edge.sourceNodeId) src = &n;
            if (n.id == edge.targetNodeId) dst = &n;
        }
        if (!src || !dst) continue;
        float sx = offsetX + (src->x + src->outputX) * scale;
        float sy = offsetY + (src->y + src->outputY) * scale;
        float dx_ = offsetX + (dst->x + dst->inputX) * scale;
        float dy_ = offsetY + edgeTargetY(edge, *dst) * scale;
        NUIColor col = m_textSecondary.withAlpha(0.25f);
        if (edge.type == Edge::SendPath) col = m_textInfo.withAlpha(0.30f);
        if (edge.type == Edge::SidechainPath) col = m_warning.withAlpha(0.30f);
        drawBezier(renderer, {sx, sy}, {dx_, dy_}, 0.5f, col, edge.type != Edge::MainPath);
    }

    // Draw all nodes as tiny rects
    for (const auto& node : m_nodes) {
        float nx = offsetX + node.x * scale;
        float ny = offsetY + node.y * scale;
        float nw = node.w * scale;
        float nh = node.h * scale;
        NUIColor fill = m_bgTertiary.withAlpha(0.85f);
        if (node.type == Node::Master) fill = m_accent.withAlpha(0.24f);
        renderer.fillRoundedRect({nx, ny, nw, nh}, 2.0f, fill);
        renderer.strokeRoundedRect({nx, ny, nw, nh}, 2.0f, 0.5f, m_border.withAlpha(0.4f));
    }

    // Viewport rectangle overlay
    float vpX = offsetX + (-m_cameraX / m_zoom) * scale;
    float vpY = offsetY + (-(canvasY - bounds.y - kFullTitleBarH + m_cameraY) / m_zoom) * scale;
    float vpW = (bounds.width / m_zoom) * scale;
    float vpH = ((bounds.height - kFullTitleBarH) / m_zoom) * scale;
    renderer.strokeRoundedRect({vpX, vpY, vpW, vpH}, 3.0f, 1.5f, m_accent.withAlpha(0.75f));
}

void UIRoutingMap::renderInspector(NUIRenderer& renderer) {
    if (m_mode != Mode::FullPanel) return;
    if (!m_inspectorVisible || m_inspectorNodeIdx < 0 || m_inspectorNodeIdx >= static_cast<int>(m_nodes.size())) return;
    if (!m_viewModel) return;

    const Node& node = m_nodes[m_inspectorNodeIdx];
    const Aestra::ChannelViewModel* ch = m_viewModel->getChannelById(node.id);
    if (!ch && node.id == 0) ch = m_viewModel->getMaster();
    if (!ch) return;

    NUIRect bounds = getBounds();
    constexpr float kFullTitleBarH = 44.0f;
    float canvasY = bounds.y + kFullTitleBarH;

    // Compute screen position of the clicked node so we can anchor the inspector near it
    float nodeScreenX = bounds.x + node.x * m_zoom + m_cameraX;
    float nodeScreenY = canvasY + node.y * m_zoom + m_cameraY;
    float nodeScreenW = node.w * m_zoom;
    float nodeScreenH = node.h * m_zoom;

    constexpr float kInspectorW = 240.0f;
    float insetX = nodeScreenX + nodeScreenW + 16.0f; // default: float to the right
    float insetY = nodeScreenY;
    // If it would overflow the right edge, flip to the left of the node
    if (insetX + kInspectorW > bounds.x + bounds.width - 12.0f) {
        insetX = nodeScreenX - kInspectorW - 16.0f;
    }
    // Clamp vertically within canvas
    float maxInsetH = 420.0f;
    float insetH = std::min(maxInsetH, bounds.y + bounds.height - insetY - 12.0f);
    if (insetY < canvasY + 8.0f) {
        insetY = canvasY + 8.0f;
        insetH = std::min(maxInsetH, bounds.y + bounds.height - insetY - 12.0f);
    }
    if (insetH < 140.0f) {
        insetY = canvasY + 8.0f;
        insetH = std::min(maxInsetH, bounds.y + bounds.height - insetY - 12.0f);
    }
    // Clamp horizontally
    if (insetX < bounds.x + 8.0f) insetX = bounds.x + 8.0f;
    if (insetX + kInspectorW > bounds.right() - 8.0f) insetX = bounds.right() - kInspectorW - 8.0f;

    // Remember panel rect for hit testing
    m_inspectorPanelRect = NUIRect{insetX, insetY, kInspectorW, insetH};

    // Background
    renderer.fillRoundedRect(m_inspectorPanelRect, 8.0f, m_bgSecondary.withAlpha(0.96f));
    renderer.strokeRoundedRect(m_inspectorPanelRect, 8.0f, 1.0f, m_border.withAlpha(0.45f));

    // Clip content to panel interior (scrollable area)
    renderer.setClipRect(NUIRect{insetX + 2.0f, insetY + 6.0f, kInspectorW - 4.0f, insetH - 12.0f});

    float y = insetY + 14.0f - m_inspectorScrollY;
    float left = insetX + 12.0f;
    float right = insetX + kInspectorW - 12.0f;
    float textW = right - left;

    // Color strip at top
    NUIColor stripColor(((node.color >> 16) & 0xFF) / 255.0f,
                        ((node.color >> 8) & 0xFF) / 255.0f,
                        (node.color & 0xFF) / 255.0f,
                        ((node.color >> 24) & 0xFF) / 255.0f);
    renderer.fillRoundedRect({insetX + 1.0f, insetY + 1.0f, kInspectorW - 2.0f, 4.0f}, 3.0f, stripColor);

    // Header: name + type
    {
        std::string typeLabel = (node.type == Node::Master) ? "MASTER BUS" : "TRACK";
        renderer.drawText(typeLabel, {left, y}, 10.0f, m_textSecondary.withAlpha(0.7f));
        float typeW = renderer.measureText(typeLabel, 10.0f).width;
        std::string name = fitLabel(renderer, ch->name.empty() ? node.label : ch->name, 13.0f, textW - typeW - 10.0f);
        renderer.drawText(name, {left + typeW + 8.0f, y - 1.0f}, 13.0f, m_text.withAlpha(0.95f));
        y += 24.0f;
    }

    // M/S badges inline
    if (ch->muted || ch->soloed) {
        float badgeX = left;
        if (ch->soloed) {
            NUIRect bRect{badgeX, y, 20.0f, 18.0f};
            renderer.fillRoundedRect(bRect, 4.0f, m_warning.withAlpha(0.85f));
            renderer.drawTextCentered("S", bRect, 10.0f, NUIColor::black());
            badgeX += 26.0f;
        }
        if (ch->muted) {
            NUIRect bRect{badgeX, y, 20.0f, 18.0f};
            renderer.fillRoundedRect(bRect, 4.0f, m_error.withAlpha(0.85f));
            renderer.drawTextCentered("M", bRect, 10.0f, NUIColor::white());
        }
        y += 24.0f;
    }

    // Routing warning
    if (node.hasRoutingWarning) {
        std::string warn = m_viewModel->getRoutingWarning(node.id);
        renderer.fillRoundedRect({left, y, textW, 20.0f}, 4.0f, m_warning.withAlpha(0.18f));
        renderer.strokeRoundedRect({left, y, textW, 20.0f}, 4.0f, 1.0f, m_warning.withAlpha(0.5f));
        std::string wtext = fitLabel(renderer, warn, 10.0f, textW - 8.0f);
        renderer.drawText(wtext, {left + 6.0f, y + 3.0f}, 10.0f, m_warning.withAlpha(0.9f));
        y += 28.0f;
    }

    // Divider
    renderer.drawLine({left, y}, {right, y}, 1.0f, m_border.withAlpha(0.25f));
    y += 10.0f;

    // === MAIN OUTPUT ===
    renderer.drawText("Main Output", {left, y}, 10.0f, m_textSecondary.withAlpha(0.65f));
    y += 14.0f;
    {
        std::string destName = ch->routeName.empty() ? "Master" : ch->routeName;
        renderer.drawText(destName, {left + 8.0f, y}, 12.0f, m_text.withAlpha(0.85f));
        if (!ch->masterSendEnabled) {
            float dnw = renderer.measureText(destName, 12.0f).width;
            renderer.drawText("(bypassed)", {left + 8.0f + dnw + 6.0f, y}, 10.0f, m_warning.withAlpha(0.75f));
        }
        y += 20.0f;
    }

    // Divider
    renderer.drawLine({left, y}, {right, y}, 1.0f, m_border.withAlpha(0.25f));
    y += 10.0f;

    // === INSERT CHAIN ===
    renderer.drawText("Insert Chain", {left, y}, 10.0f, m_textSecondary.withAlpha(0.65f));
    y += 16.0f;
    // Count filled inserts
    size_t filledInserts = 0;
    for (const auto& in : ch->inserts) if (!in.isEmpty) ++filledInserts;
    if (filledInserts == 0) {
        renderer.fillRoundedRect({left + 8.0f, y, textW - 16.0f, 22.0f}, 4.0f, m_bgSecondary.withAlpha(0.35f));
        renderer.strokeRoundedRect({left + 8.0f, y, textW - 16.0f, 22.0f}, 4.0f, 0.5f, m_accent.withAlpha(0.4f));
        renderer.drawTextCentered("+ Add plugin", {left + 8.0f, y, textW - 16.0f, 22.0f}, 11.0f, m_accent.withAlpha(0.75f));
        y += 28.0f;
    } else {
        for (size_t i = 0; i < ch->inserts.size(); ++i) {
            const auto& insert = ch->inserts[i];
            if (insert.isEmpty) continue;
            NUIColor slotBg = insert.bypassed ? m_bgSecondary.withAlpha(0.35f) : m_bgSecondary.withAlpha(0.55f);
            renderer.fillRoundedRect({left + 8.0f, y, textW - 16.0f, 20.0f}, 4.0f, slotBg);
            renderer.strokeRoundedRect({left + 8.0f, y, textW - 16.0f, 20.0f}, 4.0f, 0.5f,
                                       insert.bypassed ? m_border.withAlpha(0.3f) : m_accent.withAlpha(0.45f));
            std::string slotLabel = fitLabel(renderer, insert.name, 11.0f, textW - 50.0f);
            renderer.drawText(slotLabel, {left + 14.0f, y + 3.0f}, 11.0f,
                              insert.bypassed ? m_textSecondary.withAlpha(0.5f) : m_text.withAlpha(0.88f));
            if (insert.bypassed) {
                float slw = renderer.measureText(slotLabel, 11.0f).width;
                renderer.drawText("bypass", {left + 14.0f + slw + 6.0f, y + 4.0f}, 9.0f, m_warning.withAlpha(0.65f));
            }
            if (insert.mix < 0.99f) {
                char mixBuf[16];
                std::snprintf(mixBuf, sizeof(mixBuf), "%.0f%%", insert.mix * 100.0f);
                float mixW = renderer.measureText(mixBuf, 9.0f).width;
                renderer.drawText(mixBuf, {right - mixW - 6.0f, y + 4.0f}, 9.0f, m_textInfo.withAlpha(0.7f));
            }
            y += 24.0f;
        }
    }

    // Divider
    renderer.drawLine({left, y}, {right, y}, 1.0f, m_border.withAlpha(0.25f));
    y += 10.0f;

    // === SENDS ===
    renderer.drawText("Sends", {left, y}, 10.0f, m_textSecondary.withAlpha(0.65f));
    y += 16.0f;
    if (ch->sends.empty()) {
        renderer.drawText("No sends", {left + 8.0f, y}, 11.0f, m_textSecondary.withAlpha(0.5f));
        y += 18.0f;
    } else {
        for (size_t i = 0; i < ch->sends.size(); ++i) {
            const auto& send = ch->sends[i];
            renderer.fillRoundedRect({left + 8.0f, y, textW - 16.0f, 22.0f}, 4.0f, m_bgSecondary.withAlpha(0.35f));
            renderer.strokeRoundedRect({left + 8.0f, y, textW - 16.0f, 22.0f}, 4.0f, 0.5f, m_border.withAlpha(0.25f));

            std::string dest = send.targetName.empty() ? "?" : send.targetName;
            std::string sendLabel = fitLabel(renderer, dest, 11.0f, textW - 80.0f);
            renderer.drawText(sendLabel, {left + 14.0f, y + 4.0f}, 11.0f, m_text.withAlpha(0.85f));

            float metaX = left + 14.0f;
            float metaY = y + 14.0f;
            float sendDb = gainToDb(send.gain);
            char dbBuf[16];
            std::snprintf(dbBuf, sizeof(dbBuf), "%.1f dB", sendDb);
            renderer.drawText(dbBuf, {metaX, metaY}, 9.0f, m_textInfo.withAlpha(0.7f));
            metaX += renderer.measureText(dbBuf, 9.0f).width + 8.0f;

            if (send.sidechainOnly) {
                renderer.drawText("sidechain", {metaX, metaY}, 9.0f, m_warning.withAlpha(0.75f));
                metaX += renderer.measureText("sidechain", 9.0f).width + 8.0f;
            }
            if (!send.postFader) {
                renderer.drawText("pre", {metaX, metaY}, 9.0f, m_textSecondary.withAlpha(0.7f));
                metaX += renderer.measureText("pre", 9.0f).width + 8.0f;
            }
            if (send.muted) {
                renderer.drawText("muted", {metaX, metaY}, 9.0f, m_error.withAlpha(0.75f));
            }

            y += 28.0f;
        }
    }

    // Clear clip so close button and scroll shadow render outside the content area
    renderer.clearClipRect();

    // Close button (top-right of panel)
    m_inspectorCloseRect = NUIRect{insetX + kInspectorW - 26.0f, insetY + 8.0f, 18.0f, 18.0f};
    renderer.fillRoundedRect(m_inspectorCloseRect, 5.0f,
                             m_inspectorCloseHovered ? m_warning.withAlpha(0.45f)
                                                      : m_bgTertiary.withAlpha(0.4f));
    renderer.drawTextCentered("x", m_inspectorCloseRect, 12.0f, m_textSecondary.withAlpha(0.85f));
}

} // namespace AestraUI
