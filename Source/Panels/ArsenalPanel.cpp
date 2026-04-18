// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ArsenalPanel.h"
#include "PatternBrowserPanel.h" // For m_patternBrowser
#include "../AestraUI/Widgets/PluginBrowserPanel.h"
#include "NUIButton.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraCore/include/AestraLog.h"
#include <cstdio>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr int kArsenalMinPatternBars = 2;
constexpr int kArsenalMaxPatternBars = 16;

bool usesStepSequencerForUnitGroup(UnitGroup group) {
    return group == UnitGroup::Drums || group == UnitGroup::Audio;
}

const char* unitTypeDisplayName(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "Sampler";
    case UnitType::PitchedSampler: return "808";
    case UnitType::Instrument: return "Instrument";
    case UnitType::Audio: return "Audio";
    default: return "Sampler";
    }
}

const char* unitTypeDescription(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "Classic step sequencer";
    case UnitType::PitchedSampler: return "808s, bass, slides";
    case UnitType::Instrument: return "Melodies and chords";
    case UnitType::Audio: return "Drop a clip directly";
    default: return "";
    }
}

UnitGroup unitGroupForType(UnitType type) {
    switch (type) {
    case UnitType::Instrument: return UnitGroup::Synth;
    case UnitType::Audio: return UnitGroup::Audio;
    case UnitType::Sampler:
    case UnitType::PitchedSampler:
    default: return UnitGroup::Drums;
    }
}

std::string unitTypeContentLabel(UnitType type, int stepCount, double durationSeconds) {
    switch (type) {
    case UnitType::Sampler:
        return std::to_string(stepCount) + " Steps";
    case UnitType::PitchedSampler:
        return std::to_string(stepCount) + " Steps · Pitched";
    case UnitType::Instrument:
        return "Piano Roll";
    case UnitType::Audio: {
        if (durationSeconds > 0.0) {
            const int total = static_cast<int>(std::round(durationSeconds));
            const int minutes = total / 60;
            const int seconds = total % 60;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
            return std::string(buf);
        }
        return "Audio Clip";
    }
    default:
        return std::to_string(stepCount) + " Steps";
    }
}

AestraUI::NUIRect expandHitRect(const AestraUI::NUIRect& rect, float amount) {
    return AestraUI::NUIRect(rect.x - amount, rect.y - amount, rect.width + amount * 2.0f, rect.height + amount * 2.0f);
}

bool isInstrumentPluginDrag(const AestraUI::DragData& data) {
    if (data.type != AestraUI::DragDataType::Plugin || data.sourceClipIdString.empty()) {
        return false;
    }

    if (!data.customData.has_value()) {
        return true;
    }

    if (const auto* item = std::any_cast<AestraUI::PluginListItem>(&data.customData)) {
        return item->typeName == "Instrument";
    }

    return true;
}

bool isAudioFileDrag(const AestraUI::DragData& data) {
    if (data.type != AestraUI::DragDataType::File || data.filePath.empty()) {
        return false;
    }

    const auto dot = data.filePath.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }

    std::string ext = data.filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == "wav" || ext == "mp3" || ext == "flac" || ext == "ogg" || ext == "aiff";
}

float safeClampPanelScroll(float value, float upper) {
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) return 0.0f;
    if (value >= upper) return upper;
    return value;
}

void drawArsenalChip(AestraUI::NUIRenderer& renderer,
                     const AestraUI::NUIRect& rect,
                     const std::string& text,
                     const AestraUI::NUIColor& fill,
                     const AestraUI::NUIColor& stroke,
                     const AestraUI::NUIColor& textColor,
                     float fontSize = 9.0f)
{
    renderer.fillRoundedRect(rect, 5.0f, fill);
    renderer.strokeRoundedRect(rect, 5.0f, 1.0f, stroke);
    renderer.drawTextCentered(text, rect, fontSize, textColor);
}
} // namespace

ArsenalPanel::ArsenalPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("")
    , m_trackManager(std::move(trackManager))
{
    // setId("ArsenalPanel"); // Handled by WindowPanel title
    ensureDefaultPattern(); // Auto-create Pattern 1
    
    // Create default Unit 1 for immediate playback
    if (m_trackManager) {
        auto& unitMgr = m_trackManager->getUnitManager();
        if (unitMgr.getUnitCount() == 0) {
            UnitID defaultUnit = unitMgr.createUnit("Sampler 1", UnitType::Sampler);
            unitMgr.setUnitEnabled(defaultUnit, true);
        }
    }

    // Modern list container
    m_listContainer = std::make_shared<AestraUI::NUIComponent>();
    m_listContainer->setId("ArsenalListContainer");
    
    // Set as the content of the WindowPanel
    setContent(m_listContainer);

    // Create color picker (initially hidden)
    m_colorPicker = std::make_shared<UnitColorPicker>();
    m_colorPicker->setOnColorSelected([this](uint32_t color) {
        if (m_colorPickerTargetUnit > 0) {
            m_trackManager->getUnitManager().setUnitColor(m_colorPickerTargetUnit, color);
            refreshUnits();
        }
    });
    
    // ScrollView wrapper would go here, for now directly setting content
}

ArsenalPanel::~ArsenalPanel() {
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
}

void ArsenalPanel::createLayout() {
    // This method is no longer needed as layout is handled by WindowPanel and setContent
    // The content of this method has been moved to the constructor or is handled by the base class.
}

