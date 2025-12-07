// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIStepSequencer.h"

#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

StepSequencerView::StepSequencerView()
    : stepCount_(16)
    , beatsPerBar_(4)
    , labelColumnWidth_(96.0f)
    , minCellHeight_(24.0f)
    , hoverRow_(-1)
    , hoverStep_(-1)
{
    rowLabels_ = {"Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Perc 1", "Perc 2", "FX"};
    ensureGridSize();
}

void StepSequencerView::setRowLabels(const std::vector<std::string>& labels) {
    rowLabels_ = labels.empty() ? std::vector<std::string>{"Lane 1"} : labels;
    ensureGridSize();
    repaint();
}

void StepSequencerView::setStepCount(int steps) {
    stepCount_ = std::max(1, steps);
    ensureGridSize();
    repaint();
}

void StepSequencerView::setBeatsPerBar(int beats) {
    beatsPerBar_ = std::max(1, beats);
    repaint();
}

void StepSequencerView::setPattern(const std::vector<std::vector<SequencerStep>>& pattern) {
    steps_ = pattern;
    ensureGridSize();
    repaint();
}

void StepSequencerView::setOnPatternChanged(std::function<void(const std::vector<std::vector<SequencerStep>>&)> callback) {
    onPatternChanged_ = std::move(callback);
}

void StepSequencerView::ensureGridSize() {
    if (rowLabels_.empty()) {
        rowLabels_.push_back("Lane 1");
    }

    steps_.resize(rowLabels_.size());
    for (auto& row : steps_) {
        row.resize(stepCount_);
    }
}

void StepSequencerView::toggleCell(int row, int step, float localYInCell) {
    if (row < 0 || row >= static_cast<int>(steps_.size())) return;
    if (step < 0 || step >= stepCount_) return;

    auto& cell = steps_[static_cast<size_t>(row)][static_cast<size_t>(step)];
    // Derive velocity from click height inside the cell (top = louder)
    float normalized = 1.0f - std::clamp(localYInCell, 0.0f, minCellHeight_) / std::max(1.0f, minCellHeight_);
    float newVelocity = std::clamp(normalized, 0.2f, 1.0f);

    if (cell.active) {
        cell.active = false;
        cell.velocity = 0.0f;
    } else {
        cell.active = true;
        cell.velocity = newVelocity;
    }

    repaint();
    if (onPatternChanged_) {
        onPatternChanged_(steps_);
    }
}

void StepSequencerView::updateHover(float localX, float localY, float cellWidth, float cellHeight) {
    int prevRow = hoverRow_;
    int prevStep = hoverStep_;

    float gridStartX = labelColumnWidth_;
    float gridStartY = 4.0f; // small top padding

    if (localX < gridStartX || localY < gridStartY) {
        hoverRow_ = -1;
        hoverStep_ = -1;
        if (prevRow != hoverRow_ || prevStep != hoverStep_) repaint();
        return;
    }

    int row = static_cast<int>((localY - gridStartY) / cellHeight);
    int step = static_cast<int>((localX - gridStartX) / cellWidth);

    if (row < 0 || row >= static_cast<int>(steps_.size()) || step < 0 || step >= stepCount_) {
        hoverRow_ = -1;
        hoverStep_ = -1;
        if (prevRow != hoverRow_ || prevStep != hoverStep_) repaint();
        return;
    }

    hoverRow_ = row;
    hoverStep_ = step;
    if (prevRow != hoverRow_ || prevStep != hoverStep_) repaint();
}

