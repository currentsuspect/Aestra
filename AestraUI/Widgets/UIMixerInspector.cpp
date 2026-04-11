// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"

#include "NUIThemeSystem.h"
#include "../../AestraCore/include/AestraLog.h"
#include "NUIRenderer.h"
#include "NUIMixerWidgets.h"
#include "PluginBrowserPanel.h"
#include "MixerViewModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    constexpr float HEADER_RADIUS = 16.0f;
    constexpr float IO_DROPDOWN_H = 22.0f;
    constexpr float SEND_OUTPUT_CARD_H = 98.0f;
    constexpr float SEND_ROUTE_MAP_H = 108.0f;

    std::string fitLabel(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth)
    {
        if (renderer.measureText(text, fontSize).width <= maxWidth) {
            return text;
        }
        std::string clipped = text;
        const std::string ellipsis = "...";
        while (!clipped.empty() &&
               renderer.measureText(clipped + ellipsis, fontSize).width > maxWidth) {
            clipped.pop_back();
        }
        return clipped.empty() ? ellipsis : clipped + ellipsis;
    }

    bool pluginSupportsSidechain(const Aestra::Audio::PluginInfo& info)
    {
        return info.numAudioInputs >= 4;
    }

    bool channelHasSidechainConsumer(const Aestra::ChannelViewModel* channel)
    {
        if (!channel || !channel->channel) {
            return false;
        }

        const auto& effectChain = channel->channel->getEffectChain();
        for (size_t slotIndex = 0; slotIndex < Aestra::Audio::EffectChain::MAX_SLOTS; ++slotIndex) {
            const auto* slot = effectChain.getSlot(slotIndex);
            if (!slot || slot->isEmpty() || !slot->plugin) {
                continue;
            }
            if (pluginSupportsSidechain(slot->plugin->getInfo())) {
                return true;
            }
        }

        return false;
    }

    bool hasSidechainReadyDestination(const Aestra::MixerViewModel* viewModel, const Aestra::ChannelViewModel* channel)
    {
        if (!viewModel || !channel) {
            return false;
        }

        for (const auto& send : channel->sends) {
            if (!send.sidechainOnly) {
                continue;
            }
            const auto* targetChannel = viewModel->getChannelById(send.targetId);
            if (channelHasSidechainConsumer(targetChannel)) {
                return true;
            }
        }

        return false;
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
    m_tabControl->setAccentColor(NUIColor(0.62f, 0.58f, 0.98f, 1.0f));
    
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
    m_addHover = theme.getColor("surfaceSecondary");
    m_addText = theme.getColor("textPrimary");
}

