// © 2025 Aestra Studios — All Rights Reserved.
// SEC-001: std::stoul crash on malformed color value in project deserialization
//
// Proof: A crafted .aes project file with "color": "not_a_number" causes
// std::stoul to throw std::invalid_argument, crashing the process.
//
// After fix: The parser validates the color string before calling stoul,
// and gracefully handles malformed values without crashing.

#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <filesystem>

// Reproduce the vulnerable parsing logic from ProjectSerializer.cpp:622
bool vulnerableColorParse(const std::string& colorStr, uint32_t& result) {
    // This is the EXACT code from ProjectSerializer.cpp line 622:
    // lane->colorRGBA = static_cast<uint32_t>(std::stoul(lj[i]["color"].asString()));
    result = static_cast<uint32_t>(std::stoul(colorStr));
    return true;
}

int main() {
    std::cout << "=== SEC-001: std::stoul crash on malformed color ===" << std::endl;

    const std::string testCases[] = {
        "not_a_number",       // invalid characters
        "99999999999999999999999", // overflow
        "abc123xyz",          // mixed
        "",                   // empty
        "  ",                 // whitespace
        "-1",                 // negative
    };

    bool crashed = false;
    for (const auto& tc : testCases) {
        uint32_t result = 0;
        try {
            vulnerableColorParse(tc, result);
            std::cout << "  [UNEXPECTED] \"" << tc << "\" parsed to " << result << std::endl;
        } catch (const std::invalid_argument& e) {
            std::cout << "  [CRASH] \"" << tc << "\" threw invalid_argument: " << e.what() << std::endl;
            crashed = true;
        } catch (const std::out_of_range& e) {
            std::cout << "  [CRASH] \"" << tc << "\" threw out_of_range: " << e.what() << std::endl;
            crashed = true;
        }
    }

    if (crashed) {
        std::cout << "\n[FAIL] Vulnerability confirmed: malformed color values crash the parser." << std::endl;
        std::cout << "Fix: validate color string before calling std::stoul in ProjectSerializer.cpp:622" << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] All malformed color values handled gracefully." << std::endl;
    return 0;
}
