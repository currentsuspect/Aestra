// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"

#include "NUIThemeSystem.h"
#include "../../AestraCore/include/AestraLog.h"
#include "NUIRenderer.h"
#include "NUIMixerWidgets.h"
#include "PluginBrowserPanel.h"
#include "MixerViewModel.h"
#include "TrackColorPalette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace AestraUI {

namespace {
    constexpr float PAD = 10.0f;
    constexpr float TAB_H = 28.0f;
    constexpr float TAB_RADIUS = 12.0f;
    constexpr float SECTION_GAP = 10.0f;
    constexpr float HEADER_H = 80.0f;
    constexpr float INSERT_SUMMARY_H = 28.0f;
    constexpr float ROW_H = 28.0f;
    constexpr float ROW_RADIUS = 12.0f;
    constexpr float INPUT_METER_H = 12.0f;
    constexpr float IO_CARD_RADIUS = 14.0f;
    constexpr float HEADER_RADIUS = 12.0f;
    constexpr float IO_DROPDOWN_H = 22.0f;
    constexpr float SEND_STATUS_CARD_H = 50.0f;
    constexpr float SEND_OUTPUT_CARD_H = 70.0f;
    constexpr float SEND_ROUTE_MAP_H = 116.0f;

    std::string fitText(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth)
    {
        if (text.empty() || renderer.measureText(text, fontSize).width <= maxWidth) {
            return text;
        }

        constexpr const char* ellipsis = "...";
        const float ellipsisW = renderer.measureText(ellipsis, fontSize).width;
        if (ellipsisW >= maxWidth) {
            return ellipsis;
        }

        std::string out = text;
        while (!out.empty() && renderer.measureText(out, fontSize).width + ellipsisW > maxWidth) {
            out.pop_back();
        }
        return out + ellipsis;
    }

}

void UIMixerInspector::clampScrollOffsets()
{
    m_maxScrollOffset = std::max(0.0f, m_maxScrollOffset);
    m_targetScrollOffset = std::clamp(m_targetScrollOffset, 0.0f, m_maxScrollOffset);
    m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, m_maxScrollOffset);
}

UIMixerInspector::UIMixerInspector(Aestra::MixerViewModel* viewModel)
    : m_viewModel(viewModel)
{
    cacheThemeColors();

    m_effectRack = std::make_shared<EffectChainRack>();
    m_tabControl = std::make_shared<NUISegmentedControl>(std::vector<std::string>{"Inserts", "Sends", "I/O"});
    m_tabControl->setCornerRadius(TAB_RADIUS);
    m_tabControl->setOnSelectionChanged([this](size_t index) {
        setActiveTab(static_cast<Tab>(index));
    });
    addChild(m_tabControl);
    m_tabControl->setSelectedIndex(static_cast<size_t>(m_activeTab), false);
    m_tabControl->setAccentColor(NUIThemeManager::getInstance().getColor("primary"));
    
    // Bind Callbacks
    m_effectRack->setOnSlotBypassToggled([this](int slot, bool bypassed) {
        if (m_viewModel) {
            auto* ch = m_viewModel->getSelectedChannel();
            if (ch) m_viewModel->setInsertBypass(ch->id, slot, bypassed);
        }
    });

    m_effectRack->setOnSlotMixChanged([this](int slot, float mix) {
        if (m_viewModel) {
             auto* ch = m_viewModel->getSelectedChannel();
             if (ch) m_viewModel->setInsertMix(ch->id, slot, mix);
        }
    });

    m_effectRack->setOnSlotMoveRequested([this](int from, int to) {
        if (m_viewModel) {
             auto* ch = m_viewModel->getSelectedChannel();
             if (ch) {
                 m_viewModel->moveInsert(ch->id, from, to);
                 rebuildInsertRack(ch);
             }
        }
    });

    m_effectRack->setOnSlotRemoveRequested([this](int slot) {
        Aestra::Log::info("[Inspector] Slot Remove callback triggered.");
        if (m_viewModel) {
             Aestra::Log::info("[Inspector] m_viewModel is valid.");
             auto* ch = m_viewModel->getSelectedChannel();
             if (ch) {
                 char logBuf[128];
                 std::snprintf(logBuf, sizeof(logBuf), "[Inspector] Calling removeInsert(ch=%u, slot=%d)", ch->id, slot);
                 Aestra::Log::info(logBuf);
                 m_viewModel->removeInsert(ch->id, slot);
             } else {
                 Aestra::Log::warning("[Inspector] getSelectedChannel() returned NULL!");
             }
        } else {
            Aestra::Log::warning("[Inspector] m_viewModel is NULL!");
        }
    });

    m_effectRack->setVisible(true);
    addChild(m_effectRack);

    // I/O Input Selector
    m_ioInputDropdown = std::make_shared<NUIDropdown>();
    m_ioInputDropdown->setVisible(false);
    m_ioInputDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (!m_viewModel) return;
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch) {
            // Update engine
            if (auto* mc = ch->channel) {
                mc->setInputChannelIndex(value); 
            }
            // Update ViewModel for UI sync
            ch->inputChannelIndex = value;
        }
    });
    addChild(m_ioInputDropdown);

    m_mainOutputDropdown = std::make_shared<NUIDropdown>();
    m_mainOutputDropdown->setVisible(false);
    m_mainOutputDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        (void)index;
        (void)text;
        if (!m_viewModel) return;
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) {
            m_viewModel->setMainOutputDestination(ch->id, static_cast<uint32_t>(std::max(0, value)));
        }
    });
    addChild(m_mainOutputDropdown);

    // Routing map minimap
    m_routingMap = std::make_shared<UIRoutingMap>(UIRoutingMap::Mode::Minimap);
    m_routingMap->setVisible(false);
    m_routingMap->setViewModel(m_viewModel);
    m_routingMap->setOnNodeSelected([this](uint32_t channelId) {
        if (m_viewModel) {
            m_viewModel->setSelectedChannelId(static_cast<int32_t>(channelId));
            m_viewModel->graphDirty.emit();
        }
    });
    addChild(m_routingMap);
}

