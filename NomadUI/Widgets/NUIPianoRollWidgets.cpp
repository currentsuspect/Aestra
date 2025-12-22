// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIPianoRollWidgets.h"
#include "NUIDropdown.h"
#include "NUIButton.h"
#include "../Core/NUIIcon.h"
#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip> // For string formatting

namespace NomadUI {

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
static bool isBlackKey(int midiPitch) {
    int m = midiPitch % 12;
    return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
}

static std::string getNoteLabel(int midiPitch) {
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (midiPitch / 12) - 2; // C3=60 standard
    return std::string(noteNames[midiPitch % 12]) + std::to_string(octave);
}

// =============================================================================
// MusicTheory Implementation
// =============================================================================
// MusicTheory moved to NomadUI/Common/MusicHelpers.cpp

// =============================================================================
// PianoRollKeyLane
// =============================================================================
PianoRollKeyLane::PianoRollKeyLane()
    : keyHeight_(24.0f), scrollY_(0.0f), hoveredKey_(-1)
{
}

void PianoRollKeyLane::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    
    // CLIP: Prevent bleeding into top bar
    renderer.setClipRect(b);
    
    // Theme Colors (Refined: Standard Piano Look)
    auto bgWhiteKey = NUIColor(0.95f, 0.95f, 0.95f, 1.0f); // White keys
    auto bgBlackKey = NUIColor(0.10f, 0.10f, 0.12f, 1.0f); // Black keys
    auto textColWhiteKey = NUIColor(0.2f, 0.2f, 0.2f, 1.0f);
    auto textColBlackKey = NUIColor(0.8f, 0.8f, 0.8f, 1.0f);
    auto borderCol = NUIColor(0.5f, 0.5f, 0.5f, 0.5f);
    auto hoverCol = NUIColor(1.0f, 0.6f, 0.2f, 0.3f); // Orange-ish highlight on hover
    auto clickCol = NUIColor(1.0f, 0.5f, 0.0f, 0.6f); // Stronger action color

    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    
    startPitch = std::clamp(startPitch + 1, 0, 127);
    endPitch = std::clamp(endPitch - 1, 0, 127);

    // Render Backing first
    renderer.fillRect(b, NUIColor(0.2f, 0.2f, 0.2f, 1.0f));

    for (int p = startPitch; p >= endPitch; --p) {
        float y = b.y + (127 - p) * keyHeight_ - scrollY_;
        NUIRect keyRect(b.x, y, b.width, keyHeight_);

        bool isBlack = isBlackKey(p);
        renderer.fillRect(keyRect, isBlack ? bgBlackKey : bgWhiteKey);
        
        // Separator
        renderer.drawLine(NUIPoint(b.x, y + keyHeight_), NUIPoint(b.x + b.width, y + keyHeight_), 1.0f, borderCol);

        // Labels for C keys
        if (p % 12 == 0) {
            std::string lbl = getNoteLabel(p);
            float txtY = y + (keyHeight_ * 0.5f) - 6.0f; 
            renderer.drawText(lbl, NUIPoint(b.x + b.width - 32, txtY), 12.0f, isBlack ? textColBlackKey : textColWhiteKey);
        }
    }
    
    // Right border
    // Right border
    renderer.drawLine(NUIPoint(b.x + b.width, b.y), NUIPoint(b.x + b.width, b.y + b.height), 2.0f, NUIColor::black());
    
    renderer.clearClipRect();
}

bool PianoRollKeyLane::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position)) return false;

    auto b = getBounds();
    float localY = event.position.y - b.y + scrollY_;
    int pitch = 127 - static_cast<int>(localY / keyHeight_);
    
    return NUIComponent::onMouseEvent(event);
}

void PianoRollKeyLane::setKeyHeight(float height) {
    keyHeight_ = std::max(8.0f, height);
    repaint();
}

void PianoRollKeyLane::setScrollOffsetY(float offset) {
    scrollY_ = offset;
    repaint();
}

// =============================================================================
// PianoRollMinimap
// =============================================================================
PianoRollMinimap::PianoRollMinimap() 
    : startBeat_(0.0), viewDuration_(1.0), totalDuration_(100.0)
{
}

float PianoRollMinimap::beatToX(double beat) const {
    double ratio = beat / totalDuration_;
    return static_cast<float>(ratio * getWidth());
}

double PianoRollMinimap::xToBeat(float x) const {
    double ratio = x / getWidth();
    return ratio * totalDuration_;
}

void PianoRollMinimap::setView(double start, double duration) {
    if (isDragging_) return; // Don't fight drag
    startBeat_ = start;
    viewDuration_ = duration;
    repaint();
}

void PianoRollMinimap::setTotalDuration(double total) {
    totalDuration_ = std::max(1.0, total);
    repaint();
}

void PianoRollMinimap::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    
    // Background
    renderer.fillRect(b, NUIColor(0.1f, 0.1f, 0.12f, 1.0f));
    renderer.strokeRect(b, 1.0f, NUIColor(0.2f, 0.2f, 0.22f, 1.0f));
    
    // View Rect
    float x1 = b.x + beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    NUIRect viewRect(x1, b.y + 2, w, b.height - 4);
    
    auto thumbCol = NUIColor(0.3f, 0.3f, 0.35f, 0.6f);
    auto borderCol = NUIColor::fromHex(0x9900FF); // Purple
    
    renderer.fillRect(viewRect, thumbCol);
    renderer.strokeRect(viewRect, 1.0f, borderCol);
    
    // Handles (Visual only, logic in mouse)
    float handleW = 6.0f;
    renderer.fillRect(NUIRect(x1, b.y+2, handleW, b.height-4), NUIColor(1.0f,1.0f,1.0f, 0.2f)); // L
    renderer.fillRect(NUIRect(x1+w-handleW, b.y+2, handleW, b.height-4), NUIColor(1.0f,1.0f,1.0f, 0.2f)); // R
}

bool PianoRollMinimap::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position) && !isDragging_) return false;

    auto b = getBounds();
    float localX = event.position.x - b.x;
    
    float x1 = beatToX(startBeat_);
    float w = beatToX(viewDuration_);
    float x2 = x1 + w;
    float handleThreshold = 10.0f;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        isDragging_ = true;
        dragStartPos_ = event.position;
        dragStartStart_ = startBeat_;
        dragStartDuration_ = viewDuration_;
        
        // Hit Test
        if (std::abs(localX - x1) < handleThreshold) {
            isResizingL_ = true;
        } else if (std::abs(localX - x2) < handleThreshold) {
            isResizingR_ = true;
        } else if (localX >= x1 && localX <= x2) {
            // Pan
        } else {
            // Jump?
             double beat = xToBeat(localX);
             startBeat_ = std::max(0.0, beat - viewDuration_*0.5);
             if (onViewChanged) onViewChanged(startBeat_, viewDuration_);
             repaint();
             return true; 
        }
        return true;
    }
    else if (event.released && event.button == NUIMouseButton::Left) {
        isDragging_ = false;
        isResizingL_ = false;
        isResizingR_ = false;
        return true;
    }
    else if (!event.pressed && isDragging_) {
        float dx = event.position.x - dragStartPos_.x;
        double db = dx / b.width * totalDuration_;
        
        if (isResizingL_) {
            double newStart = dragStartStart_ + db;
            double newDur = dragStartDuration_ - db;
            
            if (newDur < 0.1) { newStart -= (0.1 - newDur); newDur = 0.1; } // Clamp min
            
            startBeat_ = std::clamp(newStart, 0.0, totalDuration_);
            viewDuration_ = newDur;
        } 
        else if (isResizingR_) {
             double newDur = dragStartDuration_ + db;
             viewDuration_ = std::max(0.1, newDur);
        }
        else {
             startBeat_ = std::clamp(dragStartStart_ + db, 0.0, totalDuration_ - viewDuration_);
        }
        
        if (onViewChanged) onViewChanged(startBeat_, viewDuration_);
        repaint();
        return true;
    }
    
    return NUIComponent::onMouseEvent(event);
}


