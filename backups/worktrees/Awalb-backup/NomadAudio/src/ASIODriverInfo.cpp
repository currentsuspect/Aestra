// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "ASIODriverInfo.h"
#include <windows.h>
#include <iostream>
#include <sstream>

namespace Nomad {
namespace Audio {

namespace {
    // Registry paths where ASIO drivers register themselves
    const char* ASIO_REGISTRY_PATH_64 = "SOFTWARE\\ASIO";
    const char* ASIO_REGISTRY_PATH_32 = "SOFTWARE\\WOW6432Node\\ASIO";
    
    /**
     * @brief Helper to read registry string value
     */
    std::string ReadRegistryString(HKEY hKey, const char* valueName) {
        char buffer[512] = {0};
        DWORD bufferSize = sizeof(buffer);
        DWORD type = REG_SZ;
        
        LONG result = RegQueryValueExA(hKey, valueName, nullptr, &type, 
                                       reinterpret_cast<LPBYTE>(buffer), &bufferSize);
        
        if (result == ERROR_SUCCESS && type == REG_SZ) {
            return std::string(buffer);
        }
        
        return "";
    }
    
    /**
     * @brief Check if a string looks like a valid CLSID
     */
    bool IsValidCLSIDFormat(const std::string& clsid) {
        // Basic format check: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
        if (clsid.length() != 38) return false;
        if (clsid[0] != '{' || clsid[37] != '}') return false;
        if (clsid[9] != '-' || clsid[14] != '-' || clsid[19] != '-' || clsid[24] != '-') return false;
        
        return true;
    }
}

std::vector<ASIODriverInfo> ASIODriverScanner::scanInstalledDrivers() {
    std::vector<ASIODriverInfo> drivers;
    
    // Scan 64-bit registry
    auto drivers64 = scanRegistry(ASIO_REGISTRY_PATH_64);
    drivers.insert(drivers.end(), drivers64.begin(), drivers64.end());
    
    // Scan 32-bit registry (WOW64)
    auto drivers32 = scanRegistry(ASIO_REGISTRY_PATH_32);
    drivers.insert(drivers.end(), drivers32.begin(), drivers32.end());
    
    // Remove duplicates (same driver in both 32/64 bit registry)
    std::vector<ASIODriverInfo> uniqueDrivers;
    for (const auto& driver : drivers) {
        bool isDuplicate = false;
        for (const auto& existing : uniqueDrivers) {
            if (existing.clsid == driver.clsid) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            uniqueDrivers.push_back(driver);
        }
    }
    
    std::cout << "[ASIO Scanner] Found " << uniqueDrivers.size() << " ASIO driver(s)" << std::endl;
    for (const auto& driver : uniqueDrivers) {
        std::cout << "  - " << driver.name << " (" << driver.clsid << ")" << std::endl;
    }
    
    return uniqueDrivers;
}

std::vector<ASIODriverInfo> ASIODriverScanner::scanRegistry(const std::string& registryPath) {
    std::vector<ASIODriverInfo> drivers;
    
    HKEY hKeyASIO = nullptr;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, registryPath.c_str(), 
                                 0, KEY_READ, &hKeyASIO);
    
    if (result != ERROR_SUCCESS) {
        // Registry path doesn't exist - no ASIO drivers installed
        return drivers;
    }
    
    // Enumerate subkeys (each represents an ASIO driver)
    DWORD index = 0;
    char driverName[256];
    DWORD nameSize = sizeof(driverName);
    
    while (RegEnumKeyExA(hKeyASIO, index, driverName, &nameSize, 
                         nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        
        // Open the driver's registry key
        HKEY hKeyDriver = nullptr;
        result = RegOpenKeyExA(hKeyASIO, driverName, 0, KEY_READ, &hKeyDriver);
        
        if (result == ERROR_SUCCESS) {
            ASIODriverInfo info;
            info.name = driverName;
            
            // Read CLSID
            info.clsid = ReadRegistryString(hKeyDriver, "CLSID");
            
            // Read Description (optional)
            info.description = ReadRegistryString(hKeyDriver, "Description");
            if (info.description.empty()) {
                info.description = info.name;
            }
            
            // Validate
            info.isValid = !info.clsid.empty() && IsValidCLSIDFormat(info.clsid);
            
            if (info.isValid) {
                drivers.push_back(info);
            }
            
            RegCloseKey(hKeyDriver);
        }
        
        // Move to next driver
        index++;
        nameSize = sizeof(driverName);
    }
    
    RegCloseKey(hKeyASIO);
    return drivers;
}

bool ASIODriverScanner::isValidCLSID(const std::string& clsid) {
    return IsValidCLSIDFormat(clsid);
}

bool ASIODriverScanner::hasInstalledDrivers() {
    return getInstalledDriverCount() > 0;
}

uint32_t ASIODriverScanner::getInstalledDriverCount() {
    auto drivers = scanInstalledDrivers();
    return static_cast<uint32_t>(drivers.size());
}

std::string ASIODriverScanner::getAvailabilityMessage() {
    auto drivers = scanInstalledDrivers();
    
    if (drivers.empty()) {
        return "No ASIO drivers detected. NOMAD uses WASAPI for professional low-latency audio.";
    }
    
    std::ostringstream oss;
    oss << "ASIO drivers detected: ";
    
    for (size_t i = 0; i < drivers.size(); ++i) {
        oss << drivers[i].name;
        if (i < drivers.size() - 1) {
            oss << ", ";
        }
    }
    
    oss << ".\n\nNOMAD uses WASAPI Exclusive mode for the same low latency. "
        << "Your ASIO devices will work through their WASAPI endpoints.";
    
    return oss.str();
}

} // namespace Audio
} // namespace Nomad
