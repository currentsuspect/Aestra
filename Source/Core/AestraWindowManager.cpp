// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraWindowManager.h"
#include "AestraRootComponent.h"
#include "AestraContent.h"
#include "UnifiedHUD.h"
#include "SettingsDialog.h"
#include "ConfirmationDialog.h"
#include "RecoveryDialog.h"
#include "../Settings/MissingAssetsDialog.h"
#include "../Settings/ExportDialog.h"
#include "ViewTypes.h"
#include "TrackManagerUI.h"
#include "FileBrowser.h"
#include "TransportBar.h"
#include "Preferences.h"

#include "../AestraUI/Graphics/OpenGL/NUIRendererGL.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUICursorRegistry.h"
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
        if (key == static_cast<int>(KC::Insert)) return NUIKC::Insert;
        if (key == static_cast<int>(KC::CapsLock)) return NUIKC::CapsLock;
        if (key == static_cast<int>(KC::Home)) return NUIKC::Home;
        if (key == static_cast<int>(KC::End)) return NUIKC::End;
        if (key == static_cast<int>(KC::PageUp)) return NUIKC::PageUp;
        if (key == static_cast<int>(KC::PageDown)) return NUIKC::PageDown;

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
    // Honour the persisted maximized state instead of forcing it (#655). This
    // was hard-coded true, so every window was born maximized and a stored
    // "not maximized" could never take effect.
    desc.startMaximized = config.startMaximized;

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
    std::unique_ptr<NUIRendererGL> glRenderer;
    try {
        glRenderer = std::make_unique<NUIRendererGL>();

        // CRITICAL: Get the ACTUAL client size after window creation
        int actualWidth = 0, actualHeight = 0;
        m_window->getSize(actualWidth, actualHeight);
        Log::info("Renderer init with actual client size: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight));

        if (!glRenderer->initialize(actualWidth, actualHeight)) {
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

        // Transfer ownership to member
        m_renderer = std::move(glRenderer);

        Log::info("UI renderer initialized");
    }
    catch (const std::exception& e) {
        Log::error("Exception during renderer initialization: " + std::string(e.what()));
        return false;
    }

    // Initialize the persisted theme before constructing theme-aware widgets.
    auto& themeManager = NUIThemeManager::getInstance();
    auto& preferences = Preferences::instance();
    if (!themeManager.setActiveTheme(preferences.theme)) {
        preferences.theme = "Aestra-dark";
        themeManager.setActiveTheme(preferences.theme);
        Log::warning("Unknown saved theme; using Aestra-dark");
    }
    Log::info("Theme system initialized");

    // Create root component
    m_rootComponent = std::make_shared<AestraRootComponent>();
    m_rootComponent->setBounds(NUIRect(0, 0, desc.width, desc.height));
    m_window->setRootComponent(m_rootComponent.get()); // WIRED: Events flow to this root
    m_themeSubscriptionId = themeManager.subscribeToThemeChanges(
        [this](const NUIThemeProperties& theme) {
            if (m_rootComponent)
                m_rootComponent->onThemeChanged(theme);
            if (m_adaptiveFPS)
                m_adaptiveFPS->signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::Animation);
        });

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

    // Recovery, missing-assets, and confirmation dialogs are routed explicitly
    // below. Stop the bridge from subsequently forwarding the same
    // pointer/text event into the root tree; the release that closes a modal
    // must remain consumed too.
    m_window->setRootInputBlockedCallback([this]() {
        return (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) ||
               (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible()) ||
               (m_confirmationDialog && m_confirmationDialog->isDialogVisible());
    });

    // Input Callbacks
    // CALLBACK ORDER CONTRACT: This AWM callback fires SECOND on every mouse move.
    // NUIPlatformBridge's internal handler fires FIRST (cache update, flag checks,
    // cursorCaptured setup). That handler is at line 84 of NUIPlatformBridge.cpp.
    // Do not move this registration before the bridge's — bridge state must be
    // current before AWM routing logic reads it.
    m_window->setMouseMoveCallback([this](int x, int y) {
        // Feed the FPS governor: without these signals it never leaves the
        // 30 FPS idle target (the signalActivity wiring only existed in the
        // unused NUIApp shell, so interaction never boosted to 60).
        if (m_adaptiveFPS)
            m_adaptiveFPS->signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::MouseMove);

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
            AestraUI::NUIComponent::dispatchMouseEvent(m_recoveryDialog.get(), event);
            return;
        }

        // MissingAssetsDialog is modal - consume mouse move when visible
        if (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = AestraUI::NUIMouseEventType::Move;
            event.position = AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y));
            event.button = AestraUI::NUIMouseButton::None;
            event.pressed = false;
            AestraUI::NUIComponent::dispatchMouseEvent(m_missingAssetsDialog.get(), event);
            return;
        }

        if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = AestraUI::NUIMouseEventType::Move;
            event.position = AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y));
            event.button = AestraUI::NUIMouseButton::None;
            event.pressed = false;
            AestraUI::NUIComponent::dispatchMouseEvent(m_confirmationDialog.get(), event);
            return;
        }

        // Drag & Drop (convert to float for NUI)
        if (m_content) {
            AestraUI::NUIDragDropManager::getInstance().updateDrag(AestraUI::NUIPoint(static_cast<float>(x), static_cast<float>(y)));
        }
    });

    // Window enter/exit lifecycle. Leaving the window releases any component
    // cursor style and any drag capture that may have missed its release (e.g.
    // a drag released outside the window), so the next interaction never
    // inherits a stale resize/grab cursor or an invisible pointer. The
    // per-frame resolveCursorState() is the guaranteed backstop.
    m_window->setMouseEnterCallback([this]() {});
    m_window->setMouseLeaveCallback([this]() {
        if (!m_window) {
            return;
        }
        if (m_window->isCursorCaptured()) {
            m_window->cancelCursorCapture();
        }
        m_window->setCursorStyle(AestraUI::NUICursorStyle::Arrow);
    });

    m_window->setMouseButtonCallback([this](int button, bool pressed) { // Fixed signature
        if (m_adaptiveFPS)
            m_adaptiveFPS->signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::MouseClick);
        // RecoveryDialog is modal - consume all mouse events when visible
        if (m_recoveryDialog && m_recoveryDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = pressed ? AestraUI::NUIMouseEventType::Down : AestraUI::NUIMouseEventType::Up;
            event.position = AestraUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
            event.button = (button == 0)   ? AestraUI::NUIMouseButton::Left
                           : (button == 1) ? AestraUI::NUIMouseButton::Right
                                           : AestraUI::NUIMouseButton::Middle;
            event.pressed = pressed;
            event.released = !pressed;
            AestraUI::NUIComponent::dispatchMouseEvent(m_recoveryDialog.get(), event);
            return; // Block all other mouse handling while recovery dialog is shown
        }

        // MissingAssetsDialog is modal - consume all mouse events when visible
        if (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = pressed ? AestraUI::NUIMouseEventType::Down : AestraUI::NUIMouseEventType::Up;
            event.position = AestraUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
            event.button = (button == 0)   ? AestraUI::NUIMouseButton::Left
                           : (button == 1) ? AestraUI::NUIMouseButton::Right
                                           : AestraUI::NUIMouseButton::Middle;
            event.pressed = pressed;
            event.released = !pressed;
            AestraUI::NUIComponent::dispatchMouseEvent(m_missingAssetsDialog.get(), event);
            return; // Block all other mouse handling while missing-assets dialog is shown
        }

        if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
            AestraUI::NUIMouseEvent event;
            event.type = pressed ? AestraUI::NUIMouseEventType::Down : AestraUI::NUIMouseEventType::Up;
            event.position = AestraUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
            event.button = (button == 0)   ? AestraUI::NUIMouseButton::Left
                           : (button == 1) ? AestraUI::NUIMouseButton::Right
                                           : AestraUI::NUIMouseButton::Middle;
            event.pressed = pressed;
            event.released = !pressed;
            AestraUI::NUIComponent::dispatchMouseEvent(m_confirmationDialog.get(), event);
            return;
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
        if (m_adaptiveFPS)
            m_adaptiveFPS->signalActivity(AestraUI::NUIAdaptiveFPS::ActivityType::KeyPress);
        // Track Modifiers
        using NM = AestraUI::NUIModifiers;
        int currentMods = static_cast<int>(m_keyModifiers);
        
        // Use Aestra::KeyCode matching Platform constants
        
        // Use Aestra::KeyCode matching Platform constants
        if (key == static_cast<int>(Aestra::KeyCode::Shift)) { // 16
            if (pressed)
                currentMods |= static_cast<int>(NM::Shift);
            else
                currentMods &= ~static_cast<int>(NM::Shift);
        }
        if (key == static_cast<int>(Aestra::KeyCode::Control)) { // 17
            if (pressed)
                currentMods |= static_cast<int>(NM::Ctrl);
            else
                currentMods &= ~static_cast<int>(NM::Ctrl);
        }
        if (key == static_cast<int>(Aestra::KeyCode::Alt)) { // 18
            if (pressed)
                currentMods |= static_cast<int>(NM::Alt);
            else
                currentMods &= ~static_cast<int>(NM::Alt);
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

        // MissingAssetsDialog is modal - consume all key events when visible
        if (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible()) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = convertToNUIKeyCode(key);
            event.pressed = pressed;
            m_missingAssetsDialog->onKeyEvent(event);
            return; // Block all other key handling while missing-assets dialog is shown
        }

        if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = convertToNUIKeyCode(key);
            event.pressed = pressed;
            event.released = !pressed;
            event.modifiers = m_keyModifiers;
            m_confirmationDialog->onKeyEvent(event);
            return;
        }

        // Dispatch to Content / Focused widgets for both press and release so
        // key latches and controls can observe the full key lifecycle.
        if (m_content) {
            AestraUI::NUIKeyEvent event;
            event.keyCode = convertToNUIKeyCode(key);
            event.pressed = pressed;
            event.released = !pressed;
            event.modifiers = m_keyModifiers;

            // Global-first bucket mirrors AestraRootComponent: releases (note
            // latches), Space, and Ctrl-chords (app shortcuts like Ctrl+Z must
            // not be swallowed by a focused widget). Content only claims the
            // undo/redo/history chords, so clipboard combos still reach
            // focused text inputs.
            if (event.keyCode == AestraUI::NUIKeyCode::Space || event.released ||
                (event.modifiers & AestraUI::NUIModifiers::Ctrl)) {
                if (m_content->onKeyEvent(event))
                    return;
            }

            if (auto* focused = AestraUI::NUIComponent::getFocusedComponent()) {
                if (focused->onKeyEvent(event)) {
                    return;
                }
            }

            if (m_content->onKeyEvent(event))
                return;
        }

        if (pressed) { // Press-only globals

            // F12: HUD
            // F12: HUD
            if (key == static_cast<int>(Aestra::KeyCode::F12)) { // 123
                if (m_unifiedHUD)
                    m_unifiedHUD->setVisible(!m_unifiedHUD->isVisible());
            }
            // Esc
            if (key == static_cast<int>(Aestra::KeyCode::Escape)) { // 27
                if (m_unifiedHUD && m_unifiedHUD->isVisible())
                    m_unifiedHUD->setVisible(false);
                this->hideActiveMenu();
            }

            // Shortcuts (Undo/Redo) — fallback only: AestraContent consumes these
            // chords (and refreshes) when the history actually changed.
            bool ctrl = (currentMods & static_cast<int>(NM::Ctrl));
            if (ctrl) {
                if (key == static_cast<int>(Aestra::KeyCode::Z) && m_content && m_content->getTrackManager()) { // Z
                    if (m_content->getTrackManager()->getCommandHistory().undo())
                        m_content->refreshAfterHistoryChange();
                }
                if (key == static_cast<int>(Aestra::KeyCode::Y) && m_content && m_content->getTrackManager()) { // Y
                    if (m_content->getTrackManager()->getCommandHistory().redo())
                        m_content->refreshAfterHistoryChange();
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
         m_windowFocused = focused;
         if (!focused && m_content) {
             m_content->releaseMusicalTypingNotes();
         }
         if (m_useCustomCursor && m_window) {
             m_window->setCursorVisible(!focused); // Hide if focused (drawn manually)
         }
    });

    return true;
}

