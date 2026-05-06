// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "PluginBrowserPanel.h"
#include "GenericPluginEditor.h"

// AestraAudio includes
#include <PluginHost.h>
#include <PluginScanner.h>
#include <PluginManager.h>
#include <EffectChain.h>

#include <functional>
#include <memory>
#include "Events/Connection.h"
#include <algorithm>
#include <string>
#include <vector>

#ifdef AESTRAUI_ENABLE_PREMIUM_EDITORS
#include "RumblePluginEditor.h"
#endif

namespace AestraUI {
class PluginSelectorMenu;

/**
 * @brief Controller that bridges plugin UI widgets to AestraAudio backend
 * 
 * This class connects:
 * - PluginBrowserPanel to PluginScanner for discovery
 * - PluginBrowserPanel to PluginManager for loading
 * - EffectChainRack to MixerChannel::EffectChain
 * 
 * Usage:
 * @code
 *   auto controller = std::make_shared<PluginUIController>();
 *   controller->setPluginScanner(scanner);
 *   controller->setPluginManager(manager);
 *   controller->bindBrowser(browserPanel);
 *   controller->bindEffectRack(rackWidget, effectChain);
 * @endcode
 */
class PluginUIController {
public:
    /** @brief Construct the plugin UI controller. */
    PluginUIController();
    /** @brief Destroy the plugin UI controller and owned popup/editor widgets. */
    ~PluginUIController();
    
    // ==============================
    // Backend References
    // ==============================
    
    /**
     * @brief Set the plugin scanner (for discovery)
     * @param scanner Plugin scanner used to populate the browser.
     */
    void setPluginScanner(Aestra::Audio::PluginScanner* scanner);
    
    /**
     * @brief Set the plugin manager (for loading instances)
     * @param manager Plugin manager used to create plugin instances.
     */
    void setPluginManager(Aestra::Audio::PluginManager* manager);
    
    /**
     * @brief Set the component to use as a layer for popups (menus, etc)
     * @param layer Popup-layer component that owns transient menus and editors.
     */
    void setPopupLayer(NUIComponent* layer);
    
    // ==============================
    // Browser Binding
    // ==============================
    
    /**
     * @brief Bind a PluginBrowserPanel to this controller
     * 
     * This wires up:
     * - Scan button to trigger scanner
     * - Scan progress to update UI
     * - Plugin list from scanner results
     * - Double-click to load plugin
     */
    void bindBrowser(PluginBrowserPanel* browser);
    
    /**
     * @brief Unbind the browser
     */
    void unbindBrowser();
    
    /**
     * @brief Refresh browser plugin list from scanner
     */
    void refreshBrowserList();
    
    /**
     * @brief Start a plugin scan
     */
    void startScan();
    
    // ==============================
    // Effect Rack Binding
    // ==============================
    
    /**
     * @brief Bind an EffectChainRack to an audio EffectChain
     * @param rack Rack widget to populate.
     * @param chain Audio effect chain mirrored by the rack.
     */
    void bindEffectRack(EffectChainRack* rack, Aestra::Audio::EffectChain* chain);
    
    /**
     * @brief Unbind effect rack
     * @param rack Rack widget to detach.
     */
    void unbindEffectRack(EffectChainRack* rack);
    
    /**
     * @brief Refresh rack display from effect chain state
     * @param rack Rack widget to refresh.
     */
    void refreshRackDisplay(EffectChainRack* rack);
    
    // ==============================
    // Plugin Loading
    // ==============================
    
    /**
     * @brief Load a plugin into an effect slot
     * @param pluginId Plugin ID to load
     * @param chain Target effect chain
     * @param slot Slot index (0-9)
     * @return true on success
     */
    bool loadPluginToSlot(const std::string& pluginId, 
                          Aestra::Audio::EffectChain* chain, 
                          int slot);
    
    /**
     * @brief Remove plugin from slot
     * @param chain Target effect chain.
     * @param slot Slot index to clear.
     */
    void removePluginFromSlot(Aestra::Audio::EffectChain* chain, int slot);
    
