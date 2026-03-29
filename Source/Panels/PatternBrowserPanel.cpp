// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PatternBrowserPanel.h"
#include "TrackManager.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "NUISegmentedControl.h"
#include "NUIButton.h"
#include "NUIIcon.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
// #include "SourceManager.h" // Removed, inside ClipSource.h
#include <cctype>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Aestra {
namespace Audio {

namespace {
std::string toLowerASCII(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

int compareNaturalNames(const std::string& lhs, const std::string& rhs) {
    size_t i = 0;
    size_t j = 0;
    const std::string a = toLowerASCII(lhs);
    const std::string b = toLowerASCII(rhs);

    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
            uint64_t av = 0;
            uint64_t bv = 0;
            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) {
                av = (av * 10) + static_cast<uint64_t>(a[i] - '0');
                ++i;
            }
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) {
                bv = (bv * 10) + static_cast<uint64_t>(b[j] - '0');
                ++j;
            }
            if (av != bv) {
                return (av < bv) ? -1 : 1;
            }
            continue;
        }

        if (a[i] != b[j]) {
            return (a[i] < b[j]) ? -1 : 1;
        }
        ++i;
        ++j;
    }

    if (a.size() == b.size()) {
        return 0;
    }
    return (a.size() < b.size()) ? -1 : 1;
}
} // namespace

PatternBrowserPanel::PatternBrowserPanel(TrackManager* trackManager)
    : m_trackManager(trackManager)
    , m_headerHeight(40.0f) 
    , m_itemHeight(32.0f)
{
    setId("PatternBrowserPanel");
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    // Cache theme colors
    m_backgroundColor = themeManager.getColor("backgroundSecondary");
    m_textColor = themeManager.getColor("textPrimary");
    m_borderColor = themeManager.getColor("border");
    m_selectedColor = themeManager.getColor("primary");
    
    // Initialize Toggle Switch
    m_modeToggle = std::make_shared<AestraUI::NUISegmentedControl>(
        std::vector<std::string>{"Clips", "Patterns"}
    );
    m_modeToggle->setSelectedIndex(static_cast<size_t>(m_mode), false);
    m_modeToggle->setOnSelectionChanged([this](size_t index) {
        switchMode(static_cast<BrowserMode>(index));
    });
    // Toggle bounds set in onResize
    addChild(m_modeToggle);
    
    // Initialize SVG icons
    m_addIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* addSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="4" ry="4"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg>)";
    m_addIcon->loadSVG(addSvg);
    m_addIcon->setIconSize(16, 16);
    m_addIcon->setColor(m_textColor);
    
    m_copyIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* copySvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>)";
    m_copyIcon->loadSVG(copySvg);
    m_copyIcon->setIconSize(16, 16);
    m_copyIcon->setColor(m_textColor);
    
    m_trashIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* trashSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>)";
    m_trashIcon->loadSVG(trashSvg);
    m_trashIcon->setIconSize(16, 16);
    m_trashIcon->setColor(themeManager.getColor("error").withAlpha(0.9f));
    
    m_midiIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* midiSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>)";
    m_midiIcon->loadSVG(midiSvg);
    m_midiIcon->setIconSize(16, 16);
    m_midiIcon->setColor(m_selectedColor);
    
    m_audioIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* audioSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12h3l3-6 4 12 4-8 3 4h3"/></svg>)";
    m_audioIcon->loadSVG(audioSvg);
    m_audioIcon->setIconSize(16, 16);
    m_audioIcon->setColor(m_selectedColor);
    
    // Create icon-based buttons
    m_createButton = std::make_shared<AestraUI::NUIButton>("");
    m_createButton->setOnClick([this]() {
        if (m_trackManager) {
            MidiPayload payload;
            auto id = m_trackManager->getPatternManager().createMidiPattern("New Pattern", 4.0, payload);
            refreshPatterns();
            m_selectedPatternId = id;
            if (m_onPatternSelected) m_onPatternSelected(id);
        }
    });
    addChild(m_createButton);
    
    m_duplicateButton = std::make_shared<AestraUI::NUIButton>("");
    m_duplicateButton->setOnClick([this]() {
        if (m_trackManager && m_selectedPatternId.isValid()) {
            auto id = m_trackManager->getPatternManager().clonePattern(m_selectedPatternId);
            refreshPatterns();
            m_selectedPatternId = id;
            if (m_onPatternSelected) m_onPatternSelected(id);
        }
    });
    addChild(m_duplicateButton);
    
    m_deleteButton = std::make_shared<AestraUI::NUIButton>("");
    m_deleteButton->setOnClick([this]() {
        if (m_trackManager && m_selectedPatternId.isValid()) {
             // Safety Check: Is it used?
            if (m_trackManager->getPlaylistModel().isPatternUsed(m_selectedPatternId)) {
                Aestra::Log::warning("Cannot delete pattern: Is currently used on timeline");
                return;
            }

            m_trackManager->getPatternManager().removePattern(m_selectedPatternId);
            m_selectedPatternId = PatternID();
            refreshPatterns();
        }
    });
    addChild(m_deleteButton);
    
    // Make toolbar buttons transparent
    AestraUI::NUIColor transparent(0, 0, 0, 0);
    
    auto styleButton = [&](std::shared_ptr<AestraUI::NUIButton> btn) {
        btn->setStyle(AestraUI::NUIButton::Style::Icon);
        btn->setBorderEnabled(false);
        btn->setBackgroundColor(transparent);
        btn->onMouseMove = [this](const AestraUI::NUIMouseEvent&) { repaint(); };
    };

    styleButton(m_createButton);
    styleButton(m_duplicateButton);
    styleButton(m_deleteButton);
    
    refreshPatterns();
    refreshClips();
    
    // Initial state setup
    switchMode(BrowserMode::Clips);
}

