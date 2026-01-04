// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginUIController.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace NomadUI {

// ============================================================================
// PluginUIController Implementation
// ============================================================================

PluginUIController::PluginUIController() = default;

PluginUIController::~PluginUIController() {
    unbindBrowser();
    for (auto& binding : m_rackBindings) {
        unbindEffectRack(binding.rack);
    }
}

void PluginUIController::setPluginScanner(Nomad::Audio::PluginScanner* scanner) {
    m_scanner = scanner;
}

void PluginUIController::setPluginManager(Nomad::Audio::PluginManager* manager) {
    m_manager = manager;
}

void PluginUIController::bindBrowser(PluginBrowserPanel* browser) {
    if (!browser) return;
    m_browser = browser;
    
    // Wire scan button
    browser->setOnScanRequested([this]() {
        startScan();
    });
    
    // Wire plugin selection (double-click)
    browser->setOnPluginLoadRequested([this](const PluginListItem& item) {
        // Just notify - actual loading depends on context (which slot/chain)
        if (m_onPluginLoaded) {
            m_onPluginLoaded(item.id, -1);
        }
    });
    
    // Initial refresh
    refreshBrowserList();
}

void PluginUIController::unbindBrowser() {
    if (m_browser) {
        m_browser->setOnScanRequested(nullptr);
        m_browser->setOnPluginLoadRequested(nullptr);
        m_browser = nullptr;
    }
}

void PluginUIController::refreshBrowserList() {
    if (!m_browser || !m_scanner) return;
    
    const auto& plugins = m_scanner->getScannedPlugins();
    std::vector<PluginListItem> items;
    items.reserve(plugins.size());
    
    for (const auto& info : plugins) {
        items.push_back(convertToListItem(info));
    }
    
    m_browser->setPluginList(items);
}

void PluginUIController::startScan() {
    if (!m_scanner || !m_browser) return;
    
    // Show scanning state
    m_browser->setScanning(true, 0.0f);
    m_browser->setScanStatus("Starting scan...");
    
    // Start async scan
    m_scanner->scanAsync(
        // Progress callback
        [this](const std::string& path, int current, int total) {
            if (m_browser) {
                float progress = total > 0 ? static_cast<float>(current) / total : 0.0f;
                m_browser->setScanning(true, progress);
                
                // Extract filename for status
                auto filename = std::filesystem::path(path).filename().string();
                m_browser->setScanStatus(filename);
            }
        },
        // Completion callback
        [this](const std::vector<Nomad::Audio::PluginInfo>& plugins, bool success) {
            if (m_browser) {
                m_browser->setScanning(false, 1.0f);
                if (success) {
                    refreshBrowserList();
                }
            }
            
            if (m_onScanComplete) {
                m_onScanComplete(static_cast<int>(plugins.size()));
            }
        }
    );
}

void PluginUIController::bindEffectRack(EffectChainRack* rack, 
                                         Nomad::Audio::EffectChain* chain) {
    if (!rack || !chain) return;
    
    // Store binding
    m_rackBindings.push_back({rack, chain});
    
    // Wire slot clicks
    rack->setOnSlotClicked([this, chain](int slot) {
        auto instance = chain->getPlugin(slot);
        if (instance && instance->hasEditor()) {
            openPluginEditor(instance, nullptr);
        }
    });
    
    rack->setOnAddPluginRequested([this, rack, chain](int slot) {
        // Need to show browser and handle selection...
        // For now, just mark slot as pending
        // In a full implementation, this would open a popup browser
    });
    
    rack->setOnSlotBypassToggled([chain](int slot, bool bypassed) {
        chain->setSlotBypassed(slot, bypassed);
    });
    
    rack->setOnSlotRemoveRequested([this, rack, chain](int slot) {
        removePluginFromSlot(chain, slot);
        refreshRackDisplay(rack);
    });
    
    // Initial refresh
    refreshRackDisplay(rack);
}