void ArsenalPanel::refreshUnits() {
    if (!m_listContainer || !m_trackManager) return;
    auto& theme = NUIThemeManager::getInstance();
    
    // Clear previous children
    m_listContainer->removeAllChildren();
    m_unitRows.clear();

    // Add Play Button (Top Left of Header)
    {
        m_playBtn = std::make_shared<NUIButton>("PLAY");
        m_playBtn->setId("ArsenalPlayBtn");
        m_playBtn->setBackgroundColor(theme.getColor("accentPrimary"));
        m_playBtn->setTextColor(theme.getColor("textOnAccent"));
        
        // Initial State
        if (m_trackManager->isPlaying() && m_trackManager->isPatternMode()) {
            m_playBtn->setText("STOP");
            m_playBtn->setBackgroundColor(theme.getColor("error"));
        }
        
        std::weak_ptr<NUIButton> weakBtn = m_playBtn;
        m_playBtn->setOnClick([this, weakBtn]() {
             if (auto btn = weakBtn.lock()) {
                 Log::info("[ArsenalPanel] Play clicked. activePatternID=" + 
                     std::to_string(m_activePatternID.value) + " isValid=" + 
                     std::to_string(m_activePatternID.isValid()));
                 if (m_trackManager->isPlaying() && m_trackManager->isPatternMode()) {
                     m_trackManager->stopArsenalPlayback(true);
                 } else {
                     if (m_onRequestPlaybackActivation) {
                         m_onRequestPlaybackActivation();
                     }
                     m_trackManager->playPatternInArsenal(m_activePatternID);
                 }
             }
        });
        m_listContainer->addChild(m_playBtn);
    }
    

    
    // Build unit rows
    auto& unitMgr = m_trackManager->getUnitManager();
    auto unitIDs = unitMgr.getAllUnitIDs();

    if (m_selectedUnitId == 0 || std::find(unitIDs.begin(), unitIDs.end(), m_selectedUnitId) == unitIDs.end()) {
        m_selectedUnitId = unitIDs.empty() ? 0 : unitIDs.front();
    }
    if (m_onSelectedUnitChanged && m_selectedUnitId != 0) {
        m_onSelectedUnitChanged(m_selectedUnitId);
    }
    
    for (size_t i = 0; i < unitIDs.size(); ++i) {
        auto row = std::make_shared<UnitRow>(m_trackManager, unitMgr, unitIDs[i], m_activePatternID);
        row->setSelected(unitIDs[i] == m_selectedUnitId);
        
        // Set step count
        row->setStepCount(m_stepCount);
        
        // Set up callbacks for drag-drop and color picker
        row->setOnDragStart([this](UnitID id) {
            onUnitDragStart(id);
        });
        
        row->setOnRequestColorPicker([this, i, unitIDs]() {
            // Get the row bounds for positioning
            if (i < m_unitRows.size()) {
                auto bounds = m_unitRows[i]->getBounds();
                showColorPicker(unitIDs[i], NUIPoint(bounds.x + 30, bounds.y + bounds.height));
            }

        });
        
        row->setOnEditUnit([this](UnitID id) {
            if (!m_trackManager) return;
            auto& unitMgr = m_trackManager->getUnitManager();
            const auto* unit = unitMgr.getUnit(id);
            if (unit && !unit->pluginId.empty() && unit->plugin) {
                // Unit has a plugin — open plugin editor
                if (m_onRequestEditor) m_onRequestEditor(id);
            } else if (unit && unit->defaultPatternId.isValid()) {
                // Unit has a default pattern — open Piano Roll
                if (m_onRequestPatternEditor) m_onRequestPatternEditor(unit->defaultPatternId);
            }
        });

        row->setOnLoadUnitSample([this](UnitID id) {
            if (m_onRequestLoadSample) m_onRequestLoadSample(id);
        });

        row->setOnPluginDropped([this](UnitID unitId, const std::string& pluginId) {
            if (m_onPluginDroppedToUnit) {
                m_onPluginDroppedToUnit(unitId, pluginId);
            }
        });
        row->setOnSampleDropped([this](UnitID unitId, const std::string& samplePath) {
            if (m_trackManager) {
                m_trackManager->getUnitManager().setUnitEnabled(unitId, true);
            }
            if (m_onSampleDroppedToUnit) {
                m_onSampleDroppedToUnit(unitId, samplePath);
            }
        });
        row->setOnPatternEdited([this](PatternID patternId) {
            if (m_onPatternEdited) {
                m_onPatternEdited(patternId);
            }
        });
        row->setOnOpenPatternEditor([this](PatternID patternId) {
            if (m_onRequestPatternEditor) {
                m_onRequestPatternEditor(patternId);
            }
        });
        
        m_listContainer->addChild(row);
        m_unitRows.push_back(row);
        
        // Register as drop target for file drag-drop
        AestraUI::NUIDragDropManager::getInstance().registerDropTarget(row);
    }
    
    // Add "Add Unit" button
    auto addBtn = std::make_shared<NUIButton>("+ Add Unit");
    addBtn->setStyle(NUIButton::Style::Secondary);
    addBtn->setBackgroundColor(NUIColor::transparent());
    addBtn->setHoverColor(theme.getColor("surfaceSecondary").withAlpha(0.45f));
    addBtn->setPressedColor(theme.getColor("surfaceSecondary").withAlpha(0.32f));
    addBtn->setTextColor(theme.getColor("textSecondary").withAlpha(0.5f));
    addBtn->setOnClick([this]() {
        onAddUnit();
    });
    m_listContainer->addChild(addBtn);
    
    layoutUnits();
    syncRowSelection();
    ensureDropTargetRegistration(true);
    
    if (auto parent = getParent()) {
        parent->repaint();
    }
}

void ArsenalPanel::onAddUnit() {
    m_showUnitTypePicker = !m_showUnitTypePicker;
    repaint();
}

void ArsenalPanel::createUnitOfType(UnitType type) {
    if (!m_trackManager) return;
    const auto count = m_trackManager->getUnitManager().getUnitCount() + 1;
    std::string name = (type == UnitType::Sampler ? "Sampler " :
                        type == UnitType::PitchedSampler ? "808 " :
                        type == UnitType::Instrument ? "Instrument " : "Audio ") + std::to_string(count);
    m_selectedUnitId = m_trackManager->getUnitManager().createUnit(name, type);
    if (m_onSelectedUnitChanged) {
        m_onSelectedUnitChanged(m_selectedUnitId);
    }
    m_showUnitTypePicker = false;
    refreshUnits();
}

