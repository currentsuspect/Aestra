// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraWindowManager.h"
#include "AestraRootComponent.h"
#include "AestraContent.h"
#include "UnifiedHUD.h"
#include "SettingsDialog.h"
#include "ConfirmationDialog.h"
#include "RecoveryDialog.h"
#include "ViewTypes.h"
#include "TrackManagerUI.h"
#include "FileBrowser.h"
#include "TransportBar.h"

#include "../AestraUI/Graphics/OpenGL/NUIRendererGL.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraCore/include/AestraLog.h"

#include <cmath>
#include <iostream>

using namespace Aestra;
using namespace AestraUI;

namespace {
    /**
     * @brief Convert Aestra::KeyCode to AestraUI::NUIKeyCode
     */
    AestraUI::NUIKeyCode convertToNUIKeyCode(int key) {
        using KC = Aestra::KeyCode;
        using NUIKC = AestraUI::NUIKeyCode;

        if (key == static_cast<int>(KC::Space)) return NUIKC::Space;
        if (key == static_cast<int>(KC::Enter)) return NUIKC::Enter;
        if (key == static_cast<int>(KC::Escape)) return NUIKC::Escape;
        if (key == static_cast<int>(KC::Tab)) return NUIKC::Tab;
        if (key == static_cast<int>(KC::Backspace)) return NUIKC::Backspace;
        if (key == static_cast<int>(KC::Delete)) return NUIKC::Delete;

        // Arrow keys
        if (key == static_cast<int>(KC::Left)) return NUIKC::Left;
        if (key == static_cast<int>(KC::Right)) return NUIKC::Right;
        if (key == static_cast<int>(KC::Up)) return NUIKC::Up;
        if (key == static_cast<int>(KC::Down)) return NUIKC::Down;

        // Letters A-Z
        if (key >= static_cast<int>(KC::A) && key <= static_cast<int>(KC::Z)) {
            int offset = key - static_cast<int>(KC::A);
            return static_cast<NUIKC>(static_cast<int>(NUIKC::A) + offset);
        }

        // Numbers 0-9
        if (key >= static_cast<int>(KC::Num0) && key <= static_cast<int>(KC::Num9)) {
            int offset = key - static_cast<int>(KC::Num0);
            return static_cast<NUIKC>(static_cast<int>(NUIKC::Num0) + offset);
        }

        // Function keys F1-F12
        if (key >= static_cast<int>(KC::F1) && key <= static_cast<int>(KC::F12)) {
            int offset = key - static_cast<int>(KC::F1);
            return static_cast<NUIKC>(static_cast<int>(NUIKC::F1) + offset);
        }

        return NUIKC::Unknown;
    }
}

AestraWindowManager::AestraWindowManager() {
    // Configure adaptive FPS system
    AestraUI::NUIAdaptiveFPS::Config fpsConfig;
    fpsConfig.fps30 = 30.0;
    fpsConfig.fps60 = 60.0;
    fpsConfig.idleTimeout = 2.0;                // 2 seconds idle before lowering FPS
    fpsConfig.performanceThreshold = 0.018;     // 18ms max frame time for 60 FPS
    fpsConfig.performanceSampleCount = 10;      // Average over 10 frames
    fpsConfig.transitionSpeed = 0.05;           // Smooth transition
    fpsConfig.enableLogging = false;            // Disable by default (can be toggled)

    m_adaptiveFPS = std::make_unique<AestraUI::NUIAdaptiveFPS>(fpsConfig);
}

AestraWindowManager::~AestraWindowManager() {
    shutdown();
}