PatternBrowserPanel::~PatternBrowserPanel() {
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

void PatternBrowserPanel::refreshPatterns() {
    m_patterns.clear();
    if (!m_trackManager) return;
    
    auto allPatterns = m_trackManager->getPatternManager().getAllPatterns();
    for (const auto& p : allPatterns) {
        if (!p || !p->isMidi()) {
            continue;
        }
        PatternEntry entry;
        entry.id = p->id;
        entry.name = p->name;
        entry.isMidi = p->isMidi();
        entry.lengthBeats = p->lengthBeats;
        entry.mixerChannel = p->getMixerChannel();
        m_patterns.push_back(entry);
    }
    setDirty(true);
}

void PatternBrowserPanel::refreshClips() {
    m_clips.clear();
    if (!m_trackManager) return;

    auto& sourceManager = m_trackManager->getSourceManager();
    std::vector<ClipSourceID> sourceIds = sourceManager.getAllSourceIDs();
    
    for (const auto& id : sourceIds) {
        auto* source = sourceManager.getSource(id);
        if (source) {
            ClipEntry entry;
            entry.id = source->getID();
            entry.name = source->getName();
            entry.filename = source->getFilePath();
            entry.sampleRate = source->getSampleRate();
            entry.numChannels = source->getNumChannels();
            entry.duration = source->getDurationSeconds();
            m_clips.push_back(entry);
        }
    }

    std::sort(m_clips.begin(), m_clips.end(), [](const ClipEntry& lhs, const ClipEntry& rhs) {
        const int nameCmp = compareNaturalNames(lhs.name, rhs.name);
        if (nameCmp != 0) {
            return nameCmp < 0;
        }

        const int fileCmp = compareNaturalNames(lhs.filename, rhs.filename);
        if (fileCmp != 0) {
            return fileCmp < 0;
        }

        return lhs.id.value < rhs.id.value;
    });

    setDirty(true);
}

void PatternBrowserPanel::switchMode(BrowserMode mode) {
    m_mode = mode;
    bool showPatternControls = (m_mode == BrowserMode::Patterns);
    
    if (m_createButton) m_createButton->setVisible(showPatternControls);
    if (m_duplicateButton) m_duplicateButton->setVisible(showPatternControls);
    if (m_deleteButton) m_deleteButton->setVisible(showPatternControls);
    
    // Refresh content for new mode
    if (m_mode == BrowserMode::Patterns) refreshPatterns();
    else refreshClips();
    
    // Force layout update next resize
    int w = static_cast<int>(getBounds().width);
    int h = static_cast<int>(getBounds().height);
    if (w > 0 && h > 0) onResize(w, h); // Manually trigger layout update
    
    setDirty(true);
}

void PatternBrowserPanel::onRender(AestraUI::NUIRenderer& renderer) {
    if (!isVisible()) return;

    auto bounds = getBounds();
    
    // Background
    renderer.fillRect(bounds, m_backgroundColor);
    
    // Border (right side)
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x + bounds.width - 1, bounds.y),
        AestraUI::NUIPoint(bounds.x + bounds.width - 1, bounds.y + bounds.height),
        1.0f, m_borderColor
    );
    
    // Drag Over Feedback
    if (m_isDragOver) {
        renderer.fillRoundedRect(bounds, 0.0f, m_selectedColor.withAlpha(0.1f));
        renderer.strokeRect(bounds, 2.0f, m_selectedColor);
    }

    renderHeader(renderer);
    renderContent(renderer);
    
    AestraUI::NUIComponent::onRender(renderer);
}