void UIMixerInspector::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("backgroundPrimary");
    m_border = theme.getColor("borderSubtle").withAlpha(0.65f);
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_tabBg = theme.getColor("buttonBgDefault").withAlpha(0.98f);
    m_tabActive = theme.getColor("buttonBgActive").withAlpha(0.99f);
    m_tabHover = theme.getColor("buttonBgHover").withAlpha(0.99f);
    m_addBg = theme.getColor("surfaceTertiary");
    m_addHover = theme.getColor("controlHover");
    m_addText = theme.getColor("textPrimary");
}

void UIMixerInspector::setActiveTab(Tab tab)
{
    if (m_activeTab == tab) return;
    m_activeTab = tab;

    // Active-tab accent is the app purple across all tabs — consistent with the
    // transport/top-nav active state (was per-tab purple/cyan/amber).
    const NUIColor accent = NUIThemeManager::getInstance().getColor("primary");
    if (m_tabControl) {
        m_tabControl->setAccentColor(accent);
        m_tabControl->setSelectedIndex(static_cast<size_t>(m_activeTab), false);
    }

    // Update child visibility
    if (m_effectRack) {
        m_effectRack->setVisible(m_activeTab == Tab::Inserts);
    }
    for (auto& w : m_sendWidgets) {
        w->setVisible(m_activeTab == Tab::Sends);
    }
    if (m_ioInputDropdown) {
        m_ioInputDropdown->setVisible(m_activeTab == Tab::IO);
    }
    if (m_routingMap) {
        m_routingMap->setVisible(m_activeTab == Tab::Sends);
    }

    // Tab-dependent layout (rack top pad, dropdowns, add-fx row) is computed in
    // onResize, but a tab switch doesn't fire a resize. Re-run it for the new tab
    // so the effect rack doesn't keep the previous tab's Y and overlap the
    // Inserts section header.
    const auto b = getBounds();
    onResize(static_cast<int>(b.width), static_cast<int>(b.height));

    repaint();
}

void UIMixerInspector::layoutHitRects()
{
    const auto b = getBounds();
    const float w = std::max(1.0f, b.width - PAD * 2.0f);
    const float x = b.x + PAD;
    const float y = b.y + PAD;

    const float tabW = std::floor((w - 6.0f) / 3.0f);
    const float gap = 3.0f;
    for (int i = 0; i < 3; ++i) {
        m_tabRects[i] = NUIRect{x + i * (tabW + gap), y, tabW, TAB_H};
    }
    if (m_tabControl) {
        m_tabControl->setBounds(x, y, w, TAB_H);
    }

    const float contentY = y + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    if (m_activeTab == Tab::Sends) {
        m_addFxRect = NUIRect{x, contentY + 18.0f, w, ROW_H};
    } else {
        m_addFxRect = NUIRect{0,0,0,0};
    }

    if (m_ioInputDropdown) {
        // Sits between the "Verify level…" caption (ends ~+53) and the
        // Source/Mode meta row (render pass places it at +88). The old +68 sat
        // on top of the meta row; +52 clipped the caption above — +58 clears both.
        const float dropdownY = contentY + 58.0f;
        m_ioInputDropdown->setBounds(x + 12.0f, dropdownY, w - 24.0f, IO_DROPDOWN_H);
    }
    if (m_mainOutputDropdown) {
        const float dropdownW = w - 20.0f;
        // Position inside the outputHeader card, below the MAIN PATH label
        // Layout: status card (SEND_STATUS_CARD_H) + 8 gap + outputHeader top (10 title) + 22 (label space)
        const float dropdownY = contentY + SEND_STATUS_CARD_H + 8.0f + 38.0f;
        m_mainOutputDropdown->setBounds(x + 10.0f, dropdownY, dropdownW, IO_DROPDOWN_H);
    }
}

int UIMixerInspector::hitTestTab(const NUIPoint& p) const
{
    for (int i = 0; i < 3; ++i) {
        if (m_tabRects[i].contains(p)) return i;
    }
    return -1;
}

void UIMixerInspector::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutHitRects();

    const auto b = getBounds();
    const float contentTop = PAD + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    if (m_effectRack) {
        // Inserts tab: tuck the rack directly under the "INSERTS" section header
        // + divider (headerBaseY+16 line, ~+20 from contentRect top) so the
        // header reads as the list's title rather than a floating card.
        const float topPad = (m_activeTab == Tab::Inserts) ? 28.0f : 10.0f;
        float rackH = std::max(0.0f, b.height - contentTop - topPad - PAD);
        m_effectRack->setBounds(b.x + PAD, b.y + contentTop + topPad, b.width - PAD * 2.0f, rackH);
    }
}

int UIMixerInspector::findTrackNumber(uint32_t channelId) const
{
    if (!m_viewModel || channelId == 0) return 0;
    const size_t count = m_viewModel->getChannelCount();
    for (size_t i = 0; i < count; ++i) {
        const auto* ch = m_viewModel->getChannelByIndex(i);
        if (ch && ch->id == channelId) {
            return static_cast<int>(i + 1);
        }
    }
    return 0;
}