bool ArsenalPanel::removeSelectedUnit() {
    if (!m_trackManager || m_selectedUnitId == 0) {
        return false;
    }

    auto& unitMgr = m_trackManager->getUnitManager();
    auto unitIDs = unitMgr.getAllUnitIDs();
    if (unitIDs.size() <= 1) {
        Log::warning("[Arsenal] Refusing to remove the last unit");
        return false;
    }

    auto it = std::find(unitIDs.begin(), unitIDs.end(), m_selectedUnitId);
    if (it == unitIDs.end()) {
        return false;
    }

    const size_t removedIndex = static_cast<size_t>(std::distance(unitIDs.begin(), it));
    const UnitID removedUnit = m_selectedUnitId;
    if (!unitMgr.removeUnit(removedUnit)) {
        return false;
    }

    removeUnitNotes(removedUnit);

    auto remaining = unitMgr.getAllUnitIDs();
    if (remaining.empty()) {
        m_selectedUnitId = 0;
    } else if (removedIndex < remaining.size()) {
        m_selectedUnitId = remaining[removedIndex];
    } else {
        m_selectedUnitId = remaining.back();
    }

    refreshUnits();
    if (m_onSelectedUnitChanged && m_selectedUnitId != 0) {
        m_onSelectedUnitChanged(m_selectedUnitId);
    }
    if (m_activePatternID.isValid() && m_onPatternEdited) {
        m_onPatternEdited(m_activePatternID);
    }
    Log::info("[Arsenal] Removed Unit " + std::to_string(removedUnit));
    return true;
}

void ArsenalPanel::syncRowSelection() {
    for (auto& row : m_unitRows) {
        if (row) {
            row->setSelected(row->getUnitId() == m_selectedUnitId);
        }
    }
}

void ArsenalPanel::removeUnitNotes(UnitID unitId) {
    if (!m_trackManager || unitId == 0) {
        return;
    }

    auto patterns = m_trackManager->getPatternManager().getAllPatterns();
    for (const auto& pattern : patterns) {
        if (!pattern || !pattern->isMidi()) {
            continue;
        }

        auto& midi = std::get<MidiPayload>(pattern->payload);
        midi.notes.erase(
            std::remove_if(
                midi.notes.begin(),
                midi.notes.end(),
                [unitId](const MidiNote& note) { return note.unitId == unitId; }),
            midi.notes.end());
    }
}

void ArsenalPanel::setActivePattern(PatternID patternId) {
    if (m_activePatternID == patternId) return;
    m_activePatternID = patternId;
    
    // [FIX] Pre-load pattern so Global Play button works immediately
    if (m_trackManager) {
        m_trackManager->preparePatternForArsenal(m_activePatternID);
    }
    
    refreshUnits(); // Rebuild UI with new pattern context
    if (m_onActivePatternChanged) {
        m_onActivePatternChanged(m_activePatternID);
    }
}

void ArsenalPanel::setSelectedUnit(UnitID unitId) {
    if (!m_trackManager || unitId == 0 || m_selectedUnitId == unitId) {
        return;
    }

    const auto unitIDs = m_trackManager->getUnitManager().getAllUnitIDs();
    if (std::find(unitIDs.begin(), unitIDs.end(), unitId) == unitIDs.end()) {
        return;
    }

    m_selectedUnitId = unitId;
    syncRowSelection();
    if (m_onSelectedUnitChanged) {
        m_onSelectedUnitChanged(m_selectedUnitId);
    }
    repaint();
}

void ArsenalPanel::setStepCount(int count) {
    if (count != 16 && count != 32 && count != 64) {
        count = 16; // Default to 16 if invalid
    }
    m_stepCount = count;
    
    // Update all unit rows
    for (auto& row : m_unitRows) {
        if (row) {
            row->setStepCount(count);
        }
    }
    
    repaint();
}

void ArsenalPanel::ensureDefaultPattern() {
    if (!m_trackManager) return;
    auto& pm = m_trackManager->getPatternManager();
    
    // Check if Pattern 1 exists
    auto patterns = pm.getAllPatterns();
    for (const auto& p : patterns) {
        if (p->name == "Pattern 1") {
            m_activePatternID = p->id;
            // [FIX] Pre-load pattern
            if (m_trackManager) {
                m_trackManager->preparePatternForArsenal(m_activePatternID);
            }
            if (m_onActivePatternChanged) {
                m_onActivePatternChanged(m_activePatternID);
            }
            return;
        }
    }
    
    // Create Pattern 1 if it doesn't exist
    Aestra::Audio::MidiPayload empty;
    m_activePatternID = pm.createMidiPattern("Pattern 1", 8.0, empty);
    
    // [FIX] Pre-load pattern
    if (m_trackManager) {
        m_trackManager->preparePatternForArsenal(m_activePatternID);
    }
    if (m_onActivePatternChanged) {
        m_onActivePatternChanged(m_activePatternID);
    }
    
    // Refresh Pattern Browser to show Pattern 1
    if (m_patternBrowser) {
        m_patternBrowser->refreshPatterns();
    }
}