void UIMixerInspector::setActiveTab(Tab tab)
{
    if (m_activeTab == tab) return;
    m_activeTab = tab;

    NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
    switch (m_activeTab) {
        case Tab::Inserts: accent = NUIColor(0.62f, 0.58f, 0.98f, 1.0f); break;
        case Tab::Sends:   accent = NUIColor(0.42f, 0.86f, 0.92f, 1.0f); break;
        case Tab::IO:      accent = NUIColor(0.90f, 0.74f, 0.50f, 1.0f); break;
    }
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
        float currentY = contentY + 28.0f;
        m_ioInputDropdown->setBounds(x, currentY, w, IO_DROPDOWN_H);
    }
    if (m_mainOutputDropdown) {
        const float dropdownW = std::floor(w * 0.50f);
        m_mainOutputDropdown->setBounds(x + 10.0f, contentY + 112.0f, dropdownW, IO_DROPDOWN_H);
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
        const float topPad = (m_activeTab == Tab::Inserts) ? 36.0f : 10.0f;
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
    m_cachedHeaderTitle = channel->name.empty()
        ? ("Track " + std::to_string(std::max(1, m_cachedTrackNumber)))
        : channel->name;

    const std::string trackLabel = (m_cachedTrackNumber > 0)
        ? ("Track " + std::to_string(m_cachedTrackNumber))
        : "Channel";
    const bool hasSends = !channel->sends.empty();
    m_cachedHeaderSubtitle = trackLabel;
    if (!channel->masterSendEnabled && channel->mainOutputId != 0) {
        m_cachedHeaderSubtitle += "  •  Bus only";
    }
    if (hasSends) {
        m_cachedHeaderSubtitle += "  •  " + std::to_string(channel->sends.size()) + " send";
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
        widget->setAccentColor(NUIColor(0.42f, 0.86f, 0.92f, 0.92f));
        
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

    NUIColor accent = NUIThemeManager::getInstance().getColor("accentPrimary");
    switch (m_activeTab) {
        case Tab::Inserts:
            accent = NUIColor(0.62f, 0.58f, 0.98f, 1.0f);
            break;
        case Tab::Sends:
            accent = NUIColor(0.42f, 0.86f, 0.92f, 1.0f);
            break;
        case Tab::IO:
            accent = NUIColor(0.90f, 0.74f, 0.50f, 1.0f);
            break;
    }
    
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
        renderer.drawShadow(emptyCard, 0.0f, 4.0f, 14.0f, NUIColor(0, 0, 0, 0.12f));
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

    renderer.drawShadow(headerRect, 0.0f, 4.0f, 14.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(headerRect, HEADER_RADIUS, m_tabBg.withAlpha(0.70f));
    renderer.strokeRoundedRect(headerRect, HEADER_RADIUS, 1.0f, m_border.withAlpha(0.50f));
    renderer.strokeRoundedRect({headerRect.x + 1.0f, headerRect.y + 1.0f, headerRect.width - 2.0f, headerRect.height - 2.0f},
                               std::max(0.0f, HEADER_RADIUS - 1.0f),
                               1.0f,
                               NUIColor::white().withAlpha(0.022f));

    const NUIRect titleChip{headerRect.x + 10.0f, headerRect.y + 10.0f, 56.0f, 18.0f};
    renderer.fillRoundedRect(titleChip, 9.0f, m_bg.withAlpha(0.34f));
    renderer.strokeRoundedRect(titleChip, 9.0f, 1.0f, accent.withAlpha(0.22f));
    renderer.drawTextCentered(channel->id == 0 ? "BUS" : "TRACK", titleChip, 9.0f, m_textSecondary.withAlpha(0.92f));

    renderer.drawText(m_cachedHeaderTitle, {headerRect.x + 10.0f, headerRect.y + 30.0f}, 12.5f, m_text);
    if (!m_cachedHeaderSubtitle.empty()) {
        renderer.drawText(m_cachedHeaderSubtitle, {headerRect.x + 10.0f, headerRect.y + 45.0f}, 10.0f, m_textSecondary.withAlpha(0.92f));
    }
    {
        const char* flowSteps[4];
        int flowCount = 0;
        switch (m_activeTab) {
            case Tab::Inserts:
                flowSteps[flowCount++] = "Input";
                flowSteps[flowCount++] = "Trim";
                flowSteps[flowCount++] = "Inserts";
                flowSteps[flowCount++] = "Output";
                break;
            case Tab::Sends:
                flowSteps[flowCount++] = "Inserts";
                flowSteps[flowCount++] = "Sends";
                flowSteps[flowCount++] = "Fader";
                flowSteps[flowCount++] = "Output";
                break;
            case Tab::IO:
                flowSteps[flowCount++] = "Input";
                flowSteps[flowCount++] = "Monitor";
                flowSteps[flowCount++] = "Record";
                break;
        }

        float chipX = headerRect.x + 10.0f;
        const float chipY = headerRect.y + 59.0f;
        for (int i = 0; i < flowCount; ++i) {
            const std::string step = flowSteps[i];
            const float textW = renderer.measureText(step, 8.0f).width;
            const float chipW = textW + 12.0f;
            const NUIRect chipRect{chipX, chipY, chipW, 13.0f};
            const bool activeStep = (m_activeTab == Tab::Inserts && step == "Inserts") ||
                                    (m_activeTab == Tab::Sends && step == "Sends") ||
                                    (m_activeTab == Tab::IO && (step == "Input" || step == "Record"));
            renderer.fillRoundedRect(chipRect,
                                     6.5f,
                                     activeStep ? m_bg.withAlpha(0.48f)
                                                : m_bg.withAlpha(0.32f));
            renderer.strokeRoundedRect(chipRect,
                                       6.5f,
                                       1.0f,
                                       activeStep ? accent.withAlpha(0.22f)
                                                  : m_border.withAlpha(0.18f));
            renderer.drawTextCentered(step, chipRect, 8.0f, m_textSecondary.withAlpha(activeStep ? 0.94f : 0.82f));
            chipX += chipW + 4.0f;
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
        const NUIRect summaryCard{contentRect.x, contentRect.y, contentRect.width, INSERT_SUMMARY_H};
        renderer.fillRoundedRect(summaryCard, 12.0f, m_tabBg.withAlpha(0.46f));
        renderer.strokeRoundedRect(summaryCard, 12.0f, 1.0f, accent.withAlpha(0.16f));
        renderer.drawText("Insert Status", {summaryCard.x + 10.0f, summaryCard.y + 7.0f}, 8.5f, m_textSecondary.withAlpha(0.88f));
        renderer.drawText(buf, {summaryCard.x + 10.0f, summaryCard.y + 18.0f}, 10.5f, m_text.withAlpha(0.96f));

        // Rack is rendered by renderChildren() if visible
    } else if (m_activeTab == Tab::Sends) {
        const int sendCount = static_cast<int>(m_sendWidgets.size());
        const std::string routingWarning = m_viewModel ? m_viewModel->getRoutingWarning(channel->id) : std::string();
        const bool hasRoutingWarning = !routingWarning.empty();

        char summaryBuf[96];
        if (sendCount == 0) {
            std::snprintf(summaryBuf, sizeof(summaryBuf), "No sends configured");
        } else {
            std::snprintf(summaryBuf, sizeof(summaryBuf), "%d send%s active", sendCount, sendCount == 1 ? "" : "s");
        }
        const NUIRect summaryCard{contentRect.x, contentRect.y, contentRect.width, 36.0f};
        renderer.fillRoundedRect(summaryCard, 12.0f, m_tabBg.withAlpha(0.46f));
        const NUIColor summaryStroke = hasRoutingWarning
            ? NUIThemeManager::getInstance().getColor("warning").withAlpha(0.30f)
            : accent.withAlpha(0.16f);
        renderer.strokeRoundedRect(summaryCard, 12.0f, 1.0f, summaryStroke);
        renderer.drawText("Send Status", {summaryCard.x + 10.0f, summaryCard.y + 7.0f}, 8.5f, m_textSecondary.withAlpha(0.88f));
        renderer.drawText(summaryBuf, {summaryCard.x + 10.0f, summaryCard.y + 19.0f}, 10.5f, m_text.withAlpha(0.96f));
        if (hasRoutingWarning) {
            renderer.drawText(routingWarning,
                              {summaryCard.x + 118.0f, summaryCard.y + 19.0f},
                              8.5f,
                              NUIThemeManager::getInstance().getColor("warning").withAlpha(0.92f));
        } else {
            renderer.drawText("Audio sends for this track.",
                              {summaryCard.x + 118.0f, summaryCard.y + 19.0f},
                              8.5f,
                              m_textSecondary.withAlpha(0.74f));
        }

        const NUIRect outputHeader{contentRect.x, summaryCard.bottom() + 8.0f, contentRect.width, SEND_OUTPUT_CARD_H};
        renderer.fillRoundedRect(outputHeader, 14.0f, m_tabBg.withAlpha(0.40f));
        renderer.strokeRoundedRect(outputHeader, 14.0f, 1.0f, m_border.withAlpha(0.36f));
        renderer.drawText("Main Output", {outputHeader.x + 10.0f, outputHeader.y + 10.0f}, 11.5f, m_text);
        renderer.drawText("Choose where the main audible path goes.",
                          {outputHeader.x + 10.0f, outputHeader.y + 25.0f}, 8.75f, m_textSecondary.withAlpha(0.82f));
        renderer.drawText("Master or subgroup destination.",
                          {outputHeader.x + 10.0f, outputHeader.y + 37.0f}, 8.5f, m_textSecondary.withAlpha(0.68f));
        renderer.drawText("Main path",
                          {outputHeader.x + outputHeader.width - 70.0f, outputHeader.bottom() - 14.0f},
                          8.25f, m_textSecondary.withAlpha(0.62f));

        const NUIRect routingCard{contentRect.x, outputHeader.bottom() + 10.0f, contentRect.width, SEND_ROUTE_MAP_H};
        renderer.fillRoundedRect(routingCard, 12.0f, m_tabBg.withAlpha(0.52f));
        renderer.strokeRoundedRect(routingCard, 12.0f, 1.0f, m_border.withAlpha(0.30f));
        renderer.drawText("Route Map", {routingCard.x + 10.0f, routingCard.y + 8.0f}, 9.5f, m_text.withAlpha(0.92f));

        const bool sidechainReady = hasSidechainReadyDestination(m_viewModel, channel);
        const char* sidechainLabel = sidechainReady ? "SC ready" : "SC unavailable";
        const float sidechainW = renderer.measureText(sidechainLabel, 8.5f).width + 18.0f;
        const bool busOnly = !channel->masterSendEnabled && channel->mainOutputId != 0;
        const char* masterLabel = busOnly ? "Master off" : "Master on";
        const float masterW = renderer.measureText(masterLabel, 8.5f).width + 18.0f;
        const NUIRect sidechainChip{routingCard.right() - sidechainW - 10.0f, routingCard.y + 7.0f, sidechainW, 18.0f};
        const NUIRect masterChip{sidechainChip.x - masterW - 6.0f, routingCard.y + 7.0f, masterW, 18.0f};
        renderer.fillRoundedRect(masterChip, 9.0f, busOnly ? accent.withAlpha(0.10f) : m_bg.withAlpha(0.28f));
        renderer.strokeRoundedRect(masterChip, 9.0f, 1.0f, busOnly ? accent.withAlpha(0.20f) : m_border.withAlpha(0.14f));
        renderer.drawTextCentered(masterLabel, masterChip, 8.5f, m_textSecondary.withAlpha(0.88f));
        renderer.fillRoundedRect(sidechainChip,
                                 9.0f,
                                 sidechainReady ? accent.withAlpha(0.10f) : m_bg.withAlpha(0.28f));
        renderer.strokeRoundedRect(sidechainChip,
                                   9.0f,
                                   1.0f,
                                   sidechainReady ? accent.withAlpha(0.20f) : m_border.withAlpha(0.14f));
        renderer.drawTextCentered(sidechainLabel,
                                  sidechainChip,
                                  8.5f,
                                  m_textSecondary.withAlpha(sidechainReady ? 0.92f : 0.82f));

        const float laneTop = routingCard.y + 34.0f;
        const float sourceW = 52.0f;
        const float targetW = 116.0f;
        const NUIRect sourceChip{routingCard.x + 10.0f, laneTop + 16.0f, sourceW, 20.0f};
        renderer.fillRoundedRect(sourceChip, 10.0f, m_bg.withAlpha(0.34f));
        renderer.strokeRoundedRect(sourceChip, 10.0f, 1.0f, accent.withAlpha(0.16f));
        const std::string sourceLabel = m_cachedTrackNumber > 0 ? ("Track " + std::to_string(m_cachedTrackNumber)) : "Track";
        renderer.drawTextCentered(sourceLabel, sourceChip, 8.75f, m_textSecondary.withAlpha(0.96f));

        const std::string routeTarget = channel->routeName.empty() ? "Master" : channel->routeName;
        const char* outputPrefix = busOnly ? "Bus: " : "Out: ";
        const NUIRect outputChip{routingCard.right() - targetW - 10.0f, laneTop + 1.0f, targetW, 18.0f};
        renderer.fillRoundedRect(outputChip, 9.0f, m_bg.withAlpha(0.22f));
        renderer.strokeRoundedRect(outputChip, 9.0f, 1.0f, m_border.withAlpha(0.14f));
        renderer.drawTextCentered(fitLabel(renderer, std::string(outputPrefix) + routeTarget, 8.5f, outputChip.width - 12.0f),
                                  outputChip, 8.5f, m_textSecondary.withAlpha(0.88f));

        const float routeBaseY = sourceChip.y + sourceChip.height * 0.5f;
        renderer.drawLine({sourceChip.right() + 6.0f, routeBaseY},
                          {outputChip.x - 8.0f, outputChip.y + outputChip.height * 0.5f},
                          1.5f,
                          m_textSecondary.withAlpha(0.24f));

        const int visibleRoutes = std::min(sendCount, 3);
        for (int i = 0; i < visibleRoutes; ++i) {
            const auto& send = channel->sends[static_cast<size_t>(i)];
            const float rowY = laneTop + 24.0f + static_cast<float>(i) * 18.0f;
            const NUIRect targetChip{routingCard.right() - targetW - 10.0f, rowY, targetW, 18.0f};
            renderer.fillRoundedRect(targetChip, 9.0f, m_bg.withAlpha(0.28f));
            renderer.strokeRoundedRect(targetChip, 9.0f, 1.0f, accent.withAlpha(0.14f));
            const std::string routeLabel = std::string(send.sidechainOnly ? "SC" : "S")
                + std::to_string(i + 1) + " -> " + send.targetName;
            renderer.drawTextCentered(fitLabel(renderer, routeLabel, 8.25f, targetChip.width - 12.0f),
                                      targetChip, 8.25f, m_textSecondary.withAlpha(0.94f));
            renderer.drawLine({sourceChip.right() + 6.0f, routeBaseY},
                              {targetChip.x - 8.0f, targetChip.y + targetChip.height * 0.5f},
                              1.5f,
                              accent.withAlpha(0.30f));
        }

        if (sendCount > visibleRoutes) {
            char extraBuf[32];
            std::snprintf(extraBuf, sizeof(extraBuf), "+%d more", sendCount - visibleRoutes);
            renderer.drawText(extraBuf,
                              {routingCard.right() - 58.0f, routingCard.bottom() - 14.0f},
                              8.0f,
                              m_textSecondary.withAlpha(0.72f));
        }

        float currentY = routingCard.bottom() + 12.0f - m_scrollOffset;
        const float sendH = 102.0f;
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
             const NUIRect ioHeader{contentRect.x, contentRect.y, contentRect.width, 48.0f};
             renderer.fillRoundedRect(ioHeader, 14.0f, m_tabBg.withAlpha(0.40f));
             renderer.strokeRoundedRect(ioHeader, 14.0f, 1.0f, m_border.withAlpha(0.36f));
             renderer.drawText("Audio Input", {ioHeader.x + 10.0f, ioHeader.y + 10.0f}, 11.5f, m_text);
             renderer.drawText("Pick a source, then verify the live level before record.",
                               {ioHeader.x + 10.0f, ioHeader.y + 25.0f}, 9.0f, m_textSecondary.withAlpha(0.94f));

             const float infoTop = contentRect.y + 84.0f;
             const NUIRect infoCard{contentRect.x, infoTop, contentRect.width, 100.0f};
             renderer.drawShadow(infoCard, 0.0f, 4.0f, 12.0f, NUIColor(0, 0, 0, 0.10f));
             renderer.fillRoundedRect(infoCard, IO_CARD_RADIUS, m_tabBg.withAlpha(0.62f));
             renderer.strokeRoundedRect(infoCard, IO_CARD_RADIUS, 1.0f, m_border.withAlpha(0.46f));
             renderer.strokeRoundedRect({infoCard.x + 1.0f, infoCard.y + 1.0f, infoCard.width - 2.0f, infoCard.height - 2.0f},
                                        std::max(0.0f, IO_CARD_RADIUS - 1.0f),
                                        1.0f,
                                        NUIColor::white().withAlpha(0.02f));

             const float labelX = infoCard.x + 12.0f;
             const float valueX = infoCard.x + 70.0f;
             renderer.drawText("Source", {labelX, infoCard.y + 12.0f}, 9.0f, m_textSecondary);
             renderer.drawText(channel->inputSourceName, {valueX, infoCard.y + 10.0f}, 10.0f, m_text);

             const std::string monitorMode = channel->monitored ? "Arm + Monitor" : "Arm Only";
             renderer.drawText("Mode", {labelX, infoCard.y + 30.0f}, 9.0f, m_textSecondary);
             renderer.drawText(monitorMode, {valueX, infoCard.y + 28.0f}, 10.0f, m_text);

             renderer.drawText("Signal", {labelX, infoCard.y + 56.0f}, 9.0f, m_textSecondary);
             const NUIRect meterRect{labelX, infoCard.y + 72.0f, infoCard.width - 24.0f, INPUT_METER_H};
             renderer.fillRoundedRect(meterRect, 6.0f, m_bg.withAlpha(0.62f));
             renderer.strokeRoundedRect(meterRect, 6.0f, 1.0f, m_border.withAlpha(0.60f));

             const float fillWidth = std::clamp(channel->inputPeak, 0.0f, 1.0f) * meterRect.width;
             if (fillWidth > 1.0f) {
                 const NUIColor meterColor = (channel->inputPeak >= 0.95f)
                     ? NUIColor::fromHex(0xffd95f5f)
                     : (channel->inputPeak >= 0.75f)
                        ? NUIColor::fromHex(0xffd7b45f)
                        : NUIColor::fromHex(0xff46d1c9);
                 renderer.fillRoundedRect({meterRect.x, meterRect.y, fillWidth, meterRect.height}, 6.0f, meterColor.withAlpha(0.95f));
             }

             const float peakDb = (channel->inputPeak > 0.0001f)
                 ? (20.0f * std::log10(channel->inputPeak))
                 : -90.0f;
             char peakBuf[64];
             std::snprintf(peakBuf, sizeof(peakBuf), "%.1f dBFS", peakDb);
             renderer.drawText(peakBuf,
                               {meterRect.right() - renderer.measureText(peakBuf, 9.0f).width, infoCard.y + 54.0f},
                               9.0f,
                               m_textSecondary);

             renderer.drawText("If this meter is flat or pinned, fix the input path before recording.",
                               {contentRect.x, infoCard.bottom() + 12.0f}, 9.0f, m_textSecondary.withAlpha(0.88f));
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

            const bool rebuildDropdown =
                m_ioInputDropdown->getItemCount() != static_cast<int>(inputs.size()) ||
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

            if (targetIndex >= 0 && targetIndex < m_ioInputDropdown->getItemCount()) {
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

            const bool rebuildDropdown =
                m_mainOutputDropdown->getItemCount() != static_cast<int>(outputNames.size()) ||
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
            if (targetIndex >= 0 && targetIndex < m_mainOutputDropdown->getItemCount()) {
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

} // namespace AestraUI
