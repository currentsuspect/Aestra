// Copyright 2025 Nomad Studios - All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <string>
#include <memory>
#include <functional>
#include <any>

namespace NomadUI {

class NUIComponent;
class NUIRenderer;

/**
 * @brief Types of data that can be dragged
 */
enum class DragDataType {
    None,
    File,           // File from browser (path string)
    AudioClip,      // Audio clip being moved in timeline
    MidiClip,       // MIDI clip being moved
    Plugin,         // Plugin from browser
    Custom          // User-defined data
};

/**
 * @brief Data payload for drag operations
 */
struct DragData {
    DragDataType type = DragDataType::None;
    std::string filePath;           // For file drags
    std::string displayName;        // Shown in drag ghost
    NUIColor accentColor;           // Visual feedback color
    int sourceTrackIndex = -1;      // For clip moves
    double sourceTimePosition = 0;  // For clip moves
    std::any customData;            // For extensibility
    
    // Original dimensions for visual preview
    float previewWidth = 100.0f;
    float previewHeight = 30.0f;
    
    bool isValid() const { return type != DragDataType::None; }
};

/**
 * @brief Result of a drop operation
 */
struct DropResult {
    bool accepted = false;
    int targetTrackIndex = -1;
    double targetTimePosition = 0;
    std::string message;
};

/**
 * @brief Visual feedback during drag over a target
 */
enum class DropFeedback {
    None,           // Not over a valid target
    Copy,           // Will copy (file drop)
    Move,           // Will move (clip reposition)
    Invalid         // Over target but can't drop here
};

/**
 * @brief Interface for components that can receive drops
 */
class IDropTarget {
public:
    virtual ~IDropTarget() = default;
    
    /**
     * Called when drag enters the target area
     * @return Feedback to show to user
     */
    virtual DropFeedback onDragEnter(const DragData& data, const NUIPoint& position) = 0;
    
    /**
     * Called continuously while dragging over target
     * @return Updated feedback based on position
     */
    virtual DropFeedback onDragOver(const DragData& data, const NUIPoint& position) = 0;
    
    /**
     * Called when drag leaves the target area
     */
    virtual void onDragLeave() = 0;
    
    /**
     * Called when drop occurs
     * @return Result of the drop operation
     */
    virtual DropResult onDrop(const DragData& data, const NUIPoint& position) = 0;
    
    /**
     * Get the component's bounds for hit testing
     */
    virtual NUIRect getDropBounds() const = 0;
};

/**
 * @brief Global drag-and-drop manager (singleton)
 * 
 * Coordinates drag operations between components.
 * Handles visual feedback and drop target detection.
 */
class NUIDragDropManager {
public:
    static NUIDragDropManager& getInstance();
    
    // Drag lifecycle
    void beginDrag(const DragData& data, const NUIPoint& startPosition, NUIComponent* source);
    void updateDrag(const NUIPoint& position);
    void endDrag(const NUIPoint& position);
    void cancelDrag();
    
    // State queries
    bool isDragging() const { return m_isDragging; }
    const DragData& getDragData() const { return m_dragData; }
    NUIPoint getDragPosition() const { return m_currentPosition; }
    NUIPoint getDragOffset() const { return m_dragOffset; }
    DropFeedback getCurrentFeedback() const { return m_currentFeedback; }
    
    // Drop target registration
    void registerDropTarget(std::weak_ptr<IDropTarget> target);
    void unregisterDropTarget(IDropTarget* target);
    
    // Visual rendering (call from top-level render)
    void renderDragGhost(NUIRenderer& renderer);
    
    // Callbacks
    void setOnDragStart(std::function<void(const DragData&)> callback) { m_onDragStart = callback; }
    void setOnDragEnd(std::function<void(const DragData&, const DropResult&)> callback) { m_onDragEnd = callback; }
    
    // Drag threshold (minimum distance before drag starts)
    void setDragThreshold(float threshold) { m_dragThreshold = threshold; }
    float getDragThreshold() const { return m_dragThreshold; }
    
    // Check if we've exceeded drag threshold
    bool hasDragStarted() const { return m_dragStarted; }
    
private:
    NUIDragDropManager();
    ~NUIDragDropManager() = default;
    NUIDragDropManager(const NUIDragDropManager&) = delete;
    NUIDragDropManager& operator=(const NUIDragDropManager&) = delete;
    
    std::shared_ptr<IDropTarget> findTargetAt(const NUIPoint& position);
    void updateCurrentTarget(const NUIPoint& position);
    
    // State
    bool m_isDragging = false;
    bool m_dragStarted = false;     // True after threshold exceeded
    DragData m_dragData;
    NUIPoint m_startPosition;
    NUIPoint m_currentPosition;
    NUIPoint m_dragOffset;          // Offset from mouse to drag origin
    float m_dragThreshold = 5.0f;   // Pixels before drag starts
    
    // Current target
    std::weak_ptr<IDropTarget> m_currentTarget;
    DropFeedback m_currentFeedback = DropFeedback::None;
    
    // Registered targets (weak_ptr to avoid ownership, expired entries are pruned during lookups)
    std::vector<std::weak_ptr<IDropTarget>> m_dropTargets;
    
    // Source component
    std::weak_ptr<NUIComponent> m_sourceComponent;
    
    // Callbacks
    std::function<void(const DragData&)> m_onDragStart;
    std::function<void(const DragData&, const DropResult&)> m_onDragEnd;
};

} // namespace NomadUI