void ArsenalPanel::onRender(NUIRenderer& renderer) {
    // The base WindowPanel::onRender will handle its own background, title, and children rendering.
    WindowPanel::onRender(renderer);

    auto& theme = NUIThemeManager::getInstance();
    const auto bounds = getBounds();

    std::string patternName = "Pattern";
    std::string trackContext = "No Track";
    if (m_trackManager && m_activePatternID.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
            if (!pattern->name.empty()) {
                patternName = pattern->name;
            }
        }
    }
    if (m_trackManager && m_selectedUnitId != 0) {
        if (const auto* unit = m_trackManager->getUnitManager().getUnit(m_selectedUnitId)) {
            if (!unit->name.empty()) {
                trackContext = unit->name;
            }
        }
    }
    renderer.drawText(patternName,
                      NUIPoint(bounds.x + 12.0f, bounds.y + 5.0f),
                      12.0f,
                      theme.getColor("textPrimary").withAlpha(0.9f));
    renderer.drawText(patternName + " · " + trackContext,
                      NUIPoint(bounds.x + 12.0f, bounds.y + 16.0f),
                      8.5f,
                      theme.getColor("textSecondary").withAlpha(0.5f));

    // Render progress header (step indicators above grid)
    if (m_listContainer && isVisible()) {
        NUIRect containerBounds = m_listContainer->getBounds();
        NUIRect headerBounds(containerBounds.x, containerBounds.y, 
                            containerBounds.width, PROGRESS_HEADER_HEIGHT);
        drawProgressHeader(renderer, headerBounds);
    }

    if (m_showUnitTypePicker) {
        drawUnitTypePicker(renderer);
    }
    
    // Only render the color picker if it's visible.
    if (m_colorPicker && m_colorPicker->isShowing()) {
        m_colorPicker->onRender(renderer);
    }
}

void ArsenalPanel::layoutUnits() {
    if (!m_listContainer) return;
    
    NUIRect bounds = m_listContainer->getBounds();
    const float horizontalPadding = 8.0f;
    const float topPadding = 6.0f;
    const float width = std::max(0.0f, bounds.width - horizontalPadding * 2.0f);
    const float startX = bounds.x + horizontalPadding;
    const float startY = bounds.y + topPadding;

    // Reserve space for progress header
    float yPos = startY + PROGRESS_HEADER_HEIGHT + 8.0f - m_scrollY;
    float spacing = 8.0f;
    float rowHeight = 56.0f;
    
    // Layout unit rows
    for (auto& row : m_unitRows) {
        if (row) {
            row->setBounds(NUIRect(startX, yPos, width, rowHeight));
            yPos += rowHeight + spacing;
        }
    }
    
    // Add Unit button (last child)
    auto children = m_listContainer->getChildren();
    if (!children.empty()) {
        auto addBtn = children.back();
        bool isAddButton = m_unitRows.empty() || (addBtn != m_unitRows.back());
        if (addBtn && isAddButton) {
            m_addUnitButtonRect = NUIRect(startX, yPos + 8.0f, 120.0f, 28.0f);
            addBtn->setBounds(m_addUnitButtonRect);
        }
    }
}

void ArsenalPanel::onResize(int width, int height) {
    // Custom resize logic is now handled by the WindowPanel base class.
    // We only need to ensure our internal content (m_listContainer) is laid out.
    WindowPanel::onResize(width, height); // Call base class first
    layoutUnits(); // Layout units within the content area
}

void ArsenalPanel::onUpdate(double dt) {
    WindowPanel::onUpdate(dt);
    ensureDropTargetRegistration();
    const float viewportHeight = std::max(0.0f, (m_listContainer ? m_listContainer->getBounds().height : 0.0f) - PROGRESS_HEADER_HEIGHT);
    const float contentHeight = static_cast<float>(m_unitRows.size()) * (56.0f + 8.0f);
    const float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
    m_targetScrollY = safeClampPanelScroll(m_targetScrollY, maxScroll);
    m_scrollY = safeClampPanelScroll(m_scrollY, maxScroll);

    const float delta = m_targetScrollY - m_scrollY;
    if (std::abs(delta) > 0.1f) {
        const float ease = 1.0f - std::exp(-static_cast<float>(dt) * 18.0f);
        m_scrollY += delta * ease;
        m_scrollY = safeClampPanelScroll(m_scrollY, maxScroll);
        layoutUnits();
    } else if (std::abs(delta) > 0.0f) {
        m_scrollY = m_targetScrollY;
        layoutUnits();
    }
    
    // Sync Play/Stop button text and color with actual engine state
    if (m_playBtn && m_trackManager) {
        bool playingNow = m_trackManager->isPlaying() && m_trackManager->isPatternMode();
        bool btnStatePlaying = (m_playBtn->getText() == "STOP");
        
        if (playingNow != btnStatePlaying) {
            auto& theme = NUIThemeManager::getInstance();
            if (playingNow) {
                m_playBtn->setText("STOP");
                m_playBtn->setBackgroundColor(theme.getColor("error"));
            } else {
                m_playBtn->setText("PLAY");
                m_playBtn->setBackgroundColor(theme.getColor("accentPrimary"));
            }
        }
    }
}

void ArsenalPanel::ensureDropTargetRegistration(bool reorder) {
    auto self = weak_from_this().lock();
    if (!self) {
        return;
    }

    auto dropTarget = std::dynamic_pointer_cast<AestraUI::IDropTarget>(self);
    if (!dropTarget) {
        return;
    }

    if (reorder || m_dropTargetRegistered) {
        AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
    }

    if (!m_dropTargetRegistered || reorder) {
        AestraUI::NUIDragDropManager::getInstance().registerDropTarget(dropTarget);
        m_dropTargetRegistered = true;
    }
}

// === Pattern Progress Visualization ===

