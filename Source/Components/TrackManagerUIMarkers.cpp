// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Song markers on the ruler (v0.8.3 essentials, slice 2 of markers; FD-61).
//
// Slice 1 (#1337) gave the model, the undo steps and the project-file section,
// and nothing could make one. This is the surface: a marker is a chip on the
// ruler with its name on it, a click puts the playhead on it, a drag moves it
// (one undo step), M makes one at the playhead, and a right-click renames or
// deletes it.
//
// The chip's width is a pure function of the name and the zoom, not of the
// renderer's text metrics. Paint, hit-test and the rename box all have to agree
// on where a chip is, and two of those three run without a renderer (hit-test
// and the headless tests). A measured width would make a click land where the
// chip was drawn last frame, which is the class of bug that makes a UI feel
// haunted. The label is truncated to the chip instead.

#include "TrackManagerUI.h"
#include "TrackManagerUIInternal.h"

#include "Commands/MarkerCommands.h"
#include "NUIRenderer.h"
#include "NUITextInput.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Aestra {
namespace Audio {
namespace {

constexpr float kChipHeight = 14.0f;
constexpr float kChipTop = 1.0f;      // inside the marker lane: 1 px above and below
constexpr float kChipPadX = 6.0f;
constexpr float kCharWidth = 6.0f;    // a 10 px label's advance, for a width both paint and hit-test compute
constexpr float kChipGap = 4.0f;      // a chip never covers the next marker's chip
constexpr float kLabelFontSize = 10.0f;
constexpr float kHitSlopX = 2.0f;     // a 10 px chip stays clickable
constexpr float kRenameMinWidth = 120.0f;
constexpr float kRenameMinHeight = 20.0f;

struct RulerGeometry {
    float gridStartX{0.0f};
    float gridEndX{0.0f};
};

// The same grid the ruler paints on (renderTimeRuler): control column, inset,
// and the scrollbar gutter. One definition, so a chip cannot drift off the
// ticks it is supposed to sit on.
RulerGeometry rulerGeometry(float componentWidth) {
    const auto& layout = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions();
    const float gridStartX = layout.trackControlsWidth + kTimelineGridInsetX;
    const float trackWidth = componentWidth - kTimelineScrollbarWidth;
    const float gridWidth = std::max(0.0f, trackWidth - layout.trackControlsWidth - 10.0f);
    return {gridStartX, gridStartX + gridWidth};
}

std::string labelThatFits(const std::string& name, float chipWidth) {
    const float inner = std::max(0.0f, chipWidth - kChipPadX * 2.0f);
    const size_t fits = static_cast<size_t>(inner / kCharWidth);
    if (name.size() <= fits) {
        return name;
    }
    if (fits <= 2) {
        return {};
    }
    return name.substr(0, fits - 2) + "..";
}

} // namespace

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

::AestraUI::NUIRect TrackManagerUI::markerChipRectFor(const Marker& marker) const {
    if (!m_trackManager) {
        return {};
    }
    const RulerGeometry geo = rulerGeometry(getBounds().width);
    const double beat =
        (m_isDraggingMarker && marker.id == m_draggingMarkerId) ? m_markerDragPreviewBeat : marker.beat;
    const float x = geo.gridStartX + beatToGridOffset(beat);

    float width = kChipPadX * 2.0f + static_cast<float>(marker.name.size()) * kCharWidth;

    // A chip may not cover the next marker's chip: section names sit close
    // together at the top of a song, and an overlap makes one of them unreadable
    // and unclickable.
    const auto& all = m_trackManager->markers().markers();
    const auto it = std::find_if(all.begin(), all.end(), [&marker](const Marker& candidate) {
        return candidate.id == marker.id;
    });
    if (it != all.end() && std::next(it) != all.end()) {
        const float nextX = geo.gridStartX + beatToGridOffset(std::next(it)->beat);
        width = std::min(width, nextX - kChipGap - x);
    }
    width = std::min(width, geo.gridEndX - x);
    width = std::max(0.0f, width);

    return {x, kTimelineRulerTop + kChipTop, width, kChipHeight};
}

::AestraUI::NUIRect TrackManagerUI::markerChipWindowRect(const Marker& marker) const {
    const ::AestraUI::NUIRect local = markerChipRectFor(marker);
    return {local.x + getBounds().x, local.y + getBounds().y, local.width, local.height};
}

::AestraUI::NUIRect TrackManagerUI::markerChipRect(uint64_t id) const {
    const Marker* marker = m_trackManager ? m_trackManager->markers().find(id) : nullptr;
    return marker ? markerChipRectFor(*marker) : ::AestraUI::NUIRect{};
}

uint64_t TrackManagerUI::markerAtX(float localX) const {
    if (!m_trackManager) {
        return 0;
    }
    const RulerGeometry geo = rulerGeometry(getBounds().width);
    if (localX < geo.gridStartX || localX > geo.gridEndX) {
        return 0;
    }
    for (const auto& marker : m_trackManager->markers().markers()) {
        const ::AestraUI::NUIRect chip = markerChipRectFor(marker);
        if (chip.width <= 0.0f) {
            continue;
        }
        if (localX >= chip.x - kHitSlopX && localX <= chip.right() + kHitSlopX) {
            return marker.id;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void TrackManagerUI::renderSongMarkers(::AestraUI::NUIRenderer& renderer,
                                       const ::AestraUI::NUIRect& rulerBounds) {
    if (!m_trackManager || m_trackManager->markers().empty()) {
        return;
    }

    auto& theme = ::AestraUI::NUIThemeManager::getInstance();
    const RulerGeometry geo = rulerGeometry(getBounds().width);
    const ::AestraUI::NUIColor accent = theme.getColor("accentPrimary");
    const ::AestraUI::NUIColor labelInk = theme.getColor("textPrimary");
    const ::AestraUI::NUIColor chipFill = theme.getColor("surfaceRaised");
    const ::AestraUI::NUIColor chipEdge = theme.getColor("borderStrong");
    const ::AestraUI::NUIColor selectedInk = theme.getColor("textOnPrimary");
    const float tickBottom = rulerBounds.bottom();

    const float gridStartWindowX = getBounds().x + geo.gridStartX;

    // Markers belong to the timeline, not to the toolbar's corner cell. A chip
    // scrolled past the grid's left edge (or a long name running over it) must
    // be cut at that edge and sit behind the corner, never paint over it.
    renderer.setClipRect(::AestraUI::NUIRect{gridStartWindowX, rulerBounds.y,
                                             geo.gridEndX - geo.gridStartX, rulerBounds.height});

    for (const auto& marker : m_trackManager->markers().markers()) {
        const ::AestraUI::NUIRect chip = markerChipWindowRect(marker);
        if (chip.width <= 0.0f || chip.right() < gridStartWindowX || chip.x > getBounds().x + geo.gridEndX) {
            continue;
        }
        const bool selected = marker.id == m_selectedMarkerId;

        // The tick is the marker's actual beat: a chip that is clamped short by
        // its neighbour must still point at the right bar.
        const float tickX = std::round(chip.x) + 0.5f;
        renderer.drawLine({tickX, chip.y + kChipHeight}, {tickX, tickBottom}, selected ? 1.5f : 1.0f,
                          accent.withAlpha(selected ? 0.95f : 0.55f));

        renderer.fillRoundedRect(chip, 3.0f, selected ? accent : chipFill);
        // In the light theme the raised surface sits a hair above the ruler bed,
        // so an unselected chip needs its own edge to read as a distinct object.
        if (!selected) {
            renderer.strokeRoundedRect(chip, 3.0f, 1.0f, chipEdge);
        }

        const std::string label = labelThatFits(marker.name, chip.width);
        if (!label.empty()) {
            const float textY = std::round(renderer.calculateTextY(chip, kLabelFontSize));
            renderer.drawText(label, {chip.x + kChipPadX, textY}, kLabelFontSize,
                              selected ? selectedInk : labelInk);
        }
    }

    renderer.clearClipRect();
}

void TrackManagerUI::renderMarkerRenameBox(::AestraUI::NUIRenderer& renderer) {
    if (m_markerRenameInput && m_markerRenameInput->isVisible()) {
        m_markerRenameInput->onRender(renderer);
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

bool TrackManagerUI::handleMarkerPress(const ::AestraUI::NUIMouseEvent& event,
                                       const ::AestraUI::NUIPoint& localPos, bool isInRuler) {
    if (!event.pressed) {
        return false;
    }
    // The toolbar's corner cell and the scrollbar gutter are not the timeline,
    // so the caller's isInRuler is the gate (it already excludes both).
    if (!isInRuler) {
        return false;
    }

    const uint64_t id = markerAtX(localPos.x);
    if (id == 0) {
        return false;
    }

    if (event.button == ::AestraUI::NUIMouseButton::Right) {
        seekToMarker(id);
        openMarkerContextMenu(id);
        return true;
    }
    if (event.button != ::AestraUI::NUIMouseButton::Left) {
        return false;
    }

    // A press on the chip is the marker's gesture, not the ruler's: it must not
    // also start a scrub, which would move the playhead under the pointer the
    // moment the drag begins.
    seekToMarker(id);
    const Marker* marker = m_trackManager ? m_trackManager->markers().find(id) : nullptr;
    if (!marker) {
        return true;
    }
    m_isDraggingMarker = true;
    m_draggingMarkerId = id;
    m_markerDragMoved = false;
    m_markerDragPreviewBeat = marker->beat;
    return true;
}

bool TrackManagerUI::handleMarkerDrag(const ::AestraUI::NUIMouseEvent& event,
                                      const ::AestraUI::NUIPoint& localPos) {
    if (!m_isDraggingMarker) {
        return false;
    }

    if (event.released && event.button == ::AestraUI::NUIMouseButton::Left) {
        const uint64_t id = m_draggingMarkerId;
        const bool moved = m_markerDragMoved;
        const double beat = m_markerDragPreviewBeat;
        m_isDraggingMarker = false;
        m_draggingMarkerId = 0;
        m_markerDragMoved = false;

        // Dropping onto another marker's beat would stack the two; the move is cancelled.
        if (moved && m_trackManager && m_trackManager->markers().find(id) != nullptr &&
            markerIdAtBeat(beat, id) == 0) {
            auto command = std::make_shared<MoveMarkerCommand>(*m_trackManager, id, beat);
            m_trackManager->getCommandHistory().pushAndExecute(command);
            seekToMarker(id); // the section moved; the playhead follows it, not the old bar
        }
        invalidateCache();
        return true;
    }

    if (!event.pressed && !event.released) {
        // A plain move: preview the chip under the pointer, snap to the grid
        // like every other timeline position the user picks.
        const RulerGeometry geo = rulerGeometry(getBounds().width);
        double beat = gridOffsetToBeat(localPos.x - geo.gridStartX);
        beat = std::max(0.0, snapBeatToGrid(beat));
        if (std::abs(beat - m_markerDragPreviewBeat) > 1e-6) {
            m_markerDragPreviewBeat = beat;
            m_markerDragMoved = true;
            invalidateCache();
        }
    }
    return true;
}

void TrackManagerUI::openMarkerContextMenu(uint64_t id) {
    if (!m_trackManager || m_trackManager->markers().find(id) == nullptr) {
        return;
    }
    if (m_activeContextMenu) {
        detachContextMenu(m_activeContextMenu);
    }

    m_activeContextMenu = std::make_shared<::AestraUI::NUIContextMenu>();
    auto menu = m_activeContextMenu;
    menu->addItem("Rename", [this, id]() { beginMarkerRename(id); });
    menu->addItem("Delete", [this, id]() { deleteMarker(id); });

    const Marker* marker = m_trackManager->markers().find(id);
    const ::AestraUI::NUIRect chip = marker ? markerChipWindowRect(*marker) : ::AestraUI::NUIRect{};
    attachAndShowContextMenu(this, menu, ::AestraUI::NUIPoint(chip.x, chip.bottom()));
}

// ---------------------------------------------------------------------------
// Model-side actions
// ---------------------------------------------------------------------------

uint64_t TrackManagerUI::markerIdAtBeat(double beat, uint64_t ignoreId) const {
    if (!m_trackManager) {
        return 0;
    }
    for (const auto& marker : m_trackManager->markers().markers()) {
        if (marker.id != ignoreId && std::abs(marker.beat - beat) <= MarkerModel::kBeatEpsilon) {
            return marker.id;
        }
    }
    return 0;
}

uint64_t TrackManagerUI::addMarkerAtPlayhead() {
    if (!m_trackManager) {
        return 0;
    }
    const double bpm = std::max(1.0, m_trackManager->getPlaylistModel().getBPM());
    const double beat = std::max(0.0, m_trackManager->getPosition() * bpm / 60.0);

    // One marker per beat: adding where one already sits selects it instead of
    // stacking a second chip under the first (which the ruler would squeeze to
    // nothing and the user could never click).
    if (const uint64_t existing = markerIdAtBeat(beat, 0); existing != 0) {
        selectMarker(existing);
        return existing;
    }
    auto command = std::make_shared<AddMarkerCommand>(*m_trackManager, beat);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    const uint64_t id = command->markerId();
    if (id != 0) {
        selectMarker(id);
        invalidateCache();
    }
    return id;
}

void TrackManagerUI::selectMarker(uint64_t id) {
    if (m_selectedMarkerId == id) {
        return;
    }
    m_selectedMarkerId = id;
    invalidateCache();
}

bool TrackManagerUI::seekToMarker(uint64_t id) {
    const Marker* marker = m_trackManager ? m_trackManager->markers().find(id) : nullptr;
    if (marker == nullptr) {
        return false;
    }
    const double seconds = std::max(0.0, m_trackManager->getPlaylistModel().beatToSeconds(marker->beat));
    m_trackManager->setPosition(seconds);
    m_trackManager->setPlayStartPosition(seconds); // stop returns here, like a ruler click
    selectMarker(id);
    return true;
}

void TrackManagerUI::deleteMarker(uint64_t id) {
    if (!m_trackManager || m_trackManager->markers().find(id) == nullptr) {
        return;
    }
    auto command = std::make_shared<DeleteMarkerCommand>(*m_trackManager, id);
    m_trackManager->getCommandHistory().pushAndExecute(command);
    if (m_selectedMarkerId == id) {
        m_selectedMarkerId = 0;
    }
    if (m_markerRenameId == id) {
        finishMarkerRename(false);
    }
    invalidateCache();
}

void TrackManagerUI::jumpToAdjacentMarker(int direction) {
    if (!m_trackManager) {
        return;
    }
    const auto& markers = m_trackManager->markers();
    if (markers.empty()) {
        return;
    }
    const double bpm = std::max(1.0, m_trackManager->getPlaylistModel().getBPM());
    const double beat = std::max(0.0, m_trackManager->getPosition() * bpm / 60.0);
    const Marker* target = direction >= 0 ? markers.nextAfter(beat, true) : markers.previousBefore(beat, true);
    if (target != nullptr) {
        seekToMarker(target->id);
    }
}

// ---------------------------------------------------------------------------
// Inline rename (the same box the Arsenal's pattern rename uses)
// ---------------------------------------------------------------------------

void TrackManagerUI::beginMarkerRename(uint64_t id) {
    if (!m_trackManager) {
        return;
    }
    const Marker* marker = m_trackManager->markers().find(id);
    if (marker == nullptr) {
        return;
    }
    finishMarkerRename(false);
    m_retiredMarkerInput.reset();

    auto& theme = ::AestraUI::NUIThemeManager::getInstance();
    m_markerRenameInput = std::make_shared<::AestraUI::NUITextInput>(marker->name);

    ::AestraUI::NUIRect box = markerChipWindowRect(*marker);
    box.width = std::max(box.width, kRenameMinWidth);
    box.height = std::max(box.height, kRenameMinHeight);
    box.y -= (box.height - kChipHeight) * 0.5f; // grow around the chip, not below it
    const float maxX = std::max(0.0f, getBounds().width - box.width - 8.0f);
    box.x = std::min(std::max(box.x, 0.0f), maxX);
    m_markerRenameInput->setBounds(box);
    m_markerRenameInput->setTextColor(theme.getColor("textPrimary"));
    m_markerRenameInput->setBackgroundColor(theme.getColor("inputBgDefault"));
    m_markerRenameInput->setBorderColor(theme.getColor("inputBorderFocus"));
    m_markerRenameInput->setBorderWidth(1.0f);
    m_markerRenameInput->setBorderRadius(3.0f);
    m_markerRenameInput->setFocusedBorderColor(theme.getColor("accentPrimary"));
    m_markerRenameInput->setOnReturnKey([this]() { finishMarkerRename(true); });
    m_markerRenameInput->setOnEscapeKey([this]() { finishMarkerRename(false); });
    m_markerRenameInput->setOnFocusLost([this]() { finishMarkerRename(true); });

    m_markerRenameId = id;
    addChild(m_markerRenameInput);
    m_markerRenameInput->setFocused(true);
    m_markerRenameInput->setCaretPosition(static_cast<int>(marker->name.size()));
    m_markerRenameInput->selectAll();
    invalidateCache();
}

void TrackManagerUI::finishMarkerRename(bool commit) {
    if (!m_markerRenameInput) {
        return;
    }
    // Take the box out first: committing can arrive from inside the box's own
    // focus-lost handler, and the second entry has to find nothing to do.
    auto input = std::move(m_markerRenameInput);
    m_markerRenameInput.reset();
    input->setFocused(false);
    removeChild(input);
    m_retiredMarkerInput = std::move(input); // the focused pointer may still be read this frame

    const std::string text = m_retiredMarkerInput->getText();
    const uint64_t id = m_markerRenameId;
    m_markerRenameId = 0;

    if (commit && m_trackManager && !text.empty()) {
        const Marker* marker = m_trackManager->markers().find(id);
        if (marker != nullptr && marker->name != text) {
            auto command = std::make_shared<RenameMarkerCommand>(*m_trackManager, id, text);
            m_trackManager->getCommandHistory().pushAndExecute(command);
        }
    }
    invalidateCache();
}

} // namespace Audio
} // namespace Aestra
