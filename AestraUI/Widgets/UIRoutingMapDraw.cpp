// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Drawing for the routing graph (V81-R1). Layout, state and input live in UIRoutingMap.cpp.
#include "UIRoutingMap.h"

#include "ChannelDisplayName.h"
#include "MixerViewModel.h"
#include "TrackColorPalette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kListHeadH = 76.0f;
constexpr float kListRowH = 30.0f;
constexpr uint32_t kNoOutput = 0xFFFFFFFFu;

NUIColor argb(uint32_t c) {
    return NUIColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string fit(NUIRenderer& r, const std::string& text, float size, float maxW) {
    if (r.measureText(text, size).width <= maxW) return text;
    std::string s = text;
    while (!s.empty() && r.measureText(s + "\xE2\x80\xA6", size).width > maxW) s.pop_back();
    return s + "\xE2\x80\xA6";
}

std::string formatDb(float db) {
    if (db <= -100.0f) return "-inf dB";
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(db));
    std::string s = buf;
    if (s[0] == '-') s = "\xE2\x88\x92" + s.substr(1); // a real minus sign
    if (db == 0.0f) s = "0.0 dB";
    return s;
}

float gainToDbStatic(float g) { return g <= 0.0f ? -144.0f : 20.0f * std::log10(g); }

bool hasMod(NUIModifiers mods, NUIModifiers m) { return (static_cast<int>(mods) & static_cast<int>(m)) != 0; }

void dashedRect(NUIRenderer& r, const NUIRect& rc, const NUIColor& c) {
    auto dash = [&](NUIPoint a, NUIPoint b) {
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        for (float t = 0; t < len; t += 8.0f) {
            const float t2 = std::min(len, t + 4.0f);
            r.drawLine({a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len},
                       {a.x + (b.x - a.x) * t2 / len, a.y + (b.y - a.y) * t2 / len}, 1.0f, c);
        }
    };
    dash({rc.x, rc.y}, {rc.right(), rc.y});
    dash({rc.right(), rc.y}, {rc.right(), rc.bottom()});
    dash({rc.right(), rc.bottom()}, {rc.x, rc.bottom()});
    dash({rc.x, rc.bottom()}, {rc.x, rc.y});
}
} // namespace

void UIRoutingMap::drawWire(NUIRenderer& r, NUIPoint a, NUIPoint b, float thickness, const NUIColor& c, bool dashed,
                            bool vertical) {
    // Horizontal tangents for outputs and sends; a sidechain drops onto the top of its node, so it ends vertical.
    const float dx = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
    const NUIPoint c1{a.x + dx, a.y};
    const NUIPoint c2 = vertical ? NUIPoint{b.x, b.y - std::max(60.0f, std::abs(b.y - a.y) * 0.6f)}
                                 : NUIPoint{b.x - dx, b.y};
    constexpr int kSteps = 40;
    NUIPoint pts[kSteps + 1];
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps, u = 1.0f - t;
        pts[i] = {u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
                  u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y};
    }
    if (std::abs(a.y - b.y) < 0.5f && !vertical) {
        if (!dashed) {
            r.drawLine(a, b, thickness, c);
            return;
        }
    }
    if (!dashed) {
        r.drawPolyline(pts, kSteps + 1, thickness, c);
        return;
    }
    for (int i = 0; i < kSteps; i += 2) r.drawLine(pts[i], pts[i + 1], thickness, c);
}

void UIRoutingMap::onRender(NUIRenderer& r) {
    const NUIRect b = getBounds();
    if (b.isEmpty()) return;
    const Geometry g = geometry();
    r.fillRect(b, m_bg);

    r.setClipRect(g.canvas);
    drawColumnHeadings(r, g);
    drawEdges(r, g);
    drawNodes(r, g);
    if (m_nodes.empty()) drawEmpty(r, g);
    r.clearClipRect();

    drawRail(r, g);
    drawDirectList(r, g);
    drawHeader(r, g);
    if (m_selectedId != 0 && !m_patching) drawDetail(r, g);
    else if (!m_patching) drawLegend(r, g);
    if (m_patching) drawPatch(r, g);
}

