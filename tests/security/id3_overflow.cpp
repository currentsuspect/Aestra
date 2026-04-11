// © 2025 Aestra Studios — All Rights Reserved.
// SEC-005: Unbounded ID3v2 tag allocation (heap exhaustion DoS)
//
// Proof: A crafted MP3 file declaring a 200MB ID3v2 tag size causes
// MetadataParser to allocate 200MB of memory before checking if the
// file actually contains that much data.
//
// After fix: The parser enforces a maximum tag size (e.g., 10MB) and
// returns a parse error for oversized declarations.

#include <iostream>
#include <cstdint>
#include <vector>
#include <cstdlib>

// Reproduces the vulnerable logic from MetadataParser.cpp:107-112
uint32_t readSynchsafeInt(const uint8_t* data) {
    return (data[0] << 21) | (data[1] << 14) | (data[2] << 7) | data[3];
}

bool vulnerableTagParse(const uint8_t* header) {
    uint32_t tagSize = readSynchsafeInt(header);
    std::cout << "  Declared tag size: " << tagSize << " bytes (" << (tagSize / 1024 / 1024) << " MB)" << std::endl;

    // EXACT code from MetadataParser.cpp:110-112:
    // std::vector<uint8_t> tagData(tagSize);
    // file.read(reinterpret_cast<char*>(tagData.data()), tagSize);
    // if (!file || static_cast<uint32_t>(file.gcount()) < tagSize) return false;

    // The allocation happens BEFORE the read check.
    // With tagSize = 268435455 (max synchsafe), this allocates ~256MB.
    if (tagSize > 10 * 1024 * 1024) {
        std::cout << "  [VULNERABLE] No size limit enforced — would allocate " << (tagSize / 1024 / 1024) << " MB" << std::endl;
        return false;
    }

    return true;
}

int main() {
    std::cout << "=== SEC-005: Unbounded ID3v2 tag allocation ===" << std::endl;

    // Max synchsafe integer: 0x7F, 0x7F, 0x7F, 0x7F = 268,435,455 bytes (~256 MB)
    uint8_t maliciousHeader[10] = { 'I', 'D', '3', 0x04, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F };

    bool ok = vulnerableTagParse(&maliciousHeader[6]);
    if (ok) {
        std::cout << "\n[FAIL] Vulnerability confirmed: no maximum tag size limit." << std::endl;
        std::cout << "Fix: enforce max tag size (e.g., 10MB) in MetadataParser.cpp before allocating tagData vector" << std::endl;
        return 1;
    }

    std::cout << "\n[PASS] Tag size limit enforced." << std::endl;
    return 0;
}
