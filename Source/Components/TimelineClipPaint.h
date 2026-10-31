// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Timeline clips in the FD-50 look (Unified Panels 2, boards 7 and 8): a flat, square body in the colour of
// the track the clip sits on, a solid header band in that track's full hue with a dark label, content (notes
// or waveform) in a light tint of the same hue, a 1 px white outline when selected, and the whole clip at
// 40 % when muted.
//
// The body and content tones still come from TrackColorPalette.h (clipBodyTone / waveformInkTone), so the
// contrast contract TimelineClipContrastTest guards is untouched; only the header band is new.
//
// Kept out of TrackUIComponent.cpp, which is at its size pin.
#pragma once

#include "../AestraUI/Graphics/NUIRenderer.h"
#include "TimelineClipGeometry.h"
#include "PatternSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace ClipPaint {

/// A clip shows a header band (and a name) only when it is big enough to hold one.
inline bool showsHeaderBand(const AestraUI::NUIRect& clipBounds) {
    return showsHeaderBand(clipBounds.width, clipBounds.height);
}

/// The identity hue of a track colour stored as ARGB. Unset colours (white or grey) fall back to the theme
/// accent, as clips always have.
AestraUI::NUIColor identityFromARGB(uint32_t argb);

/// The header band's fill: the track's hue at full strength.
AestraUI::NUIColor headerBandColor(const AestraUI::NUIColor& identity);

/// The label and glyph colour on the header band: near-black, so it reads on every palette hue.
AestraUI::NUIColor headerLabelInk(float alpha = 0.9f);

void paintBody(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible, const AestraUI::NUIColor& identity,
               bool selected);

/// The clip menu's slot: a square at the header band's left end, exactly the band's height, so the glyph and its
/// hover shade never leave the band. Empty (zero width) when the clip is too small to carry one.
AestraUI::NUIRect menuSlot(const AestraUI::NUIRect& visible);

/// Fills the header band across the visible part of the clip and writes the label after the menu slot.
void paintHeaderBand(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible,
                     const AestraUI::NUIColor& identity, const std::string& label, float fontSize);

/// The ☰ glyph in its slot: three crisp 1 px lines in the band's dark ink, the slot shaded while pointed at.
void paintMenuGlyph(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& slot, bool hot, bool pointed);

/// A dashed vertical line where a clip starts its pattern again.
void paintRepeatLine(AestraUI::NUIRenderer& renderer, float x, float top, float bottom, const AestraUI::NUIColor& ink);

/// A pattern clip's mini piano roll in @p ink: each note where it sounds (noteBeatInClip), in every repeat of the
/// pattern the clip plays (V82-T3), and a dashed line where each repeat starts. @p full is the whole clip,
/// @p visible its on-screen part; content starts @p bandHeight below the top. Only on-screen repeats are walked.
void paintMiniPianoRoll(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible,
                        const AestraUI::NUIRect& full, float bandHeight, float pixelsPerBeat,
                        const std::vector<MidiNote>& notes, double patternLengthBeats, double sourceOffsetBeats,
                        double clipDurationBeats, const AestraUI::NUIColor& ink);

void paintSelection(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible);

void paintMuted(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible);

} // namespace ClipPaint
} // namespace Audio
} // namespace Aestra
