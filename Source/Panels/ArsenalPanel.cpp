// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ArsenalPanel.h"

#include "Commands/AssignUnitToFirstFreeInsertCommand.h"
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
constexpr int kArsenalMinPatternBars = 1;
constexpr int kArsenalMaxPatternBars = 16;

const char* unitTypeDisplayName(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "Sampler";
    case UnitType::PitchedSampler: return "808";
    case UnitType::Instrument: return "MIDI";
    case UnitType::Audio: return "Audio";
    default: return "Sampler";
    }
}

const char* unitTypeDescription(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "Classic step sequencer";
    case UnitType::PitchedSampler: return "808s, bass, slides";
    case UnitType::Instrument: return "Notes, chords, instruments";
    case UnitType::Audio: return "Drop a clip directly";
    default: return "";
    }
}

std::string unitTypeContentLabel(UnitType type, int stepCount, double durationSeconds) {
    switch (type) {
    case UnitType::Sampler:
        return std::to_string(stepCount) + " Steps";
    case UnitType::PitchedSampler:
        return std::to_string(stepCount) + " Steps · Pitched";
    case UnitType::Instrument:
        return "Note Roll";
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

bool patternUsesNoteRoll(const PatternSource* pattern, UnitID unitId, UnitType type, int rootPitch) {
    if (type == UnitType::Instrument) {
        return true;
    }
    if (type != UnitType::Sampler || !pattern || !pattern->isMidi()) {
        return false;
    }

    // rootPitch: the unit's sampler root note — grid steps are placed at the
    // root, so only off-root/off-grid notes indicate real note-roll content.
    const auto& midi = std::get<MidiPayload>(pattern->payload);
    return std::any_of(midi.notes.begin(), midi.notes.end(), [unitId, rootPitch](const MidiNote& note) {
        const bool belongsToUnit = note.unitId == unitId || note.unitId == 0;
        return belongsToUnit &&
               (note.pitch != rootPitch || std::abs(note.durationBeats - 0.25) > 0.01);
    });
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
    const float chipRadius = AestraUI::NUIThemeManager::getInstance().getCurrentTheme().radiusS + 1.0f;
    renderer.fillRoundedRect(rect, chipRadius, fill);
    renderer.strokeRoundedRect(rect, chipRadius, 1.0f, stroke);
    renderer.drawTextCentered(text, rect, fontSize, textColor);
}
} // namespace

ArsenalPanel::ArsenalPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("ARSENAL")
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
            if (const auto* unit = unitMgr.getUnit(defaultUnit)) {
                const std::string destinationName = unit->name.empty() ? "Mixer Channel" : unit->name;
                // Bootstrap routing belongs to the pristine default project, not to user edit history.
                routeUnitToFirstFreeMixerChannel(*m_trackManager, defaultUnit, destinationName, unit->color, true);
            }
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
    unregisterDropTargets();
}

void ArsenalPanel::createLayout() {
    // This method is no longer needed as layout is handled by WindowPanel and setContent
    // The content of this method has been moved to the constructor or is handled by the base class.
}

int ArsenalPanel::beatsPerBar() const {
    return m_trackManager ? m_trackManager->getTimelineClock().getBeatsPerBar() : 4;
}

int ArsenalPanel::computeLoopStepCount() const {
    // One step per 1/16 note (0.25 beat), spanning the whole loop so the grid
    // shows every bar of the pattern instead of a fixed single bar. Minimum is
    // one bar of the current time signature.
    const double barBeats = static_cast<double>(beatsPerBar());
    double lengthBeats = barBeats;
    if (m_trackManager && m_activePatternID.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
            lengthBeats = std::max(barBeats, pattern->lengthBeats);
        }
    }
    const int steps = static_cast<int>(std::lround(lengthBeats / 0.25));
    return std::clamp(steps, beatsPerBar() * 4, 256);
}

float ArsenalPanel::computeGridMaxScrollX() const {
    if (m_fitToWidth) return 0.0f; // Whole loop shown → nothing to scroll
    // Mirrors the UnitRow grid geometry (control block + context paddings +
    // min 20px pads) so the shared scroll clamps against the real content.
    const float width = m_progressHeaderRect.width;
    if (width <= 0.0f) return 0.0f;
    const float controlWidth = std::clamp(width * 0.38f, 220.0f, 312.0f);
    const float availWidth = std::max(1.0f, width - controlWidth - 26.0f);
    const float stepWidth = std::max(availWidth / static_cast<float>(std::max(1, m_stepCount)), 20.0f);
    return std::max(0.0f, stepWidth * static_cast<float>(m_stepCount) - availWidth);
}