void UIRoutingMap::drawHeader(NUIRenderer& r, const Geometry& g) {
    const NUIRect& h = g.header;
    r.fillRect(h, m_panel);
    r.drawLine({h.x, h.bottom() - 0.5f}, {h.right(), h.bottom() - 0.5f}, 1.0f, m_border);
    r.drawText("Routing", {h.x + 16.0f, h.y + 16.0f}, 14.0f, m_text);
    const float titleW = r.measureText("Routing", 14.0f).width;
    const float sx = h.x + 16.0f + titleW + 16.0f;

    // Right side, right to left: close, search, Signal flow, Custom routes | All tracks.
    m_buttons[4] = {h.right() - 40.0f, h.y + 10.0f, 28.0f, 28.0f};
    m_buttons[3] = {m_buttons[4].x - 34.0f, h.y + 10.0f, 28.0f, 28.0f};
    const float flowLabelW = r.measureText("Signal flow", 11.0f).width;
    m_buttons[2] = {m_buttons[3].x - 24.0f - (32.0f + flowLabelW), h.y + 10.0f, 32.0f + flowLabelW, 28.0f};
    const float segW1 = r.measureText("Custom routes", 11.0f).width + 28.0f;
    const float segW2 = r.measureText("All tracks", 11.0f).width + 28.0f;
    m_buttons[1] = {m_buttons[2].x - 24.0f - segW2, h.y + 11.0f, segW2, 26.0f};
    m_buttons[0] = {m_buttons[1].x - segW1, h.y + 11.0f, segW1, 26.0f};

    // Close and search glyphs
    {
        const NUIRect& c = m_buttons[4];
        const NUIColor col = m_hoverButton == 4 ? m_text : m_textSecondary;
        const float cx = c.x + c.width * 0.5f, cy = c.y + c.height * 0.5f;
        r.drawLine({cx - 5, cy - 5}, {cx + 5, cy + 5}, 1.4f, col);
        r.drawLine({cx + 5, cy - 5}, {cx - 5, cy + 5}, 1.4f, col);
        const NUIRect& s = m_buttons[3];
        const NUIColor sc = (m_hoverButton == 3 || m_searchOpen) ? m_text : m_textSecondary;
        const float scx = s.x + s.width * 0.5f - 1.0f, scy = s.y + s.height * 0.5f - 1.0f;
        r.strokeCircle({scx, scy}, 5.0f, 1.4f, sc);
        r.drawLine({scx + 3.6f, scy + 3.6f}, {scx + 7.5f, scy + 7.5f}, 1.4f, sc);
    }
    // Signal flow switch
    {
        const NUIRect& f = m_buttons[2];
        const NUIRect track{f.x, f.y + 9.0f, 24.0f, 12.0f};
        r.fillRoundedRect(track, 6.0f, m_signalFlow ? m_live.withAlpha(0.45f) : m_raised);
        r.fillCircle({m_signalFlow ? track.right() - 6.0f : track.x + 6.0f, track.y + 6.0f}, 4.0f,
                     m_signalFlow ? m_live : m_textMuted);
        r.drawText("Signal flow", {track.right() + 8.0f, f.y + 8.0f}, 11.0f,
                   m_hoverButton == 2 ? m_text : m_textSecondary);
    }
    // Segment
    {
        const NUIRect seg{m_buttons[0].x, m_buttons[0].y, m_buttons[0].width + m_buttons[1].width, 26.0f};
        r.fillRoundedRect(seg, 2.0f, m_bg);
        r.strokeRoundedRect(seg, 2.0f, 1.0f, m_border);
        const NUIRect on = m_allTracks ? m_buttons[1] : m_buttons[0];
        r.fillRoundedRect({on.x + 2, on.y + 2, on.width - 4, on.height - 4}, 2.0f, m_raised);
        r.drawTextCentered("Custom routes", m_buttons[0], 11.0f, m_allTracks ? m_textSecondary : m_text);
        r.drawTextCentered("All tracks", m_buttons[1], 11.0f, m_allTracks ? m_text : m_textSecondary);
    }

    // Summary, or the search field
    const float maxSummaryW = std::max(40.0f, m_buttons[0].x - sx - 16.0f);
    if (m_searchOpen) {
        const NUIRect box{sx, h.y + 11.0f, std::min(240.0f, maxSummaryW), 26.0f};
        r.fillRoundedRect(box, 2.0f, m_bg);
        r.strokeRoundedRect(box, 2.0f, 1.0f, m_accent.withAlpha(0.7f));
        const bool empty = m_search.empty();
        r.drawText(empty ? "Find a channel" : m_search, {box.x + 8.0f, box.y + 7.0f}, 11.0f,
                   empty ? m_textMuted : m_text);
        if (!empty) {
            const float cw = r.measureText(m_search, 11.0f).width;
            r.fillRect({box.x + 9.0f + cw, box.y + 6.0f, 1.0f, 14.0f}, m_text);
        }
        return;
    }
    std::string summary;
    if (m_patching) {
        const Node* s = nodeById(m_patchSource);
        std::string name;
        for (const auto& d : m_direct)
            if (d.id == m_patchSource) name = d.label;
        summary = "Dragging " + (s ? s->label : name) + " output";
    } else if (m_selectedId != 0) {
        std::string name;
        if (const Node* n = nodeById(m_selectedId)) name = n->label;
        for (const auto& d : m_direct)
            if (d.id == m_selectedId) name = d.label;
        summary = name + "  \xC2\xB7  upstream " + std::to_string(m_trace.upstream.size() - 1) +
                  "  \xC2\xB7  downstream " + std::to_string(m_trace.downstream.size() - 1);
    } else {
        auto plural = [](int n, const char* one, const char* many) {
            return std::to_string(n) + " " + (n == 1 ? one : many);
        };
        if (m_plan.customRoutes + m_plan.sends + m_plan.sidechains == 0) summary = "No custom routes";
        else
            summary = plural(m_plan.customRoutes, "custom route", "custom routes") + "  \xC2\xB7  " +
                      plural(m_plan.sends, "send", "sends") + "  \xC2\xB7  " +
                      plural(m_plan.sidechains, "sidechain", "sidechains");
    }
    r.drawText(fit(r, summary, 11.0f, maxSummaryW), {sx, h.y + 18.0f}, 11.0f, m_textMuted);
}

