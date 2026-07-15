// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PatternBrowserPanel.h"
#include "TrackManager.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "NUISegmentedControl.h"
#include "NUIButton.h"
#include "NUIIcon.h"
#include "NUIContextMenu.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include <cctype>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Aestra {
namespace Audio {

namespace {
float safeClampBrowserScroll(float value, float upper) {
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) return 0.0f;
    if (value >= upper) return upper;
    return value;
}
}

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

namespace {
AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

void detachContextMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

void attachAndShowContextMenu(AestraUI::NUIComponent* owner,
                              const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                              const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

constexpr float kCardInsetX = 4.0f;
constexpr float kCardInsetY = 3.0f;
constexpr float kCardRadius = 8.0f;
constexpr float kAccentStripWidth = 6.0f;

AestraUI::NUIRect computeBinCardRect(const AestraUI::NUIRect& bounds, float y, float itemHeight) {
    return AestraUI::NUIRect(bounds.x + kCardInsetX, y + kCardInsetY,
                             std::max(0.0f, bounds.width - (kCardInsetX * 2.0f)),
                             std::max(0.0f, itemHeight - (kCardInsetY * 2.0f)));
}

AestraUI::NUIRect computePlayButtonRect(const AestraUI::NUIRect& cardRect) {
    const float buttonSize = 16.0f;
    return AestraUI::NUIRect(cardRect.right() - 36.0f, cardRect.center().y - (buttonSize * 0.5f), buttonSize, buttonSize);
}
} // namespace

PatternBrowserPanel::PatternBrowserPanel(TrackManager* trackManager)
    : m_trackManager(trackManager), m_headerHeight(40.0f), m_itemHeight(38.0f) {
    setId("PatternBrowserPanel");
    
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = themeManager.getCurrentTheme();

    // Cache theme colors
    m_backgroundColor = themeManager.getColor("backgroundSecondary");
    m_textColor = themeManager.getColor("textPrimary");
    m_borderColor = themeManager.getColor("borderSubtle").withAlpha(0.075f);
    m_selectedColor = themeManager.getColor("accentPrimary");

    // Initialize Toggle Switch
    m_modeToggle = std::make_shared<AestraUI::NUISegmentedControl>(
        std::vector<std::string>{"Clips", "Patterns"}
    );
    m_modeToggle->setCornerRadius(themeProps.radiusM + 2.0f);
    m_modeToggle->setAccentColor(themeManager.getColor("accentPrimary"));
    m_modeToggle->setSelectedIndex(static_cast<size_t>(m_mode), false);
    m_modeToggle->setVisible(false);
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

    m_playIcon = std::make_shared<AestraUI::NUIIcon>();
    const char* playSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>)";
    m_playIcon->loadSVG(playSvg);
    m_playIcon->setIconSize(10, 10);
    m_playIcon->setColor(themeManager.getColor("textPrimary").withAlpha(0.92f));
    