void ArsenalPanel::scrollGridBy(float deltaPx) {
    const float clamped = std::clamp(m_gridScrollX + deltaPx, 0.0f, computeGridMaxScrollX());
    m_gridScrollX = clamped;
    for (auto& row : m_unitRows) {
        if (row) row->setScrollX(clamped);
    }
    repaint();
}

void ArsenalPanel::followGridPlayhead() {
    const int playStep = calculateCurrentStep();
    if (playStep < 0) {
        m_gridFollowSuspended = false; // Transport stopped: resume following next play
        return;
    }
    const float width = m_progressHeaderRect.width;
    if (width <= 0.0f) return;
    const float controlWidth = std::clamp(width * 0.38f, 220.0f, 312.0f);
    const float availWidth = std::max(1.0f, width - controlWidth - 26.0f);
    const float stepWidth = std::max(availWidth / static_cast<float>(std::max(1, m_stepCount)), 20.0f);
    const float maxScroll = std::max(0.0f, stepWidth * static_cast<float>(m_stepCount) - availWidth);
    if (maxScroll <= 0.0f) return; // Whole loop fits: nothing to follow

    const float stepLeft = static_cast<float>(playStep) * stepWidth;
    const bool visible = stepLeft >= m_gridScrollX &&
                         stepLeft + stepWidth <= m_gridScrollX + availWidth;
    if (visible) {
        m_gridFollowSuspended = false; // Playhead in view again: re-arm following
        return;
    }
    if (m_gridFollowSuspended) return; // Don't fight a deliberate manual scroll
    // Page to the playing bar's start so a full musical bar reads at once.
    const int stepsPerBar = beatsPerBar() * 4;
    const float barLeft = static_cast<float>((playStep / stepsPerBar) * stepsPerBar) * stepWidth;
    scrollGridBy(std::clamp(barLeft, 0.0f, maxScroll) - m_gridScrollX);
}

void ArsenalPanel::refreshUnits() {
    if (!m_listContainer || !m_trackManager) return;
    auto& theme = NUIThemeManager::getInstance();
    m_stepCount = computeLoopStepCount();
    const bool shouldRestoreDropTargets = m_dropTargetRegistered;
    if (shouldRestoreDropTargets) {
        unregisterDropTargets();
    }
    
    // Clear previous children
    m_listContainer->removeAllChildren();
    m_unitRows.clear();

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
        row->setFitToWidth(m_fitToWidth);

        // Shared horizontal grid scroll: the panel owns the offset so the
        // progress header and every row stay in lockstep.
        row->setScrollX(m_gridScrollX);
        row->setOnGridScroll([this](float deltaPx) {
            m_gridFollowSuspended = true; // Manual scroll wins over playhead follow
            scrollGridBy(deltaPx);
        });

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
            if (!unit) return;
            if (!unit->pluginId.empty() && unit->plugin) {
                // Unit has a plugin — open plugin editor
                if (m_onRequestEditor) m_onRequestEditor(id);
            } else if (unit->defaultPatternId.isValid()) {
                // Unit has a default pattern — open Piano Roll
                if (m_onRequestPatternEditor) m_onRequestPatternEditor(unit->defaultPatternId);
            } else if ((unit->type == UnitType::Sampler ||
                        unit->type == UnitType::PitchedSampler) &&
                       !unit->audioClipPath.empty()) {
                // Sample loaded — open sample editor
                if (m_onRequestSampleEditor) m_onRequestSampleEditor(id);
            } else if (unit->type == UnitType::Sampler ||
                       unit->type == UnitType::PitchedSampler) {
                // No sample — open file browser to load one
                if (m_onRequestLoadSample) m_onRequestLoadSample(id);
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
        row->setOnRenameUnit([this](UnitID id, const std::string& name) {
            if (!m_trackManager) return;
            m_trackManager->getUnitManager().setUnitName(id, name);
            refreshUnits();
        });
        row->setOnDeleteUnit([this](UnitID id) {
            m_selectedUnitId = id;
            removeSelectedUnit();
        });
        row->setOnDuplicateUnit([this](UnitID id) {
            if (!m_trackManager) return;
            UnitID newId = m_trackManager->getUnitManager().duplicateUnit(id);
            if (newId != 0) {
                if (const auto* unit = m_trackManager->getUnitManager().getUnit(newId)) {
                    const std::string destinationName = unit->name.empty() ? "Mixer Channel" : unit->name;
                    routeUnitToFirstFreeMixerChannel(*m_trackManager, newId, destinationName, unit->color);
                }
                m_selectedUnitId = newId;
                refreshUnits();
            }
        });

        m_listContainer->addChild(row);
        m_unitRows.push_back(row);
        
    }
    
    // Add "Add Unit" button
    m_addUnitBtn = std::make_shared<NUIButton>("+ ADD UNIT");
    m_addUnitBtn->setStyle(NUIButton::Style::Secondary);
    m_addUnitBtn->setBackgroundColor(theme.getColor("surfaceTertiary").withAlpha(0.88f));
    m_addUnitBtn->setHoverColor(theme.getColor("accentPrimary").withAlpha(0.20f));
    m_addUnitBtn->setPressedColor(theme.getColor("accentPrimary").withAlpha(0.30f));
    m_addUnitBtn->setTextColor(theme.getColor("textPrimary").withAlpha(0.92f));
    m_addUnitBtn->setOnClick([this]() { onAddUnit(); });
    m_listContainer->addChild(m_addUnitBtn);
    
    layoutUnits();
    scrollGridBy(0.0f); // Re-clamp the shared scroll for the (possibly new) loop length
    syncRowSelection();
    if (shouldRestoreDropTargets) {
        registerDropTargets(true);
    }
    
    if (auto parent = getParent()) {
        parent->repaint();
    }
}

