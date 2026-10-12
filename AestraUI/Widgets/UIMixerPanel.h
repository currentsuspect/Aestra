// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Base/NUISlider.h"
#include "MixerDensity.h"
#include "MixerGrouping.h"
#include "UIMixerNavigator.h"
#include "UIMixerToolbar.h"
#include "NUIComponent.h"
#include "NUIDragDrop.h"
#include "NUITypes.h"
#include "Helpers/MixerPluginListPolicy.h"
#include "InspectorCollapseState.h"
#include "UIMixerInspector.h"
#include "UIMixerMeter.h"
#include "UIMixerPluginDropdown.h"
#include "UIMixerStrip.h"

#include <functional>
#include <memory>
#include <vector>

// Forward declarations
namespace Aestra {
    class MixerViewModel;
    struct ChannelViewModel;
    namespace Audio {
        class ContinuousParamBuffer;
        class MeterSnapshotBuffer;
        class TrackManager;
    }
}

namespace AestraUI {

namespace Layout {
class NUILayoutRecorder; // NUILayoutExplain.h
}

/**
 * @brief Main mixer panel container with channel meters.
 *
 * This is a barebones implementation for Checkpoint 1 ("Meters Move").
 * Creates one UIMixerMeter per channel and lays them out horizontally.
 * Future checkpoints will add faders, knobs, and full channel strips.
 *
 * Requirements: 3.1 - Channel strips with fixed width (96-112px)
 */
class UIMixerPanel : public NUIComponent, public IDropTarget {
public:
    /**
     * @brief Construct a mixer panel.
     *
     * @param viewModel Shared view model for channel state
     * @param meterSnapshots Lock-free meter snapshot buffer for reading peaks
     */
    UIMixerPanel(std::shared_ptr<Aestra::MixerViewModel> viewModel,
                 std::shared_ptr<Aestra::Audio::TrackManager> trackManager);

    ~UIMixerPanel() override;

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // IDropTarget — plugin drops land on the strip under the cursor (#395).
    // The mixer overlays the timeline, so it must claim drops before
    // TrackManagerUI resolves them geometrically against the lanes behind it.
    DropFeedback onDragEnter(const DragData& data, const NUIPoint& position) override;
    DropFeedback onDragOver(const DragData& data, const NUIPoint& position) override;
    void onDragLeave() override;
    DropResult onDrop(const DragData& data, const NUIPoint& position) override;
    NUIRect getDropBounds() const override;

    /**
     * @brief Refresh channel list from view model.
     *
     * Call when tracks are added/removed to rebuild meter widgets.
     */
    void refreshChannels();

    /// Strip density and the Group-by-output switch (FD-28 points 1 and 4). Both persist per user.
    void setDensity(MixerDensity density);
    MixerDensity density() const { return m_density; }
    void setGroupByOutput(bool on);
    bool groupByOutput() const { return m_groupByOutput; }
    /// "Reset peaks": every channel's peak-since-reset and clip latch.
    void resetPeaks();
    /// Fired when density or grouping changes, so the owner can save the preference.
    std::function<void()> onViewPreferencesChanged;
    const UIMixerToolbar* toolbar() const { return m_toolbar.get(); }
    const UIMixerNavigator* navigator() const { return m_navigator.get(); }

    /// Channel ids in the order the strips are shown (grouped by output), left to right.
    std::vector<uint32_t> displayedChannelIds() const;
    /// The group headers above the strips, left to right; empty when the project has a single group.
    std::vector<std::string> groupHeaderLabels() const;
    /// Where a channel's strip is laid out, or nullptr when there is none.
    const UIMixerStrip* stripForChannel(uint32_t channelId) const;

    /**
     * @brief Replace the plugin dropdown's catalog.
     *
     * The app layer injects from the plugin scanner (internal registry +
     * VST3/CLAP); the policy filters to mixer inserts and groups by category.
     */
    void setPluginEntries(std::vector<Aestra::Components::MixerPluginEntry> entries) {
        if (m_pluginDropdown) {
            m_pluginDropdown->setPluginEntries(std::move(entries));
        }
    }

