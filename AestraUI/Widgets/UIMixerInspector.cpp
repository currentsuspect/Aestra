// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"
#include "MixerScale.h"
#include "UIMixerSendList.h"

#include "ChannelDisplayName.h"
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
    constexpr float HEADER_H = 54.0f;
    constexpr float CHANNEL_ROW_H = 22.0f;
    // Two rows (trim, width) under the header, with a little air either side.
    constexpr float CHANNEL_BLOCK_H = CHANNEL_ROW_H * 2.0f + 10.0f;
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

    // Channel block (trim, width)
    m_trimBar = std::make_shared<UIParamBar>(UIParamBar::Look::Row);
    m_trimBar->setLabel("Trim");
    m_trimBar->setRange(-24.0f, 24.0f, 0.0f);
    m_trimBar->setFormatter([](float db) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%+.1f dB", std::fabs(db) < 0.05f ? 0.0f : db);
        return std::string(buf);
    });
    m_trimBar->onValueChanged = [this](float db) {
        if (!m_viewModel || !onTrimEdited) return;
        if (auto* ch = m_viewModel->getSelectedChannel()) onTrimEdited(ch->id, db, false);
    };
    m_trimBar->onValueCommitted = [this](float db) {
        if (!m_viewModel || !onTrimEdited) return;
        if (auto* ch = m_viewModel->getSelectedChannel()) onTrimEdited(ch->id, db, true);
    };
    addChild(m_trimBar);

    m_widthBar = std::make_shared<UIParamBar>(UIParamBar::Look::Row);
    m_widthBar->setLabel("Width");
    m_widthBar->setRange(0.0f, 3.0f, 1.0f);
    m_widthBar->setFormatter([](float w) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(w * 100.0f)));
        return std::string(buf);
    });
    m_widthBar->onValueChanged = [this](float width) {
        if (!m_viewModel || !onWidthEdited) return;
        if (auto* ch = m_viewModel->getSelectedChannel()) onWidthEdited(ch->id, width);
    };
    addChild(m_widthBar);

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
    // One column: scroll to the section instead of switching a tab.
    m_activeTab = tab;
    const int index = tab == Tab::Inserts ? 0 : tab == Tab::Sends ? 1 : 2;
    m_targetScrollOffset = std::clamp(m_sectionOffset[index], 0.0f, std::max(0.0f, m_maxScrollOffset));
    repaint();
}

void UIMixerInspector::layoutHitRects()
{
    computeColumn(m_viewModel ? m_viewModel->getSelectedChannel() : nullptr);
}

void UIMixerInspector::onResize(int width, int height)
{
    NUIComponent::onResize(width, height);
    layoutHitRects();
}

bool UIMixerInspector::sendCardVisible(size_t i) const
{
    return i < m_sendWidgets.size() && m_sendWidgets[i]->isVisible();
}