void UIRoutingMap::drawDirectList(NUIRenderer& r, const Geometry& g) {
    const NUIRect& l = g.list;
    if (l.width <= 0) return;
    r.fillRect(l, m_panel);
    r.drawLine({l.right() - 0.5f, l.y}, {l.right() - 0.5f, l.bottom()}, 1.0f, m_border);
    const bool dim = (m_selectedId != 0 || m_patching);
    const float a = dim ? 0.45f : 1.0f;
    r.drawText("DIRECT TO MASTER", {l.x + 16.0f, l.y + 18.0f}, 9.5f, m_textMuted.withAlpha(a));
    const std::string count = std::to_string(m_direct.size());
    r.drawText(count, {l.right() - 16.0f - r.measureText(count, 9.5f).width, l.y + 18.0f}, 9.5f,
               m_textMuted.withAlpha(a));
    r.drawText("No custom routing. Drag one onto", {l.x + 16.0f, l.y + 36.0f}, 10.0f, m_textMuted.withAlpha(a * 0.85f));
    r.drawText("a channel or Master to route it.", {l.x + 16.0f, l.y + 50.0f}, 10.0f, m_textMuted.withAlpha(a * 0.85f));

    r.setClipRect({l.x, l.y + kListHeadH, l.width, l.height - kListHeadH});
    float y = l.y + kListHeadH - m_listScroll;
    for (size_t i = 0; i < m_direct.size(); ++i, y += kListRowH) {
        if (y + kListRowH < l.y + kListHeadH || y > l.bottom()) continue;
        const auto& d = m_direct[i];
        const NUIRect row{l.x, y, l.width - 1.0f, kListRowH};
        const bool sel = d.id == m_selectedId;
        bool isTarget = false;
        if (m_patching) {
            isTarget = static_cast<int>(i) == hitDirect(m_patchMouse) && d.id != m_patchSource && !refuses(d.id);
        }
        if (sel) r.fillRect(row, m_accent.withAlpha(0.16f));
        else if (isTarget) r.fillRect(row, m_raised);
        else if (static_cast<int>(i) == m_hoverDirect && !m_patching) r.fillRect(row, m_raised.withAlpha(0.6f));
        const float rowA = (sel || isTarget) ? 1.0f : a;
        r.fillRect({l.x + 18.0f, y + 11.0f, 8.0f, 8.0f}, argb(d.color).withAlpha(rowA));
        r.drawText(fit(r, d.label, 12.0f, l.width - 110.0f), {l.x + 34.0f, y + 8.0f}, 12.0f,
                   m_textSecondary.withAlpha(rowA));
        const char* where = "Master";
        if (m_viewModel)
            if (const auto* ch = m_viewModel->getChannelById(d.id); ch && !ch->masterSendEnabled) where = "Off";
        r.drawText(where, {l.right() - 16.0f - r.measureText(where, 10.0f).width, y + 10.0f}, 10.0f,
                   m_textMuted.withAlpha(rowA * 0.8f));
    }
    r.clearClipRect();
}

void UIRoutingMap::drawColumnHeadings(NUIRenderer& r, const Geometry& g) {
    if (m_zoom < 0.5f) return;
    for (int c = 0; c < m_plan.columns; ++c) {
        const float x = g.canvas.x + m_cameraX + static_cast<float>(c) * 288.0f * m_zoom;
        // Headings ride with the graph (40 world units above the first row), so nodes never slide under them.
        const float y = g.canvas.y + m_cameraY - 40.0f * m_zoom;
        if (y < g.canvas.y + 4.0f) continue;
        r.drawText(RoutingGraph::columnHeading(c, m_channels, m_plan), {x, y}, 9.5f, m_textMuted);
    }
}

void UIRoutingMap::drawEdges(NUIRenderer& r, const Geometry& g) {
    const bool tracing = m_selectedId != 0 && !m_patching;
    for (const auto& e : m_edges) {
        const Node* s = nodeById(e.source);
        if (!s) continue;
        const Node* t = e.target == RoutingGraph::kMaster ? nullptr : nodeById(e.target);
        if (e.target != RoutingGraph::kMaster && !t) continue;
        const NUIPoint a = outputPort(*s);
        NUIPoint bpt = t ? (e.type == Edge::Sidechain ? sidechainPort(*t) : inputPort(*t))
                         : NUIPoint{g.rail.x, railPortY(a.y)};

        bool inChain = false;
        if (tracing) {
            const bool up = m_trace.upstream.count(e.source) && t && m_trace.upstream.count(e.target);
            const bool down = m_trace.downstream.count(e.source) > 0 && !m_trace.listening.count(e.source);
            inChain = up || down;
        }
        NUIColor c = e.type == Edge::Output ? m_textSecondary.withAlpha(0.7f)
                     : e.type == Edge::Send ? m_textMuted.withAlpha(0.55f)
                                            : m_sidechain.withAlpha(0.85f);
        float th = e.type == Edge::Output ? 1.4f : 1.2f;
        if (s->muted) c = c.withAlpha(c.a * 0.5f);
        if (tracing) {
            if (inChain) {
                c = e.type == Edge::Sidechain ? m_sidechain : m_text;
                th = 2.0f;
            } else {
                c = c.withAlpha(c.a * 0.3f);
            }
        }
        if (m_patching) c = c.withAlpha(c.a * 0.5f);
        drawWire(r, a, bpt, th, c, e.type == Edge::Sidechain, e.type == Edge::Sidechain);

        if (inChain && e.type == Edge::Send && m_zoom >= 0.5f) {
            // The level sits beside the wire on the side away from its neighbours: above a level or rising
            // wire, below a falling one.
            const std::string lbl = formatDb(e.levelDb);
            const float w = r.measureText(lbl, 9.5f).width + 12.0f;
            const float lx = a.x + (bpt.x - a.x) * 0.45f - w * 0.5f;
            const float ly = bpt.y > a.y + 1.0f ? std::max(a.y, bpt.y) + 8.0f : std::min(a.y, bpt.y) - 26.0f;
            const NUIRect box{lx, ly, w, 18.0f};
            r.fillRoundedRect(box, 2.0f, m_node);
            r.strokeRoundedRect(box, 2.0f, 1.0f, m_border);
            r.drawTextCentered(lbl, box, 9.5f, m_textSecondary);
        }
        if (m_signalFlow && e.type != Edge::Sidechain && !s->muted && s->peakDb > -60.0f) {
            for (int k = 0; k < 3; ++k) {
                const float t01 = std::fmod(m_flowPhase + k / 3.0f, 1.0f);
                const float dx = std::max(40.0f, std::abs(bpt.x - a.x) * 0.5f);
                const NUIPoint c1{a.x + dx, a.y}, c2{bpt.x - dx, bpt.y};
                const float u = 1.0f - t01;
                const NUIPoint q{u * u * u * a.x + 3 * u * u * t01 * c1.x + 3 * u * t01 * t01 * c2.x + t01 * t01 * t01 * bpt.x,
                                 u * u * u * a.y + 3 * u * u * t01 * c1.y + 3 * u * t01 * t01 * c2.y + t01 * t01 * t01 * bpt.y};
                r.fillCircle(q, 2.5f, m_live.withAlpha(0.9f));
            }
        }
    }
}