    // Create icon-based buttons
    m_createButton = std::make_shared<AestraUI::NUIButton>("");
    m_createButton->setOnClick([this]() {
        if (m_trackManager) {
            MidiPayload payload;
            auto id = m_trackManager->getPatternManager().createMidiPattern("New Pattern", 8.0, payload);
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
    detachContextMenu(m_clipContextMenu);
    m_clipContextMenu = nullptr;
    detachContextMenu(m_patternContextMenu);
    m_patternContextMenu = nullptr;
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
    m_dropTargetRegistered = false;
}

void PatternBrowserPanel::refreshPatterns() {
    m_patterns.clear();
    if (!m_trackManager) {
        rebuildFilteredItems();
        setDirty(true);
        return;
    }

    auto& playlistModel = m_trackManager->getPlaylistModel();
    auto allPatterns = m_trackManager->getPatternManager().getAllPatterns();
    for (const auto& p : allPatterns) {
        if (!p || !p->isMidi()) {
            continue;
        }
        if (m_removedPatternIds.find(p->id.value) != m_removedPatternIds.end()) {
            continue;
        }
        PatternEntry entry;
        entry.id = p->id;
        entry.name = p->name;
        entry.isMidi = p->isMidi();
        entry.lengthBeats = p->lengthBeats;
        entry.mixerChannel = p->getMixerChannel();
        entry.isPlacedOnTimeline = playlistModel.isPatternUsed(entry.id);
        m_patterns.push_back(entry);
    }

    std::sort(m_patterns.begin(), m_patterns.end(), [](const PatternEntry& lhs, const PatternEntry& rhs) {
        const int nameCmp = compareNaturalNames(lhs.name, rhs.name);
        if (nameCmp != 0) {
            return nameCmp < 0;
        }
        return lhs.id.value < rhs.id.value;
    });

    rebuildFilteredItems();
    setDirty(true);
}

void PatternBrowserPanel::setSelectedPatternId(Aestra::Audio::PatternID patternId, bool notify) {
    m_selectedPatternId = patternId;
    setDirty(true);
    if (notify && m_onPatternSelected && patternId.isValid()) {
        m_onPatternSelected(patternId);
    }
}

bool PatternBrowserPanel::isClipBinEmpty() const {
    return m_clips.empty();
}

bool PatternBrowserPanel::usesCompactRail() const {
    return m_mode == BrowserMode::Clips && m_clips.empty();
}

void PatternBrowserPanel::showPatternsTab() {
    switchMode(BrowserMode::Patterns);
}

void PatternBrowserPanel::showClipsTab() {
    switchMode(BrowserMode::Clips);
}

void PatternBrowserPanel::setSearchQuery(const std::string& query) {
    if (m_searchQuery == query) {
        return;
    }
    m_searchQuery = query;
    m_scrollOffset = 0.0f;
    m_targetScrollOffset = 0.0f;
    rebuildFilteredItems();
    setDirty(true);
}

void PatternBrowserPanel::rebuildFilteredItems() {
    m_filteredPatternIndices.clear();
    m_filteredClipIndices.clear();
    m_filteredPatternIndices.reserve(m_patterns.size());
    m_filteredClipIndices.reserve(m_clips.size());
    const std::string query = toLowerASCII(m_searchQuery);
    for (size_t i = 0; i < m_patterns.size(); ++i) {
        if (query.empty() || toLowerASCII(m_patterns[i].name).find(query) != std::string::npos) {
            m_filteredPatternIndices.push_back(i);
        }
    }
    for (size_t i = 0; i < m_clips.size(); ++i) {
        const auto& clip = m_clips[i];
        if (query.empty() || toLowerASCII(clip.name).find(query) != std::string::npos ||
            toLowerASCII(clip.filename).find(query) != std::string::npos) {
            m_filteredClipIndices.push_back(i);
        }
    }
}

void PatternBrowserPanel::refreshClips() {
    m_clips.clear();
    if (!m_trackManager) {
        rebuildFilteredItems();
        setDirty(true);
        return;
    }

    std::unordered_set<uint64_t> placedSourceIds;
    auto& playlistModel = m_trackManager->getPlaylistModel();
    const auto laneIds = playlistModel.getLaneIDs();
    for (const auto& laneId : laneIds) {
        const auto* lane = playlistModel.getLane(laneId);
        if (!lane) {
            continue;
        }
        for (const auto& clip : lane->clips) {
            if (clip.sourceId > 0) {
                placedSourceIds.insert(static_cast<uint64_t>(clip.sourceId));
            }
        }
    }

    auto& sourceManager = m_trackManager->getSourceManager();
    std::vector<ClipSourceID> sourceIds = sourceManager.getAllSourceIDs();
    
    for (const auto& id : sourceIds) {
        if (m_removedClipSourceIds.find(id.value) != m_removedClipSourceIds.end()) {
            continue;
        }
        auto* source = sourceManager.getSource(id);
        if (source) {
            ClipEntry entry;
            entry.id = source->getID();
            entry.name = source->getName();
            entry.filename = source->getFilePath();
            entry.sampleRate = source->getSampleRate();
            entry.numChannels = source->getNumChannels();
            entry.duration = source->getDurationSeconds();
            entry.isPlacedOnTimeline = (placedSourceIds.find(entry.id.value) != placedSourceIds.end());
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

    rebuildFilteredItems();
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();

    renderer.fillRect(headerRect, theme.getColor("backgroundSecondary"));
    renderer.fillRect({headerRect.x, headerRect.y, headerRect.width, 1.0f},
                      theme.getColor("borderSubtle").withAlpha(0.018f));
    renderer.drawLine(
        AestraUI::NUIPoint(bounds.x, bounds.y + m_headerHeight),
        AestraUI::NUIPoint(bounds.x + bounds.width, bounds.y + m_headerHeight),
        1.0f, m_borderColor
    );

    // Render footer background and separator
    AestraUI::NUIRect footerRect(bounds.x, bounds.bottom() - m_footerHeight, bounds.width, m_footerHeight);
    if (m_footerHeight > 0.0f) {
        renderer.fillRect(footerRect, theme.getColor("backgroundSecondary"));
        renderer.drawLine(
            AestraUI::NUIPoint(bounds.x, footerRect.y),
            AestraUI::NUIPoint(bounds.x + bounds.width, footerRect.y),
            1.0f, m_borderColor
        );
    }

    const size_t itemCount =
        (m_mode == BrowserMode::Clips) ? getFilteredClipIndices().size() : getFilteredPatternIndices().size();
    const size_t totalCount = (m_mode == BrowserMode::Clips) ? m_clips.size() : m_patterns.size();
    const std::string title = m_mode == BrowserMode::Clips ? "Clips" : "Patterns";
    renderer.drawText(title, AestraUI::NUIPoint(bounds.x + 12.0f, bounds.y + 12.0f), themeProps.fontSizeXS,
                      theme.getColor("textPrimary").withAlpha(0.88f));
    const float titleWidth = renderer.measureText(title, themeProps.fontSizeXS).width;
    const std::string count = m_searchQuery.empty() ? std::to_string(totalCount)
                                                    : std::to_string(itemCount) + " / " + std::to_string(totalCount);
    renderer.drawText(count, AestraUI::NUIPoint(bounds.x + 18.0f + titleWidth, bounds.y + 13.0f), 10.0f,
                      theme.getColor("textSecondary").withAlpha(0.48f));

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
    const bool isEmpty =
        (m_mode == BrowserMode::Patterns) ? getFilteredPatternIndices().empty() : getFilteredClipIndices().empty();

    if (isEmpty) {
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& themeProps = theme.getCurrentTheme();
        const bool dropActive = m_isDragOver;
        const bool noSearchResults = !m_searchQuery.empty();
        const bool compactRail = bounds.width < 170.0f;
        struct EmptyStateCopy {
            std::string chipLabel;
            std::string chipDetail;
            std::string title;
            std::string detailLine1;
            std::string detailLine2;
        } copy;
        if (dropActive) {
            copy = {"Drop", "release", "Release To Import", "Release audio files here", "to add them to Clips"};
        } else if (noSearchResults) {
            copy = {"No matches", "ready", "No Matching Results", "Try a different name", ""};
        } else if (m_mode == BrowserMode::Patterns) {
            copy = {"Patterns", "ready", "No Patterns Yet", "Create a pattern, or drop files", "to switch into Clips"};
        } else {
            copy = {"Clips", "ready", "Clip Bin Ready", "Clip Bin stages files for fast timeline placement",
                    "Use Clips as an optional bin"};
        }
        if (compactRail) {
            renderer.fillRect(listRect, theme.getColor("backgroundSecondary"));
            const AestraUI::NUIRect chip{
                bounds.x + 10.0f,
                bounds.y + m_headerHeight + 16.0f,
                std::max(0.0f, bounds.width - 20.0f),
                32.0f
            };
            renderer.fillRoundedRect(chip, themeProps.radiusS + 1.0f,
                                     dropActive ? theme.getColor("accentPrimary").withAlpha(0.16f)
                                                : theme.getColor("borderSubtle").withAlpha(0.025f));
            renderer.strokeRoundedRect(chip, themeProps.radiusS + 1.0f, 1.0f,
                                       dropActive ? theme.getColor("accentPrimary").withAlpha(0.38f)
                                                  : theme.getColor("borderSubtle").withAlpha(0.070f));
            renderer.drawTextCentered(copy.chipLabel, chip, 10.5f,
                                      theme.getColor("textPrimary").withAlpha(dropActive ? 0.92f : 0.70f));
            renderer.drawTextCentered(copy.chipDetail, {chip.x, chip.bottom() + 7.0f, chip.width, 14.0f}, 9.0f,
                                      theme.getColor("textSecondary").withAlpha(0.56f));
            return;
        }
        const AestraUI::NUIRect stateCard{
            bounds.x + (compactRail ? 10.0f : 26.0f),
            bounds.y + m_headerHeight + (compactRail ? 16.0f : 34.0f),
            std::max(0.0f, bounds.width - (compactRail ? 20.0f : 52.0f)),
            std::max(compactRail ? 72.0f : 96.0f, listRect.height - (compactRail ? 32.0f : 68.0f))
        };
        const float radius = compactRail ? 12.0f : 16.0f;
        const auto accent = theme.getColor("accentPrimary");
        renderer.fillRoundedRect(stateCard, radius,
                                 dropActive ? accent.withAlpha(0.10f)
                                            : theme.getColor("surfaceRaised").withAlpha(0.22f));
        renderer.strokeRoundedRect(stateCard, radius, 1.0f,
                                   dropActive ? accent.withAlpha(0.26f)
                                              : theme.getColor("borderSubtle").withAlpha(0.24f));

        const AestraUI::NUIRect iconChip{
            stateCard.center().x - (compactRail ? 20.0f : 26.0f),
            stateCard.y + (compactRail ? 12.0f : 20.0f),
            compactRail ? 40.0f : 52.0f,
            compactRail ? 24.0f : 28.0f
        };
        renderer.fillRoundedRect(iconChip, 14.0f,
                                 dropActive ? accent.withAlpha(0.16f)
                                            : theme.getColor("backgroundTertiary").withAlpha(0.52f));
        renderer.strokeRoundedRect(iconChip, 14.0f, 1.0f,
                                   dropActive ? accent.withAlpha(0.30f)
                                              : theme.getColor("border").withAlpha(0.22f));
        renderer.drawTextCentered(dropActive ? "DROP" : (m_mode == BrowserMode::Patterns ? "PATTERN" : "CLIPS"),
                                  iconChip,
                                  compactRail ? 8.0f : 9.0f,
                                  dropActive ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.92f));
        renderer.drawTextCentered(
            copy.title,
            {stateCard.x + 14.0f, iconChip.bottom() + (compactRail ? 8.0f : 12.0f), stateCard.width - 28.0f, 18.0f},
            compactRail ? 11.0f : 13.0f, theme.getColor("textPrimary").withAlpha(0.94f));
        renderer.drawTextCentered(
            copy.detailLine1,
            {stateCard.x + 14.0f, iconChip.bottom() + (compactRail ? 24.0f : 34.0f), stateCard.width - 28.0f, 14.0f},
            compactRail ? 9.0f : 10.5f, theme.getColor("textSecondary").withAlpha(0.86f));
        if (!copy.detailLine2.empty()) {
            renderer.drawTextCentered(copy.detailLine2,
                                      {stateCard.x + 24.0f, iconChip.bottom() + 48.0f, stateCard.width - 48.0f, 14.0f},
                                      10.5f, theme.getColor("textSecondary").withAlpha(0.86f));
        }

        if (!compactRail) {
            const AestraUI::NUIRect routeChip{
                stateCard.center().x - 72.0f,
                stateCard.bottom() - 28.0f,
                144.0f,
                18.0f
            };
            renderer.fillRoundedRect(routeChip, themeProps.radiusM + 1.0f, theme.getColor("accentPrimary").withAlpha(dropActive ? 0.26f : 0.14f));
            renderer.strokeRoundedRect(routeChip, themeProps.radiusM + 1.0f, 1.0f, theme.getColor("accentPrimary").withAlpha(0.34f));
            renderer.drawTextCentered("AUTO-ROUTE TO TIMELINE", routeChip, 9.0f, theme.getColor("textPrimary").withAlpha(0.92f));
        }
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

    for (const size_t index : getFilteredPatternIndices()) {
        const auto& entry = m_patterns[index];
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

    for (const size_t index : getFilteredClipIndices()) {
        const auto& entry = m_clips[index];
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    const AestraUI::NUIRect cardRect = computeBinCardRect(bounds, y, m_itemHeight);
    const auto accent = theme.getColor("accentPrimary");

    if (selected) {
        renderer.fillRoundedRect(cardRect, kCardRadius, accent.withAlpha(0.15f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, accent.withAlpha(0.46f));
    } else if (hovered) {
        renderer.fillRoundedRect(cardRect, kCardRadius, theme.getColor("hover").withAlpha(0.07f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.20f));
    } else {
        renderer.fillRoundedRect(cardRect, kCardRadius, theme.getColor("surfaceRaised").withAlpha(0.12f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.14f));
    }

    renderer.fillRect(AestraUI::NUIRect(cardRect.x, cardRect.y, kAccentStripWidth, cardRect.height),
                      accent.withAlpha(selected ? 0.95f : 0.72f));

    // Type icon using NUIIcon
    float iconX = cardRect.x + 12.0f;
    float iconY = cardRect.y + (cardRect.height - 16.0f) / 2.0f;

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

    // Name
    renderer.drawText(entry.name, AestraUI::NUIPoint(cardRect.x + 34.0f, cardRect.y + 8.5f),
                      themeProps.fontSizeXS, theme.getColor("textPrimary").withAlpha(0.90f));

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << entry.lengthBeats << "b";
    renderer.drawText(ss.str(),
                      AestraUI::NUIPoint(cardRect.right() - 56.0f, cardRect.y + 8.5f),
                      themeProps.fontSizeXS,
                      theme.getColor("textSecondary").withAlpha(0.50f));

    renderer.fillCircle(AestraUI::NUIPoint(cardRect.right() - 12.0f, cardRect.center().y),
                        2.0f,
                        accent.withAlpha(entry.isPlacedOnTimeline ? 1.0f : 0.20f));

    if (hovered && m_playIcon) {
        const AestraUI::NUIRect playButton = computePlayButtonRect(cardRect);
        renderer.fillRoundedRect(playButton, themeProps.radiusM, theme.getColor("backgroundTertiary").withAlpha(0.80f));
        renderer.strokeRoundedRect(playButton, themeProps.radiusM, 1.0f, theme.getColor("borderSubtle").withAlpha(0.38f));
        m_playIcon->setBounds(AestraUI::NUIRect(playButton.x + 3.5f, playButton.y + 3.0f, 10.0f, 10.0f));
        m_playIcon->setColor(theme.getColor("textPrimary").withAlpha(0.92f));
        m_playIcon->onRender(renderer);
    }
}

bool PatternBrowserPanel::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    auto b = getBounds();
    auto& dragManager = AestraUI::NUIDragDropManager::getInstance();
    const auto& filteredPatterns = getFilteredPatternIndices();
    const auto& filteredClips = getFilteredClipIndices();

    if (!b.contains(event.position) && !m_dragPotential) {
        return NUIComponent::onMouseEvent(event);
    }
    
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
            if (itemIndex >= 0 && itemIndex < static_cast<int>(filteredPatterns.size())) {
                const auto& pattern = m_patterns[filteredPatterns[static_cast<size_t>(itemIndex)]];
                m_hoveredPatternId = pattern.id;
                const AestraUI::NUIRect cardRect = computeBinCardRect(b, b.y + m_headerHeight + (itemIndex * m_itemHeight) - m_scrollOffset, m_itemHeight);
                const AestraUI::NUIRect playButtonRect = computePlayButtonRect(cardRect);

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                    if (playButtonRect.contains(event.position)) {
                        if (m_onPatternPreviewRequested) {
                            m_onPatternPreviewRequested(pattern.id);
                        }
                        repaint();
                        return true;
                    }

                    auto now = std::chrono::steady_clock::now();
                    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                    bool isDoubleClick = (m_lastClickedPatternId == pattern.id) &&
                                         (currentTime - m_lastClickTime < 0.4);

                    m_selectedPatternId = pattern.id;
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
                    m_lastClickedPatternId = pattern.id;

                    repaint();
                    return true;
                }

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
                    m_selectedPatternId = pattern.id;
                    showPatternContextMenu(pattern, event.position);
                    repaint();
                    return true;
                }
            } else {
                m_hoveredPatternId = PatternID();
            }

            // Drag initiation relies on m_dragPotential and the saved m_dragPatternId,
            // not the row currently under the cursor: the pointer may have moved off
            // the row or out of the list before crossing the drag threshold.
            if (m_dragPotential) {
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
            if (itemIndex >= 0 && itemIndex < static_cast<int>(filteredClips.size())) {
                const auto& clip = m_clips[filteredClips[static_cast<size_t>(itemIndex)]];
                m_hoveredClipId = clip.id;
                const AestraUI::NUIRect cardRect = computeBinCardRect(b, b.y + m_headerHeight + (itemIndex * m_itemHeight) - m_scrollOffset, m_itemHeight);
                const AestraUI::NUIRect playButtonRect = computePlayButtonRect(cardRect);

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
                    if (playButtonRect.contains(event.position)) {
                        if (m_onClipPreviewRequested) {
                            m_onClipPreviewRequested(clip.filename);
                        }
                        repaint();
                        return true;
                    }

                    auto now = std::chrono::steady_clock::now();
                    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                    bool isDoubleClick = (m_lastClickedClipId == clip.id) &&
                                         (currentTime - m_lastClickTime < 0.4);

                    m_selectedClipId = clip.id;
                    m_dragPotential = !isDoubleClick;
                    m_dragStartPos = event.position;
                    m_dragClipId = m_selectedClipId;
                    m_dragPatternId = PatternID{};
                    m_lastClickTime = currentTime;
                    m_lastClickedClipId = clip.id;
                    repaint();
                    return true;
                }

                if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
                    m_selectedClipId = clip.id;
                    showClipContextMenu(clip, event.position);
                    repaint();
                    return true;
                }
            } else {
                m_hoveredClipId = ClipSourceID{};
            }

            // Resolve the dragged clip by its saved id, not the row under the cursor
            // (which may have moved off the row/list before the drag threshold). This
            // also drops the itemIndex-in-bounds requirement so the drag still starts.
            if (m_dragPotential) {
                float dx = event.position.x - m_dragStartPos.x;
                float dy = event.position.y - m_dragStartPos.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist >= dragManager.getDragThreshold()) {
                    size_t dragIdx = m_clips.size();
                    for (size_t i = 0; i < m_clips.size(); ++i) {
                        if (m_clips[i].id == m_dragClipId) {
                            dragIdx = i;
                            break;
                        }
                    }
                    if (dragIdx < m_clips.size()) {
                        const auto& clip = m_clips[dragIdx];
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
    }

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Right) {
        // Consume empty-space right clicks so they do not fall through into TrackManagerUI
        // and start its right-drag selection path.
        return true;
    }
    
    // Mouse Wheel - Scroll Handling
    if (std::abs(event.wheelDelta) > 0.001f && getBounds().contains(event.position)) {
        const float itemCount =
            static_cast<float>((m_mode == BrowserMode::Patterns) ? filteredPatterns.size() : filteredClips.size());
        float listHeight = itemCount * m_itemHeight;
        float viewportHeight = b.height - m_headerHeight - m_footerHeight;
        float maxScroll = std::max(0.0f, listHeight - viewportHeight);
        
        m_targetScrollOffset -= event.wheelDelta * 40.0f;
        m_targetScrollOffset = safeClampBrowserScroll(m_targetScrollOffset, maxScroll);
        
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    const AestraUI::NUIRect cardRect = computeBinCardRect(bounds, y, m_itemHeight);
    const auto accent = theme.getColor("accentPrimary");

    if (selected) {
        renderer.fillRoundedRect(cardRect, kCardRadius, accent.withAlpha(0.16f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, accent.withAlpha(0.46f));
    } else if (hovered) {
        renderer.fillRoundedRect(cardRect, kCardRadius, theme.getColor("hover").withAlpha(0.07f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.20f));
    } else {
        renderer.fillRoundedRect(cardRect, kCardRadius, theme.getColor("surfaceRaised").withAlpha(0.12f));
        renderer.strokeRoundedRect(cardRect, kCardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.14f));
    }

    renderer.fillRect(AestraUI::NUIRect(cardRect.x, cardRect.y, kAccentStripWidth, cardRect.height),
                      accent.withAlpha(selected ? 0.95f : 0.72f));

    // Icon
    float iconX = cardRect.x + 12.0f;
    float iconY = cardRect.y + (cardRect.height - 16.0f) / 2.0f;
    if (m_audioIcon) {
        m_audioIcon->setBounds(AestraUI::NUIRect(iconX, iconY, 16, 16));
        m_audioIcon->setColor(theme.getColor("textSecondary"));
        m_audioIcon->onRender(renderer);
    }

    // Name truncation logic
    std::string displayName = entry.name;
    if (displayName.length() > 28) {
        displayName = displayName.substr(0, 25) + "...";
    }

    renderer.drawText(displayName,
                     AestraUI::NUIPoint(cardRect.x + 34.0f, cardRect.y + 8.5f),
                     themeProps.fontSizeXS,
                     theme.getColor("textPrimary").withAlpha(0.90f));

    // Duration
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << entry.duration << "s";
    std::string durStr = ss.str();
    renderer.drawText(durStr,
                      AestraUI::NUIPoint(cardRect.right() - 56.0f, cardRect.y + 8.5f),
                      themeProps.fontSizeXS,
                      theme.getColor("textSecondary").withAlpha(0.50f));

    renderer.fillCircle(AestraUI::NUIPoint(cardRect.right() - 12.0f, cardRect.center().y),
                        2.0f,
                        accent.withAlpha(entry.isPlacedOnTimeline ? 1.0f : 0.20f));

    if (hovered && m_playIcon) {
        const AestraUI::NUIRect playButton = computePlayButtonRect(cardRect);
        renderer.fillRoundedRect(playButton, themeProps.radiusM, theme.getColor("backgroundTertiary").withAlpha(0.80f));
        renderer.strokeRoundedRect(playButton, themeProps.radiusM, 1.0f, theme.getColor("borderSubtle").withAlpha(0.38f));
        m_playIcon->setBounds(AestraUI::NUIRect(playButton.x + 3.5f, playButton.y + 3.0f, 10.0f, 10.0f));
        m_playIcon->setColor(theme.getColor("textPrimary").withAlpha(0.92f));
        m_playIcon->onRender(renderer);
    }
}

void PatternBrowserPanel::onResize(int width, int height) {
    auto bounds = getBounds();
    float padding = 8.0f;
    float toggleWidth = std::max(104.0f, std::min(156.0f, width - 24.0f));
    
    if (m_modeToggle) {
        float toggleY = height - m_footerHeight + (m_footerHeight - 28) / 2.0f;
        float toggleX = std::round((width - toggleWidth) * 0.5f);
        m_modeToggle->setBounds(AestraUI::NUIAbsolute(bounds, toggleX, toggleY, toggleWidth, 28));
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
    ensureDropTargetRegistration();
    const float viewportHeight = std::max(0.0f, getBounds().height - m_headerHeight - m_footerHeight);
    const float itemCount = static_cast<float>((m_mode == BrowserMode::Patterns) ? getFilteredPatternIndices().size()
                                                                                 : getFilteredClipIndices().size());
    const float listHeight = itemCount * m_itemHeight;
    const float maxScroll = std::max(0.0f, listHeight - viewportHeight);
    m_targetScrollOffset = safeClampBrowserScroll(m_targetScrollOffset, maxScroll);
    m_scrollOffset = safeClampBrowserScroll(m_scrollOffset, maxScroll);

    const float delta = m_targetScrollOffset - m_scrollOffset;
    if (std::abs(delta) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(deltaTime) * 18.0f);
        m_scrollOffset += delta * ease;
        m_scrollOffset = safeClampBrowserScroll(m_scrollOffset, maxScroll);
        repaint();
    } else if (std::abs(delta) > 0.0f) {
        m_scrollOffset = m_targetScrollOffset;
        repaint();
    }
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
            const auto sourceId = sourceManager.getOrCreateSource(data.filePath);
            if (sourceId.isValid()) {
                m_removedClipSourceIds.erase(sourceId.value);
            }
            
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

void PatternBrowserPanel::ensureDropTargetRegistration() {
    if (m_dropTargetRegistered) {
        return;
    }

    auto self = weak_from_this().lock();
    if (!self) {
        return;
    }

    auto dropTarget = std::dynamic_pointer_cast<AestraUI::IDropTarget>(self);
    if (!dropTarget) {
        return;
    }

    AestraUI::NUIDragDropManager::getInstance().registerDropTarget(dropTarget);
    m_dropTargetRegistered = true;
}

void PatternBrowserPanel::showClipContextMenu(const ClipEntry& entry, const AestraUI::NUIPoint& position) {
    detachContextMenu(m_clipContextMenu);
    m_clipContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
    m_clipContextMenu->setOnHide([this]() { detachContextMenu(m_clipContextMenu); });
    m_clipContextMenu->addItem("Place on timeline", [this, filePath = entry.filename, name = entry.name]() {
        if (m_onClipPlaceOnTimelineRequested) {
            m_onClipPlaceOnTimelineRequested(filePath, name);
        }
    });
    m_clipContextMenu->addItem("Preview", [this, filePath = entry.filename]() {
        if (m_onClipPreviewRequested) {
            m_onClipPreviewRequested(filePath);
        }
    });
    m_clipContextMenu->addItem("Remove from bin", [this, sourceId = entry.id]() {
        m_removedClipSourceIds.insert(sourceId.value);
        if (m_selectedClipId == sourceId) {
            m_selectedClipId = ClipSourceID{};
        }
        refreshClips();
        repaint();
    });
    m_clipContextMenu->addItem("Show in file browser", [this, filePath = entry.filename]() {
        if (m_onClipShowInFileBrowserRequested) {
            m_onClipShowInFileBrowserRequested(filePath);
        }
    });
    attachAndShowContextMenu(this, m_clipContextMenu, position);
}

void PatternBrowserPanel::showPatternContextMenu(const PatternEntry& entry, const AestraUI::NUIPoint& position) {
    detachContextMenu(m_patternContextMenu);
    m_patternContextMenu = std::make_shared<AestraUI::NUIContextMenu>();
    m_patternContextMenu->setOnHide([this]() { detachContextMenu(m_patternContextMenu); });
    m_patternContextMenu->addItem("Place on timeline", [this, patternId = entry.id]() {
        if (m_onPatternPlaceOnTimelineRequested) {
            m_onPatternPlaceOnTimelineRequested(patternId);
        }
    });
    m_patternContextMenu->addItem("Preview", [this, patternId = entry.id]() {
        if (m_onPatternPreviewRequested) {
            m_onPatternPreviewRequested(patternId);
        }
    });
    m_patternContextMenu->addItem("Remove from bin", [this, patternId = entry.id]() {
        if (!m_trackManager || !patternId.isValid()) {
            return;
        }
        if (m_trackManager->getPlaylistModel().isPatternUsed(patternId)) {
            Aestra::Log::warning("Cannot remove pattern from bin while it is used on timeline.");
            return;
        }
        m_removedPatternIds.insert(patternId.value);
        if (m_selectedPatternId == patternId) {
            m_selectedPatternId = PatternID{};
        }
        refreshPatterns();
        repaint();
    });
    m_patternContextMenu->addItem("Show in file browser", []() {
        Aestra::Log::warning("Show in file browser is unavailable for pattern entries.");
    });
    attachAndShowContextMenu(this, m_patternContextMenu, position);
}

} // namespace Audio
} // namespace Aestra
