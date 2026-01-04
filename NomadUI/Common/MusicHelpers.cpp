// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MusicHelpers.h"

namespace NomadUI {

std::vector<std::string> MusicTheory::getRootNames() {
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

std::vector<ScaleDef> MusicTheory::getScales() {
    return {
        { "Chromatic", {0,1,2,3,4,5,6,7,8,9,10,11} },
        { "Major", {0,2,4,5,7,9,11} },
        { "Minor", {0,2,3,5,7,8,10} },
        { "Harmonic Minor", {0,2,3,5,7,8,11} },
        { "Melodic Minor", {0,2,3,5,7,9,11} },
        { "Dorian", {0,2,3,5,7,9,10} },
        { "Phrygian", {0,1,3,5,7,8,10} },
        { "Lydian", {0,2,4,6,7,9,11} },
        { "Mixolydian", {0,2,4,5,7,9,10} },
        { "Locrian", {0,1,3,5,6,8,10} },
        { "Pentatonic Major", {0,2,4,7,9} },
        { "Pentatonic Minor", {0,3,5,7,10} },
        { "Blues", {0,3,5,6,7,10} }
    };
}

bool MusicTheory::isNoteInScale(int pitch, int rootKey, ScaleType type) {
    if (type == ScaleType::Chromatic) return true;
    
    int noteInOctave = pitch % 12;
    // Normalize to root C:
    // If root is D (2), and note is D (2), relative is 0.
    int relative = (noteInOctave - rootKey + 12) % 12;
    
    auto scales = getScales();
    int typeIdx = static_cast<int>(type);
    if (typeIdx < 0 || typeIdx >= scales.size()) return true; // Fallback
    
    const auto& intervals = scales[typeIdx].intervals;
    for (int i : intervals) {
        if (i == relative) return true;
    }
    return false;
}


double MusicTheory::getSnapDuration(SnapGrid snap) {
    switch (snap) {
        case SnapGrid::Bar: return 4.0;
        case SnapGrid::Beat: return 1.0;
        case SnapGrid::Half: return 0.5;
        case SnapGrid::Quarter: return 0.25;
        case SnapGrid::Eighth: return 0.125;
        case SnapGrid::Sixteenth: return 0.0625;
        case SnapGrid::Triplet: return 1.0 / 3.0;
        default: return 0.0;
    }
}

std::string MusicTheory::getSnapName(SnapGrid snap) {
    switch (snap) {
        case SnapGrid::Bar: return "Bar";
        case SnapGrid::Beat: return "Beat";
        case SnapGrid::Half: return "1/2 Beat";
        case SnapGrid::Quarter: return "1/4 Beat";
        case SnapGrid::Eighth: return "1/8 Beat";
        case SnapGrid::Sixteenth: return "1/16 Beat";
        case SnapGrid::Triplet: return "1/3 Beat";
        case SnapGrid::None: return "None";
    }
    return "Unknown";
}

std::vector<SnapGrid> MusicTheory::getSnapOptions() {
    return { 
        SnapGrid::Bar, SnapGrid::Beat, SnapGrid::Half, 
        SnapGrid::Quarter, SnapGrid::Eighth, SnapGrid::Sixteenth, 
        SnapGrid::Triplet, SnapGrid::None 
    };
}

} // namespace NomadUI