void UIRoutingMap::drawNodes(NUIRenderer& r, const Geometry&) {
    const bool tracing = m_selectedId != 0 && !m_patching;
    const std::string q = lower(m_search);
    bool patchTargetMaster = false;
    const uint32_t patchTo = m_patching ? patchTarget(m_patchMouse, patchTargetMaster) : kNoOutput;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const Node& n = m_nodes[i];
        const NUIRect rc = nodeRect(n);
        const NUIColor col = argb(n.color);
        if (m_zoom < 0.5f) {
            // Far: colour blocks only.
            r.fillRect(rc, col.withAlpha(tracing && !m_trace.contains(n.id) ? 0.3f : 0.9f));
            if (n.id == m_selectedId) r.strokeRect(rc, 1.5f, m_accent);
            continue;
        }
        const bool refused = m_patching && refuses(n.id);
        r.fillRoundedRect(rc, 2.0f, m_node);
        r.strokeRoundedRect(rc, 2.0f, 1.0f, static_cast<int>(i) == m_hoverNode && !m_patching ? m_textMuted : m_border);
        if (refused) {
            // Hatched: this one would loop.
            r.setClipRect(rc);
            for (float x = rc.x - rc.height; x < rc.right(); x += 8.0f)
                r.drawLine({x, rc.bottom()}, {x + rc.height, rc.y}, 1.0f, m_textMuted.withAlpha(0.18f));
            r.clearClipRect();
            r.setClipRect(geometry().canvas);
        }

        const float z = m_zoom;
        const float cy = rc.y + rc.height * 0.5f;
        r.fillRect({rc.x + 14.0f * z, cy - 5.0f * z, 10.0f * z, 10.0f * z}, col);
        const float font = std::max(8.0f, 12.0f * z);
        // The role tag needs room beside the name; below 75 % it would collide, so only LOOP survives.
        const char* tag = refused                                         ? "LOOP"
                          : z < 0.75f                                     ? ""
                          : n.role == RoutingGraph::Role::Bus    ? "BUS"
                          : n.role == RoutingGraph::Role::Return ? "RETURN"
                                                                 : "";
        const float tagFont = std::max(7.0f, 8.5f * z);
        const float tagW = *tag ? r.measureText(tag, tagFont).width : 0.0f;
        r.drawText(fit(r, n.label, font, rc.width - 44.0f * z - tagW), {rc.x + 30.0f * z, cy - font * 0.6f}, font,
                   n.muted ? m_textMuted : m_text);
        if (*tag)
            r.drawText(tag, {rc.right() - 12.0f * z - tagW, cy - tagFont * 0.6f}, tagFont,
                       refused ? m_refuse : m_textMuted);

        // Ports: the output lights with level.
        const NUIPoint in = inputPort(n), out = outputPort(n);
        r.fillCircle(in, 3.5f * z, m_bg);
        r.strokeCircle(in, 3.5f * z, 1.0f, m_textMuted);
        const bool lit = i < m_lit.size() && m_lit[i];
        r.fillCircle(out, 3.5f * z, lit ? m_live : m_bg);
        r.strokeCircle(out, 3.5f * z, 1.0f, lit ? m_live : m_textMuted);

        bool sidechained = false;
        for (const auto& e : m_edges) sidechained |= e.type == Edge::Sidechain && e.target == n.id;
        if (sidechained) {
            const NUIPoint d = sidechainPort(n);
            const float s = 4.5f * z;
            r.fillRect({d.x - 1, d.y - 1, 2, 2}, m_sidechain);
            r.drawLine({d.x, d.y - s}, {d.x + s, d.y}, 1.2f, m_sidechain);
            r.drawLine({d.x + s, d.y}, {d.x, d.y + s}, 1.2f, m_sidechain);
            r.drawLine({d.x, d.y + s}, {d.x - s, d.y}, 1.2f, m_sidechain);
            r.drawLine({d.x - s, d.y}, {d.x, d.y - s}, 1.2f, m_sidechain);
        }

        if (tracing && !m_trace.contains(n.id)) r.fillRoundedRect(rc, 2.0f, m_bg.withAlpha(0.62f));
        if (refused) r.fillRoundedRect(rc, 2.0f, m_bg.withAlpha(0.4f));
        if (n.id == m_selectedId && !m_patching)
            r.strokeRoundedRect({rc.x - 1, rc.y - 1, rc.width + 2, rc.height + 2}, 2.0f, 1.5f, m_accent);
        if (m_patching && n.id == m_patchSource)
            r.strokeRoundedRect({rc.x - 1, rc.y - 1, rc.width + 2, rc.height + 2}, 2.0f, 1.5f, m_accent);
        if (m_patching && n.id == patchTo && !refused) r.strokeRoundedRect(rc, 2.0f, 1.5f, m_text);
        if (!q.empty() && lower(n.label).find(q) != std::string::npos)
            r.strokeRoundedRect({rc.x - 4, rc.y - 4, rc.width + 8, rc.height + 8}, 3.0f, 1.5f, m_accent.withAlpha(0.7f));
    }
    // Near (from 140 %): the selected node opens into its chain, drawn over its neighbours.
    if (m_zoom >= 1.4f && !m_patching)
        if (const Node* sel = nodeById(m_selectedId)) drawExpanded(r, *sel);
}