// =============================================================================
// PianoRollRuler
// =============================================================================
PianoRollRuler::PianoRollRuler()
    : scrollX_(0.0f), pixelsPerBeat_(80.0f), beatsPerBar_(4)
{
}

bool PianoRollRuler::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position)) return false;

    // Zoom on Wheel
    if (event.wheelDelta != 0.0f) {
        if (onZoomRequested) {
            float localX = event.position.x - getBounds().x;
            onZoomRequested(event.wheelDelta, localX);
            return true;
        }
    }
    return NUIComponent::onMouseEvent(event);
}

// Ruler Render: "Mature" Playlist Style
void PianoRollRuler::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    
    // CLIP: Prevent left bleeding
    renderer.setClipRect(b);
    
    // Darker, more professional background
    auto bg = NUIColor(0.08f, 0.08f, 0.10f, 1.0f); 
    auto textCol = NUIColor(0.7f, 0.7f, 0.75f, 1.0f);
    auto tickCol = NUIColor(0.35f, 0.35f, 0.40f, 1.0f);
    auto borderCol = NUIColor(0.0f, 0.0f, 0.0f, 0.5f);
    
    renderer.fillRect(b, bg);
    
    // Bottom border
    renderer.drawLine(NUIPoint(b.x, b.y + b.height), NUIPoint(b.x + b.width, b.y + b.height), 1.0f, borderCol);

    float startBeat = scrollX_ / pixelsPerBeat_;
    float endBeat = (scrollX_ + b.width) / pixelsPerBeat_;
    
    int iStart = static_cast<int>(startBeat);
    int iEnd = static_cast<int>(endBeat) + 1;

    for (int i = iStart; i <= iEnd; ++i) {
        double relativeX = (static_cast<double>(i) * pixelsPerBeat_) - static_cast<double>(scrollX_);
        float x = b.x + static_cast<float>(relativeX);
        
        // Prevent drawing text partially off-screen left if we can help it, 
        // but setClipRect handles the actual pixels.
        
        bool isBar = (beatsPerBar_ > 0 && i % beatsPerBar_ == 0);
        
        if (isBar) {
            // Bar: Taller tick, Label
            int barNum = (i / beatsPerBar_) + 1;
            
            // Modern Look: Line doesn't go all the way, just top portion or bottom tick
            // Playlist usually has numbers at the start of the bar.
            
            // Major Tick (Bottom up)
            renderer.drawLine(NUIPoint(x, b.y + b.height * 0.5f), NUIPoint(x, b.y + b.height), 1.0f, tickCol);
            
            // Label
            renderer.drawText(std::to_string(barNum), NUIPoint(x + 4, b.y + 2), 11.0f, textCol);
        } else {
            // Beat: Short Tick
            renderer.drawLine(NUIPoint(x, b.y + b.height * 0.75f), NUIPoint(x, b.y + b.height), 1.0f, tickCol.withAlpha(0.6f));
        }
    }
    
    renderer.clearClipRect();
}

void PianoRollRuler::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollRuler::setScrollX(float scrollX) { scrollX_ = scrollX; repaint(); }



// =============================================================================
// PianoRollToolbar
// =============================================================================
PianoRollToolbar::PianoRollToolbar() {
    setupUI();
}

void PianoRollToolbar::setupUI() {
    // 0. Snap Dropdown
    m_snapDropdown = std::make_shared<NUIDropdown>();
    auto snaps = MusicTheory::getSnapOptions();
    for (int i = 0; i < snaps.size(); ++i) {
        m_snapDropdown->addItem(MusicTheory::getSnapName(snaps[i]), static_cast<int>(snaps[i]));
    }
    m_snapDropdown->setSelectedIndex(1); // Beat default
    m_snapDropdown->setMaxVisibleItems(15);
    m_snapDropdown->setOnSelectionChanged([this](int idx, int id, const std::string& text){
        SnapGrid val = static_cast<SnapGrid>(id);
        if (auto g = grid_.lock()) g->setSnap(val);
        if (auto n = notes_.lock()) n->setSnap(val);
    });

    // 1. Root & Scale Dropdowns
    m_rootDropdown = std::make_shared<NUIDropdown>();
    auto roots = MusicTheory::getRootNames();
    for (int i = 0; i < roots.size(); ++i) m_rootDropdown->addItem(roots[i], i);
    m_rootDropdown->setSelectedIndex(0);
    m_rootDropdown->setMaxVisibleItems(15);
    m_rootDropdown->setOnSelectionChanged([this](int idx, int id, const std::string& text){
        if (auto g = grid_.lock()) g->setRootKey(id);
    });

    m_scaleDropdown = std::make_shared<NUIDropdown>();
    auto scales = MusicTheory::getScales();
    for (int i = 0; i < scales.size(); ++i) m_scaleDropdown->addItem(scales[i].name, i);
    m_scaleDropdown->setSelectedIndex(0); // Chromatic
    m_scaleDropdown->setMaxVisibleItems(15);
    m_scaleDropdown->setOnSelectionChanged([this](int idx, int id, const std::string& text){
        if (auto g = grid_.lock()) g->setScaleType(static_cast<ScaleType>(id));
    });

    // 2. Tool Buttons
    m_ptrBtn = std::make_shared<NUIButton>("");
    m_ptrBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pointer); });

    m_pencilBtn = std::make_shared<NUIButton>("");
    m_pencilBtn->setOnClick([this](){ setActiveTool(GlobalTool::Pencil); });

    m_eraserBtn = std::make_shared<NUIButton>("");
    m_eraserBtn->setOnClick([this](){ setActiveTool(GlobalTool::Eraser); });
    
    // Icons
    const char* ptrSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 2l12 11.2-5.8.5 3.3 7.3-2.2.9-3.2-7.4-4.4 4V2z"/></svg>)";
    m_ptrIcon = std::make_shared<NUIIcon>(ptrSvg);
    
    const char* penSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>)";
    m_pencilIcon = std::make_shared<NUIIcon>(penSvg);
    
    const char* eraserSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M15.14 3c-.51 0-1.02.2-1.41.59L2.59 14.73c-.78.78-.78 2.05 0 2.83L5.43 20.39c.39.39.9.59 1.41.59.51 0 1.02-.2 1.41-.59l10.96-10.96c.78-.78.78-2.05 0-2.83l-2.66-2.67c-.39-.39-.9-.59-1.41-.59zM9 16l-3.37-3.37L15.14 3.1 19 6.9 9 16z"/></svg>)";
    m_eraserIcon = std::make_shared<NUIIcon>(eraserSvg);

    addChild(m_snapDropdown); // Add Snap
    addChild(m_rootDropdown);
    addChild(m_scaleDropdown);
    addChild(m_ptrBtn);
    addChild(m_pencilBtn);
    addChild(m_eraserBtn);
}

void PianoRollToolbar::setPatternName(const std::string& name) {
    m_patternName = name;
    repaint();
}


