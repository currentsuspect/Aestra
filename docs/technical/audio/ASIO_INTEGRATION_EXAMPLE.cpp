// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
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
            std::cout << "  â€¢ " << driver.name << std::endl;
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
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚                                                 â”‚
â”‚  Driver Selection:                              â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”        â”‚
â”‚  â”‚ â–¼ WASAPI Exclusive (Recommended)    â”‚        â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜        â”‚
â”‚     â€¢ WASAPI Exclusive                          â”‚
â”‚     â€¢ WASAPI Shared                             â”‚
â”‚     â€¢ DirectSound (Legacy)                      â”‚
â”‚                                                 â”‚
â”‚  Current Status:                                â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”‚
â”‚  â”‚ Active: WASAPI Exclusive                 â”‚   â”‚
â”‚  â”‚ Device: Speakers (Realtek High Def)     â”‚   â”‚
â”‚  â”‚ Sample Rate: 48000 Hz                   â”‚   â”‚
â”‚  â”‚ Buffer Size: 256 frames (5.33ms)        â”‚   â”‚
â”‚  â”‚ Latency: 6.2ms (measured)               â”‚   â”‚
â”‚  â”‚ CPU Load: 3.4%                           â”‚   â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
â”‚                                                 â”‚
â”‚  ASIO Information:                              â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”‚
â”‚  â”‚ â„¹ï¸ ASIO drivers detected:                â”‚   â”‚
â”‚  â”‚   â€¢ ASIO4ALL v2                          â”‚   â”‚
â”‚  â”‚   â€¢ FL Studio ASIO                       â”‚   â”‚
â”‚  â”‚                                          â”‚   â”‚
â”‚  â”‚ NOMAD uses WASAPI Exclusive mode for    â”‚   â”‚
â”‚  â”‚ professional low-latency audio (3-5ms). â”‚   â”‚
â”‚  â”‚                                          â”‚   â”‚
â”‚  â”‚ Your ASIO devices will work through     â”‚   â”‚
â”‚  â”‚ their WASAPI endpoints automatically.   â”‚   â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
â”‚                                                 â”‚
â”‚  Buffer Size: [128] [256] [512] [1024] frames  â”‚
â”‚                                                 â”‚
â”‚  [Apply] [OK] [Cancel]                          â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜

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
        "  â€¢ 3-5ms latency (same as ASIO)\n"
        "  â€¢ Direct hardware access\n"
        "  â€¢ No external drivers needed\n"
        "  â€¢ Built into Windows\n"
        "  â€¢ 100% stable and compatible\n\n"
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
            std::cout << "  â€¢ " << driver.name << std::endl;
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