void UIRoutingMap::drawExpanded(NUIRenderer& r, const Node& n) {
    const auto* ch = m_viewModel ? m_viewModel->getChannelById(n.id) : nullptr;
    if (!ch) return;
    std::vector<const Aestra::ChannelViewModel::InsertViewModel*> inserts;
    for (const auto& ins : ch->inserts)
        if (!ins.isEmpty && !ins.name.empty()) inserts.push_back(&ins);
    const NUIRect rc = nodeRect(n);
    constexpr float kHead = 48.0f, kLabel = 26.0f, kRow = 32.0f, kOut = 62.0f;
    const float h = kHead + kLabel + std::max<size_t>(1, inserts.size()) * kRow + kOut;
    const NUIRect card{rc.x, rc.y, std::max(rc.width, 280.0f), h};
    r.fillRect(card, m_node);
    r.strokeRect(card, 1.5f, m_accent);

    r.fillRect({card.x + 16.0f, card.y + 18.0f, 12.0f, 12.0f}, argb(n.color));
    r.drawText(fit(r, n.label, 15.0f, card.width - 130.0f), {card.x + 38.0f, card.y + 15.0f}, 15.0f, m_text);
    const std::string count = std::to_string(inserts.size()) + (inserts.size() == 1 ? " insert" : " inserts");
    r.drawText(count, {card.right() - 16.0f - r.measureText(count, 10.5f).width, card.y + 18.0f}, 10.5f, m_textMuted);
    r.drawLine({card.x, card.y + kHead}, {card.right(), card.y + kHead}, 1.0f, m_border);

    float y = card.y + kHead + 8.0f;
    r.drawText("INSERT CHAIN", {card.x + 16.0f, y}, 9.5f, m_textMuted);
    y = card.y + kHead + kLabel;
    if (inserts.empty()) {
        r.drawText("No inserts", {card.x + 16.0f, y + 9.0f}, 12.0f, m_textMuted);
        y += kRow;
    }
    for (size_t i = 0; i < inserts.size(); ++i, y += kRow) {
        const auto& ins = *inserts[i];
        const NUIColor tc = ins.bypassed ? m_textMuted : m_text;
        r.drawText(std::to_string(i + 1), {card.x + 16.0f, y + 9.0f}, 12.0f, m_textMuted);
        r.drawText(fit(r, ins.name + (ins.bypassed ? "  (bypassed)" : ""), 12.5f, card.width - 60.0f),
                   {card.x + 38.0f, y + 8.0f}, 12.5f, tc);
    }
    r.drawLine({card.x, y + 4.0f}, {card.right(), y + 4.0f}, 1.0f, m_border);
    r.drawText("OUTPUT", {card.x + 16.0f, y + 14.0f}, 9.5f, m_textMuted);
    std::string out = "Master";
    if (ch->mainOutputId != 0 && ch->mainOutputId != kNoOutput)
        if (const auto* t = m_viewModel->getChannelById(ch->mainOutputId)) out = channelDisplayName(t->id, t->name);
    r.drawText(out, {card.x + 16.0f, y + 34.0f}, 12.5f, m_text);
    const float barW = 72.0f;
    const NUIRect bar{card.right() - 16.0f - barW, y + 40.0f, barW, 4.0f};
    r.fillRect(bar, m_raised);
    const float f = std::clamp((ch->faderGainDb + 60.0f) / 66.0f, 0.0f, 1.0f);
    r.fillRect({bar.x, bar.y, bar.width * f, bar.height}, m_live);
    const std::string db = formatDb(ch->faderGainDb);
    r.drawText(db, {bar.x - 10.0f - r.measureText(db, 11.5f).width, y + 34.0f}, 11.5f, m_textSecondary);
}

