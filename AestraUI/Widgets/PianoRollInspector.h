// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The piano roll's inspector (FD-63, board "A + C"): a 232 px column at the right that says what is selected and edits
// it. Velocity and Chance are bars you drag; Ratchet is four buttons. Every change is one undo step, through the note
// layer's commit like a lane drag. The Muse section joins it in a later slice.

#include "NUIComponent.h"
#include "../Common/MusicHelpers.h"

#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class PianoRollNoteLayer;

class PianoRollInspector : public NUIComponent {
public:
    static constexpr float kWidth = 232.0f;

    void setNoteLayer(std::shared_ptr<PianoRollNoteLayer> layer) { layer_ = layer; repaint(); }
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb < 1 ? 1 : bpb; }

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /// What the selection holds, for painting and tests.
    struct Summary {
        int count = 0;
        int firstBar = 0;          ///< 1-based bar of the earliest selected note
        int lastBar = 0;           ///< 1-based bar of the latest selected note's start
        float velocity = 0.0f;     ///< mean, 0..1
        bool velocityMixed = false;
        float chance = 1.0f;       ///< mean, 0..1
        bool chanceMixed = false;
        int ratchet = 1;           ///< the common ratchet, or 0 when they differ
        bool operator==(const Summary& o) const {
            return count == o.count && firstBar == o.firstBar && lastBar == o.lastBar && velocity == o.velocity &&
                   velocityMixed == o.velocityMixed && chance == o.chance && chanceMixed == o.chanceMixed &&
                   ratchet == o.ratchet;
        }
    };
    Summary summarize() const;

    // The control rects, laid out from the bounds (public for tests).
    NUIRect velocityBar() const;
    NUIRect chanceBar() const;
    NUIRect ratchetButton(int hits) const; ///< hits 1..4

private:
    enum class Drag { None, Velocity, Chance };
    void applyToSelection(Drag what, float value);
    void setRatchet(int hits);
    float barValueAt(const NUIRect& bar, float x) const;

    std::weak_ptr<PianoRollNoteLayer> layer_;
    int beatsPerBar_ = 4;
    Drag drag_ = Drag::None;
    std::vector<MidiNote> dragBefore_;
    Summary lastShown_;
};

} // namespace AestraUI
