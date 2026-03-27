// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerInspector.h"

#include "NUIThemeSystem.h"
#include "../../AestraCore/include/AestraLog.h"
#include "NUIRenderer.h"
#include "NUIMixerWidgets.h"
#include "PluginBrowserPanel.h"
#include "MixerViewModel.h"

#include <algorithm>
#include <cstdio>

namespace AestraUI {

namespace {
    constexpr float PAD = 10.0f;
    constexpr float TAB_H = 26.0f;
    constexpr float TAB_RADIUS = 12.0f;
    constexpr float SECTION_GAP = 10.0f;
    constexpr float HEADER_H = 44.0f;
    constexpr float ROW_H = 26.0f;
    constexpr float ROW_RADIUS = 12.0f;
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
}

void UIMixerInspector::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("backgroundPrimary");
    m_border = theme.getColor("borderSubtle").withAlpha(0.65f);
    m_text = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_tabBg = theme.getColor("surfaceTertiary");
    m_tabActive = theme.getColor("accentPrimary").withAlpha(0.22f);
    m_tabHover = theme.getColor("surfaceSecondary");
    m_addBg = theme.getColor("surfaceTertiary");
    m_addHover = theme.getColor("surfaceSecondary");
    m_addText = theme.getColor("textPrimary");
}

void UIMixerInspector::setActiveTab(Tab tab)
{
    if (m_activeTab == tab) return;
    m_activeTab = tab;

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

    const float contentY = y + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    if (m_activeTab == Tab::Sends) {
        m_addFxRect = NUIRect{x, contentY + 18.0f, w, ROW_H};
    } else {
        m_addFxRect = NUIRect{0,0,0,0};
    }

    if (m_ioInputDropdown) {
        // Label takes approx 20px height, so position dropdown below it
        float currentY = contentY + 20.0f;
        m_ioInputDropdown->setBounds(x, currentY, w, 22.0f);
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
        // Reduced padding from 18.0f to 12.0f to extend hit test area upwards
        const float topPad = 12.0f;
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
        (channel ? (m_cachedName == channel->name && m_cachedRoute == channel->routeName) : (m_cachedName.empty() && m_cachedRoute.empty()));
    if (identityUnchanged) return;

    m_cachedSelectedId = selectedId;
    m_cachedHeaderTitle.clear();
    m_cachedHeaderSubtitle.clear();
    m_cachedTrackNumber = 0;
    m_cachedName = channel ? channel->name : std::string();
    m_cachedRoute = channel ? channel->routeName : std::string();

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
    if (m_cachedTrackNumber > 0) {
        m_cachedHeaderTitle = "Track " + std::to_string(m_cachedTrackNumber) + " — " + channel->name;
    } else {
        m_cachedHeaderTitle = channel->name;
    }

    // Track type is currently audio-only.
    if (!channel->routeName.empty()) {
        m_cachedHeaderSubtitle = "Audio → " + channel->routeName;
    } else {
        m_cachedHeaderSubtitle = "Audio";
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
        
        widget->setSendIndex(static_cast<int>(i));
        widget->setLevel(sendData.gain);

        // Bind Callbacks
        uint32_t cid = channel->id;
        int sIdx = static_cast<int>(i);

        widget->setOnLevelChanged([this, cid, sIdx](float val) {
            if (m_viewModel) m_viewModel->setSendLevel(cid, sIdx, val);
        });

        widget->setOnDestinationChanged([this, cid, sIdx](uint32_t dest) {
            if (m_viewModel) m_viewModel->setSendDestination(cid, sIdx, dest);
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
    
    // Continuous sync for knobs to reflect automation/backend changes
    if (m_activeTab == Tab::Inserts && channel) {
        rebuildInsertRack(channel);
    }

    // Tabs
    static constexpr const char* tabLabels[3] = {"Inserts", "Sends", "I/O"};
    for (int i = 0; i < 3; ++i) {
        const bool active = (static_cast<int>(m_activeTab) == i);
        const bool hovered = (m_hoveredTab == i);
        NUIColor bg = active ? m_tabActive : (hovered ? m_tabHover : m_tabBg);
        renderer.fillRoundedRect(m_tabRects[i], TAB_RADIUS, bg);
        renderer.strokeRoundedRect(m_tabRects[i], TAB_RADIUS, 1.0f, m_border);
        renderer.drawTextCentered(tabLabels[i], m_tabRects[i], 10.0f, active ? m_text : m_textSecondary);
    }

    // Header
    const float headerY = b.y + PAD + TAB_H + SECTION_GAP;
    const NUIRect headerRect{b.x + PAD, headerY, b.width - PAD * 2.0f, HEADER_H};

    renderer.drawText(m_cachedHeaderTitle, {headerRect.x, headerRect.y}, 12.0f, m_text);
    if (!m_cachedHeaderSubtitle.empty()) {
        renderer.drawText(m_cachedHeaderSubtitle, {headerRect.x, headerRect.y + 16.0f}, 10.0f, m_textSecondary);
    }
    if (channel) {
        renderer.drawText("Input → Trim → Inserts → Sends → Fader → Output",
                          {headerRect.x, headerRect.y + 32.0f}, 9.0f, m_textSecondary.withAlpha(0.85f));
    }

    const float contentTop = b.y + PAD + TAB_H + SECTION_GAP + HEADER_H + SECTION_GAP;
    const NUIRect contentRect{b.x + PAD, contentTop, b.width - PAD * 2.0f, b.height - (contentTop - b.y) - PAD};

    // Content
    if (!channel) {
        renderer.drawTextCentered("Select a track to edit Inserts, Sends, and I/O", contentRect, 11.0f, m_textSecondary);
        return;
    }

    if (m_activeTab == Tab::Inserts) {
        const int fxCount = channel->fxCount;
        char buf[64];
        if (fxCount <= 0) {
            std::snprintf(buf, sizeof(buf), "No inserts");
        } else {
            std::snprintf(buf, sizeof(buf), "%d insert%s active", fxCount, fxCount == 1 ? "" : "s");
        }
        renderer.drawText(buf, {contentRect.x, contentRect.y}, 11.0f, m_textSecondary);

        // Rack is rendered by renderChildren() if visible
    } else if (m_activeTab == Tab::Sends) {
        // Sends Tab
        const int sendCount = static_cast<int>(m_sendWidgets.size());
        
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Sends: %d", sendCount);
        renderer.drawText(buf, {contentRect.x, contentRect.y}, 11.0f, m_textSecondary);

        float currentY = contentRect.y + 20.0f;
        const float sendH = 26.0f;
        const float gap = 4.0f;

        for (auto& widget : m_sendWidgets) {
            widget->setVisible(true);
            widget->setBounds({contentRect.x, currentY, contentRect.width, sendH});
            currentY += sendH + gap;
        }

        // Hide unused widgets (if any logic issue, though we rebuild)
        // Note: rebuild clears them, so we are good.

        // "Add Send" button
        m_addFxRect = NUIRect{contentRect.x, currentY + 4.0f, contentRect.width, ROW_H};
        
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
             renderer.drawText("Audio Input:", {contentRect.x, contentRect.y}, 11.0f, m_textSecondary);
             
             // Dropdown renders itself via renderChildren
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
    
    NUIComponent::onUpdate(deltaTime);
    
    // Sync I/O Dropdown
    if (m_activeTab == Tab::IO && m_ioInputDropdown && m_viewModel) {
        auto* ch = m_viewModel->getSelectedChannel();
        if (ch && ch->id != 0) { // Not for Master
             m_ioInputDropdown->setVisible(true);
             
             // Update Items using device IDs
             const auto& inputs = m_viewModel->inputNames;
             const auto& deviceIds = m_viewModel->inputDeviceIds;
             
             if (m_ioInputDropdown->getItemCount() != inputs.size()) {
                 m_ioInputDropdown->clearItems();
                 for (size_t i = 0; i < inputs.size(); ++i) {
                     // Use actual device ID as the value
                     int deviceId = (i < deviceIds.size()) ? deviceIds[i] : -1;
                     m_ioInputDropdown->addItem(inputs[i], deviceId);
                 }
             }
             
             // Sync Selection
             int currentDeviceId = ch->inputChannelIndex;
             // Find item index with matching device ID
             int targetIndex = 0; // Default to "None"
             for (size_t i = 0; i < deviceIds.size(); ++i) {
                 if (deviceIds[i] == currentDeviceId) {
                     targetIndex = static_cast<int>(i);
                     break;
                 }
             }
             
             if (targetIndex >= 0 && targetIndex < (int)m_ioInputDropdown->getItemCount()) {
                 if (m_ioInputDropdown->getSelectedIndex() != targetIndex) {
                      m_ioInputDropdown->setSelectedIndex(targetIndex);
                 }
             }
        } else {
             m_ioInputDropdown->setVisible(false);
        }
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
        const int tab = hitTestTab(event.position);
        if (tab != m_hoveredTab) {
            m_hoveredTab = tab;
            repaint();
        }

        const bool addHover = (m_viewModel && m_viewModel->getSelectedChannel()) && m_addFxRect.contains(event.position);
        if (addHover != m_addHovered) {
            m_addHovered = addHover;
            repaint();
        }
        // Consume hover if inside bounds to prevent hover-through to components behind
        return b.contains(event.position);
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int tab = hitTestTab(event.position);
        if (tab >= 0) {
            setActiveTab(static_cast<Tab>(tab));
            return true;
        }

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
