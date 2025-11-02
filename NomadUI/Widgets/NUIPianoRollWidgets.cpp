// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPianoRollWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>

namespace NomadUI {

static bool isBlackKey(int midiNote)
{
    int m = midiNote % 12;
    return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
}

PianoKeyboard::PianoKeyboard()
    : keyHeight_(16.0f), firstNote_(72), numKeys_(36)
{
}

void PianoKeyboard::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto bg = theme.getColor("backgroundPrimary");
    auto border = theme.getColor("border");
    auto white = NUIColor(0.9f, 0.9f, 0.9f, 1.0f);
    auto black = NUIColor(0.2f, 0.2f, 0.22f, 1.0f);

    auto b = getBounds();
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, border);

    for (int i = 0; i < numKeys_; ++i)
    {
        int midi = firstNote_ - i;
        float y = b.y + i * keyHeight_;
        NUIRect keyRect(b.x, y, b.width, keyHeight_);
        bool blackKey = isBlackKey(midi);
        renderer.fillRect(keyRect, blackKey ? black : white);
        renderer.strokeRect(keyRect, 1, border.withAlpha(0.2f));
    }
}

void PianoKeyboard::setKeyHeight(float height)
{
    keyHeight_ = std::max(6.0f, height);
    repaint();
}

void PianoKeyboard::setFirstMidiNote(int note)
{
    firstNote_ = std::clamp(note, 0, 127);
    repaint();
}

void PianoKeyboard::setNumKeys(int numKeys)
{
    numKeys_ = std::max(1, std::min(numKeys, 128));
    repaint();
}

PianoGrid::PianoGrid()
    : pixelsPerBeat_(60.0f), beatsPerBar_(4), scrollX_(0.0f), scrollY_(0.0f)
{
}

void PianoGrid::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto border = theme.getColor("border");
    auto gridMajor = theme.getColor("accentPrimary").withAlpha(0.25f);
    auto gridMinor = theme.getColor("textSecondary").withAlpha(0.15f);
    auto bg = NUIColor(0.05f, 0.05f, 0.06f, 1.0f);

    auto b = getBounds();
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, border);

    // Vertical bars (beats/bars)
    float startX = b.x - std::fmod(scrollX_, pixelsPerBeat_);
    int columns = static_cast<int>(b.width / pixelsPerBeat_) + 2;
    for (int i = 0; i < columns; ++i)
    {
        float x = startX + i * pixelsPerBeat_;
        bool isBar = (static_cast<int>((x + scrollX_) / pixelsPerBeat_) % beatsPerBar_) == 0;
        renderer.drawLine(NUIPoint(x, b.y), NUIPoint(x, b.y + b.height), 1.0f, isBar ? gridMajor : gridMinor);
    }

    // Horizontal key rows (every key height equals 1 unit of 16px default)
    float keyHeight = 16.0f; // Visual guideline; actual mapping handled by notes view
    float startY = b.y - std::fmod(scrollY_, keyHeight);
    int rows = static_cast<int>(b.height / keyHeight) + 2;
    for (int r = 0; r < rows; ++r)
    {
        float y = startY + r * keyHeight;
        renderer.drawLine(NUIPoint(b.x, y), NUIPoint(b.x + b.width, y), 1.0f, gridMinor.withAlpha(0.12f));
    }
}

void PianoGrid::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(8.0f, ppb); repaint(); }
void PianoGrid::setBeatsPerBar(int bpb) { beatsPerBar_ = std::max(1, bpb); repaint(); }
void PianoGrid::setScrollOffsetX(float offset) { scrollX_ = std::max(0.0f, offset); repaint(); }
void PianoGrid::setScrollOffsetY(float offset) { scrollY_ = std::max(0.0f, offset); repaint(); }

PianoRollNotes::PianoRollNotes()
    : pixelsPerBeat_(60.0f), keyHeight_(16.0f), scrollX_(0.0f), firstNote_(72)
{
}

void PianoRollNotes::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto filled = theme.getColor("accentPrimary");
    auto border = theme.getColor("border").withAlpha(0.8f);

    auto b = getBounds();
    for (const auto& n : notes_)
    {
        float x = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
        float w = std::max(4.0f, static_cast<float>(n.durationBeats * pixelsPerBeat_));
        int row = firstNote_ - n.pitch; // 0 at firstNote_
        float y = b.y + row * keyHeight_;
        NUIRect rect(x, y + 1.0f, w, keyHeight_ - 2.0f);
        renderer.fillRect(rect, filled.withAlpha(0.85f));
        renderer.strokeRect(rect, 1, border);
    }
}

bool PianoRollNotes::onMouseEvent(const NUIMouseEvent& event)
{
    // Minimal: left click to add a fixed 1-beat note at nearest grid
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        auto b = getBounds();
        float localX = event.position.x - b.x + scrollX_;
        float localY = event.position.y - b.y;

        double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
        double snapped = std::round(beat * 4.0) / 4.0; // 16th notes

        int row = static_cast<int>(localY / keyHeight_);
        int pitch = firstNote_ - row;
        pitch = std::clamp(pitch, 0, 127);

        MidiNote note{pitch, snapped, 1.0, 0.8f};
        notes_.push_back(note);
        repaint();
        if (onNotesChanged_) onNotesChanged_(notes_);
        return true;
    }
    return NUIComponent::onMouseEvent(event);
}

void PianoRollNotes::setNotes(const std::vector<MidiNote>& notes)
{
    notes_ = notes;
    repaint();
}

