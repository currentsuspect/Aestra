// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../NomadUI/Widgets/NUIPianoRollWidgets.h"
#include "../NomadAudio/include/TrackManager.h"
#include <memory>

namespace Nomad {
namespace Audio {

/**
 * @brief Piano Roll Panel - MIDI editor with piano keyboard
 * 
 * Wraps PianoRollView in a WindowPanel for docking/maximizing
 */
class PianoRollPanel : public WindowPanel {
public:
    PianoRollPanel(std::shared_ptr<TrackManager> trackManager);
    ~PianoRollPanel() override = default;

    // Piano roll settings
    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);
    
    std::shared_ptr<NomadUI::PianoRollView> getPianoRoll() const { return m_pianoRoll; }

    // Pattern editing
    void loadPattern(PatternID patternId);
    void savePattern();
    PatternID getCurrentPattern() const { return m_currentPatternId; }

    void onUpdate(double deltaTime) override;

private:
    void updateGhostChannels();

    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<NomadUI::PianoRollView> m_pianoRoll;
    PatternID m_currentPatternId;  // Track which pattern is being edited
};

} // namespace Audio
} // namespace Nomad