    /// Fired by the dropdown's "Browse all plugins" footer. The app layer
    /// opens the full plugin browser; the current search query is passed so
    /// the browser opens with the same terms the user typed.
    std::function<void(const std::string& searchQuery)> onBrowseAllPlugins;

    /// Fired when the dropdown opens with an empty catalog and asks the
    /// host to republish (used to recover when the initial setup ran
    /// before the async plugin scan completed).
    std::function<void()> onCatalogRefresh;

    /**
     * @brief Get the view model.
     */
    Aestra::MixerViewModel* getViewModel() { return m_viewModel.get(); }

    /**
     * @brief Get the inspector.
     */
    UIMixerInspector* getInspector() { return m_inspector.get(); }

    /**
     * @brief Set platform bridge for cursor warping on mixer knobs/faders.
     */
    void setPlatformBridge(class NUIPlatformBridge* bridge);

    /**
     * @brief Collapse the inspector to a thin re-open rail.
     *
     * A dedicated mixer view should be able to become almost entirely channel
     * strips; collapsing the inspector returns its width to the strips.
     */
    /// Explicit user intent. Persisted; width changes never write to it.
    void setInspectorExpandedPreference(bool expanded);
    bool getInspectorExpandedPreference() const { return m_inspectorCollapse.expandedPreference; }

    /// What the panel actually draws right now.
    bool isInspectorCollapsed() const { return !m_inspectorCollapse.effectiveExpanded(); }

    /// True when the collapse is imposed by width rather than chosen.
    bool isInspectorForcedCollapsed() const { return m_inspectorCollapse.forcedCollapsed; }

    /// Rail click — records intent even while width-constrained.
    void toggleInspectorCollapsed();

    /// Fires only when the *explicit* preference changes, so the host can
    /// persist it. Width-driven collapse deliberately does not fire.
    std::function<void(bool expanded)> onInspectorPreferenceChanged;

private:
    /// Current inspector width — the collapsed rail or the full panel.
    float inspectorWidth() const {
        return m_inspectorCollapse.effectiveExpanded() ? INSPECTOR_WIDTH : INSPECTOR_COLLAPSED_WIDTH;
    }

    /// Recompute the width-driven constraint from the current bounds.
    void updateInspectorWidthConstraint();

    /// The master strip and inspector rects, pinned to the panel's trailing
    /// (right) edge — the master strip flush against it, the inspector one
    /// spacing gap further in. V8-X2b: this was seven independent copies of
    /// the same two-line formula (rendering, hit-testing, drag preview,
    /// wheel-scroll, layout) before being collapsed to one source of truth
    /// via arrangeTrailingRow(). Both rects are in window-absolute space,
    /// matching this panel's own bounds() convention.
    struct TrailingLayout {
        NUIRect masterRect;
        NUIRect inspectorRect;
    };
    /// `recorder` is passed only by the layout pass (V8-X2b criterion 4); render and hit tests call it bare.
    TrailingLayout trailingLayout(Layout::NUILayoutRecorder* recorder = nullptr) const;

    /// Rail/handle that collapses or restores the inspector.
    NUIRect getInspectorToggleRect() const;

    bool channelLayoutMatchesViewModel() const;
    NUIRect getMinimapRect() const;
    float getChannelViewportWidth() const;
    float getChannelContentWidth() const;
    float getChannelMaxScroll() const;
    void updateNavigator();
    /// The navigator shows only when the strips overflow the panel.
    bool navigatorShown() const;
    float navigatorBlock() const;

    void showPluginDropdown(uint32_t channelId);
    void loadPluginToSelectedChannel(const std::string& pluginId);
    bool loadPluginToChannel(Aestra::ChannelViewModel* vmChannel, const std::string& pluginId);

    /// Strip under a screen position (channel strips + master), honoring the
    /// inspector/master occlusion of scrolled strips. Null when none.
    UIMixerStrip* stripAt(const NUIPoint& position) const;