void AestraWindowManager::shutdown() {
    if (m_themeSubscriptionId != 0) {
        NUIThemeManager::getInstance().unsubscribeFromThemeChanges(m_themeSubscriptionId);
        m_themeSubscriptionId = 0;
    }
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
    m_missingAssetsDialog.reset();
    m_unifiedHUD.reset();
}

bool AestraWindowManager::processEvents() {
    return m_window && m_window->processEvents();
}

void AestraWindowManager::setTransportCallback(std::function<void(TransportAction)> cb) {
    m_transportCallback = std::move(cb);
    if (m_rootComponent) {
        m_rootComponent->setTransportCallback([this](TransportAction action) {
            if (m_transportCallback) {
                m_transportCallback(action);
            }
        });
    }
}

void AestraWindowManager::setSaveCallback(std::function<void()> cb) {
    m_saveCallback = std::move(cb);
    if (m_rootComponent) {
        m_rootComponent->setSaveCallback([this]() {
            if (m_saveCallback) m_saveCallback();
        });
    }
}

void AestraWindowManager::setContent(std::shared_ptr<AestraContent> content) {
    m_content = content;
    if (m_rootComponent) {
        m_rootComponent->setContent(m_content.get());
        if (m_transportCallback) {
            m_rootComponent->setTransportCallback([this](TransportAction action) {
                m_transportCallback(action);
            });
        }
        if (m_saveCallback) {
            m_rootComponent->setSaveCallback([this]() {
                if (m_saveCallback) m_saveCallback();
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

    // Wire the title bar's membership status cluster to open the settings dialog.
    // This makes "Signed out" / "Core" behave as a clickable status indicator
    // rather than a dead label with no affordance.
    if (m_customWindow && m_customWindow->getTitleBar()) {
        m_customWindow->getTitleBar()->setOnMembershipClicked([this]() {
            if (m_settingsDialog) {
                m_settingsDialog->show();
            }
        });
    }
}

void AestraWindowManager::setConfirmationDialog(std::shared_ptr<Aestra::ConfirmationDialog> dialog) {
    m_confirmationDialog = dialog;
    m_confirmationDialogRaised = false;
    if (m_rootComponent && m_confirmationDialog) m_rootComponent->addChild(m_confirmationDialog);
}

void AestraWindowManager::setRecoveryDialog(std::shared_ptr<Aestra::RecoveryDialog> dialog) {
    m_recoveryDialog = dialog;
    // Note: RecoveryDialog is NOT added as a child - it's rendered manually
    // at the end of the render loop to ensure it appears on top of all UI
}

void AestraWindowManager::setMissingAssetsDialog(std::shared_ptr<Aestra::MissingAssetsDialog> dialog) {
    m_missingAssetsDialog = dialog;
    // Same manual render treatment as RecoveryDialog: drawn last so it sits
    // on top of everything while visible.
}

void AestraWindowManager::setExportDialog(std::shared_ptr<ExportDialog> dialog) {
    m_exportDialog = std::move(dialog);
    if (m_rootComponent && m_exportDialog) {
        // Export is built lazily from a menu callback, after the root's initial
        // resize pass. Give it the current window coordinate space before it
        // computes its centered dialog rect; otherwise a default 0x0 bounds
        // places most of the panel off-screen at the top-left.
        m_exportDialog->setBounds(m_rootComponent->getBounds());
        m_rootComponent->addChild(m_exportDialog);
    }
}

bool AestraWindowManager::requiresContinuousRender() const {
    if (m_unifiedHUD && m_unifiedHUD->isVisible())
        return true; // profiler needs fresh samples
    if (m_activeMenu)
        return true;
    if (m_recoveryDialog && m_recoveryDialog->isDialogVisible())
        return true;
    if (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible())
        return true;
    if (m_confirmationDialog && m_confirmationDialog->isDialogVisible())
        return true;
    if (m_settingsDialog && m_settingsDialog->isVisible())
        return true;
    if (m_exportDialog && m_exportDialog->isVisible())
        return true;
    return false;
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

    resolveCursorState();

    auto& themeManager = NUIThemeManager::getInstance();
    // CRITICAL: Force alpha = 1.0 to prevent DWM "Sheet of Glass" transparency.
    // The custom title bar uses DwmExtendFrameIntoClientArea which makes alpha < 1 transparent.
    NUIColor bgColor = themeManager.getColor("background").withAlpha(1.0f);

    m_renderer->clear(bgColor);
    // Dark-on-light text lacks the bloom light-on-dark gets; thicken strokes
    // on light themes so labels keep the same perceived weight in both modes.
    {
        const auto& themeBg = themeManager.getCurrentTheme().backgroundPrimary;
        const float bgLuma = 0.2126f * themeBg.r + 0.7152f * themeBg.g + 0.0722f * themeBg.b;
        m_renderer->setTextContrast(bgLuma < 0.5f ? 1.0f : 0.88f);
    }
    m_renderer->beginFrame();
    if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
        m_confirmationDialog->setBounds(m_rootComponent->getBounds());
        if (!m_confirmationDialogRaised) {
            m_rootComponent->removeChild(m_confirmationDialog);
            m_rootComponent->addChild(m_confirmationDialog);
            m_confirmationDialogRaised = true;
        }
    } else {
        m_confirmationDialogRaised = false;
    }
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

    // Same treatment for the missing-assets dialog (T-7).
    if (m_missingAssetsDialog && m_missingAssetsDialog->isDialogVisible()) {
        if (m_rootComponent) {
            m_missingAssetsDialog->setBounds(m_rootComponent->getBounds());
        }
        m_missingAssetsDialog->onRender(*m_renderer);
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

void AestraWindowManager::resolveCursorState() {
    if (!m_useCustomCursor || !m_window) {
        return;
    }

    // Self-heal a stranded Hidden style: a drag capture that lost its release
    // leaves the bridge style Hidden with no active capture — no custom cursor
    // draws and the native cursor stays hidden (= the invisible-pointer
    // failure). Reset to Arrow so every frame resolves to a known state.
    if (m_window->getCursorStyle() == AestraUI::NUICursorStyle::Hidden && !m_window->isCursorCaptured()) {
        m_window->setCursorStyle(AestraUI::NUICursorStyle::Arrow);
    }

    const AestraUI::NUICursorStyle style = m_window->getCursorStyle();
    bool trackManagerHasCustomCursor = false;
    if (m_content && m_content->getTrackManagerUI()) {
        trackManagerHasCustomCursor = m_content->getTrackManagerUI()->isCustomCursorActive();
    }

    // Native cursor visibility must exactly track whether the pointer is being
    // drawn (custom overlay or the TrackManager tool cursor) or intentionally
    // hidden by an active drag capture. Any other state shows the native
    // cursor — the guaranteed fallback.
    const bool hideNative =
        m_window->isCursorCaptured() ||
        (m_windowFocused && (trackManagerHasCustomCursor || style != AestraUI::NUICursorStyle::Hidden));
    if (hideNative != m_cachedNativeCursorHidden) {
        m_cachedNativeCursorHidden = hideNative;
        m_window->setCursorVisible(!hideNative);
    }
}

void AestraWindowManager::initializeCustomCursors() {
    const auto mk = [](AestraUI::NUICursorStyle style) {
        return std::make_shared<AestraUI::NUIIcon>(AestraUI::nuiCursorSvg(style));
    };

    // Canonical cursor artwork comes from NUICursorRegistry — the single
    // source for every interaction cursor, so resize in one editor uses the
    // same asset as resize anywhere else.
    m_cursorArrow = mk(AestraUI::NUICursorStyle::Arrow);
    m_cursorHandPointing = mk(AestraUI::NUICursorStyle::Hand);
    m_cursorHand = mk(AestraUI::NUICursorStyle::Grab);
    m_cursorHandGrabbing = mk(AestraUI::NUICursorStyle::Grabbing);
    m_cursorIBeam = mk(AestraUI::NUICursorStyle::IBeam);
    m_cursorResizeH = mk(AestraUI::NUICursorStyle::ResizeEW);
    m_cursorResizeV = mk(AestraUI::NUICursorStyle::ResizeNS);
    m_cursorResizeDiagNESW = mk(AestraUI::NUICursorStyle::ResizeNESW);
    m_cursorResizeDiagNWSE = mk(AestraUI::NUICursorStyle::ResizeNWSE);

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
        // Persist the RESTORE geometry, not the current rect (#655). While
        // maximized, getSize() returns the maximized rectangle; writing that into
        // width/height destroyed the size the user actually chose, and
        // applyWindowState never reads it back because it skips sizing entirely
        // when the maximized flag is set. Verified under a nested stacking WM:
        // a 500x360 window, maximized and closed, persisted as 640x480.
        state.maximized = m_window->isMaximized();
        if (!m_window->getRestoreBounds(state.x, state.y, state.width, state.height)) {
            m_window->getPosition(state.x, state.y);
            m_window->getSize(state.width, state.height);
        }
    }
    return state;
}

void AestraWindowManager::applyWindowState(const WindowState& state) {
    if (!m_window) return;

    // Restore geometry is applied ALWAYS, then the maximized state is layered on
    // top (#655). Previously the two were exclusive, so a window restored as
    // maximized had no restore geometry to un-maximize back to, and a window
    // restored as non-maximized could not escape the maximized state it was born
    // in (see startMaximized in initialize()).
    if (state.width >= 640 && state.height >= 480) {
        m_window->setSize(state.width, state.height);
        if (state.x > -1000 && state.y > -1000) {
            m_window->setPosition(state.x, state.y);
        }
    }

    if (state.maximized) {
        m_window->maximize();
    } else {
        // Explicitly leave the maximized state. The window is created with
        // SDL_WINDOW_MAXIMIZED (see initialize()), so without this a persisted
        // "not maximized" was silently ignored — observed under a nested
        // stacking WM as a 500x360 request rendering full-screen.
        m_window->restore();
    }
}