void PianoRollToolbar::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    // Background
    renderer.fillRect(b, NUIColor(0.12f, 0.12f, 0.14f, 1.0f));
    renderer.drawLine(NUIPoint(b.x, b.y + b.height), NUIPoint(b.x + b.width, b.y + b.height), 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.5f));

    // Layout
    float x = b.x + 10.0f;
    float y = b.y + 4.0f; // Center vertically in 30px height (h=22 -> 4px pad)
    float h = 22.0f;
    
    // Snap
    m_snapDropdown->setBounds(NUIRect(x, y, 90, h));
    m_snapDropdown->onRender(renderer);
    x += 95.0f;

    // Scale
    m_rootDropdown->setBounds(NUIRect(x, y, 70, h));
    m_rootDropdown->onRender(renderer);
    x += 75.0f;

    m_scaleDropdown->setBounds(NUIRect(x, y, 110, h));
    m_scaleDropdown->onRender(renderer);
    x += 120.0f;

    // Tools
    float btnW = 32.0f;
    auto renderBtn = [&](std::shared_ptr<NUIButton> btn, std::shared_ptr<NUIIcon> icon, GlobalTool t) {
        btn->setBounds(NUIRect(x, y, btnW, h));
        btn->setText("");
        
        // Active Highlight
        if (activeTool_ == t) {
             renderer.fillRoundedRect(btn->getBounds(), 4.0f, NUIColor(0.0f, 0.8f, 1.0f, 0.3f));
             renderer.strokeRoundedRect(btn->getBounds(), 4.0f, 1.0f, NUIColor(0.0f, 0.8f, 1.0f, 1.0f));
             icon->setColor(NUIColor(0.0f, 0.9f, 1.0f, 1.0f));
        } else {
             if (btn->isHovered()) {
                 renderer.fillRoundedRect(btn->getBounds(), 4.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.1f));
                 icon->setColor(NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
             } else {
                 icon->setColor(NUIColor(1.0f, 1.0f, 1.0f, 0.5f));
             }
        }
        
        // Center Icon
        float isz = 16.0f;
        NUIRect r = btn->getBounds();
        icon->setBounds(NUIRect(r.x + (r.width-isz)/2, r.y + (r.height-isz)/2, isz, isz)); // Fixed logic
        
        icon->onRender(renderer);
        x += btnW + 5.0f;
    };

    renderBtn(m_ptrBtn, m_ptrIcon, GlobalTool::Pointer);
    renderBtn(m_pencilBtn, m_pencilIcon, GlobalTool::Pencil);
    renderBtn(m_eraserBtn, m_eraserIcon, GlobalTool::Eraser);

    // Editing Pattern Label (Right Side)
    if (!m_patternName.empty()) {
        std::string labelStr = "Source: " + m_patternName;
        float fontSize = 11.5f;
        auto size = renderer.measureText(labelStr, fontSize);
        float lx = b.right() - size.width - 25.0f;
        renderer.drawText(labelStr, NUIPoint(lx, y + 4.0f), fontSize, NUIColor(1.0f, 1.0f, 1.0f, 0.45f));
    }

    // Popups Last (Render reverse order of add usually, or explicitly)
    // Z-order: Last Added is Top.
    // Dropdowns add popups to Overlay layer?
    // NUIDropdown::renderDropdownList renders internal Overlay?
    // Or does it render directly? 386 implies it renders directly.
    // Render Open popups last.
    if (m_snapDropdown->isOpen()) m_snapDropdown->renderDropdownList(renderer);
    if (m_rootDropdown->isOpen()) m_rootDropdown->renderDropdownList(renderer);
    if (m_scaleDropdown->isOpen()) m_scaleDropdown->renderDropdownList(renderer);
}

void PianoRollToolbar::setGrid(std::shared_ptr<PianoRollGrid> grid) {
    grid_ = grid;
    if (grid) {
        // Sync initial default state if needed
    }
}

void PianoRollToolbar::setNoteLayer(std::shared_ptr<PianoRollNoteLayer> notes) {
    notes_ = notes;
}

void PianoRollToolbar::setActiveTool(GlobalTool tool) {
    activeTool_ = tool;
    if (auto n = notes_.lock()) n->setTool(tool);
    repaint();
}


bool PianoRollToolbar::onMouseEvent(const NUIMouseEvent& event) {
    if (m_snapDropdown->onMouseEvent(event)) return true;
    if (m_rootDropdown->onMouseEvent(event)) return true;
    if (m_scaleDropdown->onMouseEvent(event)) return true;
    
    if (m_ptrBtn->onMouseEvent(event)) return true;
    if (m_pencilBtn->onMouseEvent(event)) return true;
    if (m_eraserBtn->onMouseEvent(event)) return true;
    
    return false;
}

PianoRollGrid::PianoRollGrid()
    : pixelsPerBeat_(80.0f), keyHeight_(24.0f), scrollX_(0.0f), scrollY_(0.0f),
      beatsPerBar_(4)
{
}

void PianoRollGrid::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    
    // CLIP TO BOUNDS to prevent bleeding
    renderer.setClipRect(b);

    // Colors (FL-ish Dark Theme)
    auto bgWhiteRow = NUIColor(0.16f, 0.16f, 0.18f, 1.0f); // Lighter row for white keys
    auto bgBlackRow = NUIColor(0.13f, 0.13f, 0.15f, 1.0f); // Darker row for black keys
    
    // New Grid Colors
    auto gridBeat = NUIColor(0.3f, 0.3f, 0.3f, 0.3f);
    auto gridBar = NUIColor(0.5f, 0.5f, 0.5f, 0.5f);
    // 1. Draw Rows (Matching Keys)
    int startPitch = 127 - static_cast<int>((scrollY_) / keyHeight_);
    int endPitch = 127 - static_cast<int>((scrollY_ + b.height) / keyHeight_);
    // Scale Highlight Colors
    auto bgInScale = NUIColor(0.18f, 0.18f, 0.20f, 1.0f); // Slightly lighter
    auto bgRoot = NUIColor(0.22f, 0.22f, 0.25f, 1.0f); // Root key highlight
    auto bgOutOfScale = NUIColor(0.08f, 0.08f, 0.10f, 1.0f); // Darker / Dimmed
    
    for (int p = startPitch; p >= endPitch; --p) {
        float y = b.y + (127 - p) * keyHeight_ - scrollY_;
        NUIRect rowRect(b.x, y, b.width, keyHeight_);
        
        NUIColor rowColor;
        
        bool inScale = MusicTheory::isNoteInScale(p, rootKey_, scaleType_);
        bool isRoot = ((p % 12) == rootKey_);
        
        if (scaleType_ == ScaleType::Chromatic) {
            // Default logic BUT highlight Root Key
            if (isRoot) rowColor = bgRoot;
            else rowColor = isBlackKey(p) ? bgBlackRow : bgWhiteRow;
        } else {
            // Scale Highlighting Logic
            if (inScale) {
                if (isRoot) rowColor = bgRoot;
                else rowColor = isBlackKey(p) ? bgBlackRow : bgInScale; // Keep piano pattern somewhat visible or override?
                // Let's override to make scale obvious.
                // Actually, FL keeps black/white pattern but TINTS in-scale vs out-of-scale?
                // Or user wants "In Scale" vs "Out".
                // Let's use:
                // Root: Highlight
                // In Scale: Standard (or Light)
                // Out Scale: Dark
                if (isRoot) rowColor = bgRoot;
                else rowColor = isBlackKey(p) ? bgBlackRow : bgWhiteRow; // Keep natural pattern for in-scale
            } else {
                rowColor = bgOutOfScale;
            }
        }
        
        renderer.fillRect(rowRect, rowColor);
        
        // Horizontal grid lines
        renderer.drawLine(NUIPoint(b.x, y), NUIPoint(b.x + b.width, y), 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.3f));
    }

    // Vertical Lines (Snap Grid)
    double snapDur = MusicTheory::getSnapDuration(snap_);
    if (snapDur <= 0.0001) snapDur = 1.0;
    if (snap_ == SnapGrid::None) snapDur = 1.0; // Fallback to beat lines?
    
    // Dynamic Density: If too dense, double interval
    while ((pixelsPerBeat_ * snapDur) < 12.0f) {
        snapDur *= 2.0;
    }

    double startBeat = static_cast<double>(scrollX_) / pixelsPerBeat_;
    double endBeat = static_cast<double>(scrollX_ + b.width) / pixelsPerBeat_;
    
    double current = std::floor(startBeat / snapDur) * snapDur;
    
    for (; current <= endBeat + snapDur; current += snapDur) {
        // Calculate X in double precision relative to scrollX_ BEFORE casting to float
        double relativeX = (current * pixelsPerBeat_) - static_cast<double>(scrollX_);
        float x = b.x + static_cast<float>(relativeX);
        
        // Check hierarchy
        bool isBar = (std::fmod(std::abs(current), (double)beatsPerBar_) < 0.001);
        bool isBeat = (std::fmod(std::abs(current), 1.0) < 0.001);
        
        NUIColor col = isBar ? gridBar : (isBeat ? gridBeat : gridBeat.withAlpha(0.15f));
        renderer.drawLine(NUIPoint(x, b.y), NUIPoint(x, b.y + b.height), 1.0f, col);
    }
    
    renderer.clearClipRect();
}