void PatternBrowserPanel::renderHeader(AestraUI::NUIRenderer& renderer) {
    auto bounds = getBounds();
    AestraUI::NUIRect headerRect(bounds.x, bounds.y, bounds.width, m_headerHeight);
    
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, bounds.y + m_headerHeight),
        AestraUI::NUIPoint(bounds.x + bounds.width, bounds.y + m_headerHeight),
        1.0f, m_borderColor
    );

    // Render footer background and separator
    AestraUI::NUIRect footerRect(bounds.x, bounds.bottom() - m_footerHeight, bounds.width, m_footerHeight);
    renderer.fillRect(footerRect, m_backgroundColor);
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, footerRect.y),
        AestraUI::NUIPoint(bounds.x + bounds.width, footerRect.y),
        1.0f, m_borderColor
    );
    
    // Mode toggle is rendered by addChild mechanism automatically
    // The buttons are also rendered by addChild mechanism
    
    // If in Patterns mode, render button icons manually for crisp SVG
    if (m_mode == BrowserMode::Patterns) {
        // We need to render the icons at the positions of the invisible buttons
        if (m_createButton->isVisible()) {
            auto btnBounds = m_createButton->getBounds();
            m_addIcon->setBounds(AestraUI::NUIRect(btnBounds.center().x - 8, btnBounds.center().y - 8, 16, 16));
            m_addIcon->onRender(renderer);
        }
        if (m_duplicateButton->isVisible()) {
            auto btnBounds = m_duplicateButton->getBounds();
            m_copyIcon->setBounds(AestraUI::NUIRect(btnBounds.center().x - 8, btnBounds.center().y - 8, 16, 16));
            m_copyIcon->onRender(renderer);
        }
        if (m_deleteButton->isVisible()) {
            auto btnBounds = m_deleteButton->getBounds();
            m_trashIcon->setBounds(AestraUI::NUIRect(btnBounds.center().x - 8, btnBounds.center().y - 8, 16, 16));
            m_trashIcon->onRender(renderer);
        }
    }
}

void PatternBrowserPanel::renderContent(AestraUI::NUIRenderer& renderer) {
    auto bounds = getBounds();
    AestraUI::NUIRect listRect(bounds.x, bounds.y + m_headerHeight, bounds.width, bounds.height - m_headerHeight - m_footerHeight);
    
    // Check if empty
    bool isEmpty = (m_mode == BrowserMode::Patterns) ? m_patterns.empty() : m_clips.empty();
    
    if (isEmpty) {
        std::string emptyText = (m_mode == BrowserMode::Patterns) 
            ? "No patterns" 
            : "No clips loaded\nDrop files here";
        renderer.drawTextCentered(emptyText, listRect, 12.0f, m_textColor.withAlpha(0.5f));
        return;
    }
    
    // Render list
    renderer.setClipRect(listRect);
    
    if (m_mode == BrowserMode::Patterns) {
        renderPatternList(renderer);
    } else {
        renderClipList(renderer);
    }
    
    renderer.clearClipRect();
}

void PatternBrowserPanel::renderPatternList(AestraUI::NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + m_headerHeight - m_scrollOffset;
    
    for (const auto& entry : m_patterns) {
        if (y + m_itemHeight < bounds.y + m_headerHeight) { y += m_itemHeight; continue; } 
        if (y > bounds.y + bounds.height) break;
        
        bool selected = (entry.id == m_selectedPatternId);
        bool hovered = (entry.id == m_hoveredPatternId);
        
        renderPatternItem(renderer, entry, y, selected, hovered);
        y += m_itemHeight;
    }
}

