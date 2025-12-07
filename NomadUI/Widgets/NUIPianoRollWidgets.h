// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUISlider.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NomadUI {

struct MidiNote {
    int pitch;           // MIDI note number (0-127)
    double startBeat;    // Start position in beats
    double durationBeats;// Duration in beats
    float velocity;      // 0..1
    bool selected = false;
};

class PianoKeyboard : public NUIComponent {
public:
    PianoKeyboard();

    void onRender(NUIRenderer& renderer) override;

    void setKeyHeight(float height);
    float getKeyHeight() const { return keyHeight_; }

    void setFirstMidiNote(int note);
    void setNumKeys(int numKeys);
    void setScrollOffsetY(float offset);

private:
    float keyHeight_;
    int firstNote_;
    int numKeys_;
    float scrollY_;
};

class PianoGrid : public NUIComponent {
public:
    PianoGrid();

    void onRender(NUIRenderer& renderer) override;

    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);
    void setScrollOffsetX(float offset);
    void setScrollOffsetY(float offset);

private:
    float pixelsPerBeat_;
    int beatsPerBar_;
    float scrollX_;
    float scrollY_;
};

class PianoRollNotes : public NUIComponent {
public:
    PianoRollNotes();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setNotes(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& getNotes() const { return notes_; }

    void setPixelsPerBeat(float ppb);
    void setKeyHeight(float keyHeight);
    void setScrollOffsetX(float offset);
    void setScrollOffsetY(float offset);
    void setFirstMidiNote(int note);

    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);

private:
    std::vector<MidiNote> notes_;
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_;
    int firstNote_;
    std::function<void(const std::vector<MidiNote>&)> onNotesChanged_;

    // Interaction
    enum class State { None, Moving, Resizing, Selecting };
    State state_ = State::None;
    NUIPoint dragStart_;
    int hoveredNoteIndex_ = -1;
    int resizeEdge_ = 0; // -1 left, 1 right
    std::vector<MidiNote> dragStartNotes_;
};

class VelocityLane : public NUIComponent {
public:
    VelocityLane();

    void onRender(NUIRenderer& renderer) override;

    void setNotes(const std::vector<MidiNote>* notesRef);

private:
    const std::vector<MidiNote>* notesRef_;
};

class PianoRollView : public NUIComponent {
public:
    PianoRollView();

    void onRender(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setNotes(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& getNotes() const { return notes_; }

    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);

private:
    std::shared_ptr<PianoKeyboard> keyboard_;
    std::shared_ptr<PianoGrid> grid_;
    std::shared_ptr<PianoRollNotes> notesView_;
    std::shared_ptr<VelocityLane> velocityLane_;

    std::vector<MidiNote> notes_;
    float pixelsPerBeat_;
    int beatsPerBar_;
    float keyWidth_;
    float velocityLaneHeight_;
    float scrollX_;
    float scrollY_;

    void layoutChildren();
    void syncProps();
};

} // namespace NomadUI