void PianoRollGrid::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollGrid::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollGrid::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollGrid::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }

// =============================================================================
// PianoRollNoteLayer
// =============================================================================
PianoRollNoteLayer::PianoRollNoteLayer()
    : pixelsPerBeat_(80.0f), keyHeight_(24.0f), scrollX_(0.0f), scrollY_(0.0f)
{
}

double PianoRollNoteLayer::snapToGrid(double beat) {
    if (snap_ == SnapGrid::None) return beat;
    double grid = MusicTheory::getSnapDuration(snap_);
    if (grid <= 0.00001) return beat;
    return std::round(beat / grid) * grid;
}

void PianoRollNoteLayer::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    auto b = getBounds();
    
    // CLIP TO BOUNDS
    renderer.setClipRect(b);
    
    // Palette: FL Green-ish / Nomad Teal
    auto noteColor = NUIColor::fromHex(0x40C0A0).withAlpha(0.9f); // Soft Sea Green
    auto noteColorSelected = NUIColor::fromHex(0xFF5050); // Red highlight for selection (FL Style)
    auto noteBorder = NUIColor(0.0f, 0.0f, 0.0f, 0.4f); // Subtle shadow border
    
    // 1. GHOST NOTES (Read-only backgrounds)
    for (const auto& ghost : ghostPatterns_) {
        NUIColor gCol = ghost.color.withAlpha(0.15f); // Very faint
        NUIColor gBorder = ghost.color.withAlpha(0.3f);
        
        for (const auto& n : ghost.notes) {
            double relX = (n.startBeat * pixelsPerBeat_) - static_cast<double>(scrollX_);
            float x = b.x + static_cast<float>(relX);
            float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
            float w = static_cast<float>(n.durationBeats * pixelsPerBeat_);
            float h = keyHeight_;
            
            if (x + w < b.x || x > b.x + b.width || y + h < b.y || y > b.y + b.height) continue;
            
            NUIRect r(x + 1, y + 1, std::max(4.0f, w - 2), h - 2);
            renderer.fillRoundedRect(r, 3.0f, gCol);
            renderer.strokeRoundedRect(r, 3.0f, 1.0f, gBorder);
        }
    }
    
    for (const auto& n : notes_) {
        // Double precision relative X subtraction
        double relX = (n.startBeat * pixelsPerBeat_) - static_cast<double>(scrollX_);
        float x = b.x + static_cast<float>(relX);
        
        float y = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
        float w = static_cast<float>(n.durationBeats * pixelsPerBeat_);
        float h = keyHeight_;
        
        if (x + w < b.x || x > b.x + b.width || y + h < b.y || y > b.y + b.height) continue;
        
        // Animation Logic: Delete (Scale Down)
        if (n.isDeleted) {
            n.animationScale -= 0.20f; // Fast shrink
            if (n.animationScale < 0.0f) n.animationScale = 0.0f;
            repaint(); // Keep animating
        }
        else {
            // Ensure idle notes are full scale (if we ever reuse this note)
            if (n.animationScale < 1.0f) n.animationScale = 1.0f;
        }

        // Skip drawing if invisible
        if (n.isDeleted && n.animationScale <= 0.001f) continue;
        
        bool isInteracting = (state_ == State::Moving || state_ == State::Resizing || state_ == State::Painting);
        
        NUIRect r(x + 1, y + 1, std::max(4.0f, w - 2), h - 2);
        
        // Apply Animation Scale (Center)
        if (n.animationScale < 1.0f) {
            float s = n.animationScale; 
            float cx = r.x + r.width * 0.5f;
            float cy = r.y + r.height * 0.5f;
            r.x = cx - (r.width * 0.5f * s);
            r.y = cy - (r.height * 0.5f * s);
            r.width *= s;
            r.height *= s;
        }

        auto color = noteColor;
        
        // Alpha based on velocity
        float velAlpha = 0.4f + (n.velocity / 127.0f) * 0.6f;
        color = color.withAlpha(velAlpha * noteColor.a);
        
        auto border = noteBorder;
        
        if (n.selected && !n.isDeleted) {
            if (isInteracting) {
                // ... (Interaction visuals)
                float inset = 2.0f;
                r.x += inset;
                r.y += inset;
                r.width = std::max(0.0f, r.width - (inset * 2.0f));
                r.height = std::max(0.0f, r.height - (inset * 2.0f));
                
                color.r *= 0.7f;
                color.g *= 0.7f;
                color.b *= 0.7f;
                border = NUIColor(1.0f, 1.0f, 1.0f, 0.5f);
            } else {
                // ... (Idle visuals)
                color.r = std::min(1.0f, color.r * 1.1f);
                color.g = std::min(1.0f, color.g * 1.1f);
                color.b = std::min(1.0f, color.b * 1.1f);
                border = NUIColor(1.0f, 1.0f, 1.0f, 0.9f);
            }
        }
        
        renderer.fillRoundedRect(r, 3.0f, color);
        renderer.strokeRoundedRect(r, 3.0f, 1.0f, border);
        
        if ((!isInteracting || !n.selected) && !n.isDeleted) {
             renderer.drawLine(NUIPoint(r.x + 2, r.y + 1), NUIPoint(r.x + r.width - 2, r.y + 1), 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.3f));
        }
    }
    
    renderer.clearClipRect();
    
    // Cleanup Deleted Notes
    bool cleanNeeded = false;
    for (const auto& n : notes_) { 
        if (n.isDeleted && n.animationScale <= 0.001f) {
            cleanNeeded = true; 
            break; 
        }
    }
    
    if (cleanNeeded) {
        auto it = std::remove_if(notes_.begin(), notes_.end(), [](const MidiNote& n){ 
            return n.isDeleted && n.animationScale <= 0.001f; 
        });
        if (it != notes_.end()) {
            notes_.erase(it, notes_.end());
            commitNotes(); // Notify removal
        }
    }
}

int PianoRollNoteLayer::findNoteAt(float localX, float localY) {
    for (int i = static_cast<int>(notes_.size()) - 1; i >= 0; --i) {
        const auto& n = notes_[i];
        float nx = static_cast<float>(n.startBeat * pixelsPerBeat_);
        float ny = (127 - n.pitch) * keyHeight_;
        float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
        float nh = keyHeight_;
        
        if (localX >= nx && localX < nx + nw && localY >= ny && localY < ny + nh) {
            return i;
        }
    }
    return -1;
}