void UIRoutingMap::drawRail(NUIRenderer& r, const Geometry& g) {
    const NUIRect& rail = g.rail;
    bool toMaster = false;
    if (m_patching) {
        bool isMaster = false;
        patchTarget(m_patchMouse, isMaster);
        toMaster = isMaster;
    }
    r.fillRoundedRect(rail, 2.0f, m_panel);
    r.strokeRoundedRect(rail, 2.0f, toMaster ? 1.5f : 1.0f, toMaster ? m_text : m_border);
    // "MASTER", one letter under the other.
    const char* word = "MASTER";
    for (int i = 0; word[i]; ++i) {
        const std::string ch(1, word[i]);
        r.drawTextCentered(ch, {rail.x, rail.y + 16.0f + i * 12.0f, rail.width, 12.0f}, 9.0f, m_textMuted);
    }
    // A port for every wire that lands on the rail.
    for (size_t i = 0; i < m_edges.size(); ++i) {
        const Edge& e = m_edges[i];
        if (e.target != RoutingGraph::kMaster || e.type == Edge::Sidechain) continue;
        const Node* s = nodeById(e.source);
        if (!s) continue;
        const NUIPoint p{rail.x, railPortY(outputPort(*s).y)};
        if (p.y < g.canvas.y || p.y > g.canvas.bottom()) continue;
        r.fillCircle(p, 3.5f, m_bg);
        r.strokeCircle(p, 3.5f, 1.0f, m_textMuted);
    }
    // Master meter at the foot of the rail.
    const float mh = std::clamp(rail.height * 0.18f, 40.0f, 96.0f);
    if (rail.height > 160.0f) {
        const float top = rail.bottom() - 12.0f - mh;
        auto bar = [&](float x, float db) {
            r.fillRect({x, top, 6.0f, mh}, m_node);
            const float f = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            if (f > 0) r.fillRect({x, top + mh * (1.0f - f), 6.0f, mh * f}, m_live);
        };
        bar(rail.x + rail.width * 0.5f - 8.0f, m_masterPeakL);
        bar(rail.x + rail.width * 0.5f + 2.0f, m_masterPeakR);
    }
}

void UIRoutingMap::drawPatch(NUIRenderer& r, const Geometry& g) {
    const bool alt = hasMod(m_patchMods, NUIModifiers::Alt);
    const bool shift = hasMod(m_patchMods, NUIModifiers::Shift);
    const NUIColor wire = shift ? m_sidechain : (alt ? m_textSecondary : m_text);
    drawWire(r, m_patchStartScreen, m_patchMouse, 1.6f, wire, true);

    std::string srcName;
    if (const Node* s = nodeById(m_patchSource)) srcName = s->label;
    for (const auto& d : m_direct)
        if (d.id == m_patchSource) srcName = d.label;

    bool isMaster = false;
    const uint32_t target = patchTarget(m_patchMouse, isMaster);
    const bool refused = target != kNoOutput && !isMaster && refuses(target);
    std::string targetName;
    NUIRect anchor{0, 0, 0, 0};
    if (isMaster) {
        targetName = "Master";
        anchor = {g.rail.x - 196.0f, m_patchMouse.y + 12.0f, 184.0f, 0.0f};
    } else if (target != kNoOutput) {
        if (const Node* t = nodeById(target)) {
            targetName = t->label;
            anchor = nodeRect(*t);
        }
        for (const auto& d : m_direct)
            if (d.id == target) {
                targetName = d.label;
                anchor = {g.list.right() + 8.0f, m_patchMouse.y, 184.0f, 0.0f};
            }
    }

    // The drop card: what letting go does, and which key changes it.
    if (target != kNoOutput && !refused) {
        struct Row {
            const char* what;
            const char* key;
            bool active;
        };
        std::vector<Row> rows{{"Route output here", "drop", isMaster || (!alt && !shift)}};
        if (!isMaster) {
            rows.push_back({"Add a send", "Alt", alt && !shift});
            rows.push_back({"Sidechain into", "Shift", shift});
        }
        const NUIRect card{anchor.x, anchor.bottom() + 8.0f, std::max(184.0f, anchor.width),
                           static_cast<float>(rows.size()) * 28.0f};
        r.fillRect(card, m_node);
        r.strokeRect(card, 1.0f, m_border);
        for (size_t i = 0; i < rows.size(); ++i) {
            const NUIRect row{card.x, card.y + i * 28.0f, card.width, 28.0f};
            if (rows[i].active) r.fillRect(row, m_accent);
            const NUIColor tc = rows[i].active ? NUIColor::white() : m_textSecondary;
            r.drawText(rows[i].what, {row.x + 10.0f, row.y + 8.0f}, 11.0f, tc);
            r.drawText(rows[i].key, {row.right() - 10.0f - r.measureText(rows[i].key, 10.0f).width, row.y + 9.0f},
                       10.0f, rows[i].active ? NUIColor::white() : m_textMuted);
        }
    }

    // Why the hatched ones refuse.
    if (!m_patchRefused.empty()) {
        std::vector<std::string> names;
        for (uint32_t id : m_patchRefused) {
            if (const Node* n = nodeById(id)) names.push_back(n->label);
            else
                for (const auto& d : m_direct)
                    if (d.id == id) names.push_back(d.label);
        }
        std::string who;
        const size_t shown = std::min<size_t>(names.size(), 3);
        for (size_t i = 0; i < shown; ++i) who += (i == 0 ? "" : (i + 1 == shown && names.size() <= 3 ? " and " : ", ")) + names[i];
        if (names.size() > 3) who += " and " + std::to_string(names.size() - 3) + " more";
        const std::string verb = names.size() == 1 ? " feeds " : " feed ";
        const std::string pron = names.size() == 1 ? "it" : "them";
        const std::string lines[3] = {who + verb + srcName + ",",
                                      "so routing it into " + pron + " would loop.",
                                      names.size() == 1 ? "It refuses the drop." : "They refuse the drop."};
        const float x = g.canvas.x + 24.0f;
        float y = g.canvas.bottom() - 132.0f;
        for (const auto& l : lines) {
            r.drawText(l, {x, y}, 11.0f, m_refuse);
            y += 16.0f;
        }
    }

    // Hint bar
    std::string hint;
    if (target == kNoOutput) hint = srcName + " \xE2\x86\x92 drop on a channel or Master";
    else if (refused) hint = srcName + " \xE2\x86\x92 " + targetName + " would loop";
    else if (shift && !isMaster) hint = srcName + " sidechains " + targetName;
    else if (alt && !isMaster) hint = srcName + " \xE2\x86\x92 " + targetName + " as a send";
    else {
        std::string current = "Master";
        if (m_viewModel)
            if (const auto* ch = m_viewModel->getChannelById(m_patchSource);
                ch && ch->mainOutputId != 0 && ch->mainOutputId != kNoOutput)
                current = channelDisplayName(ch->mainOutputId, [&] {
                    const auto* t = m_viewModel->getChannelById(ch->mainOutputId);
                    return t ? t->name : std::string();
                }());
        hint = srcName + " \xE2\x86\x92 " + targetName + (current == targetName ? "" : "  (replaces \xE2\x86\x92 " + current + ")");
    }
    const std::string keys = "Alt send  \xC2\xB7  Shift sidechain  \xC2\xB7  Esc cancel";
    const float w1 = r.measureText(hint, 11.0f).width, w2 = r.measureText(keys, 10.5f).width;
    const float w = w1 + w2 + 48.0f;
    const NUIRect bar{g.canvas.x + (g.canvas.width - w) * 0.5f, g.canvas.bottom() - 60.0f, w, 36.0f};
    r.fillRect(bar, m_node);
    r.strokeRect(bar, 1.0f, m_border);
    r.drawText(hint, {bar.x + 16.0f, bar.y + 11.0f}, 11.0f, refused ? m_refuse : m_text);
    r.drawText(keys, {bar.x + 32.0f + w1, bar.y + 12.0f}, 10.5f, m_textMuted);
}