bool AestraWindowManager::initialize(const WindowConfig& config) {
    // Create window using NUIPlatformBridge
    m_window = std::make_unique<NUIPlatformBridge>();

    WindowDesc desc;
    desc.title = config.title;
    desc.width = config.width;
    desc.height = config.height;
    desc.resizable = true;
    desc.decorated = false;  // Borderless for custom title bar
    desc.startMaximized = true;  // Start maximized by default

    if (!m_window->create(desc)) {
        Log::error("Failed to create window");
        return false;
    }

    Log::info("Window created");

    // Create OpenGL context
    if (!m_window->createGLContext()) {
        Log::error("Failed to create OpenGL context");
        return false;
    }

    if (!m_window->makeContextCurrent()) {
        Log::error("Failed to make OpenGL context current");
        return false;
    }

    Log::info("OpenGL context created");

    // Initialize UI renderer (this will initialize GLAD internally)
    try {
        // Use raw pointer for initialization to avoid unique_ptr casting issues
        auto* glRenderer = new NUIRendererGL();

        // CRITICAL: Get the ACTUAL client size after window creation
        int actualWidth = 0, actualHeight = 0;
        m_window->getSize(actualWidth, actualHeight);
        Log::info("Renderer init with actual client size: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight));

        if (!glRenderer->initialize(actualWidth, actualHeight)) {
            delete glRenderer; // Clean up on failure
            Log::error("Failed to initialize UI renderer");
            return false;
        }

        // Enable render caching by default for performance
        glRenderer->setCachingEnabled(true);

        // Transfer ownership to unique_ptr
        m_renderer.reset(glRenderer);

        Log::info("UI renderer initialized");
    }
    catch (const std::exception& e) {
        Log::error("Exception during renderer initialization: " + std::string(e.what()));
        return false;
    }

    // Initialize Aestra theme
    auto& themeManager = NUIThemeManager::getInstance();
    themeManager.setActiveTheme("Aestra-dark");
    Log::info("Theme system initialized");

    // Create root component
    m_rootComponent = std::make_shared<AestraRootComponent>();
    m_rootComponent->setBounds(NUIRect(0, 0, desc.width, desc.height));
    m_window->setRootComponent(m_rootComponent.get()); // WIRED: Events flow to this root

    // Create custom window with title bar
    m_customWindow = std::make_shared<NUICustomWindow>();
    m_rootComponent->addChild(m_customWindow); // WIRED: Window is in the tree
    m_customWindow->setTitle(config.title);
    m_customWindow->setBounds(NUIRect(0, 0, desc.width, desc.height));

    // Wire up precise Hit Test callback logic
    m_window->setHitTestCallback([this](int x, int y) {
        if (m_customWindow && m_customWindow->getTitleBar()) {
            // Convert physical pixels (OS) to logical pixels (NUI)
            float dpi = m_window->getDPIScale();
            if (dpi <= 0.0f) dpi = 1.0f;

            float lx = static_cast<float>(x) / dpi;
            float ly = static_cast<float>(y) / dpi;
            AestraUI::NUIPoint logicPt(lx, ly);

            auto titleBar = m_customWindow->getTitleBar();
            if (titleBar->isVisible() && titleBar->getBounds().contains(logicPt)) {
                return titleBar->hitTest(logicPt);
            }
        }
        return Aestra::HitTestResult::Default;
    });

    // Connect window and renderer to bridge
    m_window->setRootComponent(m_rootComponent.get());
    m_window->setRenderer(m_renderer.get());
    m_customWindow->setWindowHandle(m_window.get());

    // Input Callbacks
    m_window->setMouseMoveCallback([this](int x, int y) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        
        // Drag & Drop (convert to float for NUI)
        if (m_content) {
            AestraUI::NUIDragDropManager::getInstance().updateDrag(AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y)));
        }
    });

    m_window->setMouseButtonCallback([this](int button, bool pressed) { // Fixed signature
        if (!pressed) { // Release
            if (m_content) {
                AestraUI::NUIDragDropManager::getInstance().endDrag(AestraUI::NUIPoint((float)m_lastMouseX, (float)m_lastMouseY)); // Fixed arg
            }
        }
        if (pressed && button == 0) { // Left click
            // Only hide the menu if clicking OUTSIDE of it
            if (m_activeMenu && m_activeMenu->isVisible()) {
                AestraUI::NUIPoint clickPos(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
                AestraUI::NUIRect menuBounds = m_activeMenu->getGlobalBounds();
                if (!menuBounds.contains(clickPos)) {
                    hideActiveMenu();
                }
                // If clicking inside the menu, let the event propagate to the menu component
            }
        }
    });

    m_window->setKeyCallback([this](int key, bool pressed) {
        // Track Modifiers
        using NM = AestraUI::NUIModifiers;
        int currentMods = static_cast<int>(m_keyModifiers);
        
        // Use Aestra::KeyCode matching Platform constants
        
        // Use Aestra::KeyCode matching Platform constants
        if (key == static_cast<int>(Aestra::KeyCode::Shift)) { // 16
            if (pressed) currentMods |= static_cast<int>(NM::Shift);
            else currentMods &= ~static_cast<int>(NM::Shift);
        }
        if (key == static_cast<int>(Aestra::KeyCode::Control)) { // 17
            if (pressed) currentMods |= static_cast<int>(NM::Ctrl);
            else currentMods &= ~static_cast<int>(NM::Ctrl);
        }
        
        m_keyModifiers = static_cast<NM>(currentMods);

        if (pressed) { // Press
             // Dispatch to Content (Global handling)
             if (m_content) {
                 AestraUI::NUIKeyEvent event;
                 event.keyCode = static_cast<AestraUI::NUIKeyCode>(key);
                 event.pressed = pressed;
                 // Note: Modifiers should be passed if struct supports it, checking focused component, etc.
                 // For now, simple dispatch to root content handles our usage.
                 if (m_content->onKeyEvent(event)) return;
             }

             // F12: HUD
             // F12: HUD
             if (key == static_cast<int>(Aestra::KeyCode::F12)) { // 123
                 if (m_unifiedHUD) m_unifiedHUD->setVisible(!m_unifiedHUD->isVisible());
             }
             // Esc
             if (key == static_cast<int>(Aestra::KeyCode::Escape)) { // 27
                 if (m_unifiedHUD && m_unifiedHUD->isVisible()) m_unifiedHUD->setVisible(false);
                 this->hideActiveMenu();
             }
             
             // Shortcuts (Undo/Redo)
             bool ctrl = (currentMods & static_cast<int>(NM::Ctrl));
             if (ctrl) {
                 if (key == static_cast<int>(Aestra::KeyCode::Z) && m_content && m_content->getTrackManager()) { // Z
                     m_content->getTrackManager()->getCommandHistory().undo();
                 }
                 if (key == static_cast<int>(Aestra::KeyCode::Y) && m_content && m_content->getTrackManager()) { // Y
                     m_content->getTrackManager()->getCommandHistory().redo();
                 }
             }
        }
    });

    m_window->setFocusCallback([this](bool focused) {
         if (m_useCustomCursor && m_window) {
             m_window->setCursorVisible(!focused); // Hide if focused (drawn manually)
         }
    });

    return true;
}

