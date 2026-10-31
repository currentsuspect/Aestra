// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Plugin/PluginHost.h"
#include "Presets/Preset.h"
#include "Presets/PresetLibrary.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Presets {

/**
 * @brief Everything the preset bar does to one open plugin, without drawing anything (FD-72, board A to C).
 *
 * The bar, the browser and Save are views of this. It knows which preset the sound came from, how many knobs have
 * moved since, the list in track order, a browse in progress (each step heard live; Enter keeps it as one undo, Esc
 * puts back what was there), the A/B pair, and saving to "Yours" or "This project only".
 *
 * Undo goes through `record`, which receives a command that has ALREADY been applied (pushExecuted).
 */
class PresetSession {
public:
    using Recorder = std::function<void(std::shared_ptr<ICommand>)>;
    /// The presets kept in this project for a plugin id, and a way to add or replace one there.
    using ProjectPresets = std::function<std::vector<Preset>(const std::string& pluginId)>;
    using SaveToProject = std::function<bool(const Preset&)>;

    PresetSession(std::shared_ptr<IPluginInstance> plugin, const PresetLibrary& library, std::string trackName,
                  Recorder record);

    void setProjectPresets(ProjectPresets list, SaveToProject save);
    /// Called after undo/redo or a preset load so an open editor can redraw its knobs.
    void setOnSoundChanged(std::function<void()> callback) { *m_notifyCell = std::move(callback); }

    // ---- what the bar shows -------------------------------------------------------------------
    /// Re-reads factory, yours and the project's presets, ordered for the track.
    void refresh();
    const std::vector<Preset>& presets() const { return m_presets; }
    /// The preset the sound came from; nothing before one is picked.
    const std::optional<Preset>& current() const { return *m_current; }
    std::string currentName() const;
    /// Knobs moved since the current preset (the amber "N changes"); 0 with no current preset.
    int changes() const;

    // ---- stepping and picking (each is one undo) ---------------------------------------------
    bool pick(size_t index);
    bool step(int direction); ///< the bar's ‹ › and the scroll wheel; wraps around

    // ---- browse by ear ---------------------------------------------------------------------------
    void beginBrowse();
    bool browsing() const { return m_browseBefore.has_value(); }
    bool audition(size_t index); ///< heard live, nothing recorded
    std::optional<size_t> auditioned() const { return m_auditioned; }
    bool keepBrowse();  ///< Enter: one undo back to before the browse
    void cancelBrowse(); ///< Esc: the sound before the browse, nothing recorded

    // ---- A/B ----------------------------------------------------------------------------------------
    /// Starts comparing: A is the sound now, B starts as a copy of it. Returns false if already comparing.
    bool beginCompare();
    bool comparing() const { return m_compare.has_value(); }
    int activeSlot() const { return m_compare ? m_compare->active : 0; } ///< 0 = A, 1 = B
    void flip();          ///< hear the other slot; edits to the one being left are kept in it
    bool keepActive();    ///< ends comparing on the slot being heard; one undo back to A as it was
    void endCompare();    ///< ends comparing on A as it was, nothing recorded

    // ---- save ---------------------------------------------------------------------------------------------
    enum class Place { Yours, Project };
    /// Save's prefilled name (FD-72): from the track and the preset the sound started from.
    std::string suggestedName() const;
    /// True when Save should offer "Update" first: the sound started from one of the producer's own presets.
    bool canUpdateCurrent() const;
    std::optional<Preset> save(const std::string& name, Place place, bool update, std::string* error = nullptr);

private:
    Preset snapshot() const;
    void notify();
    /// Records a change that is already heard: undo returns `before` and the bar to `beforeCurrent`.
    void record(Preset before, std::optional<Preset> beforeCurrent, std::optional<Preset> afterCurrent,
                const std::string& label);

    struct Compare {
        Preset a;      ///< A as it was when comparing began
        Preset slot[2];
        std::optional<Preset> from[2]; ///< the preset each slot came from, for the name on the bar
        int active = 0;
    };

    std::shared_ptr<IPluginInstance> m_plugin;
    const PresetLibrary& m_library;
    std::string m_trackName;
    Recorder m_record;
    ProjectPresets m_projectList;
    SaveToProject m_projectSave;

    std::vector<Preset> m_presets;
    /// Shared with recorded commands, so an undo puts the matching name back on the bar while the editor is open.
    std::shared_ptr<std::optional<Preset>> m_current = std::make_shared<std::optional<Preset>>();
    std::shared_ptr<std::function<void()>> m_notifyCell = std::make_shared<std::function<void()>>();
    std::optional<Preset> m_browseBefore;
    std::optional<Preset> m_browseFrom; ///< the current preset when the browse began
    std::optional<size_t> m_auditioned;
    std::optional<Compare> m_compare;
};

} // namespace Presets
} // namespace Audio
} // namespace Aestra