void UIMixerInspector::computeColumn(const Aestra::ChannelViewModel* channel)
{
    updateHeaderCache(channel); // sends and inserts follow the selection even before the first paint
    const auto b = getBounds();
    const float x = b.x + PAD;
    const float w = std::max(1.0f, b.width - PAD * 2.0f);
    m_headerRect = NUIRect{x, b.y + PAD, w, HEADER_H};
    m_viewportRect = NUIRect{b.x, m_headerRect.bottom() + 6.0f, b.width, std::max(0.0f, b.bottom() - m_headerRect.bottom() - 6.0f)};
    m_fedChips.clear();
    m_addFxRect = m_addSidechainRect = m_linkRect = NUIRect{};

    constexpr float kHeadH = 22.0f;
    constexpr float kSectionGap = 10.0f;
    const bool master = channel && channel->id == 0;
    float y = m_viewportRect.y - m_scrollOffset;
    const float columnTop = m_viewportRect.y;

    auto hide = [](const std::shared_ptr<NUIComponent>& c) { if (c) c->setVisible(false); };
    if (!channel) {
        hide(m_trimBar); hide(m_widthBar); hide(m_effectRack); hide(m_mainOutputDropdown); hide(m_ioInputDropdown);
        for (auto& wdg : m_sendWidgets) wdg->setVisible(false);
        m_maxScrollOffset = 0.0f;
        return;
    }

    // Channel block (trim, width): not for the master.
    if (!master) {
        if (m_trimBar) m_trimBar->setBounds(x + 4.0f, y + 4.0f, w - 8.0f, CHANNEL_ROW_H);
        if (m_widthBar) m_widthBar->setBounds(x + 4.0f, y + 4.0f + CHANNEL_ROW_H, w - 8.0f, CHANNEL_ROW_H);
        y += CHANNEL_BLOCK_H;
    }

    // OUTPUT
    m_sectionOffset[2] = y - columnTop + m_scrollOffset;
    m_outputHeadRect = NUIRect{x, y, w, kHeadH};
    y += kHeadH;
    m_outputRowRect = NUIRect{x + 2.0f, y, w - 4.0f, IO_DROPDOWN_H + 2.0f};
    if (m_mainOutputDropdown && !master) m_mainOutputDropdown->setBounds(m_outputRowRect);
    y += m_outputRowRect.height + 4.0f;
    m_linkRect = NUIRect{x + 2.0f, y, w - 4.0f, 18.0f};
    y += m_linkRect.height + 2.0f;

    // FED BY (a bus lists what feeds it)
    if (m_viewModel) {
        std::vector<const Aestra::ChannelViewModel*> feeders;
        for (size_t i = 0; i < m_viewModel->getChannelCount(); ++i) {
            const auto* other = m_viewModel->getChannelByIndex(i);
            if (other && other->id != channel->id && other->mainOutputId == channel->id && channel->id != 0) feeders.push_back(other);
        }
        if (!feeders.empty()) {
            y += 6.0f;
            m_fedHeadRect = NUIRect{x, y, w, kHeadH};
            y += kHeadH;
            float cx = x + 2.0f, cy = y;
            constexpr float kChipH = 20.0f;
            for (const auto* f : feeders) {
                const std::string name = f->name.empty() ? channelFallbackLabel(f->id) : f->name;
                const float chipW = std::min(w - 4.0f, 22.0f + static_cast<float>(name.size()) * 6.2f);
                if (cx + chipW > x + w) { cx = x + 2.0f; cy += kChipH + 4.0f; }
                FedChip chip;
                chip.rect = NUIRect{cx, cy, chipW, kChipH};
                chip.name = name;
                const uint32_t argb = f->trackColorIndex >= 0 ? paletteIndexToARGB(f->trackColorIndex) : 0xFF808080u;
                chip.color = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
                m_fedChips.push_back(std::move(chip));
                cx += chipW + 4.0f;
            }
            y = cy + kChipH + 4.0f;
        } else {
            m_fedHeadRect = NUIRect{};
        }
    }
    y += kSectionGap;

    // INSERTS
    m_sectionOffset[0] = y - columnTop + m_scrollOffset;
    m_insertsHeadRect = NUIRect{x, y, w, kHeadH};
    y += kHeadH;
    if (m_effectRack) {
        const int rows = std::clamp(m_effectRack->visibleRowCount(), 1, 8);
        m_rackRect = NUIRect{x, y, w, rows * EffectChainRack::rowHeight() + 2.0f};
        m_effectRack->setBounds(m_rackRect);
        y += m_rackRect.height;
    }
    y += kSectionGap;

    // SENDS (the master has none)
    if (!master) {
        m_sectionOffset[1] = y - columnTop + m_scrollOffset;
        m_sendsHeadRect = NUIRect{x, y, w, kHeadH};
        y += kHeadH;
        const std::string warning = m_viewModel ? m_viewModel->getRoutingWarning(channel->id) : std::string();
        if (!warning.empty()) {
            m_warningRect = NUIRect{x + 2.0f, y, w - 4.0f, 16.0f};
            y += 18.0f;
        } else {
            m_warningRect = NUIRect{};
        }
        // One 32 px row per send; the open one unfolds into its full card right below.
        constexpr float sendRowH = 32.0f;
        constexpr float sendCardH = 124.0f;
        m_sendRowRects.clear();
        if (m_expandedSend >= static_cast<int>(m_sendWidgets.size())) m_expandedSend = -1;
        for (size_t i = 0; i < m_sendWidgets.size(); ++i) {
            m_sendRowRects.push_back(NUIRect{x, y, w, sendRowH});
            y += sendRowH;
            if (static_cast<int>(i) == m_expandedSend) {
                m_sendWidgets[i]->setBounds({x, y, w, sendCardH});
                y += sendCardH + 4.0f;
            }
        }
        y += 6.0f;
        const float gap = 6.0f;
        const float half = (w - gap) * 0.5f;
        m_addFxRect = NUIRect{x, y, half, ROW_H};
        m_addSidechainRect = NUIRect{x + half + gap, y, half, ROW_H};
        y += ROW_H + kSectionGap;

        // INPUT
        m_inputHeadRect = NUIRect{x, y, w, kHeadH};
        y += kHeadH;
        m_inputRowRect = NUIRect{x + 2.0f, y, w - 4.0f, IO_DROPDOWN_H + 2.0f};
        if (m_ioInputDropdown) m_ioInputDropdown->setBounds(m_inputRowRect);
        y += m_inputRowRect.height + 4.0f;
        m_inputMetaRect = NUIRect{x + 2.0f, y, w - 4.0f, 14.0f};
        y += 18.0f;
        m_inputMeterRect = NUIRect{x + 2.0f, y, w - 4.0f, 8.0f};
        y += 8.0f + kSectionGap;
    }

    m_maxScrollOffset = std::max(0.0f, (y + m_scrollOffset) - m_viewportRect.bottom() + 4.0f);

    // Widgets that scrolled entirely out of view (or under the header) are hidden so they take no events.
    auto inView = [&](const NUIRect& r) { return r.bottom() > m_viewportRect.y && r.y < m_viewportRect.bottom(); };
    if (m_trimBar) m_trimBar->setVisible(!master && inView(m_trimBar->getBounds()));
    if (m_widthBar) m_widthBar->setVisible(!master && inView(m_widthBar->getBounds()));
    if (m_effectRack) m_effectRack->setVisible(inView(m_rackRect));
    if (m_mainOutputDropdown) m_mainOutputDropdown->setVisible(!master && inView(m_outputRowRect));
    if (m_ioInputDropdown) m_ioInputDropdown->setVisible(!master && inView(m_inputRowRect));
    for (size_t i = 0; i < m_sendWidgets.size(); ++i) {
        m_sendWidgets[i]->setVisible(!master && static_cast<int>(i) == m_expandedSend && inView(m_sendWidgets[i]->getBounds()));
    }
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

    // Same stable-id numbering as the strip and the routing map. Numbering from
    // the dense list position instead meant the inspector could call the
    // selected channel "Channel 3" while its own strip said "Channel 7".
    const std::string trackLabel = channelFallbackLabel(channel->id);

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
        m_effectRack->setSlot(static_cast<int>(i), info); // repaints only a slot that changed
    }
}

