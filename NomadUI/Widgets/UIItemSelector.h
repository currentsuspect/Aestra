// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUIIcon.h"
#include "../Graphics/NUIRenderer.h"
#include "../Core/NUITextInput.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace NomadUI {

/**
 * @brief Item Selector Component (Spinner Style)
 * 
 * Allows selecting from a list of items using up/down arrows or scrolling.
 * Modelled after the Transport Bar's BPM display.
 */
class UIItemSelector : public NUIComponent {
public:
    UIItemSelector();
    ~UIItemSelector() override = default;

    // Data Management
    void setItems(const std::vector<std::string>& items);
    void setSelectedIndex(int index);
    int getSelectedIndex() const { return m_currentIndex; }
    std::string getSelectedItem() const;
    
    // Interaction
    void selectNext();
    void selectPrevious();
    
    // Callbacks
    void setOnSelectionChanged(std::function<void(int)> callback) { m_onSelectionChanged = callback; }
    
    // Component Overrides
    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

private:
    std::vector<std::string> m_items;
    int m_currentIndex;
    
    // UI State
    std::shared_ptr<NUIIcon> m_upArrow;
    std::shared_ptr<NUIIcon> m_downArrow;
    std::shared_ptr<NUITextInput> m_textInput;
    
    std::function<void(int)> m_onSelectionChanged;
    
    // Visual Feedback
    bool m_isHovered;
    bool m_upArrowHovered;
    bool m_downArrowHovered;
    bool m_upArrowPressed;
    bool m_downArrowPressed;
    float m_pulseAnimation;
    
    // Editing
    void startEditing();
    void commitEditing();
    void cancelEditing();
    double m_lastClickTime; // For double click detection
    bool m_isEditing;
    
    // Hold handling
    float m_holdTimer;
    float m_holdDelay;
    
    // Helper
    NUIRect getUpArrowBounds() const;
    NUIRect getDownArrowBounds() const;
};

} // namespace NomadUI
