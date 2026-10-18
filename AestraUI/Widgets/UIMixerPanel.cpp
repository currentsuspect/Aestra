// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPanel.h"
#include "UIMixerStrip.h"
#include "MixerScale.h"
#include "TrackColorPalette.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "MixerViewModel.h"
#include "MeterSnapshot.h"
#include "Commands/SetVolumeCommand.h"
#include "../../AestraCore/include/AestraUnifiedProfiler.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetTrimCommand.h"
#include "Commands/SetMonitoringCommand.h"
#include "TrackManager.h"
#include "PluginManager.h"
#include "Commands/PluginCommands.h"
#include "Commands/SetAudioPatternMixerChannelCommand.h"
#include "Plugin/EffectChain.h"
#include "Plugin/AestraDelay.h"
#include "../Layout/NUILayoutAlgorithms.h"
#include "../Layout/NUILayoutSpace.h"
#include <algorithm>

namespace AestraUI {

namespace {

float safeClampMixerScroll(float value, float upper)
{
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

}

UIMixerPanel::UIMixerPanel(std::shared_ptr<Aestra::MixerViewModel> viewModel,
                           std::shared_ptr<Aestra::Audio::TrackManager> trackManager)
    : m_viewModel(std::move(viewModel))
    , m_trackManager(std::move(trackManager))
{
    cacheThemeColors();

    // Inspector (pinned on right, before master).
    m_inspector = std::make_shared<UIMixerInspector>(m_viewModel.get());
    // Trim and width live in the inspector now (they left the strips): trim drags live and commits one undo step.
    m_inspector->onTrimEdited = [this](uint32_t channelId, float db, bool commit) {
        if (!m_trackManager || !m_viewModel) return;
        auto* vm = m_viewModel->getChannelById(channelId);
        auto* mixerChannel = m_trackManager->getChannelById(channelId);
        if (!vm || !mixerChannel) return;
        if (!commit) {
            vm->trimDb = db;
            if (auto params = m_trackManager->getContinuousParams()) params->setTrimDb(vm->slotIndex, db);
            return;
        }
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<Aestra::Audio::SetTrimCommand>(*m_trackManager, *mixerChannel, vm->slotIndex, db));
    };
    m_inspector->onWidthEdited = [this](uint32_t channelId, float width) {
        if (!m_viewModel) return;
        auto* vm = m_viewModel->getChannelById(channelId);
        if (!vm) return;
        vm->width = width;
        if (vm->channel) vm->channel->setWidth(width);
    };
    addChild(m_inspector);

    m_navigator = std::make_shared<UIMixerNavigator>();
    m_navigator->setVisible(false);
    m_navigator->onScrollRequested = [this](float scroll) {
        m_targetScrollX = std::clamp(scroll, 0.0f, getChannelMaxScroll());
    };
    m_navigator->onTickClicked = [this](uint32_t channelId) {
        if (m_viewModel) m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));
    };
    addChild(m_navigator);

    m_toolbar = std::make_shared<UIMixerToolbar>();
    m_toolbar->onDensityChanged = [this](MixerDensity d) { setDensity(d); };
    m_toolbar->onGroupToggled = [this](bool on) { setGroupByOutput(on); };
    m_toolbar->onResetPeaks = [this]() { resetPeaks(); };
    addChild(m_toolbar);

    // Create master strip (pinned on right, does not scroll with channels).
    // Create master strip (pinned on right, does not scroll with channels).
    m_masterStrip = std::make_shared<UIMixerStrip>(0, 0, m_viewModel.get(), 
                                                   m_trackManager->getMeterSnapshots(), 
                                                   m_trackManager->getContinuousParams());
    m_masterStrip->onFXClicked = [this](uint32_t channelId) {
        showPluginDropdown(channelId);
    };
    addChild(m_masterStrip);

    // Plugin finder dropdown (created hidden, shown on Add Insert click)
    m_pluginDropdown = std::make_shared<UIMixerPluginDropdown>();
    m_pluginDropdown->setId("UIMixerPluginDropdown");
    m_pluginDropdown->onPluginSelected = [this](const std::string& pluginId, const std::string&) {
        loadPluginToSelectedChannel(pluginId);
    };
    m_pluginDropdown->onBrowseAllRequested = [this](const std::string& searchQuery) {
        if (m_inspector) {
            m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
        }
        if (onBrowseAllPlugins) {
            onBrowseAllPlugins(searchQuery);
        }
    };
    m_pluginDropdown->onRequestRefresh = [this]() {
        if (onCatalogRefresh) {
            onCatalogRefresh();
        }
    };
    addChild(m_pluginDropdown);

    // Initial channel refresh
    refreshChannels();
}

UIMixerPanel::~UIMixerPanel() {
    NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

void UIMixerPanel::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    // Deep Void Background from Theme
    m_backgroundColor = theme.getColor("backgroundPrimary");
    // Subtle Glass Separator
    m_separatorColor = theme.getColor("divider");
}

bool UIMixerPanel::channelLayoutMatchesViewModel() const
{
    if (!m_viewModel) {
        return m_strips.empty();
    }

    const size_t channelCount = m_viewModel->getChannelCount();
    if (m_strips.size() != channelCount) {
        return false;
    }

    for (size_t i = 0; i < channelCount; ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel || !m_strips[i] || m_strips[i]->getChannelId() != channel->id) {
            return false;
        }
    }

    return true;
}