void AestraWindowManager::shutdown() {
    if (m_window) {
        m_window->destroy();
    }
    m_renderer.reset();
    m_window.reset();

    // Clear smart pointers
    m_content.reset();
    m_rootComponent.reset();
    m_customWindow.reset();
    m_settingsDialog.reset();
    m_confirmationDialog.reset();
    m_recoveryDialog.reset();
    m_unifiedHUD.reset();
}

bool AestraWindowManager::processEvents() {
    return m_window && m_window->processEvents();
}

void AestraWindowManager::setContent(std::shared_ptr<AestraContent> content) {
    m_content = content;
    if (m_customWindow) {
        m_customWindow->setContent(m_content.get());
    }

    // Pass platform window to TrackManagerUI
    if (m_content && m_content->getTrackManagerUI() && m_window) {
        m_content->getTrackManagerUI()->setPlatformWindow(m_window.get());
    }

    if (m_content && m_content->getTrackManagerUI()) {
        m_content->getTrackManagerUI()->setOnCursorVisibilityChanged([this](bool visible) {
            if (m_window && !m_useCustomCursor) {
                m_window->setCursorVisible(visible);
            }
        });
    }

    // If View Toggle exists, add to title bar
    if (m_content && m_content->getViewToggle() && m_customWindow) {
        if (auto titleBar = m_customWindow->getTitleBar()) {
            titleBar->addChild(m_content->getViewToggle());
        }
    }
}