void UIMixerInspector::rebuildSendWidgets(const Aestra::ChannelViewModel* channel)
{
    // Remove old widgets
    for (auto& w : m_sendWidgets) {
        removeChild(w);
    }
    m_sendWidgets.clear();
    m_expandedSend = -1;

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
    // The inspector is a distinct functional region, so it earns one quiet boundary.
    renderer.drawLine({b.x, b.y}, {b.x, b.bottom()}, 1.0f, m_border.withAlpha(0.55f));

    const auto* channel = m_viewModel ? m_viewModel->getSelectedChannel() : nullptr;
    updateHeaderCache(channel);
    if (channel) rebuildInsertRack(channel); // keeps the rack in step with automation and backend changes
    computeColumn(channel);

    const NUIColor accent = NUIThemeManager::getInstance().getColor("primary");

    if (!channel) {
        const float emptyY = m_viewportRect.y + std::max(24.0f, m_viewportRect.height * 0.22f);
        renderer.drawTextCentered("Select a mixer channel",
                                  {b.x + PAD, emptyY, b.width - PAD * 2.0f, 18.0f}, 12.0f, m_textSecondary.withAlpha(0.78f));
        renderer.drawTextCentered("to inspect its inserts, sends, and input",
                                  {b.x + PAD, emptyY + 18.0f, b.width - PAD * 2.0f, 14.0f}, 9.5f, m_textSecondary.withAlpha(0.48f));
        return;
    }
    const bool master = channel->id == 0;

    // --- Pinned header: the channel's colour, name and what it is -----------------------------------------
    NUIColor titleAccent = accent;
    if (channel->trackColorIndex >= 0) {
        const uint32_t argb = paletteIndexToARGB(channel->trackColorIndex);
        titleAccent = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                               ((argb >> 24) & 0xFF) / 255.0f);
    }
    renderer.fillRoundedRect(m_headerRect, HEADER_RADIUS, m_tabBg.withAlpha(0.52f));
    renderer.fillRoundedRect(NUIRect{m_headerRect.x + 12.0f, m_headerRect.y + 12.0f, 4.0f, m_headerRect.height - 24.0f}, 2.0f,
                             titleAccent.withAlpha(0.90f));
    renderer.drawText(m_cachedHeaderTitle, {m_headerRect.x + 26.0f, m_headerRect.y + 11.0f}, 13.0f, m_text);
    if (!m_cachedHeaderSubtitle.empty()) {
        renderer.drawText(m_cachedHeaderSubtitle, {m_headerRect.x + 26.0f, m_headerRect.y + 29.0f}, 10.0f,
                          m_textSecondary.withAlpha(0.70f));
    }

    // --- The scrolling column ---------------------------------------------------------------------------
    renderer.setClipRect(m_viewportRect);

    auto heading = [&](const NUIRect& r, const char* label, int count, bool showCount) {
        renderer.drawText(label, {r.x + 2.0f, r.y + 6.0f}, 9.5f, m_textSecondary.withAlpha(0.8f));
        if (showCount) {
            const float lw = renderer.measureText(label, 9.5f).width;
            renderer.drawText(std::to_string(count), {r.x + 2.0f + lw + 8.0f, r.y + 6.0f}, 9.5f, m_textSecondary.withAlpha(0.5f));
        }
        renderer.fillRect(NUIRect{r.x, r.bottom() - 1.0f, r.width, 1.0f}, m_border.withAlpha(0.35f));
    };

    // OUTPUT
    heading(m_outputHeadRect, "OUTPUT", 0, false);
    if (master) {
        renderer.drawText("Hardware output 1/2", {m_outputRowRect.x + 4.0f, m_outputRowRect.y + 6.0f}, 11.0f, m_text.withAlpha(0.9f));
    }
    {
        const bool hovered = m_linkHovered;
        const NUIColor link = NUIThemeManager::getInstance().getColor("accentPrimary");
        renderer.drawText("Show in routing graph \xE2\x86\x92", {m_linkRect.x + 2.0f, m_linkRect.y + 3.0f}, 10.5f,
                          link.withAlpha(hovered ? 1.0f : 0.8f));
    }

    // FED BY
    if (!m_fedChips.empty()) {
        heading(m_fedHeadRect, "FED BY", static_cast<int>(m_fedChips.size()), true);
        for (const auto& chip : m_fedChips) {
            renderer.fillRoundedRect(chip.rect, 3.0f, m_tabBg.withAlpha(0.7f));
            renderer.strokeRoundedRect(chip.rect, 3.0f, 1.0f, m_border.withAlpha(0.4f));
            renderer.fillRect(NUIRect{chip.rect.x + 6.0f, chip.rect.y + 7.0f, 6.0f, 6.0f}, chip.color);
            renderer.drawText(fitText(renderer, chip.name, 10.0f, chip.rect.width - 22.0f),
                              {chip.rect.x + 17.0f, chip.rect.y + 4.5f}, 10.0f, m_text.withAlpha(0.92f));
        }
    }

    // INSERTS
    int insertCount = 0;
    for (const auto& ins : channel->inserts) if (!ins.isEmpty && !ins.pendingRemoval) ++insertCount;
    heading(m_insertsHeadRect, "INSERTS", insertCount, true);

    // SENDS
    if (!master) {
        heading(m_sendsHeadRect, "SENDS", static_cast<int>(channel->sends.size()), true);
        if (!m_warningRect.isEmpty()) {
            renderer.drawText(fitText(renderer, m_viewModel->getRoutingWarning(channel->id), 10.0f, m_warningRect.width),
                              {m_warningRect.x, m_warningRect.y + 2.0f}, 10.0f,
                              NUIThemeManager::getInstance().getColor("warning").withAlpha(0.92f));
        }
        // The sends, one line each: destination, level, and how it is tapped.
        for (size_t i = 0; i < m_sendRowRects.size() && i < channel->sends.size(); ++i) {
            const auto& send = channel->sends[i];
            const auto& r = m_sendRowRects[i];
            const bool open = static_cast<int>(i) == m_expandedSend;
            if (open || static_cast<int>(i) == m_hoveredSendRow) renderer.fillRect(r, m_tabBg.withAlpha(open ? 0.55f : 0.35f));
            if (open) renderer.fillRect(NUIRect{r.x, r.y, 2.0f, r.height}, accent);
            const float alpha = send.muted ? 0.4f : 1.0f;
            const float cy = r.y + r.height * 0.5f;
            const NUIColor amber = NUIThemeManager::getInstance().getColor("warning");
            if (send.sidechainOnly) {
                renderer.drawLine({r.x + 12.0f, cy - 4.0f}, {r.x + 16.0f, cy}, 1.4f, amber.withAlpha(alpha));
                renderer.drawLine({r.x + 16.0f, cy}, {r.x + 12.0f, cy + 4.0f}, 1.4f, amber.withAlpha(alpha));
                renderer.drawLine({r.x + 12.0f, cy + 4.0f}, {r.x + 8.0f, cy}, 1.4f, amber.withAlpha(alpha));
                renderer.drawLine({r.x + 8.0f, cy}, {r.x + 12.0f, cy - 4.0f}, 1.4f, amber.withAlpha(alpha));
            } else {
                const auto* target = m_viewModel ? m_viewModel->getChannelById(send.targetId) : nullptr;
                NUIColor swatch = accent;
                if (target && target->trackColorIndex >= 0) {
                    const uint32_t argb = paletteIndexToARGB(target->trackColorIndex);
                    swatch = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
                }
                renderer.fillRect(NUIRect{r.x + 8.0f, cy - 4.0f, 8.0f, 8.0f}, swatch.withAlpha(alpha));
            }
            renderer.drawText(fitText(renderer, send.targetName, 11.0f, 64.0f), {r.x + 22.0f, cy - 6.5f}, 11.0f, m_text.withAlpha(0.92f * alpha));
            const std::string tag = send.sidechainOnly ? "SC" : (send.postFader ? "POST" : "PRE");
            const float tagW = renderer.measureText(tag, 8.5f).width;
            const float db = send.gain > 1e-6f ? 20.0f * std::log10(send.gain) : -90.0f;
            const std::string dbText = send.sidechainOnly ? std::string() : UIMixerSendList::formatLevel(db);
            const float dbW = renderer.measureText(dbText, 10.0f).width;
            renderer.drawText(tag, {r.right() - 8.0f - tagW, cy - 4.5f}, 8.5f, m_textSecondary.withAlpha(0.6f * alpha));
            if (!dbText.empty()) renderer.drawText(dbText, {r.right() - 8.0f - tagW - 6.0f - dbW, cy - 5.5f}, 10.0f, m_textSecondary.withAlpha(0.9f * alpha));
            const float barL = r.x + 22.0f + 66.0f;
            const float barR = r.right() - 8.0f - tagW - 6.0f - dbW - 8.0f;
            if (barR - barL > 10.0f) {
                const NUIRect bar{barL, cy - 2.0f, barR - barL, 4.0f};
                renderer.fillRect(bar, m_border.withAlpha(0.4f));
                const float norm = send.sidechainOnly ? 1.0f : MixerScale::norm(db);
                renderer.fillRect(NUIRect{bar.x, bar.y, bar.width * norm, bar.height}, (send.sidechainOnly ? amber : m_textSecondary).withAlpha(0.8f * alpha));
            }
        }

        const NUIColor addBg = m_addPressed ? m_addHover : (m_addHovered ? m_addHover : m_addBg);
        renderer.fillRoundedRect(m_addFxRect, ROW_RADIUS, addBg);
        renderer.strokeRoundedRect(m_addFxRect, ROW_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered("Add Send", m_addFxRect, 11.0f, m_addText);
        const NUIColor scBg = m_addSidechainPressed ? m_addHover : (m_addSidechainHovered ? m_addHover : m_addBg);
        renderer.fillRoundedRect(m_addSidechainRect, ROW_RADIUS, scBg);
        renderer.strokeRoundedRect(m_addSidechainRect, ROW_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered("Add Sidechain", m_addSidechainRect, 11.0f, m_addText);

        // INPUT
        heading(m_inputHeadRect, "INPUT", 0, false);
        renderer.drawText("Source", {m_inputMetaRect.x, m_inputMetaRect.y + 1.0f}, 10.0f, m_textSecondary.withAlpha(0.7f));
        renderer.drawText(fitText(renderer, channel->inputSourceName, 10.0f, 70.0f), {m_inputMetaRect.x + 44.0f, m_inputMetaRect.y + 1.0f},
                          10.0f, m_text.withAlpha(0.92f));
        const std::string mode = channel->monitored ? "Arm + Monitor" : "Arm Only";
        renderer.drawText(fitText(renderer, mode, 10.0f, m_inputMetaRect.width * 0.4f),
                          {m_inputMetaRect.x + m_inputMetaRect.width * 0.55f, m_inputMetaRect.y + 1.0f}, 10.0f, m_text.withAlpha(0.8f));
        const float peakDb = (channel->inputPeak > 0.0001f) ? (20.0f * std::log10(channel->inputPeak)) : -90.0f;
        renderer.fillRect(m_inputMeterRect, m_border.withAlpha(0.35f));
        const float fill = std::clamp(channel->inputPeak, 0.0f, 1.0f) * m_inputMeterRect.width;
        if (fill > 1.0f) {
            auto& theme = NUIThemeManager::getInstance();
            const NUIColor c = channel->inputPeak >= 0.95f ? theme.getColor("error")
                             : channel->inputPeak >= 0.75f ? theme.getColor("warning") : theme.getColor("meterSafe");
            renderer.fillRect(NUIRect{m_inputMeterRect.x, m_inputMeterRect.y, fill, m_inputMeterRect.height}, c.withAlpha(0.95f));
        }
        (void)peakDb;
    }

    // Children inside the clip: the bars, the rack and the send cards.
    if (m_trimBar && m_trimBar->isVisible()) m_trimBar->onRender(renderer);
    if (m_widthBar && m_widthBar->isVisible()) m_widthBar->onRender(renderer);
    if (m_effectRack && m_effectRack->isVisible()) m_effectRack->onRender(renderer);
    for (auto& wdg : m_sendWidgets) {
        if (wdg->isVisible()) wdg->onRender(renderer);
    }
    renderer.clearClipRect();

    // Dropdowns draw their open list outside their own rect, so they render after the clip is gone.
    if (m_mainOutputDropdown && m_mainOutputDropdown->isVisible()) m_mainOutputDropdown->onRender(renderer);
    if (m_ioInputDropdown && m_ioInputDropdown->isVisible()) m_ioInputDropdown->onRender(renderer);
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

    // Channel block: shown for a channel (the master has neither trim nor width), values follow the model.
    {
        const auto* ch = m_viewModel ? m_viewModel->getSelectedChannel() : nullptr;
        const bool show = ch && ch->id != 0;
        if (m_trimBar) {
            if (show) m_trimBar->setValue(ch->trimDb);
        }
        if (m_widthBar) {
            if (show) m_widthBar->setValue(ch->width);
        }
    }

    // Sync I/O Dropdown
    if (m_ioInputDropdown && m_viewModel) {
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) { // Not for Master

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

    if (m_mainOutputDropdown && m_viewModel) {
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) {

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

    if (event.wheelDelta != 0.0f && m_viewportRect.contains(event.position)) {
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
        const bool linkHover = m_linkRect.contains(event.position) && m_viewportRect.contains(event.position);
        if (linkHover != m_linkHovered) {
            m_linkHovered = linkHover;
            repaint();
        }
        int rowHover = -1;
        for (size_t i = 0; i < m_sendRowRects.size(); ++i) if (m_sendRowRects[i].contains(event.position) && m_viewportRect.contains(event.position)) rowHover = static_cast<int>(i);
        if (rowHover != m_hoveredSendRow) { m_hoveredSendRow = rowHover; repaint(); }
        const bool addHover = (m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position);
        const bool sidechainHover =
            (m_viewModel && m_viewModel->getSelectedChannel()) && m_addSidechainRect.contains(event.position);
        if (addHover != m_addHovered) {
            m_addHovered = addHover;
            repaint();
        }
        if (sidechainHover != m_addSidechainHovered) {
            m_addSidechainHovered = sidechainHover;
            repaint();
        }
        // Consume hover if inside bounds to prevent hover-through to components behind
        return b.contains(event.position);
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_sendRowRects.size(); ++i) {
            if (m_sendRowRects[i].contains(event.position) && m_viewportRect.contains(event.position)) {
                m_expandedSend = (m_expandedSend == static_cast<int>(i)) ? -1 : static_cast<int>(i);
                repaint();
                return true;
            }
        }
        if (m_linkRect.contains(event.position) && m_viewportRect.contains(event.position) && m_viewModel &&
            m_viewModel->getSelectedChannel() && onShowRoutingGraph) {
            onShowRoutingGraph(m_viewModel->getSelectedChannel()->id);
            return true;
        }
        if ((m_viewModel && m_viewModel->getSelectedChannel()) && m_viewportRect.contains(event.position) && m_addFxRect.contains(event.position)) {
            m_addPressed = true;
            repaint();
            return true;
        }
        if ((m_viewModel && m_viewModel->getSelectedChannel()) && m_viewportRect.contains(event.position) &&
            m_addSidechainRect.contains(event.position)) {
            m_addSidechainPressed = true;
            repaint();
            return true;
        }
    }

    if (event.released && event.button == NUIMouseButton::Left) {
        if (m_addPressed) {
            m_addPressed = false;
            repaint();
            
            if (m_viewModel && m_viewModel->getSelectedChannel()) {
                m_viewModel->addSend(m_viewModel->getSelectedChannel()->id);
                // Rebuild UI immediately (optimistic)
                rebuildSendWidgets(m_viewModel->getSelectedChannel());
                repaint();
            }
            return true;
        }
        if (m_addSidechainPressed) {
            m_addSidechainPressed = false;
            repaint();
            if (m_viewModel && m_viewModel->getSelectedChannel()) {
                m_viewModel->addSidechain(m_viewModel->getSelectedChannel()->id);
                rebuildSendWidgets(m_viewModel->getSelectedChannel());
                repaint();
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