void UIMixerPanel::refreshChannels()
{
    if (!m_viewModel) return;

    size_t channelCount = m_viewModel->getChannelCount();

    if (channelLayoutMatchesViewModel()) {
        layoutMeters();
        return;
    }

    for (auto& strip : m_strips) {
        removeChild(strip);
    }
    m_strips.clear();

    m_strips.reserve(channelCount);
    for (size_t i = 0; i < channelCount; ++i) {
        auto* channel = m_viewModel->getChannelByIndex(i);
        if (!channel) continue;
        auto strip = std::make_shared<UIMixerStrip>(channel->id, static_cast<int>(i + 1),
                                                    m_viewModel.get(),
                                                    m_trackManager->getMeterSnapshots(),
                                                    m_trackManager->getContinuousParams());
        strip->onFXClicked = [this](uint32_t channelId) {
            showPluginDropdown(channelId);
        };
        // The output chip opens the channel's I/O tab, where the destination is chosen.
        strip->onOutputClicked = [this](uint32_t channelId) {
            if (!m_viewModel) return;
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));
            if (m_inspector) m_inspector->setActiveTab(UIMixerInspector::Tab::IO);
        };

        // Wire fader to CommandHistory for undo/redo
        uint32_t chId = channel->id;
        strip->onFaderChanged = [this, chId](float newDb) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            // Convert dB to linear gain
            float newGain = (newDb <= -90.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
            float oldGain = mixerChannel->getVolume();

            if (std::abs(newGain - oldGain) > 0.0001f) {
                m_trackManager->getCommandHistory().pushAndExecute(
                    std::make_shared<Aestra::Audio::SetVolumeCommand>(*m_trackManager, *mixerChannel, newGain));
                Aestra::Log::info("[UIMixerPanel] Fader cmd: " + std::to_string(newDb) + " dB");
            }
        };

        // Wire mute to CommandHistory for undo/redo
        strip->onMuteChanged = [this, chId](bool muted) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetMuteCommand>(*m_trackManager, *mixerChannel, muted));
        };

        // Wire solo to CommandHistory for undo/redo
        strip->onSoloChanged = [this, chId](bool soloed) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetSoloCommand>(*m_trackManager, *mixerChannel, soloed));
        };

        // Wire pan to CommandHistory for undo/redo
        strip->onPanChanged = [this, chId](float pan) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetPanCommand>(*m_trackManager, *mixerChannel, pan));
        };

        // Wire input monitoring to CommandHistory for undo/redo + dirty parity
        strip->onMonitorToggled = [this, chId](bool monitored) {
            if (!m_trackManager) return;
            auto* mixerChannel = m_trackManager->getChannelById(chId);
            if (!mixerChannel) return;

            m_trackManager->getCommandHistory().pushAndExecute(
                std::make_shared<Aestra::Audio::SetMonitoringCommand>(*m_trackManager, *mixerChannel, monitored));
        };

        strip->setDensity(m_density);
        m_strips.push_back(strip);
        addChild(strip);
    }

    // Ensure fixed panels stay on top for hit-testing/rendering.
    if (m_inspector) {
        removeChild(m_inspector);
        addChild(m_inspector);
    }
    if (m_masterStrip) {
        removeChild(m_masterStrip);
        addChild(m_masterStrip);
    }

    layoutMeters();
}

void UIMixerPanel::setPlatformBridge(NUIPlatformBridge* bridge)
{
    for (auto& strip : m_strips) {
        if (strip) strip->setPlatformBridge(bridge);
    }
    if (m_masterStrip) m_masterStrip->setPlatformBridge(bridge);
    if (m_inspector) m_inspector->setPlatformBridge(bridge);
}

UIMixerPanel::TrailingLayout UIMixerPanel::trailingLayout(Layout::NUILayoutRecorder* recorder) const
{
    // stripY/stripHeight are recomputed here rather than threaded through as
    // parameters: every one of this method's seven call sites already
    // recomputes them too, and giving trailingLayout() its own bounds-only
    // signature is what let it replace all seven independently — no caller
    // needed to change what it already had on hand.
    //
    // stripY is inlined from getMinimapRect()'s own arithmetic (y=bounds.y+2,
    // height=MINIMAP_HEIGHT) rather than calling it directly: getMinimapRect()
    // needs this method's inspectorRect.x for its own width, and depending on
    // it here would make the two call each other.
    const auto bounds = getBounds();
    const float stripY = bounds.y + 2.0f + TOOLBAR_H + std::max(navigatorBlock(), MINIMAP_GAP);
    const float stripHeight = std::max(MIXER_MIN_CHANNEL_HEIGHT, bounds.bottom() - stripY - PADDING);

    // Master flush against the trailing edge (edgeMargin 0); inspector one
    // spacing gap further in. Nearest-to-edge first, matching
    // arrangeTrailingRow's own convention.
    const Layout::NUILocalRect container(bounds.x, stripY, bounds.width, stripHeight);
    const std::vector<float> widths{MASTER_STRIP_WIDTH, inspectorWidth()};
    if (recorder) {
        recorder->scope("trailing");
    }
    const auto rects = Layout::arrangeTrailingRow(container, widths, stripHeight, STRIP_SPACING, 0.0f, recorder);

    // The row is already computed in this panel's own coordinate space
    // (bounds.x/bounds.y are folded into `container` above), so there is no
    // second space to re-enter here — unlike WindowPanel, whose children sit
    // in a nested content band with its own local origin.
    TrailingLayout layout;
    layout.masterRect = rects[0].raw();
    layout.inspectorRect = rects[1].raw();
    return layout;
}

std::vector<uint32_t> UIMixerPanel::displayedChannelIds() const
{
    std::vector<uint32_t> ids;
    for (const size_t index : m_stripOrder) {
        if (index < m_strips.size() && m_strips[index]) ids.push_back(m_strips[index]->getChannelId());
    }
    return ids;
}

std::vector<std::string> UIMixerPanel::groupHeaderLabels() const
{
    std::vector<std::string> labels;
    for (const auto& band : m_groupBands) labels.push_back(band.label);
    return labels;
}

const UIMixerStrip* UIMixerPanel::stripForChannel(uint32_t channelId) const
{
    for (const auto& strip : m_strips) {
        if (strip && strip->getChannelId() == channelId) return strip.get();
    }
    return nullptr;
}