void PluginUIController::unbindEffectRack(EffectChainRack* rack) {
    if (!rack) return;
    
    rack->setOnSlotClicked(nullptr);
    rack->setOnAddPluginRequested(nullptr);
    rack->setOnSlotBypassToggled(nullptr);
    rack->setOnSlotRemoveRequested(nullptr);
    
    // Remove from bindings
    m_rackBindings.erase(
        std::remove_if(m_rackBindings.begin(), m_rackBindings.end(),
                       [rack](const RackBinding& b) { return b.rack == rack; }),
        m_rackBindings.end());
}

void PluginUIController::refreshRackDisplay(EffectChainRack* rack) {
    if (!rack) return;
    
    // Find chain binding
    Nomad::Audio::EffectChain* chain = nullptr;
    for (const auto& binding : m_rackBindings) {
        if (binding.rack == rack) {
            chain = binding.chain;
            break;
        }
    }
    
    if (!chain) return;
    
    // Update slot display
    for (int i = 0; i < EffectChainRack::MAX_SLOTS; ++i) {
        auto instance = chain->getPlugin(i);
        EffectChainRack::EffectSlotInfo info;
        
        if (instance) {
            info.name = instance->getInfo().name;
            info.isEmpty = false;
            info.bypassed = chain->isSlotBypassed(i);
        } else {
            info.name = "Empty";
            info.isEmpty = true;
            info.bypassed = false;
        }
        
        rack->setSlot(i, info);
    }
}

bool PluginUIController::loadPluginToSlot(const std::string& pluginId,
                                           Nomad::Audio::EffectChain* chain,
                                           int slot) {
    if (!m_manager || !chain) return false;
    
    // Create instance via manager by ID
    auto instance = m_manager->createInstanceById(pluginId);
    if (!instance) return false;
    
    // Initialize with current audio settings
    // TODO: Get actual sample rate/block size from audio engine
    if (!instance->initialize(44100.0, 512)) {
        return false;
    }
    
    // Insert into chain
    chain->insertPlugin(slot, instance);
    
    if (m_onPluginLoaded) {
        m_onPluginLoaded(pluginId, slot);
    }
    
    // Refresh any bound rack
    for (const auto& binding : m_rackBindings) {
        if (binding.chain == chain) {
            refreshRackDisplay(binding.rack);
        }
    }
    
    return true;
}

void PluginUIController::removePluginFromSlot(Nomad::Audio::EffectChain* chain, 
                                               int slot) {
    if (!chain) return;
    chain->removePlugin(slot);
}

void PluginUIController::openPluginEditor(
    std::shared_ptr<Nomad::Audio::IPluginInstance> instance,
    void* parentWindow) {
    
    if (!instance || !instance->hasEditor()) return;
    
    // For now, open editor in a simple way
    // Full implementation would create/manage PluginEditorWindow
    instance->openEditor(parentWindow);
}

void PluginUIController::setOnPluginLoaded(
    std::function<void(const std::string&, int)> callback) {
    m_onPluginLoaded = std::move(callback);
}

void PluginUIController::setOnScanComplete(
    std::function<void(int)> callback) {
    m_onScanComplete = std::move(callback);
}

PluginListItem PluginUIController::convertToListItem(
    const Nomad::Audio::PluginInfo& info) const {
    
    PluginListItem item;
    item.id = info.id;
    item.name = info.name;
    item.vendor = info.vendor;
    item.version = info.version;
    item.category = info.category;
    
    // Format string
    switch (info.format) {
        case Nomad::Audio::PluginFormat::VST3:
            item.formatStr = "VST3";
            break;
        case Nomad::Audio::PluginFormat::CLAP:
            item.formatStr = "CLAP";
            break;
        default:
            item.formatStr = "Int";
            break;
    }
    
    // Type name
    switch (info.type) {
        case Nomad::Audio::PluginType::Effect:
            item.typeName = "Effect";
            break;
        case Nomad::Audio::PluginType::Instrument:
            item.typeName = "Instrument";
            break;
        case Nomad::Audio::PluginType::MidiEffect:
            item.typeName = "MIDI";
            break;
        case Nomad::Audio::PluginType::Analyzer:
            item.typeName = "Analyzer";
            break;
    }
    
    item.isFavorite = false;
    return item;
}

// ============================================================================
// PluginEditorWindow Implementation
// ============================================================================

