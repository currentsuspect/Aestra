// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include <glad/glad.h>
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

#include "Core/NUIComponent.h"
#include "Core/NUIButton.h"
#include "Core/NUILabel.h"
#include "Core/NUISlider.h"
#include "Core/NUICheckbox.h"
#include "Core/NUITextInput.h"
#include "Core/NUIProgressBar.h"
#include "Core/NUIScrollbar.h"
#include "Core/NUIContextMenu.h"
#include "Core/NUITheme.h"
#include "Core/NUIThemeSystem.h"
#include "Graphics/OpenGL/NUIRendererGL.h"
#include "Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;

using namespace NomadUI;

class NewComponentsDemo : public NUIComponent
{
private:
    std::shared_ptr<NUIContextMenu> contextMenu_;

public:
    NewComponentsDemo()
    {
        // Apply beautiful Nomad Dark theme
        auto& themeManager = NUIThemeManager::getInstance();
        themeManager.setActiveTheme("nomad-dark");
        
        // Create components
        createComponents();
    }
    
    void createComponents()
    {
        // Context Menu - Clean, focused demo for font testing
        contextMenu_ = std::make_shared<NUIContextMenu>();
        contextMenu_->addItem("Cut", []() { std::cout << "Cut selected" << std::endl; });
        contextMenu_->addItem("Copy", []() { std::cout << "Copy selected" << std::endl; });
        contextMenu_->addItem("Paste", []() { std::cout << "Paste selected" << std::endl; });
        contextMenu_->addSeparator();
        contextMenu_->addItem("Settings", []() { std::cout << "Settings selected" << std::endl; });
        
        // Add shortcuts to some items
        auto cutItem = contextMenu_->getItem(0);
        if (cutItem) cutItem->setShortcut("Ctrl+X");
        
        auto copyItem = contextMenu_->getItem(1);
        if (copyItem) copyItem->setShortcut("Ctrl+C");
        
        auto pasteItem = contextMenu_->getItem(2);
        if (pasteItem) pasteItem->setShortcut("Ctrl+V");
        
        addChild(contextMenu_);
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override
    {
        // Handle right-click to show context menu
        if (event.pressed && event.button == NUIMouseButton::Right)
        {
              if (contextMenu_)
              {
                  contextMenu_->showAt(event.position);
                  return true;
              }
        }
        
        // Handle left-click to hide context menu if clicking outside
        if (event.pressed && event.button == NUIMouseButton::Left)
        {
            if (contextMenu_ && contextMenu_->isVisible())
            {
                // Check if click is outside context menu bounds
                NUIRect contextBounds = contextMenu_->getBounds();
                if (!contextBounds.contains(event.position))
                {
                    contextMenu_->hide();
                }
            }
        }
        
        return NUIComponent::onMouseEvent(event);
    }
    
    bool onKeyEvent(const NUIKeyEvent& event) override
    {
        // Handle keyboard navigation for context menu
        if (contextMenu_ && contextMenu_->isVisible())
        {
            if (event.pressed)
            {
                switch (event.keyCode)
                {
                    case NUIKeyCode::Escape:
                        contextMenu_->hide();
                        return true;
                    case NUIKeyCode::Enter:
                    case NUIKeyCode::Space:
                        // Trigger selected item
                        if (contextMenu_->getHoveredItemIndex() >= 0)
                        {
                            auto item = contextMenu_->getItem(contextMenu_->getHoveredItemIndex());
                            if (item && item->getOnClick())
                            {
                                item->getOnClick()();
                                contextMenu_->hide();
                            }
                        }
                        return true;
                    case NUIKeyCode::Up:
                        contextMenu_->navigateUp();
                        return true;
                    case NUIKeyCode::Down:
                        contextMenu_->navigateDown();
                        return true;
                    case NUIKeyCode::W:
                        contextMenu_->navigateUp();
                        return true;
                    case NUIKeyCode::S:
                        contextMenu_->navigateDown();
                        return true;
                }
            }
        }
        
        return NUIComponent::onKeyEvent(event);
    }
    
    void onRender(NUIRenderer& renderer) override
    {
        // Draw themed background
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor bgColor = themeManager.getColor("background");
        renderer.fillRect(getBounds(), bgColor);
        
        // Render children first (UI components)
        renderChildren(renderer);
        
        // Clean demo - no debug text needed
    }
};

int main()
{
    std::cout << "NomadUI New Components Demo" << std::endl;
    std::cout << "===========================" << std::endl;
    
    // Create window
    NUIWindowWin32 window;
    if (!window.create("NomadUI New Components Demo", 400, 300))
    {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }
    
    // Create OpenGL renderer
    auto renderer = std::make_unique<NUIRendererGL>();
    if (!renderer->initialize(400, 300))
    {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return -1;
    }
    
    // Create demo component
    auto demo = std::make_shared<NewComponentsDemo>();
    demo->setBounds(NUIRect(0, 0, 400, 300));
    window.setRootComponent(demo.get());
    window.setRenderer(renderer.get());
    
    // Show window
    window.show();
    
    // Main loop
    while (window.processEvents())
    {
        // Clear screen
        renderer->beginFrame();
        renderer->clear(NUIColor::fromHex(0xff1a1d22));
        
        // Render demo
        demo->onRender(*renderer);
        
        // Present frame
        renderer->endFrame();
        window.swapBuffers();
        
        // Small delay to prevent 100% CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
    
    std::cout << "Demo completed successfully!" << std::endl;
    return 0;
}