bool PianoRollNoteLayer::onMouseEvent(const NUIMouseEvent& event) {
    if (state_ == State::None && !getBounds().contains(event.position)) return false;

    auto b = getBounds();
    float localX = event.position.x - b.x + scrollX_;
    float localY = event.position.y - b.y + scrollY_;

    // --- RIGHT CLICK / ERASER (FAST ERASE) ---
    if (event.button == NUIMouseButton::Right) {
        if (event.pressed) state_ = State::Erasing;
        if (event.released) state_ = State::None;
    }
    
    if (state_ == State::Erasing && !event.released) {
         int idx = findNoteAt(localX, localY);
         if (idx != -1) {
             auto oldNotes = notes_;
             notes_.erase(notes_.begin() + idx);
             // pushUndo("Erase", oldNotes, notes_); // TODO: Batch Undo
             commitNotes();
             repaint();
         }
         return true;
    }

    // --- LEFT CLICK HANDLING ---
    if (event.pressed && event.button == NUIMouseButton::Left) {
        int clickedIndex = findNoteAt(localX, localY);
        
        // 1. Eraser Tool
        if (tool_ == GlobalTool::Eraser) {
            if (clickedIndex != -1) {
                auto oldNotes = notes_;
                notes_.erase(notes_.begin() + clickedIndex);
                pushUndo("Erase", oldNotes, notes_);
                commitNotes();
                repaint();
            }
            return true;
        }
        
        // 2. Pencil / Pointer
        // Smart Logic: If hovering a note, allow manipulation (Move/Resize) unless explicitly blocked
        // User Request: "Pen... placing notes moving notes... place but not extend or move" -> Enable Move/Resize in Pen mode.
        
        bool intentToPaint = (tool_ == GlobalTool::Pencil && clickedIndex == -1);
        
        if (intentToPaint) {
            // --- PAINT NEW NOTE ---
            if (!(event.modifiers & NUIModifiers::Shift)) {
                for (auto& note : notes_) note.selected = false;
            }
            
            state_ = State::Painting;
            dragStartNotes_ = notes_; 
            
            double beat = std::max(0.0, static_cast<double>(localX / pixelsPerBeat_));
            paintStartBeat_ = snapToGrid(beat);
            
            int pitch = 127 - static_cast<int>(localY / keyHeight_);
            paintPitch_ = std::clamp(pitch, 0, 127);
            
            MidiNote newNote;
            newNote.pitch = paintPitch_;
            newNote.startBeat = paintStartBeat_;
            newNote.durationBeats = lastNoteDuration_; 
            newNote.velocity = lastNoteVelocity_;
            newNote.selected = true;
            
            notes_.push_back(newNote);
            paintingNoteIndex_ = static_cast<int>(notes_.size()) - 1;
            
            dragStartPos_ = event.position;
            repaint();
            return true;
        }
        
        // Interact with Existing Note (Move/Resize/Select)
        if (clickedIndex != -1) {
            bool wasSelected = notes_[clickedIndex].selected;
            
            // Shift Select Logic
            if (event.modifiers & NUIModifiers::Shift) {
                notes_[clickedIndex].selected = true; // Add to Selection
                // Even if already selected, we keep it selected for group move
            } else {
                if (!wasSelected) {
                    for (auto& N : notes_) N.selected = false;
                    notes_[clickedIndex].selected = true;
                }
                // If clicking an already selected note without shift, KEEP other selected notes (Group Move)
                // Unless we simply click and release (Deselct others logic handles at release? or MouseDown?)
                // Standard: Click on selection -> Prepare group move. 
            }

            const auto& n = notes_[clickedIndex];
            float nx = static_cast<float>(n.startBeat * pixelsPerBeat_);
            float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
            bool isRightEdge = (localX >= nx + nw - 10.0f);
            
            state_ = isRightEdge ? State::Resizing : State::Moving;
            dragStartPos_ = event.position;
            dragStartNotes_ = notes_; 
            
            repaint();
            return true;
        }
        
        // Empty Click (Pointer or Pencil logic fell through) -> Selection Box
        // Only if NOT pencil (Pencil paints) - handled by intentToPaint
        if (tool_ == GlobalTool::Pointer) {
             state_ = State::SelectingBox;
             dragStartPos_ = event.position;
             selectionRect_ = NUIRect(event.position.x, event.position.y, 0, 0); // Start size 0
             
             if (!(event.modifiers & NUIModifiers::Shift)) {
                 for (auto& n : notes_) n.selected = false;
             }
             repaint();
             return true;
        }
    }
    
    // --- DRAGGING (Left Button) ---
    if (!event.pressed && !event.released && state_ != State::None) {
        if (state_ == State::SelectingBox) {
            float w = event.position.x - dragStartPos_.x;
            float h = event.position.y - dragStartPos_.y;
            selectionRect_ = NUIRect(dragStartPos_.x, dragStartPos_.y, w, h);
            
            // Normalize for intersection check
            NUIRect norm = selectionRect_;
            if (norm.width < 0) { norm.x += norm.width; norm.width *= -1; }
            if (norm.height < 0) { norm.y += norm.height; norm.height *= -1; }
            
            // Select Intersecting Notes
            for (auto& n : notes_) {
                float nx = b.x + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
                float ny = b.y + (127 - n.pitch) * keyHeight_ - scrollY_;
                float nw = static_cast<float>(n.durationBeats * pixelsPerBeat_);
                float nh = keyHeight_;
                
                NUIRect nr(nx, ny, nw, nh);
                
                // Logic: If Shift was held essentially 'add' to selection? 
                // Simple logic: If inside rect -> Select.
                // Resetting unseen notes? 
                // For now, simple standard marquee: 
                // Notes inside = True. 
                // (Ideally should handle toggle or keep existing, but this re-evaluates every frame)
                
                if (nr.x < norm.x + norm.width && nr.x + nr.width > norm.x &&
                    nr.y < norm.y + norm.height && nr.y + nr.height > norm.y) {
                    n.selected = true;
                } else if (!(event.modifiers & NUIModifiers::Shift)) {
                    // Only deselect if not holding shift? 
                    // Actually standard box select replaces selection unless Shift.
                     n.selected = false; 
                }
            }
            repaint();
            return true;
        }
        
        if (state_ == State::Painting && paintingNoteIndex_ != -1) {
            float dx = event.position.x - dragStartPos_.x;
            double beatDelta = dx / pixelsPerBeat_;
            
            double newDur = lastNoteDuration_ + beatDelta; 
            newDur = std::max(0.125, snapToGrid(newDur));
            
            notes_[paintingNoteIndex_].durationBeats = newDur;
            repaint();
            return true;
        }
        else if (state_ == State::Moving) {
            float dx = event.position.x - dragStartPos_.x;
            float dy = event.position.y - dragStartPos_.y;
            
            double beatDelta = dx / pixelsPerBeat_;
            int pitchDelta = -static_cast<int>(dy / keyHeight_);
            
            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double newStart = dragStartNotes_[i].startBeat + beatDelta;
                    notes_[i].startBeat = std::max(0.0, snapToGrid(newStart));
                    int newPitch = dragStartNotes_[i].pitch + pitchDelta;
                    notes_[i].pitch = std::clamp(newPitch, 0, 127);
                }
            }
            repaint();
            return true;
        }
        else if (state_ == State::Resizing) {
            float dx = event.position.x - dragStartPos_.x;
            double beatDelta = dx / pixelsPerBeat_;
            
            for (size_t i = 0; i < notes_.size(); ++i) {
                if (dragStartNotes_[i].selected) {
                    double newDur = dragStartNotes_[i].durationBeats + beatDelta;
                    notes_[i].durationBeats = std::max(0.125, snapToGrid(newDur));
                }
            }
            repaint();
            return true;
        }
    }
    
    // --- RELEASE ---
    if (event.released && event.button == NUIMouseButton::Left) {
        if (state_ == State::SelectingBox) {
            state_ = State::None;
            repaint();
            return true;
        }
        
        if (state_ != State::None) {
            // Update Memory
            if (state_ == State::Painting && paintingNoteIndex_ != -1) {
                lastNoteDuration_ = notes_[paintingNoteIndex_].durationBeats;
            } else if (state_ == State::Resizing) {
                for (const auto& n : notes_) { if (n.selected) { lastNoteDuration_ = n.durationBeats; break; } }
            }
            
            pushUndo("Edit", dragStartNotes_, notes_);
            state_ = State::None;
            paintingNoteIndex_ = -1;
            commitNotes();
            repaint();
            return true;
        }
    }

    return NUIComponent::onMouseEvent(event);
}


// Static clipboard for now (shared across instances is fine/better)
static std::vector<MidiNote> s_noteClipboard;