struct PluginEditorWindow::Impl {
#ifdef _WIN32
    HWND hwnd = nullptr;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
    int posX = 100;
    int posY = 100;
};

#ifdef _WIN32
LRESULT CALLBACK PluginEditorWindow::Impl::WindowProc(HWND hwnd, UINT msg, 
                                                       WPARAM wParam, LPARAM lParam) {
    PluginEditorWindow* self = reinterpret_cast<PluginEditorWindow*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    switch (msg) {
        case WM_CLOSE:
            if (self) {
                self->close();
                if (self->m_onClose) {
                    self->m_onClose();
                }
            }
            return 0;
            
        case WM_DESTROY:
            return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

PluginEditorWindow::PluginEditorWindow()
    : m_impl(std::make_unique<Impl>()) {
}

PluginEditorWindow::~PluginEditorWindow() {
    close();
}

bool PluginEditorWindow::open(
    std::shared_ptr<Nomad::Audio::IPluginInstance> instance,
    const std::string& title) {
    
    if (!instance || !instance->hasEditor()) {
        return false;
    }
    
    if (m_isOpen) {
        close();
    }
    
    m_instance = instance;
    
    // Get preferred editor size
    auto [width, height] = instance->getEditorSize();
    if (width <= 0) width = 800;
    if (height <= 0) height = 600;
    
#ifdef _WIN32
    // Register window class (once)
    static bool classRegistered = false;
    static const wchar_t* CLASS_NAME = L"NomadPluginEditor";
    
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Impl::WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);
        classRegistered = true;
    }
    
    // Convert title to wide string
    int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::wstring wideTitle(titleLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wideTitle[0], titleLen);
    
    // Create window
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    
    m_impl->hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        wideTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        m_impl->posX, m_impl->posY,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    if (!m_impl->hwnd) {
        return false;
    }
    
    // Store this pointer
    SetWindowLongPtr(m_impl->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    // Open plugin editor into window
    if (!instance->openEditor(m_impl->hwnd)) {
        DestroyWindow(m_impl->hwnd);
        m_impl->hwnd = nullptr;
        return false;
    }
    
    // Show window
    ShowWindow(m_impl->hwnd, SW_SHOW);
    UpdateWindow(m_impl->hwnd);
    
    m_isOpen = true;
    return true;
#else
    // Non-Windows: stub implementation
    m_isOpen = true;
    return instance->openEditor(nullptr);
#endif
}

void PluginEditorWindow::close() {
    if (!m_isOpen) return;
    
    if (m_instance) {
        m_instance->closeEditor();
    }
    
#ifdef _WIN32
    if (m_impl->hwnd) {
        DestroyWindow(m_impl->hwnd);
        m_impl->hwnd = nullptr;
    }
#endif
    
    m_isOpen = false;
    m_instance.reset();
}

bool PluginEditorWindow::isOpen() const {
    return m_isOpen;
}

void* PluginEditorWindow::getNativeHandle() const {
#ifdef _WIN32
    return m_impl->hwnd;
#else
    return nullptr;
#endif
}

std::shared_ptr<Nomad::Audio::IPluginInstance> PluginEditorWindow::getPluginInstance() const {
    return m_instance;
}

void PluginEditorWindow::bringToFront() {
#ifdef _WIN32
    if (m_impl->hwnd) {
        SetForegroundWindow(m_impl->hwnd);
    }
#endif
}

void PluginEditorWindow::setPosition(int x, int y) {
    m_impl->posX = x;
    m_impl->posY = y;
#ifdef _WIN32
    if (m_impl->hwnd) {
        SetWindowPos(m_impl->hwnd, nullptr, x, y, 0, 0, 
                     SWP_NOSIZE | SWP_NOZORDER);
    }
#endif
}

std::pair<int, int> PluginEditorWindow::getPosition() const {
#ifdef _WIN32
    if (m_impl->hwnd) {
        RECT rect;
        GetWindowRect(m_impl->hwnd, &rect);
        return {rect.left, rect.top};
    }
#endif
    return {m_impl->posX, m_impl->posY};
}

void PluginEditorWindow::setOnClose(std::function<void()> callback) {
    m_onClose = std::move(callback);
}

} // namespace NomadUI
