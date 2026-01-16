// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
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
#include <string>
#include <vector>

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
    PluginUIController();
    ~PluginUIController();
    
    // ==============================
    // Backend References
    // ==============================
    
    /**
     * @brief Set the plugin scanner (for discovery)
     */
    void setPluginScanner(Aestra::Audio::PluginScanner* scanner);
    
    /**
     * @brief Set the plugin manager (for loading instances)
     */
    void setPluginManager(Aestra::Audio::PluginManager* manager);
    
    /**
     * @brief Set the component to use as a layer for popups (menus, etc)
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
     */
    void bindEffectRack(EffectChainRack* rack, Aestra::Audio::EffectChain* chain);
    
    /**
     * @brief Unbind effect rack
     */
    void unbindEffectRack(EffectChainRack* rack);
    
    /**
     * @brief Refresh rack display from effect chain state
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
     * @brief Set callback when a plugin is loaded
     */
    void setOnPluginLoaded(std::function<void(const std::string& pluginId, int slot)> callback);
    
    /**
     * @brief Set callback when scan completes
     */
    void setOnScanComplete(std::function<void(int pluginCount)> callback);

private:
    // Convert PluginInfo to PluginListItem for UI
    PluginListItem convertToListItem(const Aestra::Audio::PluginInfo& info) const;
    
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
    
    // Callbacks
    std::function<void(const std::string&, int)> m_onPluginLoaded;
    std::function<void(int)> m_onScanComplete;
    
    // UI components for popups
    NUIComponent* m_popupLayer = nullptr;
    std::shared_ptr<PluginSelectorMenu> m_activeMenu;
    std::vector<std::shared_ptr<GenericPluginEditor>> m_activeEditors;
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
    PluginEditorWindow();
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
     */
    bool isOpen() const;
    
    /**
     * @brief Get the native window handle
     */
    void* getNativeHandle() const;
    
    /**
     * @brief Get the plugin instance being edited
     */
    std::shared_ptr<Aestra::Audio::IPluginInstance> getPluginInstance() const;
    
    /**
     * @brief Bring window to front
     */
    void bringToFront();
    
    /**
     * @brief Set window position
     */
    void setPosition(int x, int y);
    
    /**
     * @brief Get window position
     */
    std::pair<int, int> getPosition() const;
    
    /**
     * @brief Set callback for when window closes
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
