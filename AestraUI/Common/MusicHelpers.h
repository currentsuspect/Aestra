// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace AestraUI {

struct MidiNote {
    int pitch;           // MIDI note number (0-127)
    double startBeat;    // Start position in beats
    double durationBeats;// Duration in beats
    float velocity;      // 0..1
    float pan = 0.0f;    // -1 (left) .. 1 (right), 0 = centre
    uint64_t unitId = 0; // Arsenal unit routing
    bool selected = false;
    bool isDeleted = false; // For delete animation
    mutable float animationScale = 1.0f; // For animations
};

// Global Tools available on the Transport Bar
enum class GlobalTool {
    Pointer, // Select, Move, Resize
    Pencil,  // Click to add, Drag to paint length
    Eraser   // Click to delete
};

enum class ScaleType {
    Chromatic,
    Major,
    Minor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    Count
};

enum class SnapGrid {
    Bar,        // 4.0 Beats
    Beat,       // 1.0 Beat
    Half,       // 1/2 Beat (1/8 note)
    Quarter,    // 1/4 Beat (1/16 note)
    Eighth,     // 1/8 Beat (1/32 note)
    Sixteenth,  // 1/16 Beat (1/64 note)
    Triplet,    // 1/3 Beat (1/12 note)
    None
};

struct ScaleDef {
    std::string name;
    std::vector<int> intervals; 
};

class MusicTheory {
public:
    static bool isNoteInScale(int pitch, int rootKey, ScaleType type);
    static std::vector<std::string> getRootNames();
    static std::vector<ScaleDef> getScales();

    // Snap Helpers
    static double getSnapDuration(SnapGrid snap);
    static std::string getSnapName(SnapGrid snap);
    static std::vector<SnapGrid> getSnapOptions();

    // Pitch Name Helpers
    static std::string getPitchName(int midiNote);    // e.g. "C4", "F#3"
    static std::string getPitchNameShort(int midiNote); // e.g. "C4", "F#3" (same, reserved for future shorthand)

    // Scale Pitch Movement Helpers
    static int nextPitchInScale(int pitch, int rootKey, ScaleType type);
    static int previousPitchInScale(int pitch, int rootKey, ScaleType type);
};

} // namespace AestraUI