void UIMixerInspector::updateHeaderCache(const Aestra::ChannelViewModel* channel)
{
    const uint32_t selectedId = channel ? channel->id : 0xFFFFFFFFu;
    const bool identityUnchanged =
        (m_cachedSelectedId == selectedId) &&
        (channel ? (m_cachedName == channel->name &&
                    m_cachedRoute == channel->routeName &&
                    m_cachedMainOutputId == channel->mainOutputId &&
                    m_cachedMasterSendEnabled == channel->masterSendEnabled &&
                    m_cachedSendsCount == channel->sends.size() &&
                    m_cachedInsertsCount == channel->inserts.size() &&
                    m_cachedFxCount == channel->fxCount)
                 : (m_cachedName.empty() && m_cachedRoute.empty()));
    if (identityUnchanged) return;

    m_cachedSelectedId = selectedId;
    m_cachedHeaderTitle.clear();
    m_cachedHeaderSubtitle.clear();
    m_cachedTrackNumber = 0;
    m_cachedName = channel ? channel->name : std::string();
    m_cachedRoute = channel ? channel->routeName : std::string();
    m_cachedMainOutputId = channel ? channel->mainOutputId : 0xFFFFFFFFu;
    m_cachedMasterSendEnabled = channel ? channel->masterSendEnabled : true;
    m_cachedSendsCount = channel ? channel->sends.size() : 0;
    m_cachedInsertsCount = channel ? channel->inserts.size() : 0;
    m_cachedFxCount = channel ? channel->fxCount : 0;

    if (!channel) {
        m_cachedHeaderTitle = "Inspector";
        return;
    }

    if (channel->id == 0) {
        m_cachedHeaderTitle = "MASTER";
        m_cachedHeaderSubtitle = "Output";
        return;
    }

    m_cachedTrackNumber = findTrackNumber(channel->id);
    const std::string trackLabel = (m_cachedTrackNumber > 0)
        ? ("Track " + std::to_string(m_cachedTrackNumber))
        : "Channel";

    m_cachedHeaderTitle = channel->name.empty()
        ? trackLabel
        : channel->name;

    // Subtitle = track label only when title is a custom name (not the track label itself)
    const bool titleIsTrackLabel = (m_cachedHeaderTitle == trackLabel);
    const bool hasSends = !channel->sends.empty();
    m_cachedHeaderSubtitle.clear();
    if (!titleIsTrackLabel) {
        m_cachedHeaderSubtitle = trackLabel;
    }
    if (!channel->masterSendEnabled && channel->mainOutputId != 0) {
        if (!m_cachedHeaderSubtitle.empty()) m_cachedHeaderSubtitle += "  •  ";
        m_cachedHeaderSubtitle += "Bus only";
    }
    if (hasSends) {
        if (!m_cachedHeaderSubtitle.empty()) m_cachedHeaderSubtitle += "  •  ";
        m_cachedHeaderSubtitle += std::to_string(channel->sends.size()) + " send";
        if (channel->sends.size() != 1) {
            m_cachedHeaderSubtitle += "s";
        }
    }

    rebuildSendWidgets(channel);
    rebuildInsertRack(channel); // Sync inserts
}

void UIMixerInspector::rebuildInsertRack(const Aestra::ChannelViewModel* channel)
{
    if (!m_effectRack) return;

    if (!channel) {
        // Clear rack
        for (int i = 0; i < EffectChainRack::MAX_SLOTS; ++i) {
             EffectChainRack::EffectSlotInfo info;
             info.isEmpty = true;
             info.name = "Empty";
             m_effectRack->setSlot(i, info);
        }
        return;
    }

    // Sync from ViewModel
    for (size_t i = 0; i < EffectChainRack::MAX_SLOTS; ++i) {
        EffectChainRack::EffectSlotInfo info;
        
        if (i < channel->inserts.size()) {
            const auto& vmInsert = channel->inserts[i];
            
            // Copy pendingRemoval state for visual debugging
            info.pendingRemoval = vmInsert.pendingRemoval;
            
            if (!vmInsert.isEmpty && !vmInsert.pendingRemoval) {
                info.isEmpty = false;
                info.name = vmInsert.name.empty() ? "Plugin" : vmInsert.name;
                info.bypassed = vmInsert.bypassed;
                info.dryWet = vmInsert.mix;
            } else {
                // Mark as empty if actually empty OR if pending removal
                info.isEmpty = true;
                info.name = vmInsert.pendingRemoval ? "Removing..." : "Empty";
            }
        } else {
            info.isEmpty = true;
            info.name = "Empty";
        }
        m_effectRack->setSlot(static_cast<int>(i), info);
    }
    m_effectRack->repaint();
}