void PianoRollNotes::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(8.0f, ppb); repaint(); }
void PianoRollNotes::setKeyHeight(float keyHeight) { keyHeight_ = std::max(6.0f, keyHeight); repaint(); }
void PianoRollNotes::setScrollOffsetX(float offset) { scrollX_ = std::max(0.0f, offset); repaint(); }
void PianoRollNotes::setFirstMidiNote(int note) { firstNote_ = std::clamp(note, 0, 127); repaint(); }
void PianoRollNotes::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) { onNotesChanged_ = std::move(cb); }

VelocityLane::VelocityLane()
    : notesRef_(nullptr)
{
}

void VelocityLane::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    auto b = getBounds();
    auto bg = NUIColor(0.07f, 0.07f, 0.09f, 1.0f);
    auto border = theme.getColor("border");
    auto vel = theme.getColor("accentSecondary");
    renderer.fillRect(b, bg);
    renderer.strokeRect(b, 1, border);

    if (!notesRef_ || notesRef_->empty()) return;
    // Simple bars across width based on order
    float barWidth = std::max(2.0f, b.width / static_cast<float>(notesRef_->size()));
    for (size_t i = 0; i < notesRef_->size(); ++i)
    {
        float x = b.x + static_cast<float>(i) * barWidth;
        float h = (*notesRef_)[i].velocity * (b.height - 4.0f);
        NUIRect r(x + 1.0f, b.y + b.height - h - 2.0f, barWidth - 2.0f, h);
        renderer.fillRect(r, vel.withAlpha(0.8f));
    }
}

void VelocityLane::setNotes(const std::vector<MidiNote>* notesRef)
{
    notesRef_ = notesRef;
    repaint();
}

PianoRollView::PianoRollView()
    : pixelsPerBeat_(60.0f), beatsPerBar_(4), keyWidth_(80.0f), velocityLaneHeight_(60.0f), scrollX_(0.0f), scrollY_(0.0f)
{
    keyboard_ = std::make_shared<PianoKeyboard>();
    grid_ = std::make_shared<PianoGrid>();
    notesView_ = std::make_shared<PianoRollNotes>();
    velocityLane_ = std::make_shared<VelocityLane>();

    addChild(grid_);
    addChild(notesView_);
    addChild(keyboard_);
    addChild(velocityLane_);

    notesView_->setOnNotesChanged([this](const std::vector<MidiNote>& n){
        notes_ = n;
        velocityLane_->setNotes(&notes_);
    });
}

void PianoRollView::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRect(getBounds(), theme.getColor("backgroundPrimary"));
}

void PianoRollView::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutChildren();
}

bool PianoRollView::onMouseEvent(const NUIMouseEvent& event)
{
    // Horizontal scroll with Shift+Wheel, vertical with wheel
    if (event.wheelDelta != 0.0f)
    {
        // Shift-modified wheel => horizontal scroll
        if ((event.modifiers & NUIModifiers::Shift))
        {
            scrollX_ = std::max(0.0f, scrollX_ - event.wheelDelta * 60.0f);
            syncProps();
            return true;
        }
        else
        {
            scrollY_ = std::max(0.0f, scrollY_ - event.wheelDelta * 16.0f);
            syncProps();
            return true;
        }
    }
    return NUIComponent::onMouseEvent(event);
}

void PianoRollView::setNotes(const std::vector<MidiNote>& notes)
{
    notes_ = notes;
    if (notesView_) notesView_->setNotes(notes_);
    if (velocityLane_) velocityLane_->setNotes(&notes_);
}

void PianoRollView::setPixelsPerBeat(float ppb)
{
    pixelsPerBeat_ = std::max(8.0f, ppb);
    syncProps();
}

void PianoRollView::setBeatsPerBar(int bpb)
{
    beatsPerBar_ = std::max(1, bpb);
    syncProps();
}

void PianoRollView::layoutChildren()
{
    auto b = getBounds();
    float header = 0.0f;
    float footer = velocityLaneHeight_;

    // Keyboard on the left
    if (keyboard_) keyboard_->setBounds(NUIAbsolute(b, 0, header, keyWidth_, b.height - header - footer));
    if (grid_) grid_->setBounds(NUIAbsolute(b, keyWidth_, header, b.width - keyWidth_, b.height - header - footer));
    if (notesView_) notesView_->setBounds(NUIAbsolute(b, keyWidth_, header, b.width - keyWidth_, b.height - header - footer));
    if (velocityLane_) velocityLane_->setBounds(NUIAbsolute(b, 0, b.height - footer, b.width, footer));
    syncProps();
}

void PianoRollView::syncProps()
{
    if (grid_) {
        grid_->setPixelsPerBeat(pixelsPerBeat_);
        grid_->setBeatsPerBar(beatsPerBar_);
        grid_->setScrollOffsetX(scrollX_);
        grid_->setScrollOffsetY(scrollY_);
    }
    if (notesView_) {
        notesView_->setPixelsPerBeat(pixelsPerBeat_);
        notesView_->setKeyHeight(16.0f);
        notesView_->setScrollOffsetX(scrollX_);
        notesView_->setScrollOffsetY(scrollY_);  // Propagate vertical scroll to notes
        notesView_->setFirstMidiNote(72);
    }
    if (keyboard_) {
        keyboard_->setScrollOffsetY(scrollY_);  // Propagate vertical scroll to keyboard
    }
}

} // namespace NomadUI