void PatternBrowserPanel::renderClipList(AestraUI::NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + m_headerHeight - m_scrollOffset;
    
    for (const auto& entry : m_clips) {
        if (y + m_itemHeight < bounds.y + m_headerHeight) { y += m_itemHeight; continue; }
        if (y > bounds.y + bounds.height) break;
        
        const bool selected = (entry.id == m_selectedClipId);
        const bool hovered = (entry.id == m_hoveredClipId);
        renderClipItem(renderer, entry, y, selected, hovered);
        y += m_itemHeight;
    }
}

void PatternBrowserPanel::renderPatternItem(AestraUI::NUIRenderer& renderer, const PatternEntry& entry, float y, bool selected, bool hovered) {
    auto bounds = getBounds();
    AestraUI::NUIRect itemRect(bounds.x, y, bounds.width, m_itemHeight);
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    
    if (selected) {
        renderer.fillRect(itemRect, m_selectedColor.withAlpha(0.2f));
        renderer.fillRect(AestraUI::NUIRect(bounds.x, y, 2, m_itemHeight), m_selectedColor);
    } else if (hovered) {
        // Hover style
        renderer.fillRoundedRect(itemRect, 4, theme.getColor("hover").withAlpha(0.1f));
    }
    
    // Type icon using NUIIcon
    float iconX = itemRect.x + 8; // Standard indent
    float iconY = y + (m_itemHeight - 16) / 2;
    
    // Ensure icons use theme colors (white/secondary) unless selected
    AestraUI::NUIColor iconColor = selected ? theme.getColor("primary") : theme.getColor("textSecondary");
    m_midiIcon->setColor(iconColor);
    m_audioIcon->setColor(iconColor);

    if (entry.isMidi) {
        m_midiIcon->setBounds(AestraUI::NUIRect(iconX, iconY, 16, 16));
        m_midiIcon->onRender(renderer);
    } else {
        m_audioIcon->setBounds(AestraUI::NUIRect(iconX, iconY, 16, 16));
        m_audioIcon->onRender(renderer);
    }
    
    // Name (12px standard font)
    AestraUI::NUIColor textColor = selected ? theme.getColor("textPrimary") : theme.getColor("textSecondary");
    renderer.drawText(entry.name, AestraUI::NUIPoint(itemRect.x + 32, y + 9), 
                     12.0f, textColor); // Vertical center (32-12)/2 approx
    
    // Mixer routing indicator (if custom routing set)
    if (entry.mixerChannel >= 0) {
        std::string routeStr = ">" + std::to_string(entry.mixerChannel + 1);  // 1-based for display
        float routeX = itemRect.x + itemRect.width - 60;
        renderer.drawText(routeStr, AestraUI::NUIPoint(routeX, y + 9), 
                         11.0f, theme.getColor("accentCyan"));  // Use theme accent
    }
    
    // Length (right aligned)
    std::string lengthStr = std::to_string(static_cast<int>(entry.lengthBeats)) + "b";
    renderer.drawText(lengthStr, AestraUI::NUIPoint(itemRect.x + itemRect.width - 25, y + 9), 
                     11.0f, theme.getColor("textDisabled"));
}