void UIMixerInspector::rebuildSendWidgets(const Aestra::ChannelViewModel* channel)
{
    // Remove old widgets
    for (auto& w : m_sendWidgets) {
        removeChild(w);
    }
    m_sendWidgets.clear();

    if (!channel) return;

    // Create new widgets
    for (size_t i = 0; i < channel->sends.size(); ++i) {
        auto& sendData = channel->sends[i];
        auto widget = std::make_shared<UIMixerSend>();
        widget->setAccentColor(NUIThemeManager::getInstance().getColor("primary").withAlpha(0.92f));
        
        widget->setSendIndex(static_cast<int>(i));
        widget->setLevel(sendData.gain);
        widget->setPostFader(sendData.postFader);
        widget->setSidechainOnly(sendData.sidechainOnly);
        widget->setMuted(sendData.muted);

        // Bind Callbacks
        uint32_t cid = channel->id;
        int sIdx = static_cast<int>(i);

        widget->setOnLevelChanged([this, cid, sIdx](float val) {
            if (m_viewModel) m_viewModel->setSendLevel(cid, sIdx, val);
        });

        widget->setOnDestinationChanged([this, cid, sIdx](uint32_t dest) {
            if (m_viewModel) m_viewModel->setSendDestination(cid, sIdx, dest);
        });

        widget->setOnPostFaderChanged([this, cid, sIdx](bool post) {
            if (m_viewModel) m_viewModel->setSendPostFader(cid, sIdx, post);
        });

        widget->setOnSidechainModeChanged([this, cid, sIdx](bool sidechainOnly) {
            if (m_viewModel) m_viewModel->setSendSidechainOnly(cid, sIdx, sidechainOnly);
        });

        // Set available destinations FIRST explicitly
        if (m_viewModel) {
            auto dests = m_viewModel->getAvailableDestinations(channel->id);
            std::vector<std::pair<uint32_t, std::string>> uiDests;
            for (const auto& d : dests) uiDests.push_back({d.id, d.name});
            widget->setAvailableDestinations(uiDests);
        }

        widget->setOnDelete([this, cid, sIdx]() {
            m_deferredActions.push_back([this, cid, sIdx]() {
                if (m_viewModel) {
                    m_viewModel->removeSend(cid, sIdx);
                    // Refresh UI immediately
                    Aestra::ChannelViewModel* ch = m_viewModel->getChannelById(cid);
                    rebuildSendWidgets(ch);
                    repaint();
                }
            });
        });
        
        // NOW set current destination (requires items to be present)
        widget->setDestination(sendData.targetId, sendData.targetName);

        addChild(widget);
        m_sendWidgets.push_back(widget);
    }
}

