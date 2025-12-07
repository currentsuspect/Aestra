// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDragDrop.h"
#include "NUIComponent.h"
#include "../Graphics/NUIRenderer.h"
#include "../../NomadCore/include/NomadLog.h"
#include <cmath>
#include <algorithm>

namespace NomadUI {

NUIDragDropManager& NUIDragDropManager::getInstance() {
    static NUIDragDropManager instance;
    return instance;
}

NUIDragDropManager::NUIDragDropManager() = default;

void NUIDragDropManager::beginDrag(const DragData& data, const NUIPoint& startPosition, NUIComponent* source) {
    if (m_isDragging) {
        cancelDrag();
    }
    
    m_dragData = data;
    m_startPosition = startPosition;
    m_currentPosition = startPosition;
    m_dragOffset = {0, 0};
    m_sourceComponent = source ? source->shared_from_this() : std::weak_ptr<NUIComponent>{};
    m_isDragging = true;
    m_dragStarted = false;  // Will become true after threshold
    m_currentTarget = std::weak_ptr<IDropTarget>{};
    m_currentFeedback = DropFeedback::None;
    
    Nomad::Log::info("[DragDrop] Drag initiated: " + data.displayName);
}

void NUIDragDropManager::updateDrag(const NUIPoint& position) {
    if (!m_isDragging) return;
    
    m_currentPosition = position;
    
    // Check if we've exceeded drag threshold
    if (!m_dragStarted) {
        if (m_startPosition.distanceTo(position) >= m_dragThreshold) {
            m_dragStarted = true;
            m_dragOffset = m_startPosition - position;
            
            Nomad::Log::info("[DragDrop] Drag threshold exceeded, drag started");
            
            if (m_onDragStart) {
                m_onDragStart(m_dragData);
            }
        }
    }
    
    // Only update targets if drag has actually started
    if (m_dragStarted) {
        updateCurrentTarget(position);
    }
}

void NUIDragDropManager::endDrag(const NUIPoint& position) {
    if (!m_isDragging) return;
    
    DropResult result;
    
    // Only process drop if drag actually started
    if (m_dragStarted && !m_currentTarget.expired()) {
        if (auto target = m_currentTarget.lock()) {
            result = target->onDrop(m_dragData, position);
            target->onDragLeave();
            
            if (result.accepted) {
                Nomad::Log::info("[DragDrop] Drop accepted at track " + 
                    std::to_string(result.targetTrackIndex) + 
                    ", time " + std::to_string(result.targetTimePosition));
            } else {
                Nomad::Log::info("[DragDrop] Drop rejected: " + result.message);
            }
        }
    } else {
        result.accepted = false;
        result.message = "Drag cancelled or not started";
    }
    
    if (m_onDragEnd) {
        m_onDragEnd(m_dragData, result);
    }
    
    // Reset state
    m_isDragging = false;
    m_dragStarted = false;
    m_currentTarget = std::weak_ptr<IDropTarget>{};
    m_currentFeedback = DropFeedback::None;
    m_sourceComponent = std::weak_ptr<NUIComponent>{};
    m_dragData = DragData{};
}

void NUIDragDropManager::cancelDrag() {
    if (!m_isDragging) return;
    
    Nomad::Log::info("[DragDrop] Drag cancelled");
    
    if (!m_currentTarget.expired()) {
        if (auto target = m_currentTarget.lock()) {
            target->onDragLeave();
        }
    }
    
    DropResult result;
    result.accepted = false;
    result.message = "Cancelled";
    
    if (m_onDragEnd) {
        m_onDragEnd(m_dragData, result);
    }
    
    m_isDragging = false;
    m_dragStarted = false;
    m_currentTarget = std::weak_ptr<IDropTarget>{};
    m_currentFeedback = DropFeedback::None;
    m_sourceComponent = std::weak_ptr<NUIComponent>{};
    m_dragData = DragData{};
}

void NUIDragDropManager::registerDropTarget(std::weak_ptr<IDropTarget> target) {
    if (auto targetPtr = target.lock()) {
        // Check if this target is already registered
        bool alreadyRegistered = false;
        for (const auto& weakTarget : m_dropTargets) {
            if (auto existingTarget = weakTarget.lock()) {
                if (existingTarget.get() == targetPtr.get()) {
                    alreadyRegistered = true;
                    break;
                }
            }
        }
        
        if (!alreadyRegistered) {
            m_dropTargets.push_back(target);
        }
    }
}

void NUIDragDropManager::unregisterDropTarget(IDropTarget* target) {
    if (!target) return;
    
    // Find and remove the target from the vector
    auto it = m_dropTargets.begin();
    while (it != m_dropTargets.end()) {
        if (auto targetPtr = it->lock()) {
            if (targetPtr.get() == target) {
                // If this is the current target, clear it
                if (!m_currentTarget.expired()) {
                    if (auto currentTarget = m_currentTarget.lock()) {
                        if (currentTarget.get() == target) {
                            currentTarget->onDragLeave();
                            m_currentTarget = std::weak_ptr<IDropTarget>{};
                        }
                    }
                }
                m_dropTargets.erase(it);
                break;
            }
        } else {
            // Clean up expired weak_ptr
            it = m_dropTargets.erase(it);
        }
    }
}

std::shared_ptr<IDropTarget> NUIDragDropManager::findTargetAt(const NUIPoint& position) {
    auto it = m_dropTargets.begin();
    while (it != m_dropTargets.end()) {
        if (auto target = it->lock()) {
            NUIRect bounds = target->getDropBounds();
            
            if (bounds.contains(position)) {
                return target;
            }
            ++it;
        } else {
            // Clean up expired weak_ptr
            it = m_dropTargets.erase(it);
        }
    }
    return nullptr;
}

void NUIDragDropManager::updateCurrentTarget(const NUIPoint& position) {
    std::shared_ptr<IDropTarget> newTarget = findTargetAt(position);
    
    if (newTarget.get() != (m_currentTarget.expired() ? nullptr : m_currentTarget.lock().get())) {
        // Leave old target
        if (auto oldTarget = m_currentTarget.lock()) {
            oldTarget->onDragLeave();
        }
        
        m_currentTarget = newTarget;
        
        // Enter new target
        if (newTarget) {
            m_currentFeedback = newTarget->onDragEnter(m_dragData, position);
        } else {
            m_currentFeedback = DropFeedback::None;
        }
    } else if (auto currentTarget = m_currentTarget.lock()) {
        // Still over same target, update position
        m_currentFeedback = currentTarget->onDragOver(m_dragData, position);
    }
}

void NUIDragDropManager::renderDragGhost(NUIRenderer& renderer) {
    if (!m_isDragging || !m_dragStarted) return;
    
    // Only show small ghost when NOT over a valid drop target
    // When over a target, the target renders its own preview (skeleton)
    if (m_currentFeedback == DropFeedback::Copy || m_currentFeedback == DropFeedback::Move) {
        return;  // Target is showing its own preview
    }
    
    // Small ghost for when dragging outside drop targets
    float ghostWidth = std::max(120.0f, std::min(200.0f, m_dragData.previewWidth));
    float ghostHeight = 24.0f;
    float ghostX = m_currentPosition.x + 12;
    float ghostY = m_currentPosition.y + 3;
    
    // Background (semi-transparent)
    NUIColor bgColor = m_dragData.accentColor;
    bgColor.a = 0.7f;
    renderer.fillRoundedRect(
        NUIRect{ghostX, ghostY, ghostWidth, ghostHeight},
        4.0f,
        bgColor
    );
    
    // Border
    NUIColor borderColor = m_dragData.accentColor;
    renderer.strokeRoundedRect(
        NUIRect{ghostX, ghostY, ghostWidth, ghostHeight},
        4.0f,
        1.5f,
        borderColor
    );
    
    // Text
    NUIColor textColor{1.0f, 1.0f, 1.0f, 0.95f};
    
    // Truncate display name if too long
    std::string displayText = m_dragData.displayName;
    if (displayText.length() > 20) {
        displayText = displayText.substr(0, 17) + "...";
    }
    
    renderer.drawTextCentered(
        displayText,
        NUIRect{ghostX, ghostY, ghostWidth, ghostHeight},
        12.0f,
        textColor
    );
    
    // Show invalid indicator if over invalid area
    if (m_currentFeedback == DropFeedback::Invalid) {
        float indicatorSize = 16.0f;
        float indicatorX = ghostX + ghostWidth - indicatorSize - 4;
        float indicatorY = ghostY + 4;
        
        renderer.fillCircle(
            NUIPoint{indicatorX + indicatorSize / 2, indicatorY + indicatorSize / 2},
            indicatorSize / 2,
            NUIColor{1.0f, 0.3f, 0.3f, 1.0f}  // Red
        );
        
        renderer.drawTextCentered(
            "x",
            NUIRect{indicatorX, indicatorY, indicatorSize, indicatorSize},
            12.0f,
            NUIColor{1.0f, 1.0f, 1.0f, 1.0f}
        );
    }
}

} // namespace NomadUI
