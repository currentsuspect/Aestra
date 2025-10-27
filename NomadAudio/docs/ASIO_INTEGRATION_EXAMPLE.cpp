// Example: How to integrate ASIO detection into AudioSettingsDialog
// Add this to AudioSettingsDialog.cpp

#include "ASIODriverInfo.h"

void AudioSettingsDialog::updateDriverInfo() {
    using namespace Nomad::Audio;
    
    // === Show ASIO Detection Info ===
    std::string asioMessage = ASIODriverScanner::getAvailabilityMessage();
    
    // Display in UI (example using your UI framework)
    // m_asioInfoText->setText(asioMessage.c_str());
    
    std::cout << "\n=== ASIO Driver Detection ===" << std::endl;
    std::cout << asioMessage << std::endl;
    std::cout << "============================\n" << std::endl;
    
    // === Optional: Show detailed list ===
    auto drivers = ASIODriverScanner::scanInstalledDrivers();
    
    if (!drivers.empty()) {
        std::cout << "Detected ASIO Drivers:" << std::endl;
        for (const auto& driver : drivers) {
            std::cout << "  • " << driver.name << std::endl;
            std::cout << "    CLSID: " << driver.clsid << std::endl;
            if (!driver.description.empty() && driver.description != driver.name) {
                std::cout << "    Description: " << driver.description << std::endl;
            }
            std::cout << std::endl;
        }
    }
    
    // === Show current active driver ===
    // Assuming you have access to AudioDeviceManager
    // auto* activeDriver = m_deviceManager->getActiveDriver();
    // if (activeDriver) {
    //     std::cout << "Active Driver: " << activeDriver->getDisplayName() << std::endl;
    //     std::cout << "  Type: " << DriverTypeToString(activeDriver->getDriverType()) << std::endl;
    //     std::cout << "  Latency: " << activeDriver->getStreamLatency() * 1000.0 << "ms" << std::endl;
    // }
}

// === UI Layout Example ===
/*

Audio Settings Dialog
┌─────────────────────────────────────────────────┐
│                                                 │
│  Driver Selection:                              │
│  ┌─────────────────────────────────────┐        │
│  │ ▼ WASAPI Exclusive (Recommended)    │        │
│  └─────────────────────────────────────┘        │
│     • WASAPI Exclusive                          │
│     • WASAPI Shared                             │
│     • DirectSound (Legacy)                      │
│                                                 │
│  Current Status:                                │
│  ┌─────────────────────────────────────────┐   │
│  │ Active: WASAPI Exclusive                 │   │
│  │ Device: Speakers (Realtek High Def)     │   │
│  │ Sample Rate: 48000 Hz                   │   │
│  │ Buffer Size: 256 frames (5.33ms)        │   │
│  │ Latency: 6.2ms (measured)               │   │
│  │ CPU Load: 3.4%                           │   │
│  └─────────────────────────────────────────┘   │
│                                                 │
│  ASIO Information:                              │
│  ┌─────────────────────────────────────────┐   │
│  │ ℹ️ ASIO drivers detected:                │   │
│  │   • ASIO4ALL v2                          │   │
│  │   • FL Studio ASIO                       │   │
│  │                                          │   │
│  │ NOMAD uses WASAPI Exclusive mode for    │   │
│  │ professional low-latency audio (3-5ms). │   │
│  │                                          │   │
│  │ Your ASIO devices will work through     │   │
│  │ their WASAPI endpoints automatically.   │   │
│  └─────────────────────────────────────────┘   │
│                                                 │
│  Buffer Size: [128] [256] [512] [1024] frames  │
│                                                 │
│  [Apply] [OK] [Cancel]                          │
└─────────────────────────────────────────────────┘

*/

// === NomadUI Widget Example ===
/*

void AudioSettingsDialog::render() {
    using namespace NomadUI;
    
    // ... existing UI code ...
    
    // ASIO Information Panel
    Panel* asioPanel = Panel::create();
    asioPanel->setTitle("ASIO Information");
    
    Text* asioText = Text::create();
    asioText->setText(ASIODriverScanner::getAvailabilityMessage());
    asioText->setWordWrap(true);
    asioText->setColor(Color(200, 200, 200)); // Light gray
    
    asioPanel->addChild(asioText);
    
    // Optional: "Learn More" button
    Button* learnMoreBtn = Button::create();
    learnMoreBtn->setText("Why WASAPI?");
    learnMoreBtn->setOnClick([this]() {
        showWASAPIExplanation();
    });
    
    asioPanel->addChild(learnMoreBtn);
    
    // Add to main dialog
    addChild(asioPanel);
}

void AudioSettingsDialog::showWASAPIExplanation() {
    // Show educational dialog
    std::string explanation = 
        "WASAPI (Windows Audio Session API)\n\n"
        "WASAPI Exclusive mode provides:\n"
        "  • 3-5ms latency (same as ASIO)\n"
        "  • Direct hardware access\n"
        "  • No external drivers needed\n"
        "  • Built into Windows\n"
        "  • 100% stable and compatible\n\n"
        "Your ASIO-compatible audio interface will work perfectly "
        "with NOMAD through its WASAPI endpoint.";
    
    // MessageBox::show("About WASAPI", explanation);
}

*/

// === Startup Check Example ===
/*

void AudioDeviceManager::initialize() {
    // ... existing initialization ...
    
    // Log ASIO detection at startup
    std::cout << "\n=== Audio System Initialization ===" << std::endl;
    
    // Check for ASIO drivers
    if (ASIODriverScanner::hasInstalledDrivers()) {
        auto drivers = ASIODriverScanner::scanInstalledDrivers();
        std::cout << "Found " << drivers.size() << " ASIO driver(s):" << std::endl;
        for (const auto& driver : drivers) {
            std::cout << "  • " << driver.name << std::endl;
        }
        std::cout << "\nNOMAD will use WASAPI for equivalent performance.\n" << std::endl;
    } else {
        std::cout << "No ASIO drivers detected (this is fine)." << std::endl;
        std::cout << "NOMAD uses WASAPI for professional audio.\n" << std::endl;
    }
    
    // Initialize WASAPI drivers
    std::cout << "Initializing WASAPI drivers..." << std::endl;
    // ... rest of initialization ...
}

*/