void UIMixerPanel::updateNavigator()
{
    if (!m_navigator) return;
    const bool show = navigatorShown() && !m_strips.empty();
    m_navigator->setVisible(show);
    if (!show) return;
    m_navigator->setBounds(getMinimapRect());

    std::vector<UIMixerNavigator::Tick> ticks;
    std::vector<UIMixerNavigator::Group> groups;
    ticks.reserve(m_stripOrder.size());
    const float step = stripWidth() + STRIP_SPACING;
    size_t groupIndex = 0;
    for (size_t slot = 0; slot < m_stripOrder.size(); ++slot) {
        while (m_grouping.grouped() && groupIndex + 1 < m_grouping.groups.size() &&
               slot >= m_grouping.groups[groupIndex].first + m_grouping.groups[groupIndex].count) {
            ++groupIndex;
        }
        const float left = static_cast<float>(slot) * step + (m_grouping.grouped() ? static_cast<float>(groupIndex) * GROUP_GAP : 0.0f);
        const auto& strip = m_strips[m_stripOrder[slot]];
        const auto* ch = m_viewModel ? m_viewModel->getChannelById(strip->getChannelId()) : nullptr;
        UIMixerNavigator::Tick tick;
        tick.channelId = strip->getChannelId();
        tick.x = left;
        tick.w = stripWidth();
        if (ch) {
            const uint32_t argb = ch->trackColorIndex >= 0 ? paletteIndexToARGB(ch->trackColorIndex) : 0xFF808080u;
            tick.color = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
            tick.level = MixerScale::norm(std::max(ch->smoothedPeakL, ch->smoothedPeakR));
            tick.clipped = ch->clipLatchL || ch->clipLatchR;
            tick.soloed = ch->soloed;
            tick.selected = m_viewModel->getSelectedChannelId() == static_cast<int32_t>(ch->id);
        }
        ticks.push_back(tick);
    }
    if (m_grouping.grouped()) {
        for (size_t g = 0; g < m_grouping.groups.size(); ++g) {
            const auto& group = m_grouping.groups[g];
            UIMixerNavigator::Group label;
            label.label = group.label;
            label.x = ticks[group.first].x;
            label.w = ticks[group.first + group.count - 1].x + stripWidth() - label.x;
            groups.push_back(std::move(label));
        }
    }
    m_navigator->setContent(std::move(ticks), std::move(groups), getChannelContentWidth());
    m_navigator->setViewport(m_scrollX, getChannelViewportWidth());
}

void UIMixerPanel::setDensity(MixerDensity density)
{
    if (density == m_density) return;
    m_density = density;
    for (auto& strip : m_strips) {
        if (strip) strip->setDensity(density);
    }
    if (m_toolbar) m_toolbar->setDensity(density);
    layoutMeters();
    repaint();
    if (onViewPreferencesChanged) onViewPreferencesChanged();
}

void UIMixerPanel::setGroupByOutput(bool on)
{
    if (on == m_groupByOutput) return;
    m_groupByOutput = on;
    if (m_toolbar) m_toolbar->setGroupByOutput(on);
    recomputeGrouping();
    layoutMeters();
    repaint();
    if (onViewPreferencesChanged) onViewPreferencesChanged();
}

void UIMixerPanel::resetPeaks()
{
    if (!m_viewModel) return;
    m_viewModel->resetPeaks();
    if (m_trackManager) {
        if (auto snaps = m_trackManager->getMeterSnapshots()) {
            for (size_t i = 0; i < m_viewModel->getChannelCount(); ++i) {
                if (const auto* ch = m_viewModel->getChannelByIndex(i)) snaps->clearClip(static_cast<int>(ch->slotIndex));
            }
            if (const auto* master = m_viewModel->getMaster()) snaps->clearClip(static_cast<int>(master->slotIndex));
        }
    }
    repaint();
}

bool UIMixerPanel::recomputeGrouping()
{
    std::vector<Aestra::MixerGroupInput> inputs;
    inputs.reserve(m_strips.size());
    std::string signature;
    for (const auto& strip : m_strips) {
        const auto* channel = (m_viewModel && strip) ? m_viewModel->getChannelById(strip->getChannelId()) : nullptr;
        Aestra::MixerGroupInput in;
        in.id = strip ? strip->getChannelId() : 0;
        if (channel) {
            in.mainOutputId = channel->mainOutputId;
            in.name = channel->name;
            in.colorIndex = channel->trackColorIndex;
        }
        signature += std::to_string(in.id) + ">" + std::to_string(in.mainOutputId) + ":" +
                     std::to_string(in.colorIndex) + ":" + in.name + ";";
        inputs.push_back(std::move(in));
    }
    signature += m_groupByOutput ? "|g" : "|f";
    if (signature == m_groupingSignature && m_stripOrder.size() == m_strips.size()) {
        return false;
    }
    m_groupingSignature = std::move(signature);
    if (m_groupByOutput) {
        m_grouping = Aestra::groupMixerStripsByOutput(inputs);
    } else {
        // Flat: the project's own channel order, one group, no headers.
        m_grouping = Aestra::MixerGroupedOrder{};
        Aestra::MixerStripGroup all;
        all.label = "Master";
        all.count = inputs.size();
        for (size_t i = 0; i < inputs.size(); ++i) m_grouping.order.push_back(i);
        if (!inputs.empty()) m_grouping.groups.push_back(all);
    }
    m_stripOrder = m_grouping.order;
    return true;
}

float UIMixerPanel::groupGapsWidth() const
{
    return m_grouping.grouped() ? static_cast<float>(m_grouping.groups.size() - 1) * GROUP_GAP : 0.0f;
}