void ArsenalPanel::onAddUnit() {
    m_showUnitTypePicker = !m_showUnitTypePicker;
    if (m_addUnitBtn) m_addUnitBtn->setText(m_showUnitTypePicker ? "CLOSE" : "+ ADD UNIT");
    repaint();
}

void ArsenalPanel::createUnitOfType(UnitType type) {
    if (!m_trackManager) return;
    const auto count = m_trackManager->getUnitManager().getUnitCount() + 1;
    std::string name = (type == UnitType::Sampler ? "Sampler " :
                        type == UnitType::PitchedSampler ? "808 " :
                        type == UnitType::Instrument ? "MIDI " : "Audio ") + std::to_string(count);
    m_selectedUnitId = m_trackManager->getUnitManager().createUnit(name, type);
    if (const auto* unit = m_trackManager->getUnitManager().getUnit(m_selectedUnitId)) {
        const std::string destinationName = unit->name.empty() ? "Mixer Channel" : unit->name;
        routeUnitToFirstFreeMixerChannel(*m_trackManager, m_selectedUnitId, destinationName, unit->color);
    }
    if (m_onSelectedUnitChanged) {
        m_onSelectedUnitChanged(m_selectedUnitId);
    }
    m_showUnitTypePicker = false;
    if (m_addUnitBtn) m_addUnitBtn->setText("+ ADD UNIT");
    refreshUnits();
}