bool PianoRollNoteLayer::onKeyEvent(const NUIKeyEvent& event) {
    bool ctrl = (event.modifiers & NUIModifiers::Ctrl);
    
    if (event.pressed) {
        // Undo / Redo
        if (ctrl && event.keyCode == NUIKeyCode::Z) {
            bool shift = (event.modifiers & NUIModifiers::Shift);
            if (shift) redo(); else undo();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::Y) {
            redo();
            return true;
        }

        if (event.keyCode == NUIKeyCode::Delete) {
            auto oldNotes = notes_; // Snapshot
            bool anyDeleted = false;
            for (auto& n : notes_) {
                if (n.selected && !n.isDeleted) {
                    n.isDeleted = true;
                    anyDeleted = true;
                }
            }
            if (anyDeleted) {
                pushUndo("Delete", oldNotes, notes_);
                repaint();
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::C) {
            // Copy
            s_noteClipboard.clear();
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) s_noteClipboard.push_back(n); // Don't copy deleted
            }
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::V) {
            // Paste (Offset by 1 beat for visibility)
            if (s_noteClipboard.empty()) return true;
            
            auto oldNotes = notes_; // Snapshot

            // Deselect current
            for (auto& n : notes_) n.selected = false;
            
            double offset = 1.0; 
            
            for (auto n : s_noteClipboard) {
                n.startBeat += offset;
                n.selected = true;
                n.isDeleted = false; 
                notes_.push_back(n);
            }
            pushUndo("Paste", oldNotes, notes_);
            commitNotes();
            repaint();
            return true;
        }
        else if (ctrl && event.keyCode == NUIKeyCode::D) {
            // Duplicate (Ctrl+D)
            double minStart = 100000.0;
            double maxEnd = -1.0;
            bool hasSelection = false;
            
            for (const auto& n : notes_) {
                if (n.selected && !n.isDeleted) {
                    hasSelection = true;
                    minStart = std::min(minStart, n.startBeat);
                    maxEnd = std::max(maxEnd, n.startBeat + n.durationBeats);
                }
            }
            
            if (hasSelection && maxEnd > 0) {
                double shift = maxEnd - minStart;
                if (shift < 0.25) shift = 0.25;
                
                
                auto oldNotes = notes_; // Snapshot
                
                for (auto& n : notes_) n.selected = false;

                for (const auto& n : oldNotes) {
                    if (n.selected && !n.isDeleted) {
                        MidiNote clone = n;
                        clone.startBeat += shift;
                        clone.selected = true; 
                        clone.isDeleted = false;
                        notes_.push_back(clone);
                    }
                }
                
                pushUndo("Duplicate", oldNotes, notes_);
                commitNotes();
                repaint();
                return true;
            }
        }
    }
    return false;
}

void PianoRollNoteLayer::setTool(PianoRollTool tool) {
    tool_ = tool;
    // Reset interaction state if needed?
    state_ = State::None;
    repaint();
}

void PianoRollNoteLayer::pushUndo(const std::string& desc, const std::vector<MidiNote>& oldN, const std::vector<MidiNote>& newN) {
    if (undoStack_.size() > 50) undoStack_.erase(undoStack_.begin()); // Limit
    PianoRollCommand cmd;
    cmd.description = desc;
    cmd.notesBefore = oldN;
    cmd.notesAfter = newN;
    undoStack_.push_back(cmd);
    redoStack_.clear();
}

void PianoRollNoteLayer::undo() {
    if (undoStack_.empty()) return;
    auto cmd = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(cmd);
    
    notes_ = cmd.notesBefore;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::redo() {
    if (redoStack_.empty()) return;
    auto cmd = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(cmd);
    
    notes_ = cmd.notesAfter;
    commitNotes();
    repaint();
}

void PianoRollNoteLayer::commitNotes() {
    if (onNotesChanged_) {
        onNotesChanged_(notes_);
    }
}

void PianoRollNoteLayer::setNotes(const std::vector<MidiNote>& notes) {
    notes_ = notes;
    repaint();
}

void PianoRollNoteLayer::setGhostPatterns(const std::vector<GhostPattern>& ghosts) {
    ghostPatterns_ = ghosts;
    repaint();
}

void PianoRollNoteLayer::setPixelsPerBeat(float ppb) { pixelsPerBeat_ = std::max(10.0f, ppb); repaint(); }
void PianoRollNoteLayer::setKeyHeight(float height) { keyHeight_ = std::max(8.0f, height); repaint(); }
void PianoRollNoteLayer::setScrollOffsetX(float offset) { scrollX_ = offset; repaint(); }
void PianoRollNoteLayer::setScrollOffsetY(float offset) { scrollY_ = offset; repaint(); }
void PianoRollNoteLayer::setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb) { onNotesChanged_ = cb; }

// =============================================================================
// PianoRollControlPanel (Velocity)
// =============================================================================
// =============================================================================
// PianoRollControlPanel (Velocity + Settings)
// =============================================================================
PianoRollControlPanel::PianoRollControlPanel()
    : pixelsPerBeat_(80.0f), scrollX_(0.0f)
{
}

// REMOVED setupUI (Moved to Toolbar)

void PianoRollControlPanel::setPixelsPerBeat(float ppb) {
    if (std::abs(pixelsPerBeat_ - ppb) > 0.001f) {
        pixelsPerBeat_ = std::max(10.0f, ppb);
        repaint();
    }
}

void PianoRollControlPanel::setScrollX(float scrollX) {
    if (std::abs(scrollX_ - scrollX) > 0.001f) {
        scrollX_ = scrollX;
        repaint();
    }
}

void PianoRollControlPanel::setNoteLayer(std::shared_ptr<PianoRollNoteLayer> layer) {
    noteLayer_ = layer;
    repaint();
}

bool PianoRollControlPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Note: If dropdown consumes event, we return true above.
    // Else check sidebar/velocity area.
    
    auto b = getBounds();
    
    // CRITICAL FIX: Ignore events outside bounds unless we are already dragging
    if (!b.contains(event.position) && !isDragging_) return false;

    float sidebarW = 60.0f; // Defined here
    // Note Layer Interaction (Velocity)
    // We assume clicks in content area are for velocity
    // COPIED FROM OLD LOGIC
    auto layer = noteLayer_.lock();
    if (layer) {
        auto b = getBounds();
        // Ignore sidebar clicks for velocity (except dragging started inside)
        // If not dragging and in sidebar, let base handle it (or return true if we want to block)
        if (!isDragging_ && event.position.x < b.x + sidebarW) {
             return NUIComponent::onMouseEvent(event);
        }

        float localX = event.position.x - b.x + scrollX_ - sidebarW;
        
        if (event.pressed && event.button == NUIMouseButton::Left) {
            float minDist = 10.0f; // Pixel threshold
            int foundIdx = -1;
            
            const auto& notes = layer->getNotes();
            std::vector<int> candidates;
            for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
                if (notes[i].isDeleted) continue;
                
                float nStart = static_cast<float>(notes[i].startBeat * pixelsPerBeat_);
                float nEnd = static_cast<float>((notes[i].startBeat + notes[i].durationBeats) * pixelsPerBeat_);
                float minW = 6.0f; // Lollipop head radius
                if (nEnd < nStart + minW) nEnd = nStart + minW; 
                
                // Hit Test: Head (Start) OR Body (Length Line)
                // Relaxed tolerance for Head
                bool hitHead = std::abs(nStart - localX) < 10.0f;
                bool hitBody = (localX >= nStart - 2.0f && localX <= nEnd + 2.0f);
                
                if (hitHead || hitBody) {
                    candidates.push_back(i);
                }
            }
            
            if (!candidates.empty()) {
                // Default heuristic: Last one (usually rendered last = on top)
                foundIdx = candidates.back();
                
                // Priority: Selected Note
                for (int idx : candidates) {
                    if (notes[idx].selected) {
                        foundIdx = idx;
                        break;
                    }
                }
            }
            
            if (foundIdx != -1) {
                isDragging_ = true;
                hoveringNoteIndex_ = foundIdx;
                dragStartPos_ = event.position;
                
                // Set velocity immediately based on click Y (Global)
                float availH = b.height - 15.0f;
                float h = (b.y + b.height - 5.0f) - event.position.y;
                int newVel = static_cast<int>((h / availH) * 127.0f);
                newVel = std::clamp(newVel, 0, 127);
                
                auto modNotes = notes;
                modNotes[foundIdx].velocity = newVel;
                
                // Single Edit Only (Batch removed)
                
                layer->setNotes(modNotes);
                repaint();
                return true;
            }
        }
        else if (isDragging_) {
            // Drag logic
            if (event.released) {
                 isDragging_ = false;
                 hoveringNoteIndex_ = -1;
                 return true;
            }

            // Move
            float availH = b.height - 15.0f;
            float h = (b.y + b.height - 5.0f) - event.position.y;
            int newVel = static_cast<int>((h / availH) * 127.0f);
            newVel = std::clamp(newVel, 0, 127);
            
            auto modNotes = layer->getNotes();
            if (hoveringNoteIndex_ >= 0 && hoveringNoteIndex_ < modNotes.size()) {
                 modNotes[hoveringNoteIndex_].velocity = newVel;
                 // Single Edit Only (Batch removed)
                 layer->setNotes(modNotes);
                 repaint();
            }
            return true;
        }
    }
    
    return NUIComponent::onMouseEvent(event);
}

