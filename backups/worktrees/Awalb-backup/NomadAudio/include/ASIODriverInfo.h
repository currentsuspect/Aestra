// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Information about an installed ASIO driver
 * 
 * NOTE: This is READ-ONLY detection. We don't load or use ASIO drivers.
 * This is purely for informational purposes and future compatibility.
 */
struct ASIODriverInfo {
    std::string name;           // Driver display name (e.g., "ASIO4ALL v2")
    std::string clsid;          // COM CLSID (e.g., "{232685C6-6548-49D8-846D-4141A3EF7560}")
    std::string description;    // Optional description
    bool isValid = false;       // Whether the registry entry is valid
    
    // Optional: Path to DLL (if we can find it in registry)
    std::string dllPath;
};

/**
 * @brief Minimal ASIO driver scanner (READ-ONLY)
 * 
 * Purpose: Detect what ASIO drivers are installed on the system.
 * 
 * What it does:
 * - Scans Windows Registry for ASIO drivers
 * - Returns list of installed drivers
 * - Does NOT load any DLLs
 * - Does NOT attempt to use ASIO drivers
 * 
 * Why read-only?
 * - Avoids licensing issues (no Steinberg SDK needed)
 * - Avoids driver compatibility issues
 * - Avoids maintenance burden
 * - Can inform user what drivers are available
 * - Future-proof: Easy to upgrade to full ASIO later
 * 
 * Use case:
 * - Display in UI: "ASIO drivers found: ASIO4ALL, FL Studio ASIO"
 * - Show message: "For ASIO support, use your device's WASAPI endpoint"
 * - Future: If enough demand, upgrade to full ASIO support
 */
class ASIODriverScanner {
public:
    /**
     * @brief Scan system for installed ASIO drivers
     * @return Vector of detected ASIO drivers
     */
    static std::vector<ASIODriverInfo> scanInstalledDrivers();
    
    /**
     * @brief Check if any ASIO drivers are installed
     * @return True if at least one ASIO driver found
     */
    static bool hasInstalledDrivers();
    
    /**
     * @brief Get count of installed ASIO drivers
     * @return Number of ASIO drivers found
     */
    static uint32_t getInstalledDriverCount();
    
    /**
     * @brief Get user-friendly message about ASIO availability
     * @return Message to display in UI
     */
    static std::string getAvailabilityMessage();
    
private:
    static std::vector<ASIODriverInfo> scanRegistry(const std::string& registryPath);
    static bool isValidCLSID(const std::string& clsid);
};

} // namespace Audio
} // namespace Nomad