int ArsenalPanel::calculateCurrentStep() {
    if (!m_trackManager) return -1;
    
    // Check if playing using TrackManager's method
    // Check if playing using TrackManager's method
    // Note: We allow visualization while paused if in pattern mode to show current position
    // if (!m_trackManager->isPlaying()) return -1;
    
    // [FIX] Freeze Arsenal Playhead in Timeline Mode (only animate in Pattern Mode)
    if (!m_trackManager->isPatternMode()) return -1;
    
    // Get position in seconds, convert to beats
    TimelineClock& clock = m_trackManager->getTimelineClock();
    double positionSeconds = m_trackManager->getPosition();
    double bpm = clock.getCurrentTempo();
    double beatsPerSecond = bpm / 60.0;
    double currentBeat = positionSeconds * beatsPerSecond;
    
    // Use the actual pattern length instead of a hard-coded 1-bar step grid.
    double patternLengthBeats = 8.0;
    if (m_activePatternID.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
            patternLengthBeats = std::max(8.0, pattern->lengthBeats);
        }
    }
    const double beatsPerVisualStep = patternLengthBeats / std::max(1, m_stepCount);

    // Calculate position within pattern (looping)
    double patternBeat = std::fmod(currentBeat, patternLengthBeats);
    if (patternBeat < 0) patternBeat += patternLengthBeats;
    
    // Convert to step index
    int step = static_cast<int>(patternBeat / std::max(1e-6, beatsPerVisualStep));
    return std::clamp(step, 0, m_stepCount - 1);
}

void ArsenalPanel::adjustPatternBars(int deltaBars) {
    if (!m_trackManager || !m_activePatternID.isValid() || deltaBars == 0) {
        return;
    }

    auto& patternManager = m_trackManager->getPatternManager();
    auto* pattern = patternManager.getPattern(m_activePatternID);
    if (!pattern) {
        return;
    }

    const int currentBars = std::clamp(
        static_cast<int>(std::round(std::max(8.0, pattern->lengthBeats) / 4.0)),
        kArsenalMinPatternBars,
        kArsenalMaxPatternBars);
    const int nextBars = std::clamp(currentBars + deltaBars, kArsenalMinPatternBars, kArsenalMaxPatternBars);
    const double nextLengthBeats = static_cast<double>(nextBars) * 4.0;
    if (std::abs(nextLengthBeats - pattern->lengthBeats) < 0.001) {
        return;
    }

    patternManager.applyPatch(m_activePatternID, [nextLengthBeats](PatternSource& source) {
        source.lengthBeats = nextLengthBeats;
    });
    m_trackManager->preparePatternForArsenal(m_activePatternID);
    refreshUnits();
    repaint();
    if (m_onPatternEdited) {
        m_onPatternEdited(m_activePatternID);
    }
}