void AestraWindowManager::setSettingsDialog(std::shared_ptr<Aestra::SettingsDialog> dialog) {
    m_settingsDialog = dialog;
    if (m_rootComponent) {
        m_rootComponent->addChild(m_settingsDialog);
        m_rootComponent->setSettingsDialog(m_settingsDialog);
    }
}

void AestraWindowManager::setConfirmationDialog(std::shared_ptr<Aestra::ConfirmationDialog> dialog) {
    m_confirmationDialog = dialog;
    if (m_rootComponent) m_rootComponent->addChild(m_confirmationDialog);
}

void AestraWindowManager::setRecoveryDialog(std::shared_ptr<Aestra::RecoveryDialog> dialog) {
    m_recoveryDialog = dialog;
    if (m_rootComponent) m_rootComponent->addChild(m_recoveryDialog);
}

void AestraWindowManager::setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud) {
    m_unifiedHUD = hud;
    if (m_rootComponent) m_rootComponent->setUnifiedHUD(m_unifiedHUD);
}

void AestraWindowManager::setMenuBar(std::shared_ptr<AestraUI::NUIMenuBar> menuBar) {
    m_menuBar = menuBar;
    if (m_customWindow) {
        if (auto titleBar = m_customWindow->getTitleBar()) {
            titleBar->addChild(m_menuBar);
        }
    }
}

void AestraWindowManager::showDropdownMenu(std::shared_ptr<AestraUI::NUIContextMenu> menu, float xOffset) {
    if (!menu || !m_rootComponent || !m_customWindow) return;

    // Toggle behavior: if clicking the same menu button, just close it
    if (m_activeMenu && m_activeMenu->isVisible() && m_activeMenuXOffset == xOffset) {
        hideActiveMenu();
        return;
    }

    // Close any existing active menu first
    hideActiveMenu();

    // Add and show the new menu
    m_rootComponent->addChild(menu);
    menu->showAt(static_cast<int>(xOffset), 32);
    m_activeMenu = menu;
    m_activeMenuXOffset = xOffset;

    // Set up callback to clear m_activeMenu when hidden
    menu->setOnHide([this, menuPtr = menu.get()]() {
        if (m_activeMenu.get() == menuPtr) {
            if (m_rootComponent) m_rootComponent->removeChild(m_activeMenu);
            m_activeMenu = nullptr;
            m_activeMenuXOffset = -1.0f;
        }
    });
}

void AestraWindowManager::hideActiveMenu() {
    if (m_activeMenu && m_activeMenu->isVisible()) {
        m_activeMenu->hide();
        if (m_rootComponent) m_rootComponent->removeChild(m_activeMenu);
        m_activeMenu = nullptr;
    }
}

bool AestraWindowManager::isMenuOpen() const {
    return m_activeMenu && m_activeMenu->isVisible();
}

void AestraWindowManager::setWindowTitle(const std::string& title) {
    if (m_customWindow) m_customWindow->setTitle(title);
}

void AestraWindowManager::toggleFullScreen() {
    if (m_customWindow) m_customWindow->toggleFullScreen();
}

bool AestraWindowManager::isFullScreen() const {
    return m_customWindow && m_customWindow->isFullScreen();
}

void AestraWindowManager::swapBuffers() {
    if (m_window) m_window->swapBuffers();
}

void AestraWindowManager::beginFrame() {
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
    lastTime = currentTime;

    // Start FPS tracking
    if (m_adaptiveFPS) {
        m_frameStart = m_adaptiveFPS->beginFrame();
    }

    if (m_rootComponent) {
        m_rootComponent->onUpdate(m_deltaTime);
    }
}

double AestraWindowManager::endFrame() {
    if (m_adaptiveFPS) {
        return m_adaptiveFPS->endFrame(m_frameStart, m_deltaTime);
    }
    return 0.0;
}