void UIMixerInspector::onRender(NUIRenderer& renderer)
{
    const auto b = getBounds();
    if (b.isEmpty()) return;

    renderer.fillRect(b, m_bg);

    // Left separator line (container draws outer separators too; keep this subtle).
    renderer.drawLine({b.x, b.y}, {b.x, b.bottom()}, 1.0f, m_border);

    const auto* channel = m_viewModel ? m_viewModel->getSelectedChannel() : nullptr;
    updateHeaderCache(channel);

    const NUIColor accent = NUIThemeManager::getInstance().getColor("primary");

    // Continuous sync for knobs to reflect automation/backend changes
    if (m_activeTab == Tab::Inserts && channel) {
        rebuildInsertRack(channel);
    }

    // Header
    const float headerY = b.y + PAD + TAB_H + SECTION_GAP;
    const NUIRect headerRect{b.x + PAD, headerY, b.width - PAD * 2.0f, HEADER_H};
    const float contentTop = b.y + PAD + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    const NUIRect contentRect{b.x + PAD, contentTop, b.width - PAD * 2.0f, b.height - (contentTop - b.y) - PAD};

    if (!channel) {
        if (m_effectRack) {
            m_effectRack->setVisible(false);
        }
        if (m_ioInputDropdown) {
            m_ioInputDropdown->setVisible(false);
        }
        if (m_mainOutputDropdown) {
            m_mainOutputDropdown->setVisible(false);
        }
        for (auto& widget : m_sendWidgets) {
            widget->setVisible(false);
        }
        const float emptyCardH = 160.0f;
        const float emptyTop = contentRect.y + 8.0f;
        const float emptyBottom = contentRect.bottom() - 54.0f;
        const float availableH = std::max(0.0f, emptyBottom - emptyTop);
        const NUIRect emptyCard{
            contentRect.x,
            std::round(emptyTop + std::max(0.0f, (availableH - emptyCardH) * 0.20f)),
            contentRect.width,
            std::min(emptyCardH, availableH)
        };
        renderer.fillRoundedRect(emptyCard, HEADER_RADIUS, m_tabBg.withAlpha(0.62f));
        renderer.strokeRoundedRect(emptyCard, HEADER_RADIUS, 1.0f, m_border.withAlpha(0.42f));
        renderer.strokeRoundedRect({emptyCard.x + 1.0f, emptyCard.y + 1.0f, emptyCard.width - 2.0f, emptyCard.height - 2.0f},
                                   std::max(0.0f, HEADER_RADIUS - 1.0f),
                                   1.0f,
                                   NUIColor::white().withAlpha(0.022f));

        const NUIRect stateChip{emptyCard.center().x - 34.0f, emptyCard.y + 18.0f, 68.0f, 18.0f};
        renderer.fillRoundedRect(stateChip, 9.0f, m_bg.withAlpha(0.34f));
        renderer.strokeRoundedRect(stateChip, 9.0f, 1.0f, accent.withAlpha(0.20f));
        renderer.drawTextCentered("INSPECTOR", stateChip, 9.0f, m_textSecondary.withAlpha(0.90f));
        renderer.drawTextCentered("Select a Track", {emptyCard.x + 18.0f, stateChip.bottom() + 16.0f, emptyCard.width - 36.0f, 18.0f},
                                  13.0f, m_text.withAlpha(0.95f));
        renderer.drawTextCentered("Choose a mixer channel to inspect",
                                  {emptyCard.x + 24.0f, stateChip.bottom() + 38.0f, emptyCard.width - 48.0f, 14.0f},
                                  10.0f, m_textSecondary.withAlpha(0.86f));
        renderer.drawTextCentered("Inserts, Sends, and I/O.",
                                  {emptyCard.x + 24.0f, stateChip.bottom() + 52.0f, emptyCard.width - 48.0f, 14.0f},
                                  10.0f, m_textSecondary.withAlpha(0.86f));
        return;
    }

    renderer.fillRoundedRect(headerRect, HEADER_RADIUS, m_tabBg.withAlpha(0.70f));
    renderer.strokeRoundedRect(headerRect, HEADER_RADIUS, 1.0f, m_border.withAlpha(0.50f));
    renderer.strokeRoundedRect({headerRect.x + 1.0f, headerRect.y + 1.0f, headerRect.width - 2.0f, headerRect.height - 2.0f},
                               std::max(0.0f, HEADER_RADIUS - 1.0f),
                               1.0f,
                               NUIColor::white().withAlpha(0.022f));

    NUIColor titleAccent = accent;
    if (channel && channel->trackColorIndex >= 0) {
        uint32_t argb = paletteIndexToARGB(channel->trackColorIndex);
        float a = ((argb >> 24) & 0xFF) / 255.0f;
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >> 8) & 0xFF) / 255.0f;
        float b = (argb & 0xFF) / 255.0f;
        titleAccent = NUIColor(r, g, b, a);
    }
    // Channel identity: a colour swatch beside the name. The old TRACK/BUS word
    // pill was redundant with the "Track 1" title right below it — you're already
    // in the mixer inspector, and the master reads as MASTER and is visually
    // distinct. The swatch gives this spot a real job: it ties the inspector to
    // the selected strip's colour.
    const float swatchSize = 12.0f;
    // Vertically centre the swatch on the title's optical middle. drawText places
    // by glyph-top and the atlas renders a touch lower, so the swatch sits ~4px
    // below the title top to line up with the text centre rather than its cap.
    const NUIRect swatch{headerRect.x + 12.0f, headerRect.y + 18.0f, swatchSize, swatchSize};
    renderer.fillRoundedRect(swatch, 4.0f, titleAccent.withAlpha(0.92f));
    renderer.strokeRoundedRect(swatch, 4.0f, 1.0f, NUIColor::white().withAlpha(0.10f));

    renderer.drawText(m_cachedHeaderTitle, {headerRect.x + 30.0f, headerRect.y + 13.0f}, 13.5f, m_text);
    if (!m_cachedHeaderSubtitle.empty()) {
        renderer.drawText(m_cachedHeaderSubtitle, {headerRect.x + 12.0f, headerRect.y + 34.0f}, 11.0f, m_textSecondary.withAlpha(0.95f));
    }
    {
        // Signal-flow breadcrumb: dim inactive steps, accent the active one,
        // separate with chevron glyph. No pill background → reads as info, not tabs.
        const char* flowSteps[4];
        int flowCount = 0;
        int activeIdx = -1;
        switch (m_activeTab) {
            case Tab::Inserts:
                flowSteps[flowCount++] = "INPUT";
                flowSteps[flowCount++] = "TRIM";
                flowSteps[flowCount++] = "INSERTS"; activeIdx = flowCount - 1;
                flowSteps[flowCount++] = "OUTPUT";
                break;
            case Tab::Sends:
                flowSteps[flowCount++] = "INSERTS";
                flowSteps[flowCount++] = "SENDS"; activeIdx = flowCount - 1;
                flowSteps[flowCount++] = "FADER";
                flowSteps[flowCount++] = "OUTPUT";
                break;
            case Tab::IO:
                flowSteps[flowCount++] = "INPUT"; activeIdx = flowCount - 1;
                flowSteps[flowCount++] = "MONITOR";
                flowSteps[flowCount++] = "RECORD";
                break;
        }

        float cursorX = headerRect.x + 10.0f;
        const float baselineY = headerRect.y + 62.0f;
        const float stepFont = 9.0f;

        for (int i = 0; i < flowCount; ++i) {
            const std::string step = flowSteps[i];
            const bool active = (i == activeIdx);
            // Active step follows the channel's own colour and carries a short
            // underline "you are here" marker; inactive steps recede.
            const NUIColor stepColor = active ? titleAccent.withAlpha(0.98f)
                                              : m_textSecondary.withAlpha(0.50f);
            renderer.drawText(step, {cursorX, baselineY}, stepFont, stepColor);
            const float textW = renderer.measureText(step, stepFont).width;
            if (active) {
                renderer.fillRect({cursorX, baselineY + 13.0f, textW, 1.5f},
                                  titleAccent.withAlpha(0.90f));
            }
            cursorX += textW + 7.0f;

            if (i < flowCount - 1) {
                renderer.drawText("›", {cursorX, baselineY}, stepFont,
                                  m_textSecondary.withAlpha(0.35f));
                cursorX += renderer.measureText("›", stepFont).width + 7.0f;
            }
        }
    }

    // Content
    if (m_activeTab == Tab::Inserts) {
        const int fxCount = channel->fxCount;
        char buf[64];
        if (fxCount <= 0) {
            std::snprintf(buf, sizeof(buf), "No inserts loaded");
        } else {
            std::snprintf(buf, sizeof(buf), "%d insert%s active", fxCount, fxCount == 1 ? "" : "s");
        }
        // Section header for the rack below — a plain eyebrow + count with a
        // hairline divider (no boxed card), so it clearly titles the slot list
        // rather than floating as a separate compartment above it.
        const float headerBaseY = contentRect.y + 4.0f;
        renderer.drawText("INSERTS",
                          {contentRect.x + 2.0f, headerBaseY},
                          9.0f, m_textSecondary.withAlpha(0.72f));
        const float countW = renderer.measureText(buf, 9.5f).width;
        renderer.drawText(buf,
                          {contentRect.x + contentRect.width - countW - 2.0f, headerBaseY},
                          9.5f, m_textSecondary.withAlpha(0.85f));
        renderer.fillRect({contentRect.x + 2.0f, headerBaseY + 16.0f, contentRect.width - 4.0f, 1.0f},
                          m_border.withAlpha(0.45f));

        // Rack is rendered by renderChildren() if visible, tucked right under
        // the divider (see onResize topPad for the Inserts tab).
    } else if (m_activeTab == Tab::Sends) {
        const int sendCount = static_cast<int>(m_sendWidgets.size());
        const std::string routingWarning = m_viewModel ? m_viewModel->getRoutingWarning(channel->id) : std::string();
        const bool hasRoutingWarning = !routingWarning.empty();

        // === Status card: cleaner two-line layout (label + count) ===
        const NUIRect summaryCard{contentRect.x, contentRect.y, contentRect.width, SEND_STATUS_CARD_H};
        renderer.fillRoundedRect(summaryCard, 10.0f, m_tabBg.withAlpha(0.42f));
        const NUIColor summaryStroke = hasRoutingWarning
            ? NUIThemeManager::getInstance().getColor("warning").withAlpha(0.45f)
            : accent.withAlpha(0.18f);
        renderer.strokeRoundedRect(summaryCard, 10.0f, 1.0f, summaryStroke);

        // Eyebrow label
        renderer.drawText("SENDS", {summaryCard.x + 12.0f, summaryCard.y + 8.0f}, 9.0f,
                          m_textSecondary.withAlpha(0.72f));

        // Primary value (right side: count, left side: descriptor)
        if (sendCount == 0) {
            renderer.drawText("No sends configured",
                              {summaryCard.x + 12.0f, summaryCard.y + 25.0f},
                              11.5f, m_text.withAlpha(0.94f));
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d send%s active", sendCount, sendCount == 1 ? "" : "s");
            renderer.drawText(buf,
                              {summaryCard.x + 12.0f, summaryCard.y + 25.0f},
                              11.5f, m_text.withAlpha(0.96f));
        }

        // Warning row (only if applicable)
        if (hasRoutingWarning) {
            renderer.drawText(routingWarning,
                              {summaryCard.x + 12.0f, summaryCard.y + 38.0f},
                              10.0f,
                              NUIThemeManager::getInstance().getColor("warning").withAlpha(0.92f));
        }

        // === Main Output card: tighter, no overlapping hint ===
        const NUIRect outputHeader{contentRect.x, summaryCard.bottom() + 8.0f,
                                    contentRect.width, SEND_OUTPUT_CARD_H};
        renderer.fillRoundedRect(outputHeader, 10.0f, m_tabBg.withAlpha(0.36f));
        renderer.strokeRoundedRect(outputHeader, 10.0f, 1.0f, m_border.withAlpha(0.30f));

        // Eyebrow
        renderer.drawText("MAIN PATH",
                          {outputHeader.x + 12.0f, outputHeader.y + 9.0f}, 9.0f,
                          m_textSecondary.withAlpha(0.72f));

        // Title
        renderer.drawText("Main Output",
                          {outputHeader.x + 12.0f, outputHeader.y + 23.0f}, 12.5f,
                          m_text.withAlpha(0.95f));

        // Routing map minimap widget
        const NUIRect routingCard{contentRect.x, outputHeader.bottom() + 10.0f, contentRect.width, SEND_ROUTE_MAP_H};
        if (m_routingMap) {
            m_routingMap->setBounds(routingCard);
            m_routingMap->setViewModel(m_viewModel);
        }

        float currentY = routingCard.bottom() + 12.0f - m_scrollOffset;
        const float sendH = 124.0f;
        const float gap = 10.0f;

        for (auto& widget : m_sendWidgets) {
            const bool visible = (currentY + sendH) >= contentRect.y && currentY <= (contentRect.bottom() - ROW_H - 8.0f);
            widget->setVisible(visible);
            widget->setBounds({contentRect.x, currentY, contentRect.width, sendH});
            currentY += sendH + gap;
        }

        const float addButtonY = currentY + 4.0f;
        m_maxScrollOffset = std::max(0.0f, addButtonY + ROW_H - contentRect.bottom());
        clampScrollOffsets();

        // "Add Send" button
        m_addFxRect = NUIRect{contentRect.x, addButtonY, contentRect.width, ROW_H};
        
        NUIColor addBg = m_addPressed ? m_addHover : (m_addHovered ? m_addHover : m_addBg);
        renderer.fillRoundedRect(m_addFxRect, ROW_RADIUS, addBg);
        renderer.strokeRoundedRect(m_addFxRect, ROW_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered("Add Send", m_addFxRect, 11.0f, m_addText);

    } else {
        // I/O Tab or Inserts
        // Clear the add rect so it doesn't capture clicks in other tabs
        m_addFxRect = NUIRect{0,0,0,0};
        
        bool isMaster = (channel && channel->id == 0);
        if (isMaster) {
             renderer.drawTextCentered("Master Output is fixed to Hardware Output 1/2", contentRect, 11.0f, m_textSecondary);
        } else if (m_activeTab == Tab::IO) {
             const NUIRect sourceCard{contentRect.x, contentRect.y, contentRect.width, 110.0f};
             renderer.fillRoundedRect(sourceCard, 12.0f, m_tabBg.withAlpha(0.46f));
             renderer.strokeRoundedRect(sourceCard, 12.0f, 1.0f, accent.withAlpha(0.20f));

             renderer.drawText("AUDIO INPUT", {sourceCard.x + 12.0f, sourceCard.y + 9.0f}, 9.0f,
                               m_textSecondary.withAlpha(0.72f));
             renderer.drawText("Choose source", {sourceCard.x + 12.0f, sourceCard.y + 25.0f}, 12.0f,
                               m_text.withAlpha(0.96f));
             renderer.drawText("Verify level before recording.",
                               {sourceCard.x + 12.0f, sourceCard.y + 42.0f}, 9.0f,
                               m_textSecondary.withAlpha(0.76f));

             // Meta row sits BELOW the input dropdown (layoutHitRects places it at
             // contentY + 58, height 22 → bottom ~80). Anchoring the labels here
             // keeps them clear of the dropdown instead of under it.
             // Label and value share one size and baseline so they align. The
             // value's prominence comes from a brighter colour, not a larger
             // size — previously it was 9.5px vs the label's 8.5px (which the
             // renderer floored differently by alpha), so they mismatched.
             const float metaY = sourceCard.y + 88.0f;
             constexpr float kMetaSize = 10.0f;
             renderer.drawText("Source", {sourceCard.x + 12.0f, metaY}, kMetaSize,
                               m_textSecondary.withAlpha(0.70f));
             renderer.drawText(fitText(renderer, channel->inputSourceName, kMetaSize, 60.0f),
                               {sourceCard.x + 58.0f, metaY}, kMetaSize, m_text.withAlpha(0.92f));

             const std::string monitorMode = channel->monitored ? "Arm + Monitor" : "Arm Only";
             renderer.drawText("Mode", {sourceCard.x + 122.0f, metaY}, kMetaSize,
                               m_textSecondary.withAlpha(0.70f));
             renderer.drawText(fitText(renderer, monitorMode, kMetaSize, sourceCard.right() - sourceCard.x - 160.0f),
                               {sourceCard.x + 158.0f, metaY}, kMetaSize, m_text.withAlpha(0.92f));

             const float signalTop = sourceCard.bottom() + 10.0f;
             const NUIRect signalCard{contentRect.x, signalTop, contentRect.width, 94.0f};
             renderer.fillRoundedRect(signalCard, 12.0f, m_tabBg.withAlpha(0.38f));
             renderer.strokeRoundedRect(signalCard, 12.0f, 1.0f, m_border.withAlpha(0.34f));

             const float peakDb = (channel->inputPeak > 0.0001f)
                 ? (20.0f * std::log10(channel->inputPeak))
                 : -90.0f;
             char peakBuf[64];
             std::snprintf(peakBuf, sizeof(peakBuf), "%.1f dBFS", peakDb);
             renderer.drawText("SIGNAL", {signalCard.x + 12.0f, signalCard.y + 10.0f}, 9.0f,
                               m_textSecondary.withAlpha(0.72f));
             renderer.drawText(peakBuf,
                               {signalCard.right() - 12.0f - renderer.measureText(peakBuf, 9.5f).width,
                                signalCard.y + 9.0f},
                               9.5f,
                               m_textSecondary.withAlpha(0.86f));

             const NUIRect meterRect{signalCard.x + 12.0f, signalCard.y + 34.0f, signalCard.width - 24.0f, 13.0f};
             renderer.fillRoundedRect(meterRect, 6.5f, m_bg.withAlpha(0.62f));
             renderer.strokeRoundedRect(meterRect, 6.5f, 1.0f, m_border.withAlpha(0.45f));

             const float fillWidth = std::clamp(channel->inputPeak, 0.0f, 1.0f) * meterRect.width;
             if (fillWidth > 1.0f) {
                 const NUIColor meterColor = (channel->inputPeak >= 0.95f)
                     ? NUIColor::fromHex(0xffd95f5f)
                     : (channel->inputPeak >= 0.75f)
                        ? NUIColor::fromHex(0xffd7b45f)
                        : NUIColor::fromHex(0xff46d1c9);
                 renderer.fillRoundedRect({meterRect.x, meterRect.y, fillWidth, meterRect.height}, 6.0f, meterColor.withAlpha(0.95f));
             }

             // Short hint, word-wrapped as a safety net. (The old long copy
             // overflowed the card into the master strip — measured width also
             // under-reads actual render at this size, so keep it concise.)
             {
                 const std::string hint = "Flat or clipped? Recheck the source.";
                 const float hintMaxW = signalCard.width - 24.0f;
                 std::string line1 = hint, line2;
                 while (renderer.measureText(line1, 8.5f).width > hintMaxW) {
                     const size_t sp = line1.find_last_of(' ');
                     if (sp == std::string::npos) break;
                     line2 = line1.substr(sp + 1) + (line2.empty() ? "" : " " + line2);
                     line1 = line1.substr(0, sp);
                 }
                 const NUIColor hintColor = m_textSecondary.withAlpha(0.72f);
                 renderer.drawText(line1, {signalCard.x + 12.0f, signalCard.y + 54.0f}, 8.5f, hintColor);
                 if (!line2.empty()) {
                     renderer.drawText(fitText(renderer, line2, 8.5f, hintMaxW),
                                       {signalCard.x + 12.0f, signalCard.y + 68.0f}, 8.5f, hintColor);
                 }
             }
        }
    }

    renderChildren(renderer);
}

