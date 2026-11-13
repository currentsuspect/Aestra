// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"
#include "MixerScale.h"
#include "UIMixerSendList.h"

#include "ChannelDisplayName.h"
#include "NUIThemeSystem.h"
#include "../Graphics/NUIDepth.h"
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
    constexpr float HEADER_H = 30.0f;
    constexpr float CHANNEL_ROW_H = 22.0f;
    // Two rows (trim, width) under the header, with a little air either side.
    constexpr float CHANNEL_BLOCK_H = CHANNEL_ROW_H * 2.0f + 14.0f;
    constexpr float INSERT_SUMMARY_H = 28.0f;
    constexpr float ROW_H = 28.0f;
    constexpr float INPUT_METER_H = 12.0f;
    constexpr float IO_CARD_RADIUS = 14.0f;
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
    m_headerRect = NUIRect{x, b.y + PAD + 2.0f, w, HEADER_H};
    m_viewportRect = NUIRect{b.x, m_headerRect.bottom() + 8.0f, b.width, std::max(0.0f, b.bottom() - m_headerRect.bottom() - 14.0f)};
    m_fedChips.clear();
    m_addFxRect = m_addSidechainRect = m_linkRect = m_levelHeadRect = NUIRect{};
    m_monitorAutoRect = m_monitorArmedRect = NUIRect{};

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
        m_levelHeadRect = NUIRect{x, y, w, kHeadH};
        y += kHeadH;
        if (m_trimBar) m_trimBar->setBounds(x, y, w, CHANNEL_ROW_H);
        if (m_widthBar) m_widthBar->setBounds(x, y + CHANNEL_ROW_H, w, CHANNEL_ROW_H);
        y += CHANNEL_BLOCK_H;
    }

    // OUTPUT
    m_sectionOffset[2] = y - columnTop + m_scrollOffset;
    m_outputHeadRect = NUIRect{x, y, w, kHeadH};
    y += kHeadH;
    m_outputRowRect = NUIRect{x, y, w, IO_DROPDOWN_H + 6.0f};
    if (m_mainOutputDropdown && !master) m_mainOutputDropdown->setBounds(m_outputRowRect);
    y += m_outputRowRect.height + 4.0f;
    // "Routing →" is a quiet button at the end of the Output heading.
    m_linkRect = NUIRect{m_outputHeadRect.right() - 74.0f, m_outputHeadRect.y + 1.0f, 74.0f, kHeadH - 4.0f};

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
        // Adding is a quiet row each, as "Add an effect" is.
        m_addFxRect = NUIRect{x, y, w, ROW_H};
        y += ROW_H;
        m_addSidechainRect = NUIRect{x, y, w, ROW_H};
        y += ROW_H + kSectionGap;

        // INPUT
        m_inputHeadRect = NUIRect{x, y, w, kHeadH};
        y += kHeadH;
        m_inputRowRect = NUIRect{x, y, w, IO_DROPDOWN_H + 6.0f};
        if (m_ioInputDropdown) m_ioInputDropdown->setBounds(m_inputRowRect);
        y += m_inputRowRect.height + 6.0f;
        // Monitor: label, then the segment switch.
        m_inputMetaRect = NUIRect{x, y, w, 24.0f};
        {
            const float segX = x + 52.0f;
            const float segW = std::max(40.0f, x + w - segX);
            m_monitorAutoRect = NUIRect{segX + 2.0f, y + 2.0f, segW * 0.5f - 2.0f, 20.0f};
            m_monitorArmedRect = NUIRect{segX + segW * 0.5f, y + 2.0f, segW * 0.5f - 2.0f, 20.0f};
        }
        y += 24.0f + 8.0f;
        m_inputMeterRect = NUIRect{x, y, w, 6.0f};
        y += 6.0f + kSectionGap;
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

    auto& themeManager = NUIThemeManager::getInstance();
    const auto& th = themeManager.getCurrentTheme();
    // Design Spec, "Panels: inspector": a panel resting on the mixer bed, like a strip: flat, rounded, a soft
    // shadow, no outline.
    renderer.fillRect(b, m_bg);
    const NUIRect panel{b.x + 4.0f, b.y + 4.0f, std::max(0.0f, b.width - 8.0f), std::max(0.0f, b.height - 8.0f)};
    Depth::restingPanel(renderer, panel, Depth::Radius::medium, th.mixerStripBg);

    const auto* channel = m_viewModel ? m_viewModel->getSelectedChannel() : nullptr;
    updateHeaderCache(channel);
    if (channel) rebuildInsertRack(channel); // keeps the rack in step with automation and backend changes
    computeColumn(channel);

    const NUIColor accent = th.primary;
    const NUIColor muted = th.textMuted;

    if (!channel) {
        const float emptyY = m_viewportRect.y + std::max(24.0f, m_viewportRect.height * 0.22f);
        renderer.drawTextCentered("Select a mixer channel",
                                  {b.x + PAD, emptyY, b.width - PAD * 2.0f, 18.0f}, 12.0f, m_textSecondary);
        renderer.drawTextCentered("to see its inserts, sends and input",
                                  {b.x + PAD, emptyY + 18.0f, b.width - PAD * 2.0f, 14.0f}, 10.0f, muted);
        return;
    }
    const bool master = channel->id == 0;

    // --- Pinned head: the channel's name on its colour (as on the strip), where it goes in quiet text ---------------
    {
        NUIColor plate = th.surfaceRaised;
        if (channel->trackColorIndex >= 0) {
            const uint32_t argb = paletteIndexToARGB(channel->trackColorIndex);
            plate = NUIColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, 1.0f);
        }
        const NUIColor ink = channel->trackColorIndex >= 0 ? themeManager.getColor("textOnStatus") : m_text;
        std::string dest = "Output";
        if (!master) {
            const auto* out = m_viewModel->getChannelById(channel->mainOutputId);
            dest = channel->mainOutputId == 0 || !out ? std::string("Master")
                 : (out->name.empty() ? channelFallbackLabel(out->id) : out->name);
        }
        constexpr float kPlateH = 26.0f;
        constexpr float kFs = 12.0f;
        const float toW = renderer.measureText("to", 11.0f).width + 4.0f + renderer.measureText(dest, 11.0f).width;
        const float maxName = std::max(20.0f, m_headerRect.width - toW - 8.0f - 24.0f);
        const std::string name = fitText(renderer, m_cachedHeaderTitle, kFs, maxName);
        const float plateW = renderer.measureText(name, kFs).width + 24.0f;
        const NUIRect pr{m_headerRect.x, m_headerRect.y + (m_headerRect.height - kPlateH) * 0.5f, plateW, kPlateH};
        renderer.fillRoundedRect(pr, kPlateH * 0.5f, plate);
        renderer.drawTextCentered(name, pr, kFs, ink);
        const float ty = pr.y + kPlateH * 0.5f - 11.0f * 0.62f;
        const float tx = pr.right() + 8.0f;
        renderer.drawText("to", {tx, ty}, 11.0f, muted);
        renderer.drawText(fitText(renderer, dest, 11.0f, std::max(0.0f, m_headerRect.right() - tx - 14.0f)),
                          {tx + renderer.measureText("to", 11.0f).width + 4.0f, ty}, 11.0f, m_textSecondary);
    }

    // --- The scrolling column ---------------------------------------------------------------------------
    renderer.setClipRect(m_viewportRect);

    // Section names are labels; space separates sections, not boxes or rules.
    auto heading = [&](const NUIRect& r, const char* label, int count, bool showCount) {
        const float fy = r.y + r.height * 0.5f - 10.5f * 0.62f;
        renderer.drawText(label, {r.x, fy}, 10.5f, m_textSecondary);
        if (showCount) {
            const float lw = renderer.measureText(label, 10.5f).width;
            renderer.drawText(std::to_string(count), {r.x + lw + 6.0f, fy + 0.5f}, 10.0f, muted);
        }
    };
    // A quiet row: muted "+ label", the row lights on hover.
    auto quietRow = [&](const NUIRect& r, const char* label, bool hot) {
        if (hot) renderer.fillRoundedRect(r, 6.0f, th.surfaceTertiary);
        const NUIColor ink = hot ? th.textPrimary : muted;
        const float cy = r.y + r.height * 0.5f;
        const float px = r.x + 14.0f;
        renderer.drawLine({px - 4.5f, cy}, {px + 4.5f, cy}, 1.5f, ink);
        renderer.drawLine({px, cy - 4.5f}, {px, cy + 4.5f}, 1.5f, ink);
        renderer.drawText(label, {r.x + 26.0f, cy - th.fontSizeS * 0.62f}, th.fontSizeS, ink);
    };

    if (!master) heading(m_levelHeadRect, "Level", 0, false);

    // OUTPUT
    heading(m_outputHeadRect, "Output", 0, false);
    if (master) {
        renderer.drawText("Hardware output 1/2", {m_outputRowRect.x + 4.0f, m_outputRowRect.y + 7.0f}, 11.0f, m_text);
    }
    {
        // "Routing →": a quiet button, lit on hover.
        const bool hot = m_linkHovered;
        if (hot) renderer.fillRoundedRect(m_linkRect, m_linkRect.height * 0.5f, th.surfaceTertiary);
        const NUIColor ink = hot ? th.textPrimary : m_textSecondary;
        const float lw = renderer.measureText("Routing", 10.5f).width;
        const float lx = m_linkRect.right() - 10.0f - 12.0f - 4.0f - lw;
        const float cy = m_linkRect.y + m_linkRect.height * 0.5f;
        renderer.drawText("Routing", {lx, cy - 10.5f * 0.62f}, 10.5f, ink);
        const float ax = lx + lw + 4.0f;
        renderer.drawLine({ax, cy}, {ax + 10.0f, cy}, 1.4f, ink);
        renderer.drawLine({ax + 6.5f, cy - 3.5f}, {ax + 10.0f, cy}, 1.4f, ink);
        renderer.drawLine({ax + 6.5f, cy + 3.5f}, {ax + 10.0f, cy}, 1.4f, ink);
    }

    // FED BY
    if (!m_fedChips.empty()) {
        heading(m_fedHeadRect, "Fed by", static_cast<int>(m_fedChips.size()), true);
        for (const auto& chip : m_fedChips) {
            renderer.fillRoundedRect(chip.rect, chip.rect.height * 0.5f, th.surfaceTertiary);
            renderer.fillRoundedRect(NUIRect{chip.rect.x + 7.0f, chip.rect.y + 6.5f, 7.0f, 7.0f}, 3.5f, chip.color);
            renderer.drawText(fitText(renderer, chip.name, 10.0f, chip.rect.width - 24.0f),
                              {chip.rect.x + 18.0f, chip.rect.y + 4.5f}, 10.0f, m_text);
        }
    }

    // INSERTS
    int insertCount = 0;
    for (const auto& ins : channel->inserts) if (!ins.isEmpty && !ins.pendingRemoval) ++insertCount;
    heading(m_insertsHeadRect, "Inserts", insertCount, true);

    // SENDS
    if (!master) {
        heading(m_sendsHeadRect, "Sends", static_cast<int>(channel->sends.size()), true);
        if (!m_warningRect.isEmpty()) {
            renderer.drawText(fitText(renderer, m_viewModel->getRoutingWarning(channel->id), 10.0f, m_warningRect.width),
                              {m_warningRect.x, m_warningRect.y + 2.0f}, 10.0f, themeManager.getColor("warning"));
        }
        // The sends, one line each: destination, level, and how it is tapped.
        for (size_t i = 0; i < m_sendRowRects.size() && i < channel->sends.size(); ++i) {
            const auto& send = channel->sends[i];
            const auto& r = m_sendRowRects[i];
            const bool open = static_cast<int>(i) == m_expandedSend;
            if (open || static_cast<int>(i) == m_hoveredSendRow) {
                renderer.fillRoundedRect(r, 6.0f, open ? th.surfaceRaised : th.surfaceTertiary);
            }
            const float alpha = send.muted ? 0.4f : 1.0f;
            const float cy = r.y + r.height * 0.5f;
            const NUIColor amber = themeManager.getColor("warning");
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
                renderer.fillRoundedRect(NUIRect{r.x + 8.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f, swatch.withAlpha(alpha));
            }
            renderer.drawText(fitText(renderer, send.targetName, 11.0f, 64.0f), {r.x + 22.0f, cy - 6.5f}, 11.0f, m_text.withAlpha(alpha));
            const std::string tag = send.sidechainOnly ? "SC" : (send.postFader ? "Post" : "Pre");
            const float tagW = renderer.measureText(tag, 9.0f).width;
            const float db = send.gain > 1e-6f ? 20.0f * std::log10(send.gain) : -90.0f;
            const std::string dbText = send.sidechainOnly ? std::string() : UIMixerSendList::formatLevel(db);
            const float dbW = renderer.measureText(dbText, 10.0f).width;
            renderer.drawText(tag, {r.right() - 8.0f - tagW, cy - 5.0f}, 9.0f, muted.withAlpha(alpha));
            if (!dbText.empty()) renderer.drawText(dbText, {r.right() - 8.0f - tagW - 6.0f - dbW, cy - 5.5f}, 10.0f, m_textSecondary.withAlpha(alpha));
            const float barL = r.x + 22.0f + 66.0f;
            const float barR = r.right() - 8.0f - tagW - 6.0f - dbW - 8.0f;
            if (barR - barL > 10.0f) {
                // The shared slider's slot, read-only here (click the row to edit the send).
                const float norm = send.sidechainOnly ? 1.0f : MixerScale::norm(db);
                Depth::sliderSlot(renderer, NUIRect{barL, cy - 8.0f, barR - barL, 16.0f}, norm, 0.0f, !send.muted);
            }
        }

        quietRow(m_addFxRect, "Add a send", m_addHovered || m_addPressed);
        quietRow(m_addSidechainRect, "Add a sidechain", m_addSidechainHovered || m_addSidechainPressed);

        // INPUT
        heading(m_inputHeadRect, "Input", 0, false);
        {
            // Monitor: the segment switch (sunk pill track, the choice on a lit thumb, unchosen labels muted).
            const float cy = m_inputMetaRect.y + m_inputMetaRect.height * 0.5f;
            renderer.drawText("Monitor", {m_inputMetaRect.x, cy - 10.5f * 0.62f}, 10.5f, m_textSecondary);
            const NUIRect track{m_monitorAutoRect.x - 2.0f, m_monitorAutoRect.y - 2.0f,
                                m_monitorArmedRect.right() - m_monitorAutoRect.x + 4.0f, m_monitorAutoRect.height + 4.0f};
            Depth::bed(renderer, track, Depth::Radius::pill(track.height), Depth::Cut::Shallow);
            const bool autoOn = channel->monitored;
            const NUIRect& thumb = autoOn ? m_monitorAutoRect : m_monitorArmedRect;
            Depth::object(renderer, thumb, Depth::Radius::pill(thumb.height));
            const char* labels[2] = {"On", "Off"};
            const NUIRect* rects[2] = {&m_monitorAutoRect, &m_monitorArmedRect};
            for (int i = 0; i < 2; ++i) {
                const bool chosen = (i == 0) == autoOn;
                const NUIColor ink = chosen ? th.textPrimary : (m_hoveredMonitor == i ? m_textSecondary : muted);
                renderer.drawTextCentered(fitText(renderer, labels[i], 10.0f, rects[i]->width - 8.0f), *rects[i], 10.0f, ink);
            }
        }
        // The input level: a slim sunk meter.
        Depth::bed(renderer, m_inputMeterRect, 3.0f, Depth::Cut::Slot);
        const float fill = std::clamp(channel->inputPeak, 0.0f, 1.0f) * m_inputMeterRect.width;
        if (fill > 1.0f) {
            const NUIColor c = channel->inputPeak >= 0.95f ? themeManager.getColor("error")
                             : channel->inputPeak >= 0.75f ? themeManager.getColor("warning") : themeManager.getColor("meterSafe");
            renderer.fillRoundedRect(NUIRect{m_inputMeterRect.x, m_inputMeterRect.y, fill, m_inputMeterRect.height}, 3.0f, c);
        }
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
        const bool inView = m_viewportRect.contains(event.position);
        const int monHover = !inView ? -1 : m_monitorAutoRect.contains(event.position) ? 0
                           : m_monitorArmedRect.contains(event.position) ? 1 : -1;
        if (monHover != m_hoveredMonitor) { m_hoveredMonitor = monHover; repaint(); }
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
        const bool onAuto = m_monitorAutoRect.contains(event.position);
        if ((onAuto || m_monitorArmedRect.contains(event.position)) && m_viewportRect.contains(event.position) &&
            m_viewModel && m_viewModel->getSelectedChannel()) {
            auto* ch = m_viewModel->getSelectedChannel();
            // Input monitoring, as the track header's Input Monitoring menu sets it (not the arm state).
            if (ch->id != 0 && ch->monitored != onAuto) {
                ch->monitored = onAuto;
                if (onMonitorEdited) onMonitorEdited(ch->id, onAuto);
                repaint();
            }
            return true;
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
