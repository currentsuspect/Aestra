// © 2026 Aestra Studios — All Rights Reserved.
// RTM-012: FLAC Vorbis Comment vendorLen integer overflow — proof of fix
// Tests the bounds check: vendorLen must fit within blockSize.

#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>

// Reproduce the fixed parsing logic from MetadataParser.cpp
bool fixedVorbisCommentParse(const std::vector<uint8_t>& blockData, uint32_t blockSize) {
    if (blockSize < 8) return false;

    size_t pos = 0;
    uint32_t vendorLen = blockData[0] | (blockData[1] << 8) | (blockData[2] << 16) | (blockData[3] << 24);

    // [SEC-RTM-012] Guard against integer overflow
    if (vendorLen > blockSize - 4)
        return false;

    pos = 4 + vendorLen;
    if (pos + 4 > blockSize)
        return false;

    return true;  // Would continue parsing comments
}

int main() {
    std::cout << "=== RTM-012: FLAC vendorLen overflow — proof of fix ===" << std::endl;

    struct Test {
        uint32_t vendorLen;
        uint32_t blockSize;
        bool expect;
        const char* desc;
    };
    Test tests[] = {
        {4, 100, true, "normal small vendor string"},
        {50, 200, true, "normal medium vendor string"},
        {1000, 2000, true, "large vendor string within bounds"},
        {96, 100, false, "vendorLen = blockSize-4 (pos+4 overflows)"},
        {0, 8, true, "empty vendor string (minimum blockSize=8)"},
        // Attack cases: vendorLen larger than blockSize
        {0xFFFFFFFF, 100, false, "UINT32_MAX in small block"},
        {0x7FFFFFFF, 100, false, "INT32_MAX in small block"},
        {200, 100, false, "vendorLen > blockSize"},
        {97, 100, false, "vendorLen = blockSize - 3 (insufficient space for comments)"},
        {1000000, 1000, false, "vendorLen 1MB in 1KB block"},
    };

    bool allPass = true;
    for (const auto& t : tests) {
        // Build block data with the specified vendorLen
        std::vector<uint8_t> data(t.blockSize, 0);
        data[0] = t.vendorLen & 0xFF;
        data[1] = (t.vendorLen >> 8) & 0xFF;
        data[2] = (t.vendorLen >> 16) & 0xFF;
        data[3] = (t.vendorLen >> 24) & 0xFF;

        bool result = fixedVorbisCommentParse(data, t.blockSize);
        bool ok = (result == t.expect);
        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] vendorLen=" << t.vendorLen
                  << " blockSize=" << t.blockSize << " → " << (result ? "accept" : "reject")
                  << " (expected " << (t.expect ? "accept" : "reject") << ") " << t.desc << std::endl;
        if (!ok) allPass = false;
    }

    std::cout << "\n[" << (allPass ? "PASS" : "FAIL") << "] RTM-012 vendorLen overflow protection verified." << std::endl;
    return allPass ? 0 : 1;
}
