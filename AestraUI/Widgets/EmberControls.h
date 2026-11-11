// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include "PluginEditorKit.h"
#include "PluginHost.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AestraUI {
namespace EditorKit {

/**
 * @brief The Ember kit's parameter-bound controls (FD-73): a board of knobs, stepped segments and a slider.
 *
 * Each one knows its parameter id, where it sits and how to draw itself from the plugin's own display strings,
 * so an editor states its layout once and the look, hover, drag highlight and bypass dimming come from here.
 * Input stays in the editor: knobs through AestraPanelWindow::handleKnobDrag (the board only lists them),
 * segments and the slider through the onPress / onMouse helpers below.
 */

/// A row of knobs under section headers, spread across a width the way the mock's rowGroups() does.
class EmberKnobBoard {
public:
    struct Spec {
        uint32_t param = 0;
        const char* label = "";
        KnobSize size = KnobSize::Medium;
        bool bipolar = false;
    };
    struct Group {
        const char* label = "";
        std::vector<Spec> knobs;
    };
    struct Cell {
        Spec spec;
        NUIRect rect;       ///< the knob, its label and its value
        bool enabled = true;
    };

    void clear();
    /// One knob with its cell's top-left at @p topLeft.
    void place(const Spec& spec, const NUIPoint& topLeft);
    /// Groups across @p width. @p origin is the header's top-left; the knobs sit 20 px below it.
    void layoutRow(const NUIPoint& origin, float width, const std::vector<Group>& groups);
    /// A header over an explicit span (for boards that place their knobs by hand).
    void addHeader(const NUIRect& span, const char* label);
    void setEnabled(uint32_t param, bool enabled);
    const std::vector<Cell>& cells() const { return m_cells; }
    NUIRect rectOf(uint32_t param) const;

    /// @param mouse nullptr when the pointer is not over the window; @param dragParam -1 when no knob is dragged.
    void draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
              int dragParam, bool bypassed) const;

private:
    struct Header {
        NUIRect span;
        std::string label;
    };
    std::vector<Cell> m_cells;
    std::vector<Header> m_headers;
};

/// Stepped parameter shown as one cell per label (value = index / (count - 1)).
struct EmberSegments {
    uint32_t param = 0;
    std::vector<std::string> labels;
    NUIRect rect;
    bool enabled = true;

    int index(const Aestra::Audio::IPluginInstance& plugin) const;
    int hit(const NUIPoint& p) const; ///< the cell under @p p, or -1
    /// Sets the parameter when @p p is on a cell. True when it was.
    bool onPress(Aestra::Audio::IPluginInstance& plugin, const NUIPoint& p) const;
    void draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
              bool bypassed) const;
};

/// A horizontal slider on one parameter: press in the track to start, move to follow, release to end.
struct EmberSliderControl {
    uint32_t param = 0;
    std::string label;
    NUIRect rect;
    float labelWidth = 40.0f;
    bool dragging = false;

    NUIRect track() const; ///< the hit area, a little wider than the drawn track
    /// True when the event was taken. Double-click puts the default back.
    bool onMouse(const NUIMouseEvent& event, Aestra::Audio::IPluginInstance& plugin);
    void draw(NUIRenderer& renderer, const Aestra::Audio::IPluginInstance& plugin, const NUIPoint* mouse,
              bool bypassed) const;
};

/// True when the plugin's bypass parameter @p bypassParam is on.
bool isBypassed(const Aestra::Audio::IPluginInstance& plugin, uint32_t bypassParam);

} // namespace EditorKit
} // namespace AestraUI
