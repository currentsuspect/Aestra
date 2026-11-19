// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDragDrop.h"
#include "NUIComponent.h"
#include "NUIRenderer.h"
#include "../../AestraCore/include/AestraLog.h"
#include <cmath>
#include <algorithm>

namespace AestraUI {

namespace {
bool isComponentEligibleForDragTarget(const NUIComponent* component) {
    for (const NUIComponent* node = component; node != nullptr; node = node->getParent()) {
        if (!node->isVisible() || !node->isHitTestVisible() || node->getOpacity() <= 0.0f) {
            return false;
        }
    }
    return true;
}

bool isDrawnForDrop(const NUIComponent* node) {
    return node->isVisible() && node->isHitTestVisible() && node->getOpacity() > 0.0f;
}

// The topmost component at the point that paints something, front to back (last child first). Pass-through
// layers (OverlayLayer) are looked through to what is behind them.
NUIComponent* topmostDrawnAt(const NUIComponent* node, const NUIPoint& position) {
    const auto& children = node->getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        NUIComponent* child = it->get();
        if (!child || !isDrawnForDrop(child) || !child->getBounds().contains(position)) continue;
        if (NUIComponent* hit = topmostDrawnAt(child, position)) return hit;
        if (!child->isDropPassThrough()) return child;
    }
    return nullptr;
}

NUIComponent* rootOf(NUIComponent* node) {
    while (node->getParent()) node = node->getParent();
    return node;
}
} // namespace

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
    
    Aestra::Log::info("[DragDrop] Drag initiated: " + data.displayName);
}