void UIRoutingMap::drawDetail(NUIRenderer& r, const Geometry& g) {
    m_sendRows.clear();
    m_outputBox = m_addSendRect = {0, 0, 0, 0};
    const NUIRect& d = g.detail;
    if (d.height <= 0 || !m_viewModel) return;
    const auto* ch = m_viewModel->getChannelById(m_selectedId);
    if (!ch) return;
    r.fillRect(d, m_bg);
    r.drawLine({d.x, d.y + 0.5f}, {d.right(), d.y + 0.5f}, 1.0f, m_border);

    // Identity and output
    const float x0 = d.x + 24.0f;
    r.fillRect({x0, d.y + 22.0f, 10.0f, 10.0f}, argb(paletteIndexToARGB(ch->trackColorIndex)));
    r.drawText(fit(r, channelDisplayName(ch->id, ch->name), 14.0f, 180.0f), {x0 + 18.0f, d.y + 19.0f}, 14.0f, m_text);
    int inputs = 0;
    for (const auto& c : m_channels) {
        if (c.output == m_selectedId && c.outputAudible) ++inputs;
        for (const auto& s : c.sends) inputs += s.target == m_selectedId ? 1 : 0;
    }
    auto plural = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    r.drawText(plural(inputs, "input", "inputs") + "  \xC2\xB7  " + plural(ch->fxCount, "insert", "inserts"),
               {x0, d.y + 42.0f}, 10.0f, m_textMuted);
    r.drawText("OUTPUT", {x0, d.y + 70.0f}, 9.5f, m_textMuted);
    m_outputBox = {x0, d.y + 86.0f, 200.0f, 28.0f};
    std::string out = "Master";
    if (ch->mainOutputId != 0 && ch->mainOutputId != kNoOutput)
        if (const auto* t = m_viewModel->getChannelById(ch->mainOutputId)) out = channelDisplayName(t->id, t->name);
    if (ch->mainOutputId == 0 && !ch->masterSendEnabled) out = "Off";
    r.fillRect(m_outputBox, m_node);
    r.strokeRect(m_outputBox, 1.0f, m_border);
    r.drawText(fit(r, out, 11.0f, 160.0f), {m_outputBox.x + 10.0f, m_outputBox.y + 8.0f}, 11.0f, m_text);
    const float ax = m_outputBox.right() - 16.0f, ay = m_outputBox.y + 13.0f;
    r.drawLine({ax - 3, ay - 1}, {ax, ay + 2}, 1.2f, m_textMuted);
    r.drawLine({ax, ay + 2}, {ax + 3, ay - 1}, 1.2f, m_textMuted);

    // Sends
    const float x1 = d.x + 256.0f;
    const float sendsW = std::min(400.0f, std::max(220.0f, d.width - 256.0f - 340.0f));
    r.drawText("SENDS", {x1, d.y + 20.0f}, 9.5f, m_textMuted);
    float y = d.y + 38.0f;
    const size_t maxRows = 3;
    for (size_t i = 0; i < ch->sends.size() && i < maxRows; ++i) {
        const auto& s = ch->sends[i];
        if (i + 1 == maxRows && ch->sends.size() > maxRows) {
            r.drawText("and " + std::to_string(ch->sends.size() - i) + " more", {x1, y + 7.0f}, 11.0f, m_textMuted);
            y += 30.0f;
            break;
        }
        const NUIRect row{x1, y, sendsW, 28.0f};
        m_sendRows.push_back({row, s.sendId});
        uint32_t color = 0xFF808080;
        std::string name = s.targetName;
        if (const auto* t = m_viewModel->getChannelById(s.targetId)) {
            color = paletteIndexToARGB(t->trackColorIndex);
            name = channelDisplayName(t->id, t->name);
        }
        r.fillRect({x1, y + 10.0f, 8.0f, 8.0f}, s.sidechainOnly ? m_sidechain : argb(color));
        r.drawText(fit(r, name, 12.0f, sendsW - 150.0f), {x1 + 18.0f, y + 6.0f}, 12.0f, m_text);
        const std::string db = formatDb(gainToDbStatic(s.gain));
        const char* kind = s.sidechainOnly ? "SC" : (s.postFader ? "Post" : "Pre");
        const float dbW = r.measureText(db, 11.0f).width;
        r.drawText(db, {row.right() - dbW, y + 7.0f}, 11.0f, m_text);
        r.drawText(kind, {row.right() - 70.0f - r.measureText(kind, 11.0f).width, y + 7.0f}, 11.0f, m_textSecondary);
        y += 30.0f;
    }
    if (ch->sends.empty()) {
        r.drawText("No sends", {x1, y + 6.0f}, 11.0f, m_textMuted);
        y += 26.0f;
    }
    m_addSendRect = {x1, y + 2.0f, r.measureText("+ Add send", 11.0f).width + 4.0f, 18.0f};
    r.drawText("+ Add send", {x1, y + 4.0f}, 11.0f, m_textSecondary);

    // Inserts
    const float x2 = x1 + sendsW + 56.0f;
    if (x2 + 160.0f > d.right()) return;
    r.drawText("INSERTS", {x2, d.y + 20.0f}, 9.5f, m_textMuted);
    int idx = 0;
    float iy = d.y + 42.0f;
    for (const auto& ins : ch->inserts) {
        if (ins.isEmpty || ins.name.empty()) continue;
        ++idx;
        if (idx > 4) {
            r.drawText("and more", {x2, iy}, 11.0f, m_textMuted);
            break;
        }
        std::string line = std::to_string(idx) + "  \xC2\xB7  " + ins.name + (ins.bypassed ? "  (bypassed)" : "");
        r.drawText(fit(r, line, 12.0f, d.right() - x2 - 24.0f), {x2, iy}, 12.0f, ins.bypassed ? m_textMuted : m_text);
        iy += 26.0f;
    }
    if (idx == 0) r.drawText("No inserts", {x2, iy}, 11.0f, m_textMuted);
}

