#include "LicenseGate.h"

#include <cstddef>
#include <iostream>
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
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F')
            return 10 + (ch - 'A');
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

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
        return false;
    }
    return true;
}
} // namespace

int main() {
    const std::string canonical =
        "{\"license_id\":\"lic_worker_fixture_1\",\"user_id\":\"user_worker_fixture_1\",\"tier\":\"Supporter\","
        "\"plugins\":[\"com.Aestrastudios.rumble\",\"com.Aestrastudios.experimental\"],"
        "\"features\":[\"rumble\",\"cloud_sync\"],"
        "\"device_hash\":\"v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:"
        "cccccccccccccccccccccccccccccccc:dddddddddddddddddddddddddddddddd\","
        "\"issued_at\":1700000000,\"expires_at\":1700604800,\"grace_policy\":\"restrict\","
        "\"revocation_epoch\":0}";
    const std::vector<unsigned char> signature = fromHex(
        "6570b63ace3cd4848ce82aa5afe4aa37120a86e6c31f44eef5dfaab350fe29d"
        "165476c64f1bfb1741b4ff4b2db1d44c9866edb96e1cf974be24c7b424c575506");
    bool ok = true;
    ok &= expect(signature.size() == 64U, "fixture signature should decode to 64 bytes");
    ok &= expect(Aestra::License::verifyEd25519Detached(canonical, signature, Aestra::License::AESTRA_LICENSE_PUBKEY),
                 "Worker fixture signature must verify with the embedded C++ LicenseGate dev key");

    std::string tamperedPayload = canonical;
    const size_t tierPos = tamperedPayload.find("Supporter");
    if (tierPos != std::string::npos) {
        tamperedPayload.replace(tierPos, std::string("Supporter").size(), "Founder");
    }
    ok &= expect(!Aestra::License::verifyEd25519Detached(tamperedPayload, signature, Aestra::License::AESTRA_LICENSE_PUBKEY),
                 "tampered canonical payload must fail verification");

    std::vector<unsigned char> tamperedSignature = signature;
    if (!tamperedSignature.empty()) {
        tamperedSignature[0] ^= 0x01U;
    }
    ok &= expect(!Aestra::License::verifyEd25519Detached(canonical, tamperedSignature, Aestra::License::AESTRA_LICENSE_PUBKEY),
                 "tampered fixture signature must fail verification");

    return ok ? 0 : 1;
}
