// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseFileDigest.h"

#include <array>
#include <cstdio>
#include <exception>
#include <fstream>

namespace Aestra {
namespace Audio {

std::string fnv1aFileDigest(const std::string& path) {
    // FNV-1a 64-bit. Chosen for being a few lines with no table, not for
    // cryptographic strength: this detects "the render changed", and it is not
    // defending the file from anyone.
    constexpr uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr uint64_t kPrime = 0x100000001b3ULL;

    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    uint64_t hash = kOffsetBasis;
    std::array<char, 64 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = file.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= kPrime;
        }
    }
    // A read that failed mid-file still leaves a hash of a prefix, which would
    // compare equal to another truncated prefix. Report that as no digest.
    if (file.bad()) return {};

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(hex);
}

} // namespace Audio
} // namespace Aestra