    /**
     * @brief Open plugin editor window
     * @param instance Plugin instance
     * @param parentWindow Native parent window handle
     */
    void openPluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance,
                          void* parentWindow = nullptr);
    
    // ==============================
    // Callbacks
    // ==============================
    
    /**
     * @brief Event payload for plugin loaded signal.
     */
    struct PluginLoadedEvent {
        std::string pluginId;
        int slot{-1};
    };

    /** @brief Signal emitted when a plugin is loaded into a slot. */
    Aestra::Events::Signal<PluginLoadedEvent> pluginLoaded;

    /**
     * @brief Signal emitted when a bound effect chain changes and the audio graph needs republishing.
     */
    Aestra::Events::Signal<void> effectChainChanged;
    
    /**
     * @brief Set callback when scan completes
     * @param callback Callback receiving the discovered plugin count.
     */
    void setOnScanComplete(std::function<void(int pluginCount)> callback);

    // Convert PluginInfo to PluginListItem for UI
    PluginListItem convertToListItem(const Aestra::Audio::PluginInfo& info) const;

private:
    
    // Backend references
    Aestra::Audio::PluginScanner* m_scanner = nullptr;
    Aestra::Audio::PluginManager* m_manager = nullptr;
    
    // Bound widgets
    PluginBrowserPanel* m_browser = nullptr;
    
    // Rack bindings (rack -> chain)
    struct RackBinding {
        EffectChainRack* rack;
        Aestra::Audio::EffectChain* chain;
    };
    std::vector<RackBinding> m_rackBindings;
    
    // effectChainChanged is a scoped subscription signal for effect-chain mutations.
    std::function<void(int)> m_onScanComplete;
    
    // UI components for popups
    NUIComponent* m_popupLayer = nullptr;
    std::shared_ptr<PluginSelectorMenu> m_activeMenu;
    std::vector<std::shared_ptr<NUIComponent>> m_activeEditors;
};

/**
 * @brief Floating window for hosting plugin editor GUIs
 * 
 * This creates a native window that hosts the plugin's own GUI.
 * Plugins draw directly into this window via their format-specific
 * mechanisms (VST3 IPlugView, CLAP gui extension).
 */
class PluginEditorWindow {
public:
    /** @brief Construct a floating plugin editor window. */
    PluginEditorWindow();
    /** @brief Destroy the floating plugin editor window. */
    ~PluginEditorWindow();
    
    /**
     * @brief Open editor for a plugin instance
     * @param instance Plugin instance with editor support
     * @param title Window title
     * @return true if window opened successfully
     */
    bool open(std::shared_ptr<Aestra::Audio::IPluginInstance> instance,
              const std::string& title = "Plugin Editor");
    
    /**
     * @brief Close the editor window
     */
    void close();
    
    /**
     * @brief Check if window is currently open
     * @return True when the native editor window is open.
     */
    bool isOpen() const;
    
    /**
     * @brief Get the native window handle
     * @return Platform-specific native window handle.
     */
    void* getNativeHandle() const;
    
    /**
     * @brief Get the plugin instance being edited
     * @return Shared pointer to the currently edited plugin instance.
     */
    std::shared_ptr<Aestra::Audio::IPluginInstance> getPluginInstance() const;
    
    /**
     * @brief Bring window to front
     */
    void bringToFront();
    
    /**
     * @brief Set window position
     * @param x New window x position.
     * @param y New window y position.
     */
    void setPosition(int x, int y);
    
    /**
     * @brief Get window position
     * @return Current window position pair.
     */
    std::pair<int, int> getPosition() const;
    
    /**
     * @brief Set callback for when window closes
     * @param callback Callback invoked after the editor window closes.
     */
    void setOnClose(std::function<void()> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::function<void()> m_onClose;
    bool m_isOpen = false;
};

} // namespace AestraUI
