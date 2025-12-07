// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include <functional>
#include <string>
#include <vector>

namespace NomadUI {

struct SequencerStep {
    bool active{false};
    float velocity{1.0f}; // 0..1 velocity for future integration
};

/**
 * @brief Simple 16-step drum sequencer grid.
 *
 * UI-only grid for laying out percussive patterns. Each row represents a lane
 * (kick, snare, hat, etc.) and each column is a step. Clicking toggles a step.
 */
class StepSequencerView : public NUIComponent {
public:
    StepSequencerView();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setRowLabels(const std::vector<std::string>& labels);
    void setStepCount(int steps);
    void setBeatsPerBar(int beats);

    void setPattern(const std::vector<std::vector<SequencerStep>>& pattern);
    const std::vector<std::vector<SequencerStep>>& getPattern() const { return steps_; }

    void setOnPatternChanged(std::function<void(const std::vector<std::vector<SequencerStep>>&)> callback);

private:
    std::vector<std::string> rowLabels_;
    std::vector<std::vector<SequencerStep>> steps_;
    int stepCount_;
    int beatsPerBar_;
    float labelColumnWidth_;
    float minCellHeight_;

    int hoverRow_;
    int hoverStep_;

    std::function<void(const std::vector<std::vector<SequencerStep>>&)> onPatternChanged_;

    void ensureGridSize();
    void toggleCell(int row, int step, float localYInCell);
    void updateHover(float localX, float localY, float cellWidth, float cellHeight);
};

} // namespace NomadUI