void UIMixerPanel::layoutMeters()
{
    // Derived state first: the rest of the layout reads inspectorWidth().
    updateInspectorWidthConstraint();

    auto bounds = getBounds();
    // AESTRA_LAYOUT_TRACE=mixer (V8-X2b criterion 4). This panel lays out in
    // window-absolute coordinates already, so its Local origin is the window's.
    auto* trace = Layout::layoutRecorderFor("mixer");
    if (trace) {
        trace->beginPass(Layout::NUIWindowPoint(0.0f, 0.0f));
    }
    const TrailingLayout trailing = trailingLayout(trace);
    recomputeGrouping();
    // Under group headers the strips start lower, so the headers have a band of their own.
    const float bandH = m_grouping.grouped() ? GROUP_HEADER_H : 0.0f;
    const float bandY = trailing.masterRect.y;
    const float stripY = trailing.masterRect.y + bandH;
    const float stripHeight = trailing.masterRect.height - bandH;

    if (m_toolbar) {
        const float toolbarW = std::max(0.0f, trailing.inspectorRect.x - STRIP_SPACING - bounds.x);
        m_toolbar->setBounds(bounds.x, bounds.y + 2.0f, toolbarW, TOOLBAR_H);
        m_toolbar->setSummary(static_cast<int>(m_strips.size()), m_grouping.grouped());
    }

    // Layout master strip on the right.
    if (m_masterStrip) {
        m_masterStrip->setBounds(trailing.masterRect);
        m_masterStrip->setVisible(true);
    }

    // Layout inspector just to the left of master.
    if (m_inspector) {
        // Collapsed, the inspector yields its width to the channel strips and
        // leaves only the re-open rail drawn by the panel. Its rect always
        // uses inspectorWidth() (the collapsed rail's own width when
        // collapsed) — INSPECTOR_WIDTH here is deliberately the *expanded*
        // width passed to onResize, matching what the original always did:
        // the widget's internal layout is sized for its full expanded
        // content regardless of the rail's current width, since it becomes
        // invisible rather than reflowing.
        m_inspector->setBounds(trailing.inspectorRect.x, stripY, INSPECTOR_WIDTH, stripHeight);
        m_inspector->setVisible(!isInspectorCollapsed());
        if (!isInspectorCollapsed()) {
            m_inspector->onResize(static_cast<int>(INSPECTOR_WIDTH), static_cast<int>(stripHeight));
        }
    }

    // Layout channel strips to the left, keeping them out of the inspector/master area.
    const float left = bounds.x + SCALE_W; // the shared dB scale sits at the far left
    const float right = trailing.inspectorRect.x - STRIP_SPACING;
    const float maxScroll = getChannelMaxScroll();
    m_scrollX = safeClampMixerScroll(m_scrollX, maxScroll);
    m_targetScrollX = safeClampMixerScroll(m_targetScrollX, maxScroll);

    const Layout::NUILocalRect viewport(left, stripY, right - left, stripHeight);
    if (trace) {
        trace->scope("strips");
    }
    const auto items =
        Layout::arrangeScrollingRow(viewport, stripWidth(), STRIP_SPACING, m_strips.size(), m_scrollX, trace);
    Layout::finishLayoutPass(trace);
    m_groupBands.clear();
    size_t groupIndex = 0;
    for (size_t slot = 0; slot < m_stripOrder.size() && slot < items.size(); ++slot) {
        while (groupIndex + 1 < m_grouping.groups.size() &&
               slot >= m_grouping.groups[groupIndex].first + m_grouping.groups[groupIndex].count) {
            ++groupIndex;
        }
        NUIRect rect = items[slot].rect.raw();
        rect.x += static_cast<float>(groupIndex) * (m_grouping.grouped() ? GROUP_GAP : 0.0f);
        const bool visible = rect.right() > viewport.raw().x && rect.x < viewport.raw().right();
        auto& strip = m_strips[m_stripOrder[slot]];
        strip->setVisible(visible);
        strip->setBounds(rect);
    }
    if (m_grouping.grouped()) {
        for (const auto& group : m_grouping.groups) {
            const auto& first = m_strips[m_stripOrder[group.first]]->getBounds();
            const auto& last = m_strips[m_stripOrder[group.first + group.count - 1]]->getBounds();
            GroupBand band;
            band.rect = NUIRect{first.x, bandY, last.right() - first.x, GROUP_HEADER_H - 3.0f};
            band.label = group.label;
            band.colorIndex = group.colorIndex;
            band.master = group.targetId == 0;
            m_groupBands.push_back(std::move(band));
        }
    }
}

void UIMixerPanel::renderDbScale(NUIRenderer& renderer)
{
    // One scale for every strip (FD-28 point 2), drawn once at the far left against the meters' own height.
    const UIMixerStrip* ref = nullptr;
    for (const auto& strip : m_strips) {
        if (strip && strip->isVisible()) { ref = strip.get(); break; }
    }
    if (!ref) return;
    const NUIRect bars = ref->meterBarsRect();
    if (bars.isEmpty()) return;
    auto& theme = NUIThemeManager::getInstance();
    const auto b = getBounds();
    const NUIColor label = theme.getColor("textSecondary").withAlpha(0.75f);
    const NUIColor tick = theme.getColor("border").withAlpha(0.55f);
    float lastY = 1e9f;
    for (const float db : MixerScale::kTicks) {
        const float y = bars.bottom() - MixerScale::norm(db) * bars.height;
        if (lastY - y < 11.0f && lastY < 1e8f) continue; // never print two labels on top of each other
        lastY = y;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%s%d", db > 0.0f ? "+" : (db < 0.0f ? "\xE2\x88\x92" : ""), static_cast<int>(std::fabs(db)));
        const float w = renderer.measureText(buf, 8.5f).width;
        renderer.drawText(buf, {b.x + SCALE_W - 8.0f - w, y - 5.5f}, 8.5f, label);
        renderer.fillRect(NUIRect{b.x + SCALE_W - 5.0f, y, 4.0f, 1.0f}, tick);
    }
}

void UIMixerPanel::renderGroupBands(NUIRenderer& renderer)
{
    if (m_groupBands.empty()) return;
    auto& theme = NUIThemeManager::getInstance();
    for (const auto& band : m_groupBands) {
        NUIColor color = theme.getColor("accentPrimary");
        if (!band.master && band.colorIndex >= 0) {
            const uint32_t argb = paletteIndexToARGB(band.colorIndex);
            color = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
        } else if (band.master) {
            color = theme.getColor("textSecondary");
        }
        renderer.fillRoundedRect(band.rect, 3.0f, color.withAlpha(band.master ? 0.10f : 0.18f));
        renderer.fillRect(NUIRect{band.rect.x, band.rect.y, band.rect.width, 2.0f}, color.withAlpha(band.master ? 0.45f : 0.9f));
        renderer.drawText("\xE2\x86\x92 " + band.label, {band.rect.x + 6.0f, band.rect.y + 3.0f}, 10.0f,
                          theme.getColor("textPrimary").withAlpha(0.88f));
    }
}