void PianoRollControlPanel::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    
    // Background (Darker panel)
    renderer.fillRect(b, NUIColor(0.10f, 0.10f, 0.12f, 1.0f));
    
    // Top border (Divider)
    renderer.drawLine(NUIPoint(b.x, b.y), NUIPoint(b.x + b.width, b.y), 1.0f, NUIColor(0.3f, 0.3f, 0.3f, 1.0f));
    
    // Sidebar Area (Left)
    float sidebarW = 60.0f; 
    NUIRect sidebarRect(b.x, b.y, sidebarW, b.height);
    
    // Sidebar Background
    renderer.fillRect(sidebarRect, NUIColor(0.14f, 0.14f, 0.16f, 1.0f));
    renderer.strokeRect(sidebarRect, 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.3f));
    
    // Sidebar Text "Control | Velocity"
    renderer.drawText("Control", NUIPoint(b.x + 5, b.y + 14), 11.0f, NUIColor(0.6f, 0.6f, 0.6f, 1.0f));
    
    auto layer = noteLayer_.lock();
    if (!layer || !isVisible()) return;

    // Content Area Clip
    NUIRect contentRect(b.x + sidebarW, b.y, b.width - sidebarW, b.height);
    renderer.setClipRect(contentRect);

    // 1. Draw Grid Background (Sync with PianoRollGrid)
    auto snap = layer->getSnap();
    double snapDur = MusicTheory::getSnapDuration(snap);
    if (snapDur <= 0.0001) snapDur = 1.0;
    if (snap == SnapGrid::None) snapDur = 1.0; 

    // Dynamic Density
    while ((pixelsPerBeat_ * snapDur) < 12.0f) {
        snapDur *= 2.0;
    }

    float startX = b.x + sidebarW;
    double startBeat = scrollX_ / pixelsPerBeat_;
    double endBeat = (scrollX_ + contentRect.width) / pixelsPerBeat_;
    
    // Align to snap
    double current = std::floor(startBeat / snapDur) * snapDur;

    auto gridCol = NUIColor(1.0f, 1.0f, 1.0f, 0.05f);
    auto barCol = NUIColor(1.0f, 1.0f, 1.0f, 0.1f);
    int beatsPerBar = 4;
    
    for (; current <= endBeat + snapDur; current += snapDur) {
        // Double precision relative subtraction
        double relX = (current * pixelsPerBeat_) - static_cast<double>(scrollX_);
        float x = startX + static_cast<float>(relX);
        
        bool isBar = (std::fmod(std::abs(current), (double)beatsPerBar) < 0.001);
        
        // Draw line
        renderer.drawLine(NUIPoint(x, b.y), NUIPoint(x, b.y + b.height), 1.0f, isBar ? barCol : gridCol);
    }
    
    // 2. Render Velocity Bars (Lollipop Style + Note Width)
    const auto& notes = layer->getNotes();
    auto velColorBase = NUIColor::fromHex(0x50E0D0); // Teal
    
    float availH = b.height - 15.0f; // Leave room at top
    float bottomY = b.y + b.height - 5.0f; // Floor (Global Y)

    for (const auto& n : notes) {
        if (n.isDeleted && n.animationScale < 0.01f) continue;
        
        float x = startX + static_cast<float>(n.startBeat * pixelsPerBeat_) - scrollX_;
        
        // Skip if out of view
        if (x > b.x + b.width) continue;
        
        float h = (n.velocity / 127.0f) * availH;
        float y = bottomY - h;
        
        // Alpha based on velocity logic for bars too?
        float alpha = 0.5f + (n.velocity / 127.0f) * 0.5f;
        auto col = velColorBase.withAlpha(alpha);
        if (n.selected) col = NUIColor::fromHex(0xFF5050); // Highlight selected
        
        // STEM (Thicker)
        renderer.drawLine(NUIPoint(x, bottomY), NUIPoint(x, y), 2.0f, col);
        
        // POP / CIRCLE HEAD
        float circleSize = 6.0f;
        NUIRect circleRect(x - circleSize/2, y - circleSize/2, circleSize, circleSize);
        renderer.fillRoundedRect(circleRect, circleSize/2, col);
        
        // Note Length Line
        float w = static_cast<float>(n.durationBeats * pixelsPerBeat_);
        if (w > 4.0f) {
            renderer.drawLine(NUIPoint(x, y), NUIPoint(x + w, y), 1.0f, col.withAlpha(0.6f));
        }
    }
    
    renderer.clearClipRect();
}
// ...






// =============================================================================
// PianoRollView
// =============================================================================
PianoRollView::PianoRollView()
    : m_keyLaneWidth(60.0f), m_rulerHeight(30.0f), m_pixelsPerBeat(80.0f), m_keyHeight(24.0f), m_scrollX(0.0f), m_scrollY(1800.0f)
{
    m_keys = std::make_shared<PianoRollKeyLane>();
    m_ruler = std::make_shared<PianoRollRuler>();
    m_grid = std::make_shared<PianoRollGrid>();
    m_notes = std::make_shared<PianoRollNoteLayer>();
    m_controls = std::make_shared<PianoRollControlPanel>();
    
    // Toolbar
    m_toolbar = std::make_shared<PianoRollToolbar>();
    m_toolbar->setGrid(m_grid);
    m_toolbar->setNoteLayer(m_notes);

    m_controls->setNoteLayer(m_notes);
    
    m_minimap = std::make_shared<PianoRollMinimap>(); // Local Minimap
    
    m_vScroll = std::make_shared<NUIScrollbar>(NUIScrollbar::Orientation::Vertical);
    m_vScroll->setOrientation(NUIScrollbar::Orientation::Vertical);

    // Initial default layout config
    m_minimap->setVisible(true);
    m_vScroll->setVisible(true);
    
    // Ruler Zoom Callback
    m_ruler->onZoomRequested = [this](float delta, float mouseX) {
        float oldPPB = m_pixelsPerBeat;
        float zoomFactor = (delta > 0) ? 1.15f : 0.85f;
        float newPPB = std::clamp(oldPPB * zoomFactor, 10.0f, 500.0f);
        
        // Anchor logic: Keep beat under mouse stationary
        float mouseBeat = (m_scrollX + mouseX) / oldPPB;
        float newWorldX = mouseBeat * newPPB;
        float newScrollX = newWorldX - mouseX;
        
        if (newScrollX < 0) newScrollX = 0;
        
        m_pixelsPerBeat = newPPB;
        m_scrollX = newScrollX;
        
        updateScrollbars();
        syncChildren();
    };

    m_minimap->onViewChanged = [this](double start, double duration) {
        m_scrollX = static_cast<float>(start * m_pixelsPerBeat);
        
        // ZOOM LOGIC: 
        // duration * ppb = visibleWidth
        // ppb = visibleWidth / duration
        float visibleW = m_grid->getWidth();
        if (duration > 0.001) {
             m_pixelsPerBeat = visibleW / static_cast<float>(duration);
        }
        
        syncChildren();
    };
    
    m_vScroll->setOnScroll([this](double val) {
        float totalH = 128 * m_keyHeight;
        float visibleH = m_grid->getHeight();
        float maxScroll = std::max(0.0f, totalH - visibleH);
        m_scrollY = std::clamp(static_cast<float>(val), 0.0f, maxScroll);
        syncChildren();
    });
    
    addChild(m_keys);
    addChild(m_ruler);
    addChild(m_grid);
    addChild(m_notes);
    addChild(m_controls);
    addChild(m_minimap);
    addChild(m_vScroll);
    addChild(m_toolbar); // Top (Render Last)
}