void UIMixerInspector::onUpdate(double deltaTime)
{
    // Process deferred actions (like deletions)
    if (!m_deferredActions.empty()) {
        auto actions = std::move(m_deferredActions);
        m_deferredActions.clear();
        for (auto& action : actions) {
            action();
        }
    }

    if (std::abs(m_targetScrollOffset - m_scrollOffset) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);
        m_scrollOffset += (m_targetScrollOffset - m_scrollOffset) * ease;
        clampScrollOffsets();
    } else if (std::abs(m_targetScrollOffset - m_scrollOffset) > 0.0f) {
        m_scrollOffset = m_targetScrollOffset;
        clampScrollOffsets();
    }
    
    NUIComponent::onUpdate(deltaTime);
    
    // Sync I/O Dropdown
    if (m_activeTab == Tab::IO && m_ioInputDropdown && m_viewModel) {
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) { // Not for Master
            m_ioInputDropdown->setVisible(true);

            const auto& inputs = m_viewModel->inputNames;
            const auto& deviceIds = m_viewModel->inputDeviceIds;

            const auto itemCount = m_ioInputDropdown->getItemCount();

            const bool rebuildDropdown =
                itemCount != inputs.size() ||
                m_cachedInputNames != inputs ||
                m_cachedInputDeviceIds != deviceIds;

            if (rebuildDropdown) {
                m_ioInputDropdown->clearItems();
                for (size_t i = 0; i < inputs.size(); ++i) {
                    m_ioInputDropdown->addItem(inputs[i], deviceIds[i]);
                }
                m_cachedInputNames = inputs;
                m_cachedInputDeviceIds = deviceIds;
            }

            const int currentChannelIndex = ch->inputChannelIndex;
            int targetIndex = 0; // Default to "None".
            for (size_t i = 0; i < deviceIds.size(); ++i) {
                if (deviceIds[i] == currentChannelIndex) {
                    targetIndex = static_cast<int>(i);
                    break;
                }
            }

            if (targetIndex >= 0 && static_cast<size_t>(targetIndex) < itemCount) {
                if (m_ioInputDropdown->getSelectedIndex() != targetIndex) {
                    m_ioInputDropdown->setSelectedIndex(targetIndex);
                }
            }
        } else {
            m_ioInputDropdown->setVisible(false);
        }
    }

    if (m_activeTab == Tab::Sends && m_mainOutputDropdown && m_viewModel) {
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) {
            m_mainOutputDropdown->setVisible(true);

            std::vector<std::string> outputNames;
            std::vector<int> outputIds;
            outputNames.push_back("Master");
            outputIds.push_back(0);

            auto available = m_viewModel->getAvailableDestinations(ch->id);
            for (const auto& dest : available) {
                if (dest.id == 0) continue;
                outputNames.push_back(dest.name);
                outputIds.push_back(static_cast<int>(dest.id));
            }

            const auto outputItemCount = m_mainOutputDropdown->getItemCount();

            const bool rebuildDropdown =
                outputItemCount != outputNames.size() ||
                m_cachedOutputNames != outputNames ||
                m_cachedOutputIds != outputIds;

            if (rebuildDropdown) {
                m_mainOutputDropdown->clearItems();
                for (size_t i = 0; i < outputNames.size(); ++i) {
                    m_mainOutputDropdown->addItem(outputNames[i], outputIds[i]);
                }
                m_cachedOutputNames = outputNames;
                m_cachedOutputIds = outputIds;
            }

            const uint32_t currentOutputId = ch->mainOutputId;
            int targetIndex = 0;
            for (size_t i = 0; i < outputIds.size(); ++i) {
                if (static_cast<uint32_t>(outputIds[i]) == currentOutputId) {
                    targetIndex = static_cast<int>(i);
                    break;
                }
            }
            if (targetIndex >= 0 && static_cast<size_t>(targetIndex) < outputItemCount) {
                if (m_mainOutputDropdown->getSelectedIndex() != targetIndex) {
                    m_mainOutputDropdown->setSelectedIndex(targetIndex);
                }
            }
        } else {
            m_mainOutputDropdown->setVisible(false);
        }
    } else if (m_mainOutputDropdown) {
        m_mainOutputDropdown->setVisible(false);
    }
}