void ArsenalPanel::drawProgressHeader(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    
    // Calculate current step from clock
    m_currentPlayStep = calculateCurrentStep();
    
    // Step layout (matches UnitRow grid layout)
    float controlWidth = 312.0f;
    float gridStartX = bounds.x + controlWidth + 6.0f;
    float availWidth = bounds.width - controlWidth - 12.0f;

    double lengthBeats = static_cast<double>(m_stepCount) * 0.25;
    UnitType selectedType = UnitType::Sampler;
    double selectedDurationSeconds = 0.0;
    if (m_trackManager && m_activePatternID.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
            lengthBeats = std::max(lengthBeats, pattern->lengthBeats);
        }
    }
    if (m_trackManager && m_selectedUnitId != 0) {
        if (const auto* unit = m_trackManager->getUnitManager().getUnit(m_selectedUnitId)) {
            selectedType = unit->type;
            selectedDurationSeconds = unit->audioDurationSeconds;
        }
    }
    std::string contentLabel = unitTypeContentLabel(selectedType, m_stepCount, selectedDurationSeconds);

    const int unitCount = m_trackManager ? static_cast<int>(m_trackManager->getUnitManager().getUnitCount()) : 0;
    const int bars = std::clamp(static_cast<int>(std::round(lengthBeats / 4.0)), kArsenalMinPatternBars, kArsenalMaxPatternBars);
    const bool decEnabled = bars > kArsenalMinPatternBars;
    const bool incEnabled = bars < kArsenalMaxPatternBars;

    const NUIRect leftCard(bounds.x + 4.0f, bounds.y + 1.0f, std::max(216.0f, controlWidth - 10.0f), bounds.height - 2.0f);
    renderer.fillRoundedRect(leftCard, 8.0f, theme.getColor("backgroundSecondary").withAlpha(0.92f));
    renderer.strokeRoundedRect(leftCard, 8.0f, 1.0f, theme.getColor("borderSubtle").withAlpha(0.85f));

    m_barsDecrementRect = NUIRect(leftCard.x + 10.0f, leftCard.y + 4.0f, 18.0f, leftCard.height - 8.0f);
    m_barsValueRect = NUIRect(m_barsDecrementRect.right() + 4.0f, leftCard.y + 4.0f, 56.0f, leftCard.height - 8.0f);
    m_barsIncrementRect = NUIRect(m_barsValueRect.right() + 4.0f, leftCard.y + 4.0f, 18.0f, leftCard.height - 8.0f);

    const auto pillFill = theme.getColor("surfaceTertiary").withAlpha(0.78f);
    const auto pillStroke = theme.getColor("borderSubtle").withAlpha(0.85f);
    const auto pillText = theme.getColor("textSecondary").withAlpha(0.9f);
    renderer.fillRoundedRect(m_barsDecrementRect, 5.0f, pillFill);
    renderer.strokeRoundedRect(m_barsDecrementRect, 5.0f, 1.0f, pillStroke);
    renderer.drawTextCentered("-", m_barsDecrementRect, 10.5f, decEnabled ? pillText : NUIColor(1.0f, 1.0f, 1.0f, 0.25f));

    renderer.fillRoundedRect(m_barsValueRect, 5.0f, pillFill);
    renderer.strokeRoundedRect(m_barsValueRect, 5.0f, 1.0f, pillStroke);
    renderer.drawTextCentered(std::to_string(bars) + " Bars", m_barsValueRect, 8.5f, pillText);

    renderer.fillRoundedRect(m_barsIncrementRect, 5.0f, pillFill);
    renderer.strokeRoundedRect(m_barsIncrementRect, 5.0f, 1.0f, pillStroke);
    renderer.drawTextCentered("+", m_barsIncrementRect, 10.5f, incEnabled ? pillText : NUIColor(1.0f, 1.0f, 1.0f, 0.25f));

    const float contentBadgeWidth = contentLabel == "Piano Roll" ? 74.0f : 62.0f;
    const NUIRect contentBadge(m_barsIncrementRect.right() + 8.0f, leftCard.y + 4.0f, contentBadgeWidth, leftCard.height - 8.0f);
    const NUIRect unitsBadge(contentBadge.right() + 6.0f, leftCard.y + 4.0f, 54.0f, leftCard.height - 8.0f);
    drawArsenalChip(renderer,
                    contentBadge,
                    contentLabel,
                    theme.getColor("surfaceTertiary").withAlpha(0.78f),
                    theme.getColor("borderSubtle").withAlpha(0.85f),
                    theme.getColor("textSecondary").withAlpha(0.9f),
                    8.25f);
    drawArsenalChip(renderer,
                    unitsBadge,
                    std::to_string(unitCount) + " Units",
                    theme.getColor("surfaceTertiary").withAlpha(0.78f),
                    theme.getColor("borderSubtle").withAlpha(0.85f),
                    theme.getColor("textSecondary").withAlpha(0.9f),
                    8.25f);

    const NUIRect gridCard(gridStartX - 2.0f, bounds.y + 1.0f, std::max(0.0f, availWidth + 4.0f), bounds.height - 2.0f);
    renderer.fillRoundedRect(gridCard, 8.0f, theme.getColor("backgroundSecondary").withAlpha(0.82f));
    renderer.strokeRoundedRect(gridCard, 8.0f, 1.0f, theme.getColor("borderSubtle").withAlpha(0.7f));
    
    float stepWidth = std::max(availWidth / static_cast<float>(m_stepCount), 26.0f);
    float indicatorHeight = PROGRESS_HEADER_HEIGHT - 10.0f;
    float indicatorY = bounds.y + 5.0f;
    
    // Scanlines/Clipping: Clip to header bounds to prevent overflow
    renderer.setClipRect(bounds);
    
    // Draw step indicators
    for (int i = 0; i < m_stepCount; ++i) {
        float stepX = gridStartX + (i * stepWidth) + 2.0f;
        float indicatorWidth = stepWidth - 4.0f;
        
        NUIRect indicatorRect(stepX, indicatorY, indicatorWidth, indicatorHeight);
        
        // Base color: more visible background
        NUIColor bgColor = theme.getColor("surfaceTertiary").withAlpha(0.5f);
        
        // Bar/beat markers
        bool isBarStart = (i % 4 == 0);
        if (isBarStart) {
            bgColor = bgColor.lightened(0.1f);
        }
        
        renderer.fillRoundedRect(indicatorRect, 2.5f, bgColor);
        
        // Highlight current playing step
        if (i == m_currentPlayStep) {
            NUIColor playColor = theme.getColor("accentPrimary");
            renderer.fillRoundedRect(indicatorRect, 2.5f, playColor);
            
            // Glow effect
            NUIRect glowRect(indicatorRect.x - 1, indicatorRect.y - 1, 
                           indicatorRect.width + 2, indicatorRect.height + 2);
            renderer.strokeRoundedRect(glowRect, 3.0f, 1.0f, playColor.withAlpha(0.6f));
        }
        // Show progress for steps already played in current loop
        else if (m_currentPlayStep >= 0 && i < m_currentPlayStep) {
            NUIColor playedColor = theme.getColor("accentPrimary").withAlpha(0.5f);
            renderer.fillRoundedRect(indicatorRect, 2.5f, playedColor);
        }
        
        // Subtle border
        renderer.strokeRoundedRect(indicatorRect, 2.5f, 0.5f, 
                                   theme.getColor("borderSubtle").withAlpha(0.6f));

        if (isBarStart) {
            renderer.drawTextCentered(std::to_string((i / 4) + 1), indicatorRect, 7.5f, theme.getColor("textSecondary").withAlpha(0.88f));
        }
    }
    
    renderer.clearClipRect();
}

void ArsenalPanel::drawUnitTypePicker(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    if (m_addUnitButtonRect.width <= 0.0f || m_addUnitButtonRect.height <= 0.0f) {
        return;
    }

    const float cardWidth = 332.0f;
    const float cardHeight = 156.0f;
    m_unitTypePickerRect = NUIRect(m_addUnitButtonRect.x,
                                   m_addUnitButtonRect.bottom() + 8.0f,
                                   cardWidth,
                                   cardHeight);

    renderer.fillRoundedRect(m_unitTypePickerRect, 10.0f, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(m_unitTypePickerRect, 10.0f, 1.0f, theme.getColor("borderSubtle").withAlpha(0.9f));

    const float padding = 10.0f;
    const float gap = 8.0f;
    const float cellWidth = (cardWidth - padding * 2.0f - gap) * 0.5f;
    const float cellHeight = (cardHeight - padding * 2.0f - gap) * 0.5f;
    const UnitType types[4] = {UnitType::Sampler, UnitType::PitchedSampler, UnitType::Instrument, UnitType::Audio};
    const char* icons[4] = {"▦", "≋", "♫", "◫"};

    for (int i = 0; i < 4; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        const NUIRect cell(m_unitTypePickerRect.x + padding + col * (cellWidth + gap),
                           m_unitTypePickerRect.y + padding + row * (cellHeight + gap),
                           cellWidth,
                           cellHeight);
        renderer.fillRoundedRect(cell, 8.0f, theme.getColor("surfaceTertiary").withAlpha(0.8f));
        renderer.strokeRoundedRect(cell, 8.0f, 1.0f, theme.getColor("borderSubtle").withAlpha(0.72f));
        renderer.drawText(icons[i], NUIPoint(cell.x + 10.0f, cell.y + 11.0f), 13.0f, theme.getColor("accentPrimary").withAlpha(0.9f));
        renderer.drawText(unitTypeDisplayName(types[i]), NUIPoint(cell.x + 32.0f, cell.y + 10.0f), 11.0f, theme.getColor("textPrimary").withAlpha(0.95f));
        renderer.drawText(unitTypeDescription(types[i]), NUIPoint(cell.x + 10.0f, cell.y + 28.0f), 8.5f, theme.getColor("textSecondary").withAlpha(0.68f));
    }
}

// === Drag-Drop Callbacks ===

AestraUI::DropFeedback ArsenalPanel::onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    return onDragOver(data, position);
}