void PianoRollView::onRender(NUIRenderer& renderer) {
    renderer.fillRect(getBounds(), NUIColor(0.12f, 0.12f, 0.14f, 1.0f));
    NUIComponent::onRender(renderer);
}

void PianoRollView::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutChildren();
}

void PianoRollView::layoutChildren() {
    auto b = getBounds();
    float sbSize = 14.0f; 
    
    // 0. Toolbar (Restored)
    float toolbarH = 30.0f;
    if (m_toolbar) m_toolbar->setBounds(NUIRect(b.x, b.y, b.width, toolbarH));
    
    // 1. Scrollbar/Minimap Section (Below Toolbar)
    float miniMapH = 24.0f;
    
    // 2. Ruler Section (Below Minimap)
    float rulerH = 24.0f; // Slightly taller for readability
    
    float topTotalH = toolbarH + miniMapH + rulerH;
    
    float keyW = m_keyLaneWidth;
    float contentW = b.width - keyW - sbSize;
    float contentH = b.height - topTotalH - m_controlPanelHeight; // Subtract control panel
    
    // 1. Minimap (Top)
    m_minimap->setBounds(NUIRect(b.x + keyW, b.y + toolbarH, contentW, miniMapH));
    
    // 2. Ruler
    m_ruler->setBounds(NUIRect(b.x + keyW, b.y + toolbarH + miniMapH, contentW, rulerH));
    
    // 3. Grid/Notes (Below Ruler)
    NUIRect contentRect(b.x + keyW, b.y + topTotalH, contentW, contentH);
    m_grid->setBounds(contentRect);
    m_notes->setBounds(contentRect);
    
    // 4. Keys (Left, spans Grid height)
    m_keys->setBounds(NUIRect(b.x, b.y + topTotalH, keyW, contentH));
    
    // 5. V-Scroll (Right, spans Grid height only)
    m_vScroll->setBounds(NUIRect(b.x + b.width - sbSize, b.y + topTotalH, sbSize, contentH));
    
    // 6. Control Panel (Bottom) - Spans Full Width (Keys + Content)
    // Ensures "Control" sidebar aligns with Keys
    m_controls->setBounds(NUIRect(b.x, b.y + topTotalH + contentH, b.width, m_controlPanelHeight));
    
    updateScrollbars();
    syncChildren();
}

void PianoRollView::updateScrollbars() {
    float totalBeats = 100.0f * 4.0f; // 400 beats total
    float visibleW = m_grid->getWidth();
    double viewDur = visibleW / m_pixelsPerBeat;
    double start = m_scrollX / m_pixelsPerBeat;
    
    m_minimap->setTotalDuration(totalBeats);
    m_minimap->setView(start, viewDur);

    // Vertical
    float totalH = 128 * m_keyHeight;
    float visibleH = m_grid->getHeight();
    
    m_vScroll->setRangeLimit(0.0, totalH);
    m_vScroll->setCurrentRange(m_scrollY, visibleH);
}

void PianoRollView::syncChildren() {
    if (!m_keys) return;
    
    float x = m_scrollX;
    float y = m_scrollY; 
    
    m_keys->setScrollOffsetY(y);
    m_keys->setKeyHeight(m_keyHeight);
    
    m_ruler->setScrollX(x);
    m_ruler->setPixelsPerBeat(m_pixelsPerBeat);

    m_grid->setPixelsPerBeat(m_pixelsPerBeat);
    m_grid->setKeyHeight(m_keyHeight);
    m_grid->setScrollOffsetX(x);
    m_grid->setScrollOffsetY(y);
    
    m_notes->setPixelsPerBeat(m_pixelsPerBeat);
    m_notes->setKeyHeight(m_keyHeight);
    m_notes->setScrollOffsetX(m_scrollX);
    m_notes->setScrollOffsetY(m_scrollY);
    
    if (m_controls) {
        m_controls->setPixelsPerBeat(m_pixelsPerBeat);
        m_controls->setScrollX(m_scrollX);
    }
    
}

bool PianoRollView::onMouseEvent(const NUIMouseEvent& event) {
    if (!getBounds().contains(event.position) && !m_isResizingPanel) return false;

    // Splitter Logic
    // Access control panel rect logic relative to bounds... 
    // We know layout: topTotalH + m_grid->height? No grid spans contentH.
    // keys width = m_keyLaneWidth.
    // The control panel is at the BOTTOM.
    // The splitter line is at `b.y + b.height - m_controlPanelHeight`.
    
    auto b = getBounds();
    float splitterY = b.y + b.height - m_controlPanelHeight;
    float splitterZone = 5.0f;
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (std::abs(event.position.y - splitterY) < splitterZone) {
            m_isResizingPanel = true;
            m_dragStartPos = event.position;
            m_dragStartPanelHeight = m_controlPanelHeight;
            return true;
        }
    }
    else if (m_isResizingPanel && event.pressed) {
        float dy = event.position.y - m_dragStartPos.y;
        // Dragging UP increases height
        float newH = m_dragStartPanelHeight - dy;
        m_controlPanelHeight = std::clamp(newH, 20.0f, b.height * 0.5f);
        layoutChildren();
        return true;
    }
    else if (event.released) {
        if (m_isResizingPanel) {
            m_isResizingPanel = false;
            return true;
        }
    }

    // 1. Give children priority (Ruler, Minimap, Grid, Notes)
    if (NUIComponent::onMouseEvent(event)) return true;

    // 2. View-level fallback for Grid Scrolling (if Grid didn't handle it)
    if (event.wheelDelta != 0.0f) {
        bool shift = (event.modifiers & NUIModifiers::Shift);
        bool ctrl = (event.modifiers & NUIModifiers::Ctrl);
        
        if (ctrl) {
            // Zoom (Fallback)
            m_pixelsPerBeat = std::max(20.0f, m_pixelsPerBeat + event.wheelDelta * 5.0f);
        } else if (shift) {
            // H-Scroll
            m_scrollX = std::max(0.0f, m_scrollX - event.wheelDelta * 40.0f);
        } else {
            // V-Scroll
            float totalH = 128 * m_keyHeight;
            float visibleH = m_grid->getHeight();
            float maxScroll = std::max(0.0f, totalH - visibleH);
            
            float newY = m_scrollY - event.wheelDelta * 30.0f;
            m_scrollY = std::clamp(newY, 0.0f, maxScroll);
        }
        
        updateScrollbars(); 
        syncChildren();
        return true;
    }
    
    return false;
}

bool PianoRollView::onKeyEvent(const NUIKeyEvent& event) {
    if (m_notes->onKeyEvent(event)) return true;
    return NUIComponent::onKeyEvent(event);
}

void PianoRollView::setNotes(const std::vector<MidiNote>& notes) {
    m_notes->setNotes(notes);
}

void PianoRollView::setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts) {
    m_notes->setGhostPatterns(ghosts);
}

const std::vector<MidiNote>& PianoRollView::getNotes() const {
    return m_notes->getNotes();
}

void PianoRollView::setPixelsPerBeat(float ppb) {
    m_pixelsPerBeat = ppb;
    updateScrollbars();
    syncChildren();
}

void PianoRollView::setBeatsPerBar(int bpb) {
    if (m_grid) m_grid->setBeatsPerBar(bpb);
    if (m_ruler) m_ruler->setBeatsPerBar(bpb);
}

void PianoRollView::setTool(GlobalTool tool) {
    if (m_notes) m_notes->setTool(tool);
}

void PianoRollView::setScale(int root, ScaleType type) {
    if (m_grid) {
        m_grid->setRootKey(root);
        m_grid->setScaleType(type);
    }
}

void PianoRollView::setPatternName(const std::string& name) {
    if (m_toolbar) m_toolbar->setPatternName(name);
}

} // namespace NomadUI
