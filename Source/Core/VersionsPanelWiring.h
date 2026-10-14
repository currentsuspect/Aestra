// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Panels/TakesPanel.h"
#include "TakeManager.h"

#include "Analysis/AudioAnalysis.h"
#include "Commands/CommandHistory.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief What the Versions dock reads from the command history (SPEC-017): the open version's steps, newest first.
 * Steps taken back past (the redo side) come first and are dimmed; the current step is the first one not undone.
 * Pure: reads the history, touches nothing.
 */
inline std::vector<Audio::TakesPanel::StepEntry> versionStepEntries(const Audio::CommandHistory& history) {
    std::vector<Audio::TakesPanel::StepEntry> out;
    const auto name = [](const std::shared_ptr<Audio::ICommand>& c) {
        std::string n = c ? c->getName() : std::string();
        return n.empty() ? std::string("(unnamed)") : n;
    };
    const auto& redo = history.getRedoStack();
    const auto& undo = history.getUndoStack();
    for (int i = 0; i < static_cast<int>(redo.size()); ++i) out.push_back({name(redo[static_cast<size_t>(i)]), true, i});
    for (int i = static_cast<int>(undo.size()) - 1; i >= 0; --i) out.push_back({name(undo[static_cast<size_t>(i)]), false, i});
    return out;
}

/// A click on a step: land on the state just after it was made.
inline void goToVersionStep(Audio::CommandHistory& history, const Audio::TakesPanel::StepEntry& step) {
    if (step.undone) history.redoTo(step.stackIndex);
    else history.undoTo(step.stackIndex + 1);
}

/// Steps made since the open version was saved: the history's depth beyond the steps the version recorded.
inline int unsavedStepCount(const Audio::CommandHistory& history, const TakeManager::Manifest& manifest) {
    const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
    const int recorded = open ? static_cast<int>(open->steps.size()) : 0;
    return std::max(0, static_cast<int>(history.getUndoStack().size()) - recorded);
}

/// Hands the dock its live data: the project's name, the open version's steps, the unsaved count, and step jumps.
inline void wireVersionsDock(Audio::TakesPanel& panel, std::function<Audio::CommandHistory&()> history,
                             std::function<std::string()> projectPath, std::function<std::string()> projectName) {
    panel.setProjectNameProvider(std::move(projectName));
    panel.setStepsProvider([history]() { return versionStepEntries(history()); });
    panel.setOnStepActivated([history](const Audio::TakesPanel::StepEntry& e) { goToVersionStep(history(), e); });
    panel.setUnsavedProvider([history, projectPath]() {
        const std::string path = projectPath();
        return path.empty() ? 0 : unsavedStepCount(history(), TakeManager::loadManifest(path));
    });
}

/// Bounce a just-saved version with the real loudness measure (EBU R128 integrated loudness, the analysis service).
inline bool bounceSavedVersion(const std::string& projectPath, const std::string& takeId, const VersionBounce::Render& render) {
    return VersionBounce::bounceVersion(projectPath, takeId, render, [](const std::vector<float>& audio, uint32_t channels, uint32_t rate) {
        return Audio::analyzeAudio(audio, channels, rate).integratedLufs;
    });
}

/// Lets the dock show and play bounces: the card info read from the sidecar, and play/stop through the preview engine.
inline void wireVersionBounces(Audio::TakesPanel& panel, std::function<std::string()> projectPath,
                               std::function<bool(const std::string&)> togglePreview, std::function<bool(const std::string&)> isPlaying,
                               std::function<bool(const std::string&)> bounce) {
    panel.setBounceProvider([projectPath](const TakeManager::TakeEntry& take) -> std::optional<Audio::TakesPanel::BounceCard> {
        const std::string wav = TakeManager::resolveBouncePath(projectPath(), take);
        Audio::TakesPanel::BounceCard card;
        if (wav.empty() || !VersionBounce::readInfo(wav, card.info)) return std::nullopt;
        card.path = wav;
        return card;
    });
    panel.setOnToggleBounce(std::move(togglePreview));
    panel.setBouncePlayingProvider(std::move(isPlaying));
    panel.setOnBounce(std::move(bounce));
}

} // namespace Aestra