AestraUI::DropFeedback ArsenalPanel::onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    (void)position;
    if (isInstrumentPluginDrag(data) || isAudioFileDrag(data)) {
        return AestraUI::DropFeedback::Copy;
    }
    return AestraUI::DropFeedback::None;
}

void ArsenalPanel::onDragLeave() {
}

AestraUI::DropResult ArsenalPanel::onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) {
    (void)position;
    AestraUI::DropResult result;

    UnitID targetUnit = 0;
    if (m_trackManager) {
        targetUnit = m_selectedUnitId;
        if (targetUnit == 0) {
            auto unitIDs = m_trackManager->getUnitManager().getAllUnitIDs();
            if (!unitIDs.empty()) {
                targetUnit = unitIDs.front();
            }
        }
    }

    if (isAudioFileDrag(data)) {
        if (!m_trackManager) {
            result.accepted = false;
            result.message = "No TrackManager bound";
            return result;
        }

        if (targetUnit == 0) {
            result.accepted = false;
            result.message = "No Arsenal unit available";
            return result;
        }

        auto& unitMgr = m_trackManager->getUnitManager();
        std::string filename = data.filePath.substr(data.filePath.find_last_of("/\\") + 1);
        const auto lastDot = filename.find_last_of('.');
        if (lastDot != std::string::npos) {
            filename = filename.substr(0, lastDot);
        }

        unitMgr.setUnitName(targetUnit, filename);
        unitMgr.setUnitEnabled(targetUnit, true);
        unitMgr.setUnitAudioClip(targetUnit, data.filePath);
        if (m_onSampleDroppedToUnit) {
            m_onSampleDroppedToUnit(targetUnit, data.filePath);
        }
        if (m_onSelectedUnitChanged) {
            m_onSelectedUnitChanged(targetUnit);
        }
        refreshUnits();

        result.accepted = true;
        result.message = "Sample loaded into Arsenal";
        Log::info("[Arsenal] File dropped into Arsenal: " + data.filePath);
        return result;
    }

    if (!isInstrumentPluginDrag(data)) {
        result.accepted = false;
        result.message = "Arsenal accepts instrument plugins and audio files";
        return result;
    }

    if (targetUnit != 0 && m_onPluginDroppedToUnit) {
        if (m_onSelectedUnitChanged) {
            m_onSelectedUnitChanged(targetUnit);
        }
        m_onPluginDroppedToUnit(targetUnit, data.sourceClipIdString);
        refreshUnits();
        result.accepted = true;
        result.message = "Loaded instrument into Arsenal unit";
        Log::info("[Arsenal] Plugin dropped into unit " + std::to_string(targetUnit) +
                  ": " + data.sourceClipIdString);
        return result;
    }

    if (!m_onPluginDropped) {
        result.accepted = false;
        result.message = "No Arsenal plugin drop handler bound";
        return result;
    }

    m_onPluginDropped(data.sourceClipIdString);
    result.accepted = true;
    result.message = "Loaded instrument into Arsenal";
    Log::info("[Arsenal] Plugin dropped into Arsenal: " + data.sourceClipIdString);
    return result;
}

AestraUI::NUIRect ArsenalPanel::getDropBounds() const {
    return getBounds();
}

void ArsenalPanel::onUnitDragStart(UnitID unitId) {
    m_isDragging = true;
    m_draggedUnitId = unitId;
    m_dropTargetIndex = -1;
    Log::info("[Arsenal] Started dragging Unit " + std::to_string(unitId));
}

void ArsenalPanel::onUnitDrop(UnitID unitId, int dropIndex) {
    if (!m_trackManager) return;
    
    m_trackManager->getUnitManager().reorderUnit(unitId, static_cast<size_t>(dropIndex));
    m_isDragging = false;
    m_draggedUnitId = 0;
    m_dropTargetIndex = -1;
    
    refreshUnits();
    Log::info("[Arsenal] Dropped Unit " + std::to_string(unitId) + " at index " + std::to_string(dropIndex));
}

void ArsenalPanel::showColorPicker(UnitID unitId, NUIPoint position) {
    m_colorPickerTargetUnit = unitId;
    if (m_colorPicker) {
        m_colorPicker->showAt(position);
        repaint();
    }
}

// === Copy/Paste Operations ===

void ArsenalPanel::copySelectedPattern() {
    if (!m_trackManager || m_selectedUnitId == 0 || !m_activePatternID.isValid()) return;
    
    auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID);
    if (!pattern || !pattern->isMidi()) return;
    
    auto& midi = std::get<MidiPayload>(pattern->payload);
    
    PatternClipboard clip;
    clip.sourceUnitId = m_selectedUnitId;
    
    // Copy notes belonging to selected unit
    for (const auto& note : midi.notes) {
        if (note.unitId == m_selectedUnitId || note.unitId == 0) {
            clip.notes.push_back(note);
        }
    }
    
    m_clipboard = clip;
    Log::info("[Arsenal] Copied " + std::to_string(clip.notes.size()) + " notes from Unit " + std::to_string(m_selectedUnitId));
}