bool ArsenalPanel::removeSelectedUnit() {
    if (!m_trackManager || m_selectedUnitId == 0) {
        return false;
    }

    auto& unitMgr = m_trackManager->getUnitManager();
    auto unitIDs = unitMgr.getAllUnitIDs();
    if (unitIDs.size() <= 1) {
        // Last unit — reset to fresh default instead of refusing
        UnitID oldId = m_selectedUnitId;
        unitMgr.removeUnit(oldId);
        removeUnitNotes(oldId);
        UnitID newId = unitMgr.createUnit();
        if (const auto* unit = unitMgr.getUnit(newId)) {
            const std::string destinationName = unit->name.empty() ? "Mixer Channel" : unit->name;
            routeUnitToFirstFreeMixerChannel(*m_trackManager, newId, destinationName, unit->color);
        }
        m_selectedUnitId = newId;
        refreshUnits();
        if (m_onSelectedUnitChanged && m_selectedUnitId != 0) {
            m_onSelectedUnitChanged(m_selectedUnitId);
        }
        return true;
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
    WindowPanel::onRender(renderer);
    drawCommandHeader(renderer);
    if (m_listContainer && isVisible()) drawProgressHeader(renderer, m_progressHeaderRect);

    // The custom header surfaces are drawn after WindowPanel's child pass.
    if (m_addUnitBtn) m_addUnitBtn->onRender(renderer);

    if (m_showUnitTypePicker) {
        drawUnitTypePicker(renderer);
    }

    // Only render the color picker if it's visible.
    if (m_colorPicker && m_colorPicker->isShowing()) {
        m_colorPicker->onRender(renderer);
    }
}

void ArsenalPanel::drawCommandHeader(NUIRenderer& renderer) {
    if (!m_listContainer || m_commandHeaderRect.width <= 0.0f) return;

    auto& theme = NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    renderer.fillRoundedRect(m_commandHeaderRect, themeProps.radiusM,
                             theme.getColor("backgroundSecondary").withAlpha(0.97f));
    renderer.strokeRoundedRect(m_commandHeaderRect, themeProps.radiusM, 1.0f,
                               theme.getColor("borderSubtle").withAlpha(0.82f));

    const float iconX = m_commandHeaderRect.x + 15.0f;
    const float iconY = m_commandHeaderRect.y + 13.0f;
    const auto accent = theme.getColor("accentPrimary");
    for (int row = 0; row < 3; ++row) {
        const float width = 18.0f - static_cast<float>(row) * 3.0f;
        renderer.fillRoundedRect({iconX, iconY + static_cast<float>(row) * 8.0f, width, 4.0f}, 2.0f,
                                 accent.withAlpha(0.95f - static_cast<float>(row) * 0.18f));
    }

    std::string patternName = "Pattern";
    std::string selectedName = "No unit selected";
    std::string selectedType = "READY";
    int unitCount = 0;
    if (m_trackManager) {
        unitCount = static_cast<int>(m_trackManager->getUnitManager().getUnitCount());
        if (m_activePatternID.isValid()) {
            if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID);
                pattern && !pattern->name.empty()) {
                patternName = pattern->name;
            }
        }
        if (m_selectedUnitId != 0) {
            if (const auto* unit = m_trackManager->getUnitManager().getUnit(m_selectedUnitId)) {
                selectedName = unit->name.empty() ? "Untitled unit" : unit->name;
                selectedType = unitTypeDisplayName(unit->type);
                if (m_activePatternID.isValid()) {
                    const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID);
                    if (patternUsesNoteRoll(pattern, m_selectedUnitId, unit->type,
                                            m_trackManager->getUnitManager().getUnitRootMidiNote(m_selectedUnitId))) {
                        selectedType = "MIDI";
                    }
                }
            }
        }
    }

    const float textX = iconX + 28.0f;
    renderer.drawText(patternName, {textX, m_commandHeaderRect.y + 10.0f}, themeProps.fontSizeS,
                      theme.getColor("textPrimary").withAlpha(0.96f));
    renderer.drawText(selectedName + "  ·  " + selectedType + "  ·  " + std::to_string(unitCount) +
                          (unitCount == 1 ? " UNIT" : " UNITS"),
                      {textX, m_commandHeaderRect.y + 28.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.70f));

    // View toggle: reflects current mode (accent = Fit, muted = Scroll).
    if (m_fitToggleRect.width > 0.0f) {
        const float radius = themeProps.radiusS;
        const NUIColor fill = m_fitToWidth ? theme.getColor("accentPrimary").withAlpha(0.18f)
                                           : theme.getColor("surfaceTertiary").withAlpha(0.85f);
        const NUIColor stroke = m_fitToWidth ? theme.getColor("accentPrimary").withAlpha(0.70f)
                                             : theme.getColor("borderSubtle").withAlpha(0.85f);
        const NUIColor text = m_fitToWidth ? theme.getColor("accentPrimary")
                                           : theme.getColor("textSecondary").withAlpha(0.9f);
        renderer.fillRoundedRect(m_fitToggleRect, radius, fill);
        renderer.strokeRoundedRect(m_fitToggleRect, radius, 1.0f, stroke);
        renderer.drawTextCentered(m_fitToWidth ? "FIT" : "SCROLL", m_fitToggleRect, themeProps.fontSizeMicro, text);
    }
}