NUIRect UIMixerPanel::getInspectorToggleRect() const
{
    const TrailingLayout trailing = trailingLayout();

    // Collapsed: the whole thin rail is the target. Expanded: a grip strip down
    // the inspector's leading edge.
    const float railW = isInspectorCollapsed() ? INSPECTOR_COLLAPSED_WIDTH : 10.0f;
    return NUIRect{trailing.inspectorRect.x, trailing.inspectorRect.y, railW, trailing.inspectorRect.height};
}

void UIMixerPanel::updateInspectorWidthConstraint()
{
    const float available = getBounds().width;
    if (available <= 0.0f) {
        return; // Pre-layout; leave the constraint alone rather than guessing.
    }

    // The inspector only earns its width if the strips it sits beside are still
    // usable. Below that, collapse is imposed regardless of preference.
    const float needed = MASTER_STRIP_WIDTH + STRIP_SPACING + INSPECTOR_WIDTH + STRIP_SPACING
                       + MIN_STRIPS_BESIDE_INSPECTOR * (stripWidth() + STRIP_SPACING);

    const bool wasForced = m_inspectorCollapse.forcedCollapsed;
    m_inspectorCollapse.setForcedCollapsed(available < needed);

    // The "window too narrow" tooltip is raised and cleared from onMouseEvent,
    // and only when the pointer crosses the rail boundary. Widening the window
    // lifts the constraint without moving the pointer, so a tooltip shown while
    // narrow would keep asserting a reason that no longer holds until the user
    // happened to move off the rail. Clear it as the constraint lifts.
    if (wasForced && !m_inspectorCollapse.forcedCollapsed && m_inspectorToggleHovered) {
        NUIComponent::hideRemoteTooltip(this);
    }
}

void UIMixerPanel::setInspectorExpandedPreference(bool expanded)
{
    if (m_inspectorCollapse.expandedPreference == expanded) {
        return;
    }
    m_inspectorCollapse.expandedPreference = expanded;
    if (onInspectorPreferenceChanged) {
        onInspectorPreferenceChanged(expanded);
    }
    layoutMeters();
    repaint();
}

void UIMixerPanel::toggleInspectorCollapsed()
{
    const bool before = m_inspectorCollapse.expandedPreference;
    m_inspectorCollapse.onRailClicked();
    if (m_inspectorCollapse.expandedPreference != before) {
        if (onInspectorPreferenceChanged) {
            onInspectorPreferenceChanged(m_inspectorCollapse.expandedPreference);
        }
        layoutMeters();
    }
    repaint();
}

void UIMixerPanel::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutMeters();
}

void UIMixerPanel::onUpdate(double deltaTime)
{
    ensureDropTargetRegistration();

    const float maxScroll = getChannelMaxScroll();
    m_targetScrollX = safeClampMixerScroll(m_targetScrollX, maxScroll);

    const float delta = m_targetScrollX - m_scrollX;
    if (std::abs(delta) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);
        m_scrollX += delta * ease;
        m_scrollX = safeClampMixerScroll(m_scrollX, maxScroll);
        layoutMeters();
    } else if (std::abs(delta) > 0.0f) {
        m_scrollX = m_targetScrollX;
        layoutMeters();
    }

    // A route or colour change reorders the groups; lay out again only when it did.
    if (recomputeGrouping()) {
        layoutMeters();
    }
    updateNavigator();

    if (m_viewModel && m_trackManager) {
        auto snapshots = m_trackManager->getMeterSnapshots();
        if (snapshots) {
            m_viewModel->updateMeters(*snapshots, deltaTime);
        }
        m_viewModel->updateInputDiagnostics(*m_trackManager, deltaTime);
    }

    // Update children
    updateChildren(deltaTime);
}

void UIMixerPanel::ensureDropTargetRegistration() {
    auto& dnd = NUIDragDropManager::getInstance();

    // One-time registration (shared_from_this is unavailable in the constructor).
    if (!m_dropTargetRegistered) {
        try {
            if (auto sharedThis = std::dynamic_pointer_cast<IDropTarget>(shared_from_this())) {
                dnd.registerDropTarget(sharedThis);
                m_dropTargetRegistered = true;
            }
        } catch (const std::bad_weak_ptr&) {
            // Not yet owned by a shared_ptr; retry next frame.
            return;
        }
    }

    // The drop manager scans targets in reverse registration order and the
    // mixer overlays the timeline, so re-register once per drag to move this
    // panel to the back of the list — it must be checked before TrackManagerUI
    // or plugin drops fall through to the lane geometry behind it (#395).
    // onUpdate only runs while the mixer is shown; when hidden, the manager's
    // visibility eligibility check skips this target entirely.
    if (dnd.isDragging()) {
        if (!m_dropOrderClaimedForDrag) {
            try {
                if (auto sharedThis = std::dynamic_pointer_cast<IDropTarget>(shared_from_this())) {
                    dnd.unregisterDropTarget(this);
                    dnd.registerDropTarget(sharedThis);
                    m_dropOrderClaimedForDrag = true;
                }
            } catch (const std::bad_weak_ptr&) {}
        }
    } else {
        m_dropOrderClaimedForDrag = false;
        m_dropHoverChannelId = -1; // clear highlight if a drag ended without onDragLeave
    }
}

void UIMixerPanel::renderSeparators(NUIRenderer& renderer)
{
    auto bounds = getBounds();
    const NUIRect minimapRect = getMinimapRect();
    float y1 = minimapRect.bottom() + MINIMAP_GAP;
    float y2 = bounds.y + bounds.height;

    const TrailingLayout trailing = trailingLayout();
    const float inspectorX = trailing.inspectorRect.x;
    const NUIColor boundaryColor = m_separatorColor.withAlpha(0.62f);

    // Draw separator before inspector
    if (m_inspector && m_inspector->isVisible()) {
        renderer.drawLine({inspectorX - STRIP_SPACING, y1}, {inspectorX - STRIP_SPACING, y2}, 1.0f, boundaryColor);
    }

    // Draw separator before master strip
    if (m_masterStrip && m_masterStrip->isVisible()) {
        const float masterX = trailing.masterRect.x;
        renderer.drawLine({masterX - STRIP_SPACING, y1}, {masterX - STRIP_SPACING, y2}, 1.0f, boundaryColor);
    }
}