void ArsenalPanel::pastePattern() {
    if (!m_trackManager || !m_clipboard.has_value() || m_selectedUnitId == 0 || !m_activePatternID.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    
    pm.applyPatch(m_activePatternID, [this](PatternSource& p) {
        if (!p.isMidi()) return;
        auto& midi = std::get<MidiPayload>(p.payload);
        
        // Paste notes with remapped unitId
        for (auto note : m_clipboard->notes) {
            note.unitId = m_selectedUnitId;
            midi.notes.push_back(note);
        }
    });
    
    refreshUnits();
    if (m_onPatternEdited) {
        m_onPatternEdited(m_activePatternID);
    }
    Log::info("[Arsenal] Pasted " + std::to_string(m_clipboard->notes.size()) + " notes to Unit " + std::to_string(m_selectedUnitId));
}

// === Event Handlers ===

bool ArsenalPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Handle color picker first if visible
    if (m_colorPicker && m_colorPicker->isShowing()) {
        if (m_colorPicker->onMouseEvent(event)) {
            repaint();
            return true;
        }
        // Click outside closes picker
        if (event.pressed && !m_colorPicker->getBounds().contains(event.position)) {
            m_colorPicker->hide();
            repaint();
        }
    }

    if (m_showUnitTypePicker && event.pressed && event.button == NUIMouseButton::Left) {
        const float padding = 10.0f;
        const float gap = 8.0f;
        const float cellWidth = (m_unitTypePickerRect.width - padding * 2.0f - gap) * 0.5f;
        const float cellHeight = (m_unitTypePickerRect.height - padding * 2.0f - gap) * 0.5f;
        const UnitType types[4] = {UnitType::Sampler, UnitType::PitchedSampler, UnitType::Instrument, UnitType::Audio};

        if (m_unitTypePickerRect.contains(event.position)) {
            for (int i = 0; i < 4; ++i) {
                const int col = i % 2;
                const int row = i / 2;
                const NUIRect cell(m_unitTypePickerRect.x + padding + col * (cellWidth + gap),
                                   m_unitTypePickerRect.y + padding + row * (cellHeight + gap),
                                   cellWidth,
                                   cellHeight);
                if (cell.contains(event.position)) {
                    createUnitOfType(types[i]);
                    return true;
                }
            }
        } else if (!m_addUnitButtonRect.contains(event.position)) {
            m_showUnitTypePicker = false;
            repaint();
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        int bars = 4;
        if (m_trackManager && m_activePatternID.isValid()) {
            if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
                bars = std::clamp(static_cast<int>(std::round(std::max(8.0, pattern->lengthBeats) / 4.0)),
                                  kArsenalMinPatternBars,
                                  kArsenalMaxPatternBars);
            }
        }

        const auto decHitRect = expandHitRect(m_barsDecrementRect, 4.0f);
        const auto incHitRect = expandHitRect(m_barsIncrementRect, 4.0f);

        if (decHitRect.contains(event.position) && bars > kArsenalMinPatternBars) {
            adjustPatternBars(-1);
            return true;
        }
        if (incHitRect.contains(event.position) && bars < kArsenalMaxPatternBars) {
            adjustPatternBars(1);
            return true;
        }
    }

    // Handle drag-drop logic for reordering units
    if (m_isDragging) {
        auto bounds = m_listContainer->getBounds();
        float rowHeight = 56.0f;
        float spacing = 8.0f;
        
        // Calculate which drop zone we're over
        float localY = event.position.y - bounds.y + m_scrollY - 6.0f;
        int dropIndex = static_cast<int>((localY + (rowHeight + spacing) / 2) / (rowHeight + spacing));
        dropIndex = std::clamp(dropIndex, 0, static_cast<int>(m_unitRows.size()));
        
        if (dropIndex != m_dropTargetIndex) {
            m_dropTargetIndex = dropIndex;
            repaint();
        }
        
        // Release to drop
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            onUnitDrop(m_draggedUnitId, m_dropTargetIndex);
            return true;
        }
    }
    
    // Scroll handling — delegate to UnitRow first (pitch viewport), then list scroll
    if (std::abs(event.wheelDelta) > 0.001f && getBounds().contains(event.position)) {
        // Check if mouse is over a UnitRow — let it handle pitch scrolling
        for (auto& row : m_unitRows) {
            if (row && row->getBounds().contains(event.position)) {
                if (row->onMouseEvent(event)) {
                    repaint();
                    return true;
                }
            }
        }
        
        // Fallback: scroll the unit list
        float contentHeight = (m_unitRows.size() * (56.0f + 8.0f)) + 40.0f + 12.0f; 
        float viewportHeight = m_listContainer ? m_listContainer->getBounds().height : 100.0f;
        float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
        
        m_targetScrollY -= event.wheelDelta * 40.0f;
        m_targetScrollY = safeClampPanelScroll(m_targetScrollY, maxScroll);
        return true;
    }
    
    // Track which unit is selected (for copy/paste)
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_unitRows.size(); ++i) {
            if (m_unitRows[i] && m_unitRows[i]->getBounds().contains(event.position)) {
                m_selectedUnitId = m_unitRows[i]->getUnitId();
                syncRowSelection();
                if (m_onSelectedUnitChanged) {
                    m_onSelectedUnitChanged(m_selectedUnitId);
                }
                repaint();
                break;
            }
        }
    }
    
    return WindowPanel::onMouseEvent(event);
}

bool ArsenalPanel::onKeyEvent(const NUIKeyEvent& event) {
    if (!event.pressed) return false;
    
    // Check for Ctrl modifier
    bool isCtrl = (event.modifiers & NUIModifiers::Ctrl);
    
    // Ctrl+C: Copy
    if (isCtrl && (event.keyCode == NUIKeyCode::C)) {
        copySelectedPattern();
        return true;
    }
    
    // Ctrl+V: Paste
    if (isCtrl && (event.keyCode == NUIKeyCode::V)) {
        pastePattern();
        return true;
    }

    if (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) {
        return removeSelectedUnit();
    }

    return false;
}

} // namespace Audio
} // namespace Aestra