void UIRoutingMap::drawLegend(NUIRenderer& r, const Geometry& g) {
    const float y = g.canvas.bottom() - 30.0f;
    float x = g.canvas.x + 24.0f;
    auto item = [&](const char* label, const NUIColor& c, bool dashed) {
        if (dashed)
            for (float t = 0; t < 22.0f; t += 6.0f) r.drawLine({x + t, y + 6}, {x + std::min(22.0f, t + 3.0f), y + 6}, 1.2f, c);
        else
            r.drawLine({x, y + 6}, {x + 22.0f, y + 6}, 1.4f, c);
        r.drawText(label, {x + 30.0f, y}, 10.0f, m_textMuted);
        x += 30.0f + r.measureText(label, 10.0f).width + 26.0f;
    };
    item("Output", m_textSecondary.withAlpha(0.7f), false);
    item("Send", m_textMuted.withAlpha(0.55f), false);
    item("Sidechain", m_sidechain.withAlpha(0.85f), true);
    const char* note = m_signalFlow ? "Signal flow is on: wires move while audio plays."
                                    : "Idle graph is a still image. Ports light with level; nothing moves.";
    if (x + r.measureText(note, 10.0f).width < g.rail.x - 16.0f) r.drawText(note, {x, y}, 10.0f, m_textMuted.withAlpha(0.8f));
}

void UIRoutingMap::drawEmpty(NUIRenderer& r, const Geometry& g) {
    const float w = 320.0f, h = 140.0f;
    const NUIRect box{g.canvas.x + (g.rail.x - g.canvas.x - w) * 0.5f, g.canvas.y + (g.canvas.height - h) * 0.45f, w, h};
    dashedRect(r, box, m_border);
    r.drawTextCentered("Everything goes straight to Master.", {box.x, box.y + 48.0f, box.width, 18.0f}, 13.0f, m_text);
    r.drawTextCentered("Drag a track onto another to make a bus.", {box.x, box.y + 74.0f, box.width, 16.0f}, 10.5f,
                       m_textMuted);
}

} // namespace AestraUI
