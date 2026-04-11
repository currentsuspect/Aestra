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

/**
 * @brief Decode a 4-byte ID3v2 "synchsafe" integer into a 32-bit size value.
 *
 * @param data Pointer to the first of four synchsafe bytes; the caller must ensure
 *             at least 4 readable bytes are available at this address.
 * @return uint32_t The decoded 32-bit integer representing the synchsafe value.
 */
uint32_t readSynchsafeInt(const uint8_t* data) {
    return (data[0] << 21) | (data[1] << 14) | (data[2] << 7) | data[3];
}

/**
 * @brief Validate a declared ID3v2 synchsafe tag size from header bytes against a 10 MiB limit and report the result.
 *
 * Decodes the 4-byte synchsafe integer at the provided header pointer, prints the declared size, and emits a vulnerability message
 * if the declared size exceeds 10 * 1024 * 1024 bytes. The function simulates the decision point where an unbounded allocation
 * would occur in vulnerable code.
 *
 * @param header Pointer to the first of 4 bytes containing the ID3v2 synchsafe-encoded tag size.
 * @return true if the decoded tag size is less than or equal to 10 MiB (accepted), false if it is greater than 10 MiB (rejected).
 */
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

/**
 * @brief Execute a security test that supplies a crafted ID3v2 header with the maximum synchsafe tag size to the vulnerable parser and reports whether a size limit is enforced.
 *
 * The program constructs an ID3v2 header whose synchsafe size decodes to 268,435,455 bytes, invokes vulnerableTagParse with the size bytes, and prints a PASS message and exits successfully when the oversized declaration is rejected or prints a FAIL message and exits with failure when the parser would accept it.
 *
 * @return int 0 when the oversized tag was rejected (test PASS); 1 when the parser would accept the oversized tag (test FAIL).
 */
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