void ArsenalPanel::layoutUnits() {
    if (!m_listContainer) return;
    
    NUIRect bounds = m_listContainer->getBounds();
    const float horizontalPadding = 8.0f;
    const float width = std::max(0.0f, bounds.width - horizontalPadding * 2.0f);
    const float startX = bounds.x + horizontalPadding;
    m_commandHeaderRect = {startX, bounds.y + 8.0f, width, COMMAND_HEADER_HEIGHT};

    constexpr float buttonH = 30.0f;
    constexpr float addW = 112.0f;
    const float buttonY = m_commandHeaderRect.y + (m_commandHeaderRect.height - buttonH) * 0.5f;
    m_addUnitButtonRect = {m_commandHeaderRect.right() - 10.0f - addW, buttonY, addW, buttonH};
    if (m_addUnitBtn) m_addUnitBtn->setBounds(m_addUnitButtonRect);

    // View toggle (Fit whole loop <-> readable + scroll) sits just left of Add.
    constexpr float fitW = 84.0f;
    m_fitToggleRect = {m_addUnitButtonRect.x - 8.0f - fitW, buttonY, fitW, buttonH};

    m_progressHeaderRect = {startX, m_commandHeaderRect.bottom() + 8.0f, width, PROGRESS_HEADER_HEIGHT};
    float yPos = m_progressHeaderRect.bottom() + 8.0f - m_scrollY;
    float spacing = 8.0f;
    float rowHeight = 56.0f;

    // Rows may only draw / take clicks between the progress header and the
    // panel bottom; anything scrolled past that is clipped by the row itself.
    const float viewportTop = m_progressHeaderRect.bottom() + 4.0f;
    const float viewportBottom = bounds.bottom() - 6.0f;
    m_listViewportRect = {startX, viewportTop, width, std::max(0.0f, viewportBottom - viewportTop)};

    // Layout unit rows
    for (auto& row : m_unitRows) {
        if (row) {
            row->setBounds(NUIRect(startX, yPos, width, rowHeight));
            row->setViewport(m_listViewportRect);
            yPos += rowHeight + spacing;
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
    const float reservedHeight = COMMAND_HEADER_HEIGHT + PROGRESS_HEADER_HEIGHT + 32.0f;
    const float viewportHeight = std::max(0.0f,
        (m_listContainer ? m_listContainer->getBounds().height : 0.0f) - reservedHeight);
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

    followGridPlayhead();
}

void ArsenalPanel::registerDropTargets(bool reorder) {
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
        for (const auto& row : m_unitRows) {
            if (row) {
                AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(row.get());
            }
        }
    }

    if (!m_dropTargetRegistered || reorder) {
        AestraUI::NUIDragDropManager::getInstance().registerDropTarget(dropTarget);
        for (const auto& row : m_unitRows) {
            if (row) {
                AestraUI::NUIDragDropManager::getInstance().registerDropTarget(row);
            }
        }
        m_dropTargetRegistered = true;
    }
}

void ArsenalPanel::unregisterDropTargets() {
    AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(this);
    for (const auto& row : m_unitRows) {
        if (row) {
            AestraUI::NUIDragDropManager::getInstance().unregisterDropTarget(row.get());
        }
    }
    m_dropTargetRegistered = false;
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
    const double barBeats = static_cast<double>(beatsPerBar());
    double patternLengthBeats = barBeats;
    if (m_activePatternID.isValid()) {
        if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
            patternLengthBeats = std::max(barBeats, pattern->lengthBeats);
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

    const double barBeats = static_cast<double>(beatsPerBar());
    const int currentBars = std::clamp(
        static_cast<int>(std::round(std::max(barBeats, pattern->lengthBeats) / barBeats)),
        kArsenalMinPatternBars,
        kArsenalMaxPatternBars);
    const int nextBars = std::clamp(currentBars + deltaBars, kArsenalMinPatternBars, kArsenalMaxPatternBars);
    const double nextLengthBeats = static_cast<double>(nextBars) * barBeats;
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
    
    // Step layout (matches UnitRow grid layout: control block + 8px context
    // inset + 6px lane padding, so header pads line up with row pads exactly)
    const float controlWidth = std::clamp(bounds.width * 0.38f, 220.0f, 312.0f);
    const float gridStartX = bounds.x + controlWidth + 14.0f;
    const float availWidth = std::max(0.0f, bounds.width - controlWidth - 26.0f);

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
    if (m_trackManager && m_activePatternID.isValid()) {
        const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID);
        if (patternUsesNoteRoll(pattern, m_selectedUnitId, selectedType,
                                m_trackManager->getUnitManager().getUnitRootMidiNote(m_selectedUnitId))) {
            contentLabel = "Note Roll";
        }
    }

    const int stepsPerBar = beatsPerBar() * 4;
    const int bars = std::clamp(static_cast<int>(std::round(lengthBeats / static_cast<double>(beatsPerBar()))),
                                kArsenalMinPatternBars, kArsenalMaxPatternBars);
    const bool decEnabled = bars > kArsenalMinPatternBars;
    const bool incEnabled = bars < kArsenalMaxPatternBars;

    const auto& themeProps = theme.getCurrentTheme();
    const float cardRadius = themeProps.radiusM;
    const float pillRadius = themeProps.radiusS;
    const auto disabledText = theme.getColor("textDisabled");

    const NUIRect leftCard(bounds.x + 4.0f, bounds.y + 1.0f, std::max(216.0f, controlWidth - 10.0f), bounds.height - 2.0f);
    renderer.fillRoundedRect(leftCard, cardRadius, theme.getColor("backgroundSecondary").withAlpha(0.92f));
    renderer.strokeRoundedRect(leftCard, cardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.85f));

    const float compactHitArea = themeProps.layout.minimumHitArea;
    m_barsDecrementRect = NUIRect(leftCard.x + 10.0f, leftCard.y + 4.0f, compactHitArea, leftCard.height - 8.0f);
    m_barsValueRect = NUIRect(m_barsDecrementRect.right() + 4.0f, leftCard.y + 4.0f, 56.0f, leftCard.height - 8.0f);
    m_barsIncrementRect = NUIRect(m_barsValueRect.right() + 4.0f, leftCard.y + 4.0f, compactHitArea, leftCard.height - 8.0f);

    const auto pillFill = theme.getColor("surfaceTertiary").withAlpha(0.78f);
    const auto pillStroke = theme.getColor("borderSubtle").withAlpha(0.85f);
    const auto pillText = theme.getColor("textSecondary").withAlpha(0.9f);
    renderer.fillRoundedRect(m_barsDecrementRect, pillRadius, pillFill);
    renderer.strokeRoundedRect(m_barsDecrementRect, pillRadius, 1.0f, pillStroke);
    renderer.drawTextCentered("-", m_barsDecrementRect, themeProps.fontSizeXS, decEnabled ? pillText : disabledText);

    renderer.fillRoundedRect(m_barsValueRect, pillRadius, pillFill);
    renderer.strokeRoundedRect(m_barsValueRect, pillRadius, 1.0f, pillStroke);
    renderer.drawTextCentered(std::to_string(bars) + " Bars", m_barsValueRect, themeProps.fontSizeMicro, pillText);

    renderer.fillRoundedRect(m_barsIncrementRect, pillRadius, pillFill);
    renderer.strokeRoundedRect(m_barsIncrementRect, pillRadius, 1.0f, pillStroke);
    renderer.drawTextCentered("+", m_barsIncrementRect, themeProps.fontSizeXS, incEnabled ? pillText : disabledText);

    const float contentBadgeWidth = contentLabel == "Note Roll" ? 74.0f : 62.0f;
    const NUIRect contentBadge(m_barsIncrementRect.right() + 8.0f, leftCard.y + 4.0f, contentBadgeWidth, leftCard.height - 8.0f);
    drawArsenalChip(renderer,
                    contentBadge,
                    contentLabel,
                    theme.getColor("surfaceTertiary").withAlpha(0.78f),
                    theme.getColor("borderSubtle").withAlpha(0.85f),
                    theme.getColor("textSecondary").withAlpha(0.9f),
                    themeProps.fontSizeMicro);
    const NUIRect gridCard(gridStartX - 2.0f, bounds.y + 1.0f, std::max(0.0f, availWidth + 4.0f), bounds.height - 2.0f);
    renderer.fillRoundedRect(gridCard, cardRadius, theme.getColor("backgroundSecondary").withAlpha(0.82f));
    renderer.strokeRoundedRect(gridCard, cardRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.7f));

    // Same step width formula as the rows + the shared scroll offset, so the
    // header ruler and every row grid stay in lockstep. Fit mode shrinks pads
    // to show the whole loop; scroll mode keeps a readable 20px minimum.
    const float fitStepWidth = availWidth / static_cast<float>(std::max(1, m_stepCount));
    float stepWidth = m_fitToWidth ? std::max(fitStepWidth, 4.0f) : std::max(fitStepWidth, 20.0f);
    const float totalWidth = stepWidth * static_cast<float>(m_stepCount);
    const float maxScrollX = std::max(0.0f, totalWidth - availWidth);
    m_gridScrollX = std::clamp(m_gridScrollX, 0.0f, maxScrollX);
    float indicatorHeight = PROGRESS_HEADER_HEIGHT - 10.0f;
    float indicatorY = bounds.y + 5.0f;
    const float indicatorRadius = themeProps.radiusXS;

    // Clip to the grid card so scrolled steps never paint over the bars pills.
    renderer.setClipRect(gridCard);

    // Draw step indicators
    for (int i = 0; i < m_stepCount; ++i) {
        float stepX = gridStartX + (i * stepWidth) - m_gridScrollX + 2.0f;
        float indicatorWidth = stepWidth - 4.0f;
        if (stepX > gridCard.right()) break;
        if (stepX + stepWidth < gridCard.x) continue;

        NUIRect indicatorRect(stepX, indicatorY, indicatorWidth, indicatorHeight);

        // Base color: more visible background
        NUIColor bgColor = theme.getColor("surfaceTertiary").withAlpha(0.5f);

        // Bar/beat markers (4 steps = 1 beat at 1/16 resolution)
        const bool isBeatStart = (i % 4 == 0);
        const bool isBarStart = (i % stepsPerBar == 0);
        if (isBarStart) {
            bgColor = bgColor.lightened(0.16f);
        } else if (isBeatStart) {
            bgColor = bgColor.lightened(0.08f);
        }

        renderer.fillRoundedRect(indicatorRect, indicatorRadius, bgColor);

        // Highlight current playing step
        if (i == m_currentPlayStep) {
            NUIColor playColor = theme.getColor("accentPrimary");
            renderer.fillRoundedRect(indicatorRect, indicatorRadius, playColor);

            // Glow effect
            NUIRect glowRect(indicatorRect.x - 1, indicatorRect.y - 1,
                           indicatorRect.width + 2, indicatorRect.height + 2);
            renderer.strokeRoundedRect(glowRect, themeProps.radiusXS + 1.0f, 1.0f, playColor.withAlpha(0.6f));
        }
        // Show progress for steps already played in current loop
        else if (m_currentPlayStep >= 0 && i < m_currentPlayStep) {
            NUIColor playedColor = theme.getColor("accentPrimary").withAlpha(0.5f);
            renderer.fillRoundedRect(indicatorRect, indicatorRadius, playedColor);
        }

        // Subtle border
        renderer.strokeRoundedRect(indicatorRect, indicatorRadius, 0.5f,
                                   theme.getColor("borderSubtle").withAlpha(0.6f));

        if (isBarStart) {
            const NUIRect labelRect(stepX, indicatorY, stepWidth * static_cast<float>(stepsPerBar), indicatorHeight);
            renderer.drawTextCentered(std::to_string((i / stepsPerBar) + 1), labelRect, 7.5f, theme.getColor("textSecondary").withAlpha(0.88f));
        }
    }

    // Scroll indicator: show which slice of the loop the grids are viewing.
    if (maxScrollX > 0.0f) {
        const float trackX = gridCard.x + 3.0f;
        const float trackWidth = std::max(0.0f, gridCard.width - 6.0f);
        const float trackY = gridCard.bottom() - 4.0f;
        renderer.fillRoundedRect({trackX, trackY, trackWidth, 2.5f}, 1.25f,
                                 theme.getColor("borderSubtle").withAlpha(0.55f));
        const float thumbWidth = std::max(16.0f, trackWidth * (availWidth / totalWidth));
        const float thumbX = trackX + (trackWidth - thumbWidth) * (m_gridScrollX / maxScrollX);
        renderer.fillRoundedRect({thumbX, trackY, thumbWidth, 2.5f}, 1.25f,
                                 theme.getColor("accentPrimary").withAlpha(0.75f));
    }

    renderer.clearClipRect();
}