void StepSequencerView::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    auto bounds = getBounds();

    // Background
    renderer.fillRect(bounds, theme.getColor("backgroundSecondary"));
    renderer.strokeRect(bounds, 1.0f, theme.getColor("border"));

    const int rowCount = static_cast<int>(rowLabels_.size());
    const float gridStartX = bounds.x + labelColumnWidth_;
    const float gridStartY = bounds.y + 4.0f;
    const float availableWidth = std::max(1.0f, bounds.width - labelColumnWidth_ - 8.0f);
    const float cellWidth = availableWidth / static_cast<float>(std::max(1, stepCount_));
    const float cellHeight = std::max(minCellHeight_, (bounds.height - 8.0f) / static_cast<float>(std::max(1, rowCount)));

    // Labels + row separators
    for (int r = 0; r < rowCount; ++r) {
        float rowY = gridStartY + r * cellHeight;
        NUIRect labelRect(bounds.x + 4.0f, rowY, labelColumnWidth_ - 8.0f, cellHeight);
        renderer.drawText(rowLabels_[static_cast<size_t>(r)], NUIPoint(labelRect.x + 4.0f, labelRect.y + 6.0f),
                          12.0f, theme.getColor("textSecondary"));

        // Row separator
        renderer.drawLine(NUIPoint(bounds.x, rowY + cellHeight), NUIPoint(bounds.x + bounds.width, rowY + cellHeight),
                          1.0f, theme.getColor("border").withAlpha(0.5f));
    }

    // Grid + steps
    NomadUI::NUIColor activeColor = theme.getColor("accentPrimary");
    NomadUI::NUIColor activeBorder = theme.getColor("border").withAlpha(0.8f);
    NomadUI::NUIColor inactiveColor = theme.getColor("surfaceTertiary");
    NomadUI::NUIColor hoverColor = theme.getColor("accentCyan").withAlpha(0.35f);

    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < stepCount_; ++c) {
            float x = gridStartX + c * cellWidth;
            float y = gridStartY + r * cellHeight;
            NUIRect cellRect(x + 1.0f, y + 1.0f, cellWidth - 2.0f, cellHeight - 2.0f);

            bool isBarLine = (beatsPerBar_ > 0) && (c % beatsPerBar_ == 0);
            if (isBarLine) {
                renderer.fillRect(NUIRect(x, y, 2.0f, cellHeight), theme.getColor("accentCyan").withAlpha(0.12f));
            }

            const auto& cell = steps_[static_cast<size_t>(r)][static_cast<size_t>(c)];
            if (cell.active) {
                float alpha = std::clamp(cell.velocity, 0.2f, 1.0f);
                renderer.fillRect(cellRect, activeColor.withAlpha(alpha));
                renderer.strokeRect(cellRect, 1.0f, activeBorder);
            } else {
                renderer.fillRect(cellRect, inactiveColor);
                renderer.strokeRect(cellRect, 1.0f, theme.getColor("border").withAlpha(0.6f));
            }

            if (hoverRow_ == r && hoverStep_ == c) {
                renderer.fillRect(cellRect, hoverColor);
            }
        }
    }

    // Vertical beat markers
    if (beatsPerBar_ > 0) {
        for (int c = 0; c <= stepCount_; ++c) {
            if (c % beatsPerBar_ != 0) continue;
            float x = gridStartX + c * cellWidth;
            renderer.drawLine(NUIPoint(x, bounds.y), NUIPoint(x, bounds.y + bounds.height),
                              1.0f, theme.getColor("border").withAlpha(0.45f));
        }
    }
}

bool StepSequencerView::onMouseEvent(const NUIMouseEvent& event) {
    auto bounds = getBounds();

    const int rowCount = static_cast<int>(rowLabels_.size());
    const float gridStartX = labelColumnWidth_;
    const float gridStartY = 4.0f;
    const float availableWidth = std::max(1.0f, bounds.width - labelColumnWidth_ - 8.0f);
    const float cellWidth = availableWidth / static_cast<float>(std::max(1, stepCount_));
    const float cellHeight = std::max(minCellHeight_, (bounds.height - 8.0f) / static_cast<float>(std::max(1, rowCount)));

    float localX = event.position.x - bounds.x;
    float localY = event.position.y - bounds.y;

    updateHover(localX, localY, cellWidth, cellHeight);

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hoverRow_ >= 0 && hoverStep_ >= 0) {
            float cellLocalY = std::fmod(std::max(0.0f, localY - gridStartY), cellHeight);
            toggleCell(hoverRow_, hoverStep_, cellLocalY);
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}

} // namespace NomadUI