void UIMixerPanel::onRender(NUIRenderer& renderer)
{
    AESTRA_ZONE("Mixer_Render");
    auto bounds = getBounds();

    // Background
    renderer.fillRect(bounds, m_backgroundColor);

    if (m_toolbar) m_toolbar->onRender(renderer);

    if (m_navigator && m_navigator->isVisible()) m_navigator->onRender(renderer);

    // Separators
    renderSeparators(renderer);

    // Render channel strips with a clip so they never draw into the inspector/master area.
    const TrailingLayout trailing = trailingLayout();
    const float channelW = std::max(0.0f, (trailing.inspectorRect.x - STRIP_SPACING) - (bounds.x + SCALE_W));
    const NUIRect minimapRect = getMinimapRect();
    const float channelY = minimapRect.bottom() + MINIMAP_GAP;
    const NUIRect channelClip(bounds.x + SCALE_W, channelY, channelW, std::max(0.0f, bounds.bottom() - channelY));

    // The scale lives left of the clipped strip area, so it is drawn before the clip is set.
    renderDbScale(renderer);

    bool clipEnabled = false;
    if (!channelClip.isEmpty()) {
        renderer.setClipRect(channelClip);
        clipEnabled = true;
    }

    renderGroupBands(renderer);
    for (const auto& strip : m_strips) {
        if (strip && strip->isVisible()) {
            strip->onRender(renderer);
        }
    }

    // Drop-target highlight for channel strips (#395) — inside the clip so it
    // never bleeds into the inspector/master area.
    if (m_dropHoverChannelId >= 0) {
        const NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
        for (const auto& strip : m_strips) {
            if (strip && strip->isVisible() && static_cast<int64_t>(strip->getChannelId()) == m_dropHoverChannelId) {
                renderer.strokeRoundedRect(strip->getBounds(), 6.0f, 2.0f, accent);
                break;
            }
        }
    }

    if (clipEnabled) {
        renderer.clearClipRect();
    }

    if (m_inspector && m_inspector->isVisible()) {
        m_inspector->onRender(renderer);
    }

    // Inspector collapse rail. Collapsed it is the only thing left of the
    // inspector; expanded it is a slim grip on the panel's leading edge.
    {
        auto& theme = NUIThemeManager::getInstance();
        const NUIRect rail = getInspectorToggleRect();
        const bool collapsed = isInspectorCollapsed();
        const bool forced = isInspectorForcedCollapsed();

        // A width-imposed collapse reads as unavailable rather than chosen: the
        // rail still accepts the click (it records intent) but must not look
        // like it will open the panel right now, or it feels broken.
        const NUIColor railColor = m_inspectorToggleHovered
            ? theme.getColor("accentPrimary").withAlpha(forced ? 0.18f : 0.34f)
            : theme.getColor("surfaceTertiary").withAlpha(collapsed ? 0.72f : 0.34f);
        renderer.fillRoundedRect(rail, 3.0f, railColor);

        // Chevron points the way the panel will move.
        float glyphAlpha = m_inspectorToggleHovered ? 0.95f : 0.6f;
        if (forced) {
            glyphAlpha *= 0.45f;
        }
        const NUIColor glyph = theme.getColor("textSecondary").withAlpha(glyphAlpha);
        const float cx = rail.x + rail.width * 0.5f;
        const float cy = rail.y + rail.height * 0.5f;
        const float dir = collapsed ? -1.0f : 1.0f;
        for (int i = 0; i < 5; ++i) {
            const float dy = static_cast<float>(i) - 2.0f;
            const float dx = dir * (2.0f - std::abs(dy));
            renderer.fillRect(NUIRect{cx + dx - 0.5f, cy + dy * 2.0f, 1.5f, 2.0f}, glyph);
        }
    }

    // Master strip renders on top / outside the clip.
    if (m_masterStrip && m_masterStrip->isVisible()) {
        m_masterStrip->onRender(renderer);
    }

    // Drop-target highlight for the master strip (rendered outside the clip).
    if (m_dropHoverChannelId >= 0 && m_masterStrip && m_masterStrip->isVisible() &&
        static_cast<int64_t>(m_masterStrip->getChannelId()) == m_dropHoverChannelId) {
        const NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
        renderer.strokeRoundedRect(m_masterStrip->getBounds(), 6.0f, 2.0f, accent);
    }

    // Plugin dropdown renders as a floating overlay on top of everything.
    if (m_pluginDropdown && m_pluginDropdown->isVisible()) {
        m_pluginDropdown->onRender(renderer);
    }
}