bool PatternBrowserPanel::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    auto b = getBounds();
    auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
    
    // 1. Handle active drag updates
    // 1. Handle active drag updates - DELEGATED TO GLOBAL MAIN LOOP
    // We only need to return true if dragging to prevent other interactions
    if (dragManager.isDragging()) {
        return true; 
    }
    
    // Check if in list area
    // Robust check: Compare event Y relative to panel Y
    float relativeY = event.position.y - b.y;
    bool inListArea = relativeY > m_headerHeight && relativeY < (b.height - m_footerHeight);
    
    // Ensure we are horizontally within bounds too
    if (event.position.x < b.x || event.position.x > b.x + b.width) {
        inListArea = false; 
    }
    
    if (inListArea) {
        // Find hovered item
        float listScrollY = relativeY - m_headerHeight + m_scrollOffset;
        int itemIndex = static_cast<int>(listScrollY / m_itemHeight);
        if (m_mode == BrowserMode::Patterns) {
            if (itemIndex >= 0 && itemIndex < static_cast<int>(m_patterns.size())) {
                m_hoveredPatternId = m_patterns[itemIndex].id;

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                    auto now = std::chrono::steady_clock::now();
                    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                    bool isDoubleClick = (m_lastClickedPatternId == m_patterns[itemIndex].id) &&
                                         (currentTime - m_lastClickTime < 0.4);

                    m_selectedPatternId = m_patterns[itemIndex].id;
                    if (m_onPatternSelected) m_onPatternSelected(m_selectedPatternId);

                    if (isDoubleClick) {
                        if (m_onPatternDoubleClick) m_onPatternDoubleClick(m_selectedPatternId);
                        m_dragPotential = false;
                    } else {
                        m_dragPotential = true;
                        m_dragStartPos = event.position;
                        m_dragPatternId = m_selectedPatternId;
                        m_dragClipId = ClipSourceID{};
                    }

                    m_lastClickTime = currentTime;
                    m_lastClickedPatternId = m_patterns[itemIndex].id;

                    repaint();
                    return true;
                }
            } else {
                m_hoveredPatternId = PatternID();
            }

            if (m_dragPotential && itemIndex >= 0 && itemIndex < static_cast<int>(m_patterns.size())) {
                float dx = event.position.x - m_dragStartPos.x;
                float dy = event.position.y - m_dragStartPos.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist >= dragManager.getDragThreshold()) {
                    AestraUI::DragData dragData;
                    dragData.type = AestraUI::DragDataType::Pattern;

                    for (const auto& p : m_patterns) {
                        if (p.id == m_dragPatternId) {
                            dragData.displayName = p.name;
                            break;
                        }
                    }

                    dragData.customData = m_dragPatternId;
                    dragData.previewWidth = 120.0f;
                    dragData.previewHeight = m_itemHeight;
                    dragData.accentColor = m_selectedColor;

                    dragManager.beginDrag(dragData, m_dragStartPos, this);
                    m_isDragging = true;
                    m_dragPotential = false;

                    if (m_onPatternDragStart) m_onPatternDragStart(m_dragPatternId);

                    return true;
                }
            }
        } else {
            if (itemIndex >= 0 && itemIndex < static_cast<int>(m_clips.size())) {
                m_hoveredClipId = m_clips[itemIndex].id;

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                    auto now = std::chrono::steady_clock::now();
                    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                    bool isDoubleClick = (m_lastClickedClipId == m_clips[itemIndex].id) &&
                                         (currentTime - m_lastClickTime < 0.4);

                    m_selectedClipId = m_clips[itemIndex].id;
                    m_dragPotential = !isDoubleClick;
                    m_dragStartPos = event.position;
                    m_dragClipId = m_selectedClipId;
                    m_dragPatternId = PatternID{};
                    m_lastClickTime = currentTime;
                    m_lastClickedClipId = m_clips[itemIndex].id;
                    repaint();
                    return true;
                }
            } else {
                m_hoveredClipId = ClipSourceID{};
            }

            if (m_dragPotential && itemIndex >= 0 && itemIndex < static_cast<int>(m_clips.size())) {
                float dx = event.position.x - m_dragStartPos.x;
                float dy = event.position.y - m_dragStartPos.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist >= dragManager.getDragThreshold()) {
                    const auto& clip = m_clips[itemIndex];
                    AestraUI::DragData dragData;
                    dragData.type = AestraUI::DragDataType::File;
                    dragData.filePath = clip.filename;
                    dragData.displayName = clip.name;
                    dragData.customData = clip.id;
                    dragData.previewWidth = 140.0f;
                    dragData.previewHeight = m_itemHeight;
                    dragData.accentColor = m_selectedColor;

                    dragManager.beginDrag(dragData, m_dragStartPos, this);
                    m_isDragging = true;
                    m_dragPotential = false;

                    if (m_onClipDragStart) m_onClipDragStart(clip.id);

                    return true;
                }
            }
        }
    }
    
    // Mouse Wheel - Scroll Handling
    if (std::abs(event.wheelDelta) > 0.001f && getBounds().contains(event.position)) {
        float listHeight = (m_mode == BrowserMode::Patterns) ? m_patterns.size() * m_itemHeight : m_clips.size() * m_itemHeight;
        float viewportHeight = b.height - m_headerHeight - m_footerHeight;
        float maxScroll = std::max(0.0f, listHeight - viewportHeight);
        
        m_scrollOffset -= event.wheelDelta * 40.0f;
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, maxScroll);
        
        repaint();
        return true;
    }

    // Mouse Release
    if (!event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        m_dragPotential = false;
    }
    
    repaint();
    return NUIComponent::onMouseEvent(event);
}