bool UIMixerInspector::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible() || !isEnabled()) return false;

    const auto b = getBounds();
    
    // Early exit if event is outside our bounds (except for drags that might have started inside)
    if (!b.contains(event.position) && event.button != NUIMouseButton::None) {
        return false;
    }

    if (m_activeTab == Tab::Sends && event.wheelDelta != 0.0f && b.contains(event.position)) {
        m_targetScrollOffset -= event.wheelDelta * 36.0f;
        clampScrollOffsets();
        repaint();
        return true;
    }

    // 1. Allow children (UIMixerSend widgets, EffectChainRack) to handle events
    // We manually iterate children to ensure the EffectRack gets events even if slightly out of bounds
    // (Standard NUIComponent::onMouseEvent transforms coordinates and enforces strict bounds, which was failing here)
    const auto& kids = getChildren();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        auto& child = *it;
        if (child->isVisible()) {
             // Try to handle event without transforming coordinates (Raw dispatch)
             if (child->onMouseEvent(event)) return true;
        }
    }
    // if (NUIComponent::onMouseEvent(event)) return true; // Disabled standard dispatch

    if (event.button == NUIMouseButton::None) {
        const bool addHover = (m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position);
        if (addHover != m_addHovered) {
            m_addHovered = addHover;
            repaint();
        }
        // Consume hover if inside bounds to prevent hover-through to components behind
        return b.contains(event.position);
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_activeTab == Tab::Sends && (m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position)) {
            m_addPressed = true;
            repaint();
            return true;
        }
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        if (m_addPressed) {
            m_addPressed = false;
            repaint();
            
            if (m_activeTab == Tab::Inserts) {
                // Placeholder action (effect insertion is handled elsewhere).
            } else if (m_activeTab == Tab::Sends) {
                if (m_viewModel && m_viewModel->getSelectedChannel()) {
                    m_viewModel->addSend(m_viewModel->getSelectedChannel()->id);
                    // Rebuild UI immediately (optimistic)
                    rebuildSendWidgets(m_viewModel->getSelectedChannel());
                    repaint();
                }
            }
            return true;
        }
    }

    // Consume events within our visual bounds to prevent clickthrough
    return b.contains(event.position);
}

void UIMixerInspector::setPlatformBridge(NUIPlatformBridge* bridge)
{
    if (m_effectRack) m_effectRack->setPlatformBridge(bridge);
}

} // namespace AestraUI