void NUIDragDropManager::updateDrag(const NUIPoint& position) {
    if (!m_isDragging) return;
    
    m_currentPosition = position;
    
    // Check if we've exceeded drag threshold
    if (!m_dragStarted) {
        if (m_startPosition.distanceTo(position) >= m_dragThreshold) {
            m_dragStarted = true;
            m_dragOffset = m_startPosition - position;
            
            Aestra::Log::info("[DragDrop] Drag threshold exceeded, drag started");
            
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

    // Resolve the target where the release is, against the current order: a panel can claim the front of the
    // list after the last move (ArsenalPanel, UIMixerPanel), and the cached target would still be the one behind it.
    if (m_dragStarted) updateCurrentTarget(position);

    // Only process drop if drag actually started
    if (m_dragStarted && !m_currentTarget.expired()) {
        if (auto target = m_currentTarget.lock()) {
            result = target->onDrop(m_dragData, position);
            target->onDragLeave();
            
            if (result.accepted) {
                Aestra::Log::info("[DragDrop] Drop accepted at track " + 
                    std::to_string(result.targetTrackIndex) + 
                    ", time " + std::to_string(result.targetTimePosition));
            } else {
                Aestra::Log::info("[DragDrop] Drop rejected: " + result.message);
            }
        }
    } else {
        result.accepted = false;
        result.message = "Drag cancelled or not started";
        // A started drag that lands nowhere used to leave no trace at all; say why, so a miss can be traced from
        // the log alone (a restart can clear the state that caused it).
        if (m_dragStarted) Aestra::Log::info("[DragDrop] Drop landed nowhere " + describeDropPoint(position));
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
    
    Aestra::Log::info("[DragDrop] Drag cancelled");
    
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
            ++it;
        } else {
            // Clean up expired weak_ptr
            it = m_dropTargets.erase(it);
        }
    }
}

std::shared_ptr<IDropTarget> NUIDragDropManager::findTargetAt(const NUIPoint& position) {
    // Every eligible target under the pointer, most recently registered first.
    struct Candidate {
        std::shared_ptr<IDropTarget> target;
        NUIComponent* component;
    };
    std::vector<Candidate> candidates;
    NUIComponent* sharedRoot = nullptr;
    bool rankableByScreen = true;

    for (auto it = m_dropTargets.rbegin(); it != m_dropTargets.rend();) {
        auto target = it->lock();
        if (!target) {
            // Clean up expired weak_ptr
            it = std::reverse_iterator<std::vector<std::weak_ptr<IDropTarget>>::iterator>(
                m_dropTargets.erase(std::next(it).base()));
            continue;
        }
        ++it;

        auto* component = dynamic_cast<NUIComponent*>(target.get());
        if (component && !isComponentEligibleForDragTarget(component)) continue;
        if (!target->getDropBounds().contains(position)) continue;

        if (!component) {
            rankableByScreen = false;
        } else if (NUIComponent* root = rootOf(component); !sharedRoot) {
            sharedRoot = root;
        } else if (root != sharedRoot) {
            rankableByScreen = false;
        }
        candidates.push_back({std::move(target), component});
    }

    if (candidates.empty()) return nullptr;
    // A target with no place in the shared tree can only be ranked by registration (the old rule).
    if (!rankableByScreen) return candidates.front().target;

    // Registration order says nothing about what is on top: the timeline registers on its first frame and stays
    // visible under floating panels, so it used to take their drops (#395, #1328). Take what is drawn at the
    // pointer and walk up to the nearest target holding it.
    NUIComponent* hit = topmostDrawnAt(sharedRoot, position);
    if (!hit && !sharedRoot->isDropPassThrough() && sharedRoot->getBounds().contains(position)) hit = sharedRoot;
    if (!hit) return candidates.front().target; // nothing drawn reaches the pointer (out-of-parent bounds)

    for (const NUIComponent* node = hit; node; node = node->getParent()) {
        for (const auto& candidate : candidates) {
            if (candidate.component == node) return candidate.target;
        }
    }
    // Something that takes no drops covers every target here (piano roll over the timeline).
    Aestra::Log::trace("[DragDrop] Drop point covered by a non-target");
    return nullptr;
}

namespace {
std::string nameOf(const NUIComponent* node) {
    if (!node) return "(nothing)";
    const std::string id = node->getId();
    return id.empty() ? std::string("(unnamed)") : id;
}

// Why a component cannot take a drop, from itself up through its parents; empty when it can.
std::string whyNotEligible(const NUIComponent* component) {
    for (const NUIComponent* node = component; node != nullptr; node = node->getParent()) {
        const char* why = !node->isVisible()          ? "hidden"
                          : !node->isHitTestVisible() ? "takes no pointer"
                          : node->getOpacity() <= 0.0f ? "transparent"
                                                       : nullptr;
        if (why) return node == component ? std::string(why) : std::string(why) + " by " + nameOf(node);
    }
    return {};
}
} // namespace

std::string NUIDragDropManager::describeDropPoint(const NUIPoint& position) {
    std::string out = "at " + std::to_string(static_cast<int>(position.x)) + "," +
                      std::to_string(static_cast<int>(position.y)) + ": ";
    size_t live = 0;
    std::string under;
    NUIComponent* root = nullptr;
    for (const auto& weak : m_dropTargets) {
        auto target = weak.lock();
        if (!target) continue;
        ++live;
        if (!target->getDropBounds().contains(position)) continue;
        auto* component = dynamic_cast<NUIComponent*>(target.get());
        if (!component) {
            under += " [a target outside the component tree]";
            continue;
        }
        if (!root) root = rootOf(component);
        const std::string why = whyNotEligible(component);
        under += " [" + nameOf(component) + (why.empty() ? std::string(": can take it") : ": " + why) + "]";
    }
    if (under.empty()) return out + "no drop target there (" + std::to_string(live) + " registered)";
    out += std::to_string(live) + " registered; under the pointer:" + under;
    if (root) {
        // What is drawn on top there, and what holds it: a cover that takes no drops shows up here.
        const NUIComponent* hit = topmostDrawnAt(root, position);
        out += "; drawn on top: ";
        int depth = 0;
        for (const NUIComponent* node = hit; node && depth < 4; node = node->getParent(), ++depth) {
            if (depth > 0) out += " < ";
            out += nameOf(node);
        }
        if (!hit) out += "(nothing)";
    }
    return out;
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

} // namespace AestraUI