    /// View-model channel for a strip (master strip resolves to getMaster()).
    Aestra::ChannelViewModel* channelForStrip(const UIMixerStrip* strip) const;

    void ensureDropTargetRegistration();

    std::shared_ptr<Aestra::MixerViewModel> m_viewModel;
    std::shared_ptr<Aestra::Audio::TrackManager> m_trackManager;

    // Callback for fader undo/redo: channelId, newDb
    std::function<void(uint32_t, float)> m_onFaderChanged;

    /// Channel strips (header + meter + fader)
    std::vector<std::shared_ptr<UIMixerStrip>> m_strips;

    /// Display order and groups (V81-M1): strips are shown grouped by where they go.
    /// m_stripOrder maps a display slot to an index in m_strips.
    MixerDensity m_density{MixerDensity::Normal};
    bool m_groupByOutput{true};
    std::shared_ptr<UIMixerToolbar> m_toolbar;
    std::vector<size_t> m_stripOrder;
    Aestra::MixerGroupedOrder m_grouping;
    std::string m_groupingSignature;
    struct GroupBand {
        NUIRect rect;
        std::string label;
        int colorIndex{-1};
        bool master{false};
    };
    std::vector<GroupBand> m_groupBands;
    /// Rebuilds the grouping from the view model; true when it changed.
    bool recomputeGrouping();
    /// Extra width the gaps between groups add to the strip row.
    float groupGapsWidth() const;
    void renderGroupBands(NUIRenderer& renderer);
    void renderDbScale(NUIRenderer& renderer);

    /// Master strip (pinned on the right)
    std::shared_ptr<UIMixerStrip> m_masterStrip;

    /// Inspector panel (pinned on the right, before master)
    std::shared_ptr<UIMixerInspector> m_inspector;
    InspectorCollapseState m_inspectorCollapse;
    bool m_inspectorToggleHovered{false};

    /// Plugin finder dropdown (topmost, shown on Add Insert click)
    std::shared_ptr<UIMixerPluginDropdown> m_pluginDropdown;

    // Drag-and-drop state (#395)
    bool m_dropTargetRegistered{false};
    bool m_dropOrderClaimedForDrag{false};
    int64_t m_dropHoverChannelId{-1}; ///< Channel id of strip under an active plugin drag, -1 = none

    // Horizontal scroll offset for channel strips (pixels).
    float m_scrollX{0.0f};
    float m_targetScrollX{0.0f};
    std::shared_ptr<UIMixerNavigator> m_navigator;

    // Layout constants (from design spec)
    static constexpr float TOOLBAR_H = 26.0f;
    float stripWidth() const { return mixerStripWidth(m_density); }
    static constexpr float STRIP_SPACING = 6.0f;
    static constexpr float GROUP_HEADER_H = 18.0f;
    static constexpr float GROUP_GAP = 10.0f;
    static constexpr float SCALE_W = 26.0f; // the shared dB scale at the far left
    static constexpr float HEADER_HEIGHT = 28.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float MASTER_STRIP_WIDTH = 146.0f;
    static constexpr float INSPECTOR_WIDTH = 236.0f;
    static constexpr float INSPECTOR_COLLAPSED_WIDTH = 16.0f;
    /// Strips that must remain usable beside the inspector before its width is
    /// worth spending; below this the collapse is imposed by layout.
    static constexpr int MIN_STRIPS_BESIDE_INSPECTOR = 4;
    static constexpr float MINIMAP_HEIGHT = 22.0f;
    static constexpr float MINIMAP_GAP = 6.0f;
    static constexpr float MIXER_MIN_CHANNEL_HEIGHT = 220.0f;

    // Cached theme colors
    NUIColor m_backgroundColor;
    NUIColor m_separatorColor;

    /**
     * @brief Layout all meter widgets horizontally.
     */
    void layoutMeters();

    /**
     * @brief Render separator lines between strips.
     */
    void renderSeparators(NUIRenderer& renderer);

    /**
     * @brief Cache theme colors.
     */
    void cacheThemeColors();
};

} // namespace AestraUI
