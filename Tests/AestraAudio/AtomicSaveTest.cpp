// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PlaylistSerializer.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

using namespace Aestra::Audio;

namespace fs = std::filesystem;

// Real SourceManager used (linked in CMake)
// No Mock needed as logic is simple (in-memory map)

int main() {
    std::cout << "Starting Atomic Save Test..." << std::endl;
    
    std::string testFile = "test_atomic_save.json";
    std::string tempFile = testFile + ".tmp";
    
    // Cleanup previous runs
    if (fs::exists(testFile)) fs::remove(testFile);
    if (fs::exists(tempFile)) fs::remove(tempFile);
    
    PlaylistModel model;
    model.createLane("Test Lane");
    SourceManager sourceMgr;
    
    // Test 1: Successful Save
    std::cout << "Test 1: Normal Save... ";
    bool success = PlaylistSerializer::saveToFile(testFile, model, sourceMgr);
    
    if (success && fs::exists(testFile) && !fs::exists(tempFile)) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAILURE (File missing or temp file failed cleanup)" << std::endl;
        return 1;
    }
    
    // Check content
    {
        std::ifstream file(testFile);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.find("Test Lane") != std::string::npos) {
            std::cout << "  Content verified." << std::endl;
        } else {
            std::cout << "  FAILURE: Content corrupted." << std::endl;
            return 1;
        }
    }
    
    // Test 2: Overwrite existing
    std::cout << "Test 2: Overwrite existing... ";
    model.createLane("Updated Lane");
    
    success = PlaylistSerializer::saveToFile(testFile, model, sourceMgr);
    
    {
        std::ifstream file2(testFile);
        std::string content2((std::istreambuf_iterator<char>(file2)), std::istreambuf_iterator<char>());
        
        if (success && content2.find("Updated Lane") != std::string::npos) {
            std::cout << "SUCCESS" << std::endl;
        } else {
            std::cout << "FAILURE: Did not overwrite correctly." << std::endl;
            return 1;
        }
    }

    // Cleanup
    if (fs::exists(testFile)) fs::remove(testFile);
    
    std::cout << "Atomic Save Verification Complete." << std::endl;
    return 0;
}
