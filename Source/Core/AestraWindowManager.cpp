// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraWindowManager.h"
#include "AestraRootComponent.h"
#include "AestraContent.h"
#include "UnifiedHUD.h"
#include "SettingsDialog.h"
#include "ConfirmationDialog.h"
#include "RecoveryDialog.h"
#include "../Settings/ExportDialog.h"
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

        // Linux release builds currently have an optimization-sensitive failure
        // in the GL widget cache path that can leave the window blank even though
        // startup completes. Prefer direct rendering until the cache bug is fixed.
#if defined(__linux__)
        glRenderer->setCachingEnabled(false);
#else
        glRenderer->setCachingEnabled(true);
#endif

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
    m_rootComponent->setCustomWindow(m_customWindow); // WIRED: Window is in the tree
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
    // CALLBACK ORDER CONTRACT: This AWM callback fires SECOND on every mouse move.
    // NUIPlatformBridge's internal handler fires FIRST (cache update, flag checks,
    // cursorCaptured setup). That handler is at line 84 of NUIPlatformBridge.cpp.
    // Do not move this registration before the bridge's — bridge state must be
    // current before AWM routing logic reads it.
    m_window->setMouseMoveCallback([this](int x, int y) {
        m_lastMouseX = x;
        m_lastMouseY = y;

        if (m_content) {
            m_activeCursorStyle = m_content->getPanelResizeCursorStyle(
                AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y))
            );
        } else {
            m_activeCursorStyle = AestraUI::NUICursorStyle::Arrow;
        }

        // Let per-component cursor overrides (e.g. piano roll smart cursor) take precedence
        if (m_window) {
            auto bridgeStyle = m_window->getCursorStyle();
            if (bridgeStyle != AestraUI::NUICursorStyle::Arrow &&
                bridgeStyle != AestraUI::NUICursorStyle::Hidden) {
                m_activeCursorStyle = bridgeStyle;
            }
        }
        
        // RecoveryDialog is modal - consume mouse move when visible
        if (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = AestraUI::NUIMouseEventType::Move;
            event.position = AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y));
            event.button = AestraUI::NUIMouseButton::None;
            event.pressed = false;
            m_recoveryDialog->onMouseEvent(event);
            return;
        }
        
        // Drag & Drop (convert to float for NUI)
        if (m_content) {
            AestraUI::NUIDragDropManager::getInstance().updateDrag(AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y)));
        }
    });

    m_window->setMouseButtonCallback([this](int button, bool pressed) { // Fixed signature
        // RecoveryDialog is modal - consume all mouse events when visible
        if (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = pressed ? AestraUI::NUIMouseEventType::Down : AestraUI::NUIMouseEventType::Up;
            event.position = AestraUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
            event.button = (button == 0) ? AestraUI::NUIMouseButton::Left :
                          (button == 1) ? AestraUI::NUIMouseButton::Right : AestraUI::NUIMouseButton::Middle;
            event.pressed = pressed;
            m_recoveryDialog->onMouseEvent(event);
            return; // Block all other mouse handling while recovery dialog is shown
        }
        
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
        if (key == static_cast<int>(Aestra::KeyCode::Alt)) { // 18
            if (pressed) currentMods |= static_cast<int>(NM::Alt);
            else currentMods &= ~static_cast<int>(NM::Alt);
        }
        
        m_keyModifiers = static_cast<NM>(currentMods);

        // RecoveryDialog is modal - consume all key events when visible
        if (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = convertToNUIKeyCode(key);
            event.pressed = pressed;
            m_recoveryDialog->onKeyEvent(event);
            return; // Block all other key handling while recovery dialog is shown
        }

        // Dispatch to Content / Focused widgets for both press and release so
        // key latches and controls can observe the full key lifecycle.
        if (m_content) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = convertToNUIKeyCode(key);
            event.pressed = pressed;
            event.released = !pressed;
            event.modifiers = m_keyModifiers;

            if (event.keyCode == AestraUI::NUIKeyCode::Space) {
                if (m_content->onKeyEvent(event)) return;
            }

            if (auto* focused = AestraUI::NUIComponent::getFocusedComponent()) {
                if (focused->onKeyEvent(event)) {
                    return;
                }
            }

            if (m_content->onKeyEvent(event)) return;
        }

        if (pressed) { // Press-only globals
             
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

    m_window->setCharCallback([this](unsigned int codepoint) {
        if (codepoint < 32 || codepoint > 126) {
            return;
        }
        if (auto* focused = AestraUI::NUIComponent::getFocusedComponent()) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = AestraUI::NUIKeyCode::Unknown;
            event.character = static_cast<char>(codepoint);
            event.pressed = true;
            event.modifiers = m_keyModifiers;
            focused->onKeyEvent(event);
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

void AestraWindowManager::setTransportCallback(std::function<void(TransportAction)> cb) {
    m_transportCallback = std::move(cb);
    if (m_rootComponent) {
        m_rootComponent->setTransportCallback([this](AestraRootComponent::TransportAction action) {
            if (!m_transportCallback) {
                return;
            }

            switch (action) {
                case AestraRootComponent::TransportAction::Play:
                    m_transportCallback(TransportAction::Play);
                    break;
                case AestraRootComponent::TransportAction::Pause:
                    m_transportCallback(TransportAction::Pause);
                    break;
                case AestraRootComponent::TransportAction::Stop:
                    m_transportCallback(TransportAction::Stop);
                    break;
            }
        });
    }
}

void AestraWindowManager::setContent(std::shared_ptr<AestraContent> content) {
    m_content = content;
    if (m_rootComponent) {
        m_rootComponent->setContent(m_content.get());
        if (m_transportCallback) {
            m_rootComponent->setTransportCallback([this](AestraRootComponent::TransportAction action) {
                if (!m_transportCallback) {
                    return;
                }

                switch (action) {
                    case AestraRootComponent::TransportAction::Play:
                        m_transportCallback(TransportAction::Play);
                        break;
                    case AestraRootComponent::TransportAction::Pause:
                        m_transportCallback(TransportAction::Pause);
                        break;
                    case AestraRootComponent::TransportAction::Stop:
                        m_transportCallback(TransportAction::Stop);
                        break;
                }
            });
        }
    }
    if (m_customWindow) {
        m_customWindow->setContent(m_content.get());
    }

    // Pass platform window to TrackManagerUI
    if (m_content && m_content->getTrackManagerUI() && m_window) {
        m_content->getTrackManagerUI()->setPlatformWindow(m_window.get());
    }

    // Pass platform bridge to content (for plugin editors)
    if (m_content && m_window) {
        m_content->setPlatformBridge(m_window.get());
    }

    if (m_window) {
        m_window->setMousePositionFilter([this](int& x, int& y) {
            if (!m_content) return;
            auto trackUI = m_content->getTrackManagerUI();
            if (!trackUI || !trackUI->isInstantClipDragActive()) return;

            AestraUI::NUIPoint pos(static_cast<float>(x), static_cast<float>(y));
            if (trackUI->clampInstantClipDragPosition(pos)) {
                x = static_cast<int>(std::lround(pos.x));
                y = static_cast<int>(std::lround(pos.y));
                if (m_window) {
                    m_window->setCursorPosition(x, y);
                }
            }
        });
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
    // Note: RecoveryDialog is NOT added as a child - it's rendered manually
    // at the end of the render loop to ensure it appears on top of all UI
}

void AestraWindowManager::setExportDialog(std::shared_ptr<ExportDialog> dialog) {
    m_exportDialog = std::move(dialog);
    if (m_rootComponent && m_exportDialog) {
        m_rootComponent->addChild(m_exportDialog);
    }
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

void AestraWindowManager::setExportProgress(float progress) {
    if (m_customWindow) {
        if (auto titleBar = m_customWindow->getTitleBar()) {
            titleBar->setExportProgress(progress);
        }
    }
}

void AestraWindowManager::setExporting(bool exporting) {
    if (m_customWindow) {
        if (auto titleBar = m_customWindow->getTitleBar()) {
            titleBar->setExporting(exporting);
        }
    }
}

void AestraWindowManager::setOnExportRequested(std::function<void()> cb) {
    if (m_customWindow) {
        if (auto titleBar = m_customWindow->getTitleBar()) {
            titleBar->setOnExportRequested(std::move(cb));
        }
    }
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
    // CRITICAL: Force alpha = 1.0 to prevent DWM "Sheet of Glass" transparency.
    // The custom title bar uses DwmExtendFrameIntoClientArea which makes alpha < 1 transparent.
    NUIColor bgColor = themeManager.getColor("background").withAlpha(1.0f);

    m_renderer->clear(bgColor);
    m_renderer->beginFrame();
    m_rootComponent->onRender(*m_renderer);
    NUIDragDropManager::getInstance().renderDragGhost(*m_renderer);

    // Render RecoveryDialog on top of everything if visible
    // This ensures the modal dialog blocks interaction with main UI
    if (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) {
        // Set bounds to full window size so dialog centers correctly
        if (m_rootComponent) {
            m_recoveryDialog->setBounds(m_rootComponent->getBounds());
        }
        m_recoveryDialog->onRender(*m_renderer);
    }

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

    // Pointing hand cursor
    m_cursorHandPointing = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M7 12.5V9.5C7 8.95 7.45 8.5 8 8.5C8.55 8.5 9 8.95 9 9.5V12.2H9.6V5C9.6 4.45 10.05 4 10.6 4C11.15 4 11.6 4.45 11.6 5V12.2H12.2V3.2C12.2 2.65 12.65 2.2 13.2 2.2C13.75 2.2 14.2 2.65 14.2 3.2V12.2H14.8V6.2C14.8 5.65 15.25 5.2 15.8 5.2C16.35 5.2 16.8 5.65 16.8 6.2V14.1C16.8 17.35 14.15 20 10.9 20H10.6C7.9 20 5.7 17.8 5.7 15.1V12.5C5.7 11.95 6.15 11.5 6.7 11.5C6.93 11.5 7.14 11.58 7.3 11.72C7.32 11.74 7.33 11.75 7.35 11.77C7.56 11.96 7.7 12.22 7.7 12.5H7Z" fill="white" stroke="black" stroke-width="1.15" stroke-linejoin="round"/>
        </svg>
    )");

    // Open-hand grab cursor
    m_cursorHand = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 6V3C12 2.45 12.45 2 13 2C13.55 2 14 2.45 14 3V10H15V4C15 3.45 15.45 3 16 3C16.55 3 17 3.45 17 4V10H18V5C18 4.45 18.45 4 19 4C19.55 4 20 4.45 20 5V15C20 18.31 17.31 21 14 21H12C8.69 21 6 18.31 6 15V12C6 11.45 6.45 11 7 11C7.55 11 8 11.45 8 12V14H9V6C9 5.45 9.45 5 10 5C10.55 5 11 5.45 11 6V10H12V6Z" fill="white" stroke="black" stroke-width="1"/>
        </svg>
    )");

    // Closed-hand grabbing cursor
    m_cursorHandGrabbing = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M9.5 5.8C9.5 5.14 10.04 4.6 10.7 4.6C11.36 4.6 11.9 5.14 11.9 5.8V9.1H12.5V4.7C12.5 4.04 13.04 3.5 13.7 3.5C14.36 3.5 14.9 4.04 14.9 4.7V9.1H15.5V6.4C15.5 5.74 16.04 5.2 16.7 5.2C17.36 5.2 17.9 5.74 17.9 6.4V11.7C17.9 15.73 14.63 19 10.6 19C7.51 19 5 16.49 5 13.4V10.7C5 10.04 5.54 9.5 6.2 9.5C6.86 9.5 7.4 10.04 7.4 10.7V12.9H8V7C8 6.34 8.54 5.8 9.2 5.8H9.5Z" fill="white" stroke="black" stroke-width="1.2" stroke-linejoin="round"/>
            <path d="M7.9 14.3C8.05 15.72 9.25 16.8 10.7 16.8C12.28 16.8 13.56 15.52 13.56 13.94V12.7H7.8V13.5C7.8 13.77 7.84 14.04 7.9 14.3Z" fill="black" fill-opacity="0.16"/>
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

    // Diagonal resize cursor (NE-SW)
    m_cursorResizeDiagNESW = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M16 8L22 2M22 2H18M22 2V6M8 16L2 22M2 22H6M2 22V18M9 15L15 9" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M16 8L22 2M22 2H18M22 2V6M8 16L2 22M2 22H6M2 22V18M9 15L15 9" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
        </svg>
    )");

    // Diagonal resize cursor (NW-SE)
    m_cursorResizeDiagNWSE = std::make_shared<AestraUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M8 8L2 2M2 2H6M2 2V6M16 16L22 22M22 22H18M22 22V18M9 9L15 15" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
            <path d="M8 8L2 2M2 2H6M2 2V6M16 16L22 22M22 22H18M22 22V18M9 9L15 15" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
        </svg>
    )");

    Log::info("Custom cursor icons initialized");
    m_useCustomCursor = true;
    if (m_window) m_window->setCursorVisible(false);
}

void AestraWindowManager::renderCustomCursor() {
    if (!m_renderer) return;
    
    // Skip rendering custom cursor when hidden style is active
    if (m_window && m_window->getCursorStyle() == AestraUI::NUICursorStyle::Hidden) {
        return;
    }

    std::shared_ptr<AestraUI::NUIIcon> cursorIcon;
    float offsetX = 0.0f, offsetY = 0.0f;
    float size = 24.0f;

    switch (m_activeCursorStyle) {
        case AestraUI::NUICursorStyle::Hand:
            cursorIcon = m_cursorHandPointing ? m_cursorHandPointing : m_cursorHand;
            offsetX = -7.0f; offsetY = -3.0f;
            break;
        case AestraUI::NUICursorStyle::Grab:
            cursorIcon = m_cursorHand;
            offsetX = -7.0f; offsetY = -3.0f;
            break;
        case AestraUI::NUICursorStyle::Grabbing:
            cursorIcon = m_cursorHandGrabbing ? m_cursorHandGrabbing : m_cursorHand;
            offsetX = -7.0f; offsetY = -3.0f;
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
        case AestraUI::NUICursorStyle::ResizeNESW:
            cursorIcon = m_cursorResizeDiagNESW;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        case AestraUI::NUICursorStyle::ResizeNWSE:
            cursorIcon = m_cursorResizeDiagNWSE;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        default:
            cursorIcon = m_cursorArrow;
            offsetX = 0.0f; offsetY = 0.0f;
            break;
    }

    if (cursorIcon) {
        // Use authoritative platform cursor position rather than cached event coords.
        // This prevents stale-position renders after programmatic warps (e.g. slider drag-end).
        float cursorX = static_cast<float>(m_lastMouseX);
        float cursorY = static_cast<float>(m_lastMouseY);
        if (m_window) {
            auto pos = m_window->getCursorPosition();
            cursorX = pos.x;
            cursorY = pos.y;
        }
        float x = cursorX + offsetX;
        float y = cursorY + offsetY;
        cursorIcon->setBounds(AestraUI::NUIRect(x, y, size, size));
        cursorIcon->onRender(*m_renderer);
    }
}

// updateCursorState removed — was dead code, never called.
// Cursor suppression during hidden-cursor drag is now owned by:
//   NUIPlatformBridge::setCursorStyle (sets s_cursorCaptureActive)
//   NUIComponent::setHovered / showRemoteTooltip (check the flag)
//   Individual component onMouseEvent overrides (check event.cursorCaptured)
// See NUIComponent.h for the global flag, NUITypes.h for the event field.

// =============================================================================
// Window State Capture/Restore for Persistence (Issue #120)
// =============================================================================

AestraWindowManager::WindowState AestraWindowManager::captureWindowState() const {
    WindowState state;
    if (m_window) {
        m_window->getPosition(state.x, state.y);
        m_window->getSize(state.width, state.height);
        state.maximized = m_window->isMaximized();
    }
    return state;
}

void AestraWindowManager::applyWindowState(const WindowState& state) {
    if (!m_window) return;
    
    // Only apply position/size if not maximized (maximized state handled separately)
    if (!state.maximized) {
        // Validate reasonable bounds (prevent off-screen or tiny windows)
        if (state.width >= 640 && state.height >= 480) {
            m_window->setSize(state.width, state.height);
            // Only set position if it seems reasonable (not negative/off-screen by much)
            if (state.x > -1000 && state.y > -1000) {
                m_window->setPosition(state.x, state.y);
            }
        }
    } else {
        m_window->maximize();
    }
}