void ArsenalPanel::drawUnitTypePicker(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& themeProps = theme.getCurrentTheme();
    if (m_addUnitButtonRect.width <= 0.0f || m_addUnitButtonRect.height <= 0.0f) {
        return;
    }

    const float cardWidth = 332.0f;
    const float cardHeight = 156.0f;
    const float pickerX = m_listContainer
                              ? std::max(m_listContainer->getBounds().x + 8.0f,
                                         m_addUnitButtonRect.right() - cardWidth)
                              : m_addUnitButtonRect.x;
    m_unitTypePickerRect = NUIRect(pickerX, m_addUnitButtonRect.bottom() + 8.0f, cardWidth, cardHeight);

    const float pickerRadius = std::max(themeProps.radiusL - 2.0f, 0.0f);
    renderer.fillRoundedRect(m_unitTypePickerRect, pickerRadius, theme.getColor("backgroundSecondary").withAlpha(0.98f));
    renderer.strokeRoundedRect(m_unitTypePickerRect, pickerRadius, 1.0f, theme.getColor("borderSubtle").withAlpha(0.9f));

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
        renderer.fillRoundedRect(cell, themeProps.radiusM, theme.getColor("surfaceTertiary").withAlpha(0.8f));
        renderer.strokeRoundedRect(cell, themeProps.radiusM, 1.0f, theme.getColor("borderSubtle").withAlpha(0.72f));
        renderer.drawText(icons[i], NUIPoint(cell.x + 10.0f, cell.y + 11.0f), themeProps.fontSizeS - 1.0f, theme.getColor("accentPrimary").withAlpha(0.9f));
        renderer.drawText(unitTypeDisplayName(types[i]), NUIPoint(cell.x + 32.0f, cell.y + 10.0f), themeProps.fontSizeXS - 1.0f, theme.getColor("textPrimary").withAlpha(0.95f));
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
            if (m_addUnitBtn) m_addUnitBtn->setText("+ ADD UNIT");
            repaint();
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left && m_fitToggleRect.contains(event.position)) {
        m_fitToWidth = !m_fitToWidth;
        m_gridScrollX = 0.0f;
        for (auto& row : m_unitRows) {
            if (row) row->setFitToWidth(m_fitToWidth);
        }
        scrollGridBy(0.0f); // Re-clamp shared scroll for the new mode
        repaint();
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        int bars = 4;
        if (m_trackManager && m_activePatternID.isValid()) {
            if (const auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID)) {
                const double barBeats = static_cast<double>(beatsPerBar());
                bars = std::clamp(static_cast<int>(std::round(std::max(barBeats, pattern->lengthBeats) / barBeats)),
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
    
    // Track which unit is selected (for copy/paste). Only rows inside the
    // list viewport are clickable — scrolled-out rows are not rendered.
    if (event.pressed && event.button == NUIMouseButton::Left &&
        (m_listViewportRect.height <= 0.0f || m_listViewportRect.contains(event.position))) {
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

    // Assign the selected unit to the first unused mixer destination.
    if (isCtrl && event.keyCode == NUIKeyCode::L) {
        if (!m_trackManager || m_selectedUnitId == 0)
            return false;
        const auto* unit = m_trackManager->getUnitManager().getUnit(m_selectedUnitId);
        if (!unit)
            return false;
        const std::string destinationName = unit->name.empty() ? "Mixer Channel" : unit->name;
        assignUnitToFirstFreeInsert(*m_trackManager, m_selectedUnitId, destinationName, unit->color);
        return true;
    }

    if (event.keyCode == NUIKeyCode::Delete || event.keyCode == NUIKeyCode::Backspace) {
        return removeSelectedUnit();
    }

    return false;
}

} // namespace Audio
} // namespace Aestra