void PatternBrowserPanel::renderClipItem(AestraUI::NUIRenderer& renderer, const ClipEntry& entry, float y, bool selected, bool hovered) {
    auto bounds = getBounds();
    AestraUI::NUIRect itemRect(bounds.x, y, bounds.width, m_itemHeight);
    auto& theme = AestraUI::NUIThemeManager::getInstance();

    if (selected) {
        renderer.fillRect(itemRect, m_selectedColor.withAlpha(0.2f));
        renderer.fillRect(AestraUI::NUIRect(bounds.x, y, 2, m_itemHeight), m_selectedColor);
    } else if (hovered) {
        // Use darker hover for contrast
        renderer.fillRoundedRect(itemRect, 4, theme.getColor("hover").withAlpha(0.1f));
    }

    // Icon
    float iconX = itemRect.x + 8;
    float iconY = y + (m_itemHeight - 16) / 2;
    if (m_audioIcon) {
        m_audioIcon->setBounds(AestraUI::NUIRect(iconX, iconY, 16, 16));
        m_audioIcon->setColor(theme.getColor("textSecondary"));
        m_audioIcon->onRender(renderer);
    }

    // Name truncation logic
    std::string displayName = entry.name;
    float maxNameWidth = itemRect.width - 32 - 50; // icon + padding + duration
    if (displayName.length() > 25) { // Simple char count heuristic for now
        displayName = displayName.substr(0, 22) + "...";
    }

    renderer.drawText(displayName,
                     AestraUI::NUIPoint(itemRect.x + 32, y + 9),
                     12.0f,
                     selected ? theme.getColor("textPrimary") : theme.getColor("textSecondary"));
    
    // Duration
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << entry.duration << "s";
    std::string durStr = ss.str();
    renderer.drawText(durStr, AestraUI::NUIPoint(itemRect.x + itemRect.width - 40, y + 9), 11.0f, theme.getColor("textDisabled"));
}

void PatternBrowserPanel::onResize(int width, int height) {
    auto bounds = getBounds();
    float padding = 8.0f;
    float toggleWidth = 140.0f;
    
    if (m_modeToggle) {
        float toggleY = height - m_footerHeight + (m_footerHeight - 24) / 2.0f;
        m_modeToggle->setBounds(AestraUI::NUIAbsolute(bounds, padding, toggleY, toggleWidth, 24));
    }
    
    // Layout buttons (right aligned in header)
    float btnSize = 24.0f;
    float x_offset = width - padding - btnSize;
    
    if (m_deleteButton) m_deleteButton->setBounds(AestraUI::NUIAbsolute(bounds, x_offset, padding + 4, btnSize, btnSize));
    x_offset -= (btnSize + 4);
    if (m_duplicateButton) m_duplicateButton->setBounds(AestraUI::NUIAbsolute(bounds, x_offset, padding + 4, btnSize, btnSize));
    x_offset -= (btnSize + 4);
    if (m_createButton) m_createButton->setBounds(AestraUI::NUIAbsolute(bounds, x_offset, padding + 4, btnSize, btnSize));
}

void PatternBrowserPanel::onUpdate(double deltaTime) {
    if (m_modeToggle) m_modeToggle->onUpdate(deltaTime);
}

// IDropTarget Implementation
AestraUI::DropFeedback PatternBrowserPanel::onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    if (data.type == AestraUI::DragDataType::File) {
        m_isDragOver = true;
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::None;
}

AestraUI::DropFeedback PatternBrowserPanel::onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    if (data.type == AestraUI::DragDataType::File) return AestraUI::DropFeedback::Copy;
    return AestraUI::DropFeedback::None;
}

void PatternBrowserPanel::onDragLeave() {
    m_isDragOver = false;
}

AestraUI::DropResult PatternBrowserPanel::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
     m_isDragOver = false;
    if (data.type == AestraUI::DragDataType::File && !data.filePath.empty()) {
        if (m_trackManager) {
            auto& sourceManager = m_trackManager->getSourceManager();
            sourceManager.getOrCreateSource(data.filePath);
            
            // Switch to clips and refresh
            if (m_modeToggle) m_modeToggle->setSelectedIndex(0); // Clips is 0
            refreshClips();
            AestraUI::DropResult result;
            result.accepted = true;
            return result;
        }
    }
    return AestraUI::DropResult(); // Default accepted=false
}

AestraUI::NUIRect PatternBrowserPanel::getDropBounds() const {
    return getBounds();
}

} // namespace Audio
} // namespace Aestra