bool UIMixerPanel::onMouseEvent(const NUIMouseEvent& event)
{
    const NUIRect minimapRect = getMinimapRect();
    const float maxScroll = getChannelMaxScroll();

    // A bracket drag keeps the pointer even when it strays off the navigator.
    if (m_navigator && m_navigator->isDragging()) return m_navigator->onMouseEvent(event);

    // One dismissal path for inline fader entry. A press on another strip is
    // never routed to the editing fader, so without this the editor stays open.
    if (event.pressed) {
        for (const auto& strip : m_strips) {
            if (strip) strip->dismissFaderEdit(event.position);
        }
        if (m_masterStrip) m_masterStrip->dismissFaderEdit(event.position);
    }

    // Inspector collapse rail claims the pointer before anything underneath it.
    {
        const NUIRect rail = getInspectorToggleRect();
        const bool overRail = rail.contains(event.position);
        if (m_inspectorToggleHovered != overRail) {
            m_inspectorToggleHovered = overRail;
            if (overRail && isInspectorForcedCollapsed()) {
                // Say why it will not open, so a recorded-but-not-applied click
                // reads as "waiting for room" instead of "ignored".
                NUIComponent::showRemoteTooltip(
                    "Inspector hidden - window too narrow",
                    NUIPoint{rail.x + rail.width + 8.0f, rail.y + rail.height * 0.5f},
                    this);
            } else if (!overRail) {
                NUIComponent::hideRemoteTooltip(this);
            }
            repaint();
        }
        if (overRail && event.pressed && event.button == NUIMouseButton::Left) {
            toggleInspectorCollapsed();
            return true;
        }
    }

    if (event.wheelDelta != 0.0f) {
        auto bounds = getBounds();
        // These three already matched getChannelViewportWidth()/
        // getChannelContentWidth()/getChannelMaxScroll() exactly — calling
        // them directly instead of re-deriving the same values by hand.
        const float visibleW = getChannelViewportWidth();
        const float maxScroll = getChannelMaxScroll();
        const float channelY = minimapRect.bottom() + MINIMAP_GAP;

        const NUIRect channelClip(bounds.x + SCALE_W, channelY, visibleW, std::max(0.0f, bounds.bottom() - channelY));
        if (maxScroll > 0.0f && channelClip.contains(event.position)) {
            constexpr float SCROLL_PX = 60.0f;
            m_targetScrollX = std::clamp(m_targetScrollX - static_cast<float>(event.wheelDelta) * SCROLL_PX, 0.0f, maxScroll);
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}

bool UIMixerPanel::navigatorShown() const
{
    // Derived from the layout constants rather than trailingLayout(), which itself needs the navigator's height.
    const auto bounds = getBounds();
    const float viewport = bounds.width - MASTER_STRIP_WIDTH - inspectorWidth() - STRIP_SPACING * 2.0f - SCALE_W;
    return getChannelContentWidth() > viewport + 0.5f;
}

float UIMixerPanel::navigatorBlock() const
{
    return navigatorShown() ? UIMixerNavigator::heightFor(m_grouping.grouped()) + MINIMAP_GAP : 0.0f;
}

NUIRect UIMixerPanel::getMinimapRect() const
{
    // The navigator's rect (zero height when channels fit): kept under its old name for the callers below.
    const auto bounds = getBounds();
    const float width = std::max(0.0f, bounds.width - MASTER_STRIP_WIDTH - inspectorWidth() - STRIP_SPACING * 2.0f - SCALE_W);
    const float h = navigatorShown() ? UIMixerNavigator::heightFor(m_grouping.grouped()) : 0.0f;
    return NUIRect(bounds.x + SCALE_W, bounds.y + 2.0f + TOOLBAR_H, width, h);
}

float UIMixerPanel::getChannelViewportWidth() const
{
    const auto bounds = getBounds();
    const TrailingLayout trailing = trailingLayout();
    return std::max(0.0f, (trailing.inspectorRect.x - STRIP_SPACING) - (bounds.x + SCALE_W));
}

float UIMixerPanel::getChannelContentWidth() const
{
    return m_strips.empty() ? 0.0f : (m_strips.size() * (stripWidth() + STRIP_SPACING) - STRIP_SPACING) + groupGapsWidth();
}

float UIMixerPanel::getChannelMaxScroll() const
{
    return std::max(0.0f, getChannelContentWidth() - getChannelViewportWidth());
}


void UIMixerPanel::showPluginDropdown(uint32_t channelId)
{
    if (!m_pluginDropdown || !m_viewModel) return;

    // Select the channel first
    m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));

    // Find the strip that triggered this so we can anchor to its FX summary button
    UIMixerStrip* triggerStrip = nullptr;
    for (const auto& strip : m_strips) {
        if (strip && strip->getChannelId() == channelId) {
            triggerStrip = strip.get();
            break;
        }
    }
    if (!triggerStrip) {
        triggerStrip = m_masterStrip.get();
    }
    if (!triggerStrip) return;

    // Anchor dropdown to the left edge of the strip, below the Add Insert button.
    auto triggerBounds = triggerStrip->getBounds();
    auto fxBounds     = triggerStrip->getFXSummaryBounds();

    auto panelBounds = getBounds();
    const TrailingLayout trailing = trailingLayout();
    const float viewportRight = trailing.inspectorRect.x - STRIP_SPACING;

    constexpr float DROP_W = 240.0f;
    float dropX = triggerBounds.x; // left edge of strip, per design spec
    if (dropX + DROP_W > viewportRight) {
        // Strip is too far right – park dropdown on the left side of the viewport
        // so it is unmistakably clear of the inspector.
        dropX = panelBounds.x + 40.0f;
    }
    dropX = std::max(dropX, panelBounds.x);

    // The mixer renders in absolute window coordinates end to end (strips and
    // their children are laid out with the parent origin included), so the
    // trigger rect needs no local→global conversion here.
    NUIRect anchor{dropX, fxBounds.y, fxBounds.width, fxBounds.height};

    m_pluginDropdown->bringToFront();
    m_pluginDropdown->showAt(anchor, panelBounds.bottom(), panelBounds.y);
}

void UIMixerPanel::loadPluginToSelectedChannel(const std::string& pluginId)
{
    if (!m_viewModel)
        return;
    loadPluginToChannel(m_viewModel->getSelectedChannel(), pluginId);
}

bool UIMixerPanel::loadPluginToChannel(Aestra::ChannelViewModel* vmChannel, const std::string& pluginId) {
    if (!m_trackManager || !vmChannel || !vmChannel->channel)
        return false;

    auto& pm = Aestra::Audio::PluginManager::getInstance();
    auto instance = pm.createInstanceById(pluginId);
    if (!instance) {
        AESTRA_LOG_ERROR("Failed to create plugin instance for: " + pluginId);
        return false;
    }

    if (!instance->initialize(pm.getDefaultSampleRate(), pm.getDefaultBlockSize())) {
        AESTRA_LOG_ERROR("Failed to initialize plugin instance for: " + pluginId);
        return false;
    }
    if (auto delay = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraDelay>(instance)) {
        delay->setBPM(static_cast<float>(m_trackManager->getPlaylistModel().getBPM()));
    }
    instance->activate();

    auto& chain = vmChannel->channel->getEffectChain();
    size_t slot = chain.getFirstEmptySlot();
    if (slot < Aestra::Audio::EffectChain::kMaxSlots) { // kNoSlot (chain full) is not < kMaxSlots
        m_trackManager->getCommandHistory().pushAndExecute(
            std::make_shared<Aestra::Audio::AddPluginCommand>(*vmChannel->channel, slot, std::move(instance)));
        // The playback graph only picks up chain changes on rebuild (which also
        // re-prepares chains with the live sample rate/block size) — without
        // this, the plugin sits in the chain but never processes already-playing
        // tracks until something else dirties the graph.
        m_trackManager->requestAudioGraphRebuild(Aestra::Audio::GraphDirtyReason::EffectChainChanged);
        // Ensure inspector reflects the new insert
        if (m_inspector) {
            m_inspector->setActiveTab(UIMixerInspector::Tab::Inserts);
        }
        return true;
    }

    AESTRA_LOG_WARNING("No empty effect slots on channel " + std::to_string(vmChannel->id));
    return false;
}

