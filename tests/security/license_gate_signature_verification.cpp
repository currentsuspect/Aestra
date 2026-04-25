#include "LicenseGate.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> fromHex(const std::string& hex) {
    std::vector<unsigned char> out;
    if ((hex.size() % 2U) != 0U) {
        return out;
    }
    out.reserve(hex.size() / 2U);
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2U) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1U]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return out;
        }
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return out;
}
} // namespace

int main() {
    // RFC 8032 Ed25519 test vector 1 (empty message).
    const unsigned char publicKey[32] = {
        0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
        0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
        0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
    std::vector<unsigned char> signature = fromHex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");

    const std::string payload;
    assert(Aestra::License::verifyEd25519Detached(payload, signature, publicKey));

    std::string tamperedPayload = "x";
    assert(!Aestra::License::verifyEd25519Detached(tamperedPayload, signature, publicKey));

    signature[0] ^= 0x01;
    assert(!Aestra::License::verifyEd25519Detached(payload, signature, publicKey));

    std::vector<unsigned char> shortSignature(63, 0x00);
    assert(!Aestra::License::verifyEd25519Detached(payload, shortSignature, publicKey));

    return 0;
}