void AestraWindowManager::render() {
    if (!m_renderer || !m_rootComponent) return;

    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor bgColor = themeManager.getColor("background");

    m_renderer->clear(bgColor);
    m_renderer->beginFrame();
    m_rootComponent->onRender(*m_renderer);
    NUIDragDropManager::getInstance().renderDragGhost(*m_renderer);

    if (m_useCustomCursor && m_windowFocused) {
        // Ensure cursor is not clipped by previous UI elements
        m_renderer->clearClipRect();

        bool trackManagerHasCustomCursor = false;
        if (m_content && m_content->getTrackManagerUI()) {
            trackManagerHasCustomCursor = m_content->getTrackManagerUI()->isCustomCursorActive();
        }

        if (!trackManagerHasCustomCursor) {
            renderCustomCursor();
        }
    }

    m_renderer->endFrame();
}

// ==============================
// Custom Software Cursor
// ==============================

void AestraWindowManager::initializeCustomCursors() {
    // Arrow cursor
    m_cursorArrow = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M5 2L5 18L9 14L12 21L14 20L11 13L17 13L5 2Z" fill="white" stroke="black" stroke-width="1.5"/>
        </svg>
    )");

    // Hand cursor
    m_cursorHand = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 6V3C12 2.45 12.45 2 13 2C13.55 2 14 2.45 14 3V10H15V4C15 3.45 15.45 3 16 3C16.55 3 17 3.45 17 4V10H18V5C18 4.45 18.45 4 19 4C19.55 4 20 4.45 20 5V15C20 18.31 17.31 21 14 21H12C8.69 21 6 18.31 6 15V12C6 11.45 6.45 11 7 11C7.55 11 8 11.45 8 12V14H9V6C9 5.45 9.45 5 10 5C10.55 5 11 5.45 11 6V10H12V6Z" fill="white" stroke="black" stroke-width="1"/>
        </svg>
    )");

    // I-Beam cursor
    m_cursorIBeam = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20" stroke="white" stroke-width="2" stroke-linecap="round"/>
            <path d="M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20" stroke="black" stroke-width="3" stroke-linecap="round" opacity="0.3"/>
        </svg>
    )");

    // Horizontal resize cursor
    m_cursorResizeH = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
        </svg>
    )");

    // Vertical resize cursor
    m_cursorResizeV = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
        </svg>
    )");

    Log::info("Custom cursor icons initialized");
    m_useCustomCursor = true;
    if (m_window) m_window->setCursorVisible(false);
}

void AestraWindowManager::renderCustomCursor() {
    if (!m_renderer) return;

    std::shared_ptr<AestraUI::NUIIcon> cursorIcon;
    float offsetX = 0.0f, offsetY = 0.0f;
    float size = 20.0f;

    switch (m_activeCursorStyle) {
        case AestraUI::NUICursorStyle::Hand:
        case AestraUI::NUICursorStyle::Grab:
        case AestraUI::NUICursorStyle::Grabbing:
            cursorIcon = m_cursorHand;
            offsetX = -6.0f; offsetY = -2.0f;
            break;
        case AestraUI::NUICursorStyle::IBeam:
            cursorIcon = m_cursorIBeam;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        case AestraUI::NUICursorStyle::ResizeEW:
            cursorIcon = m_cursorResizeH;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        case AestraUI::NUICursorStyle::ResizeNS:
            cursorIcon = m_cursorResizeV;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        default:
            cursorIcon = m_cursorArrow;
            offsetX = 0.0f; offsetY = 0.0f;
            break;
    }

    if (cursorIcon) {
        float x = static_cast<float>(m_lastMouseX) + offsetX;
        float y = static_cast<float>(m_lastMouseY) + offsetY;
        cursorIcon->setBounds(AestraUI::NUIRect(x, y, size, size));
        cursorIcon->onRender(*m_renderer);
    }
}

void AestraWindowManager::updateCursorState(bool focused, AestraUI::NUICursorStyle style) {
    m_windowFocused = focused;
    m_activeCursorStyle = style;
    if (focused) {
        if (m_useCustomCursor && m_window) m_window->setCursorVisible(false);
    } else {
        m_keyModifiers = AestraUI::NUIModifiers::None;
        AestraUI::NUIComponent::clearFocusedComponent();
    }
}