// ============================================================================
// IDropTarget (#395) — plugin drops target the strip under the cursor
// ============================================================================

UIMixerStrip* UIMixerPanel::stripAt(const NUIPoint& position) const {
    // The inspector and master strip are pinned on the right and render above
    // scrolled channel strips — resolve them first so occluded strips can't win.
    if (m_inspector && m_inspector->isVisible() && m_inspector->getBounds().contains(position)) {
        return nullptr; // inspector is not a drop surface
    }
    if (m_masterStrip && m_masterStrip->isVisible() && m_masterStrip->getBounds().contains(position)) {
        return m_masterStrip.get();
    }

    // Channel strips are clipped to the viewport left of the inspector.
    const TrailingLayout trailing = trailingLayout();
    const float viewportRight = trailing.inspectorRect.x - STRIP_SPACING;
    if (position.x > viewportRight) {
        return nullptr;
    }

    for (const auto& strip : m_strips) {
        if (strip && strip->isVisible() && strip->getBounds().contains(position)) {
            return strip.get();
        }
    }
    return nullptr;
}

Aestra::ChannelViewModel* UIMixerPanel::channelForStrip(const UIMixerStrip* strip) const {
    if (!strip || !m_viewModel)
        return nullptr;
    if (m_masterStrip && strip == m_masterStrip.get()) {
        return m_viewModel->getMaster();
    }
    return m_viewModel->getChannelById(strip->getChannelId());
}

DropFeedback UIMixerPanel::onDragEnter(const DragData& data, const NUIPoint& position) {
    return onDragOver(data, position);
}

DropFeedback UIMixerPanel::onDragOver(const DragData& data, const NUIPoint& position) {
    m_dropHoverChannelId = -1;
    if (data.type != DragDataType::Plugin && data.type != DragDataType::AudioSourceRoute) {
        return DropFeedback::Invalid;
    }
    auto* strip = stripAt(position);
    if (!strip || !channelForStrip(strip)) {
        return DropFeedback::Invalid;
    }
    m_dropHoverChannelId = static_cast<int64_t>(strip->getChannelId());
    return data.type == DragDataType::AudioSourceRoute ? DropFeedback::Move : DropFeedback::Copy;
}

void UIMixerPanel::onDragLeave() {
    m_dropHoverChannelId = -1;
}

DropResult UIMixerPanel::onDrop(const DragData& data, const NUIPoint& position) {
    DropResult result;
    m_dropHoverChannelId = -1;

    // The mixer claims every drop over its bounds, including ones it rejects:
    // letting a rejected drop fall through to the timeline behind the mixer is
    // exactly the misrouting this target exists to prevent (#395).
    if (data.type != DragDataType::Plugin && data.type != DragDataType::AudioSourceRoute) {
        result.accepted = false;
        result.message = "Only plugins and audio-source routes can be dropped on the mixer";
        return result;
    }

    auto* strip = stripAt(position);
    auto* vmChannel = channelForStrip(strip);
    if (!strip || !vmChannel) {
        result.accepted = false;
        result.message = "No mixer strip under drop";
        return result;
    }

    if (data.type == DragDataType::AudioSourceRoute) {
        const auto* patternValue = std::any_cast<uint64_t>(&data.customData);
        if (!patternValue || *patternValue == 0 || !m_trackManager) {
            result.accepted = false;
            result.message = "Drag data missing audio source";
            return result;
        }

        const Aestra::Audio::PatternID patternId(*patternValue);
        const auto* pattern = m_trackManager->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isAudio()) {
            result.accepted = false;
            result.message = "Audio source is no longer available";
            return result;
        }

        const uint32_t destinationId =
            vmChannel->id == 0 ? Aestra::Audio::MASTER_MIXER_CHANNEL_ID : vmChannel->id;
        if (pattern->getMixerChannelId() != destinationId) {
            auto command = std::make_shared<Aestra::Audio::SetAudioPatternMixerChannelCommand>(
                *m_trackManager, patternId, destinationId);
            m_trackManager->getCommandHistory().pushAndExecute(command);
        }
        if (m_viewModel) {
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(vmChannel->id));
        }

        result.accepted = true;
        result.targetTrackIndex = static_cast<int>(vmChannel->id);
        result.message = "Routed " + (data.displayName.empty() ? pattern->name : data.displayName) + " to " +
                         (vmChannel->id == 0 ? "Master" : vmChannel->name);
        Aestra::Log::info("[UIMixerPanel] Audio source drop: " + result.message);
        return result;
    }

    const std::string& pluginId = data.sourceClipIdString;
    if (pluginId.empty()) {
        result.accepted = false;
        result.message = "Drag data missing plugin id";
        return result;
    }

    // Select the drop target so the inspector's Inserts tab shows the channel
    // the plugin actually landed on — same call a strip click makes.
    if (m_viewModel) {
        m_viewModel->setSelectedChannelId(static_cast<int32_t>(strip->getChannelId()));
    }

    if (!loadPluginToChannel(vmChannel, pluginId)) {
        result.accepted = false;
        result.message = "Failed to load plugin (see log)";
        return result;
    }

    result.accepted = true;
    result.targetTrackIndex = static_cast<int>(vmChannel->id);
    result.message = "Loaded " + (data.displayName.empty() ? pluginId : data.displayName) + " on '" +
                     vmChannel->channel->getName() + "'";
    Aestra::Log::info("[UIMixerPanel] Plugin drop: " + result.message);
    return result;
}

NUIRect UIMixerPanel::getDropBounds() const {
    return getBounds();
}

} // namespace AestraUI
