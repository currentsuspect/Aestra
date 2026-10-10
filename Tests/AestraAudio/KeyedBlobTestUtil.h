// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Test-side helpers for the KEYED v2 state blob.
//
// A v2 blob is {magic u32, version u32, count u32, (paramId u32, value f32) * count}.
// Two properties make hand-computed offsets a bad idea, and this header exists
// because of both:
//
//   1. The entry order is the TABLE's order, not id order. Offset
//      `8 + 4 * id` was correct for v1's positional layout; under v2 it silently
//      lands on an entry's id field instead of its value. A test that pokes
//      there writes junk into an id, the reader ignores the out-of-range id, and
//      the test then reports a PASS for a load that never happened. That is
//      exactly what happened to AestraDelayUpgradeTest when v2 landed.
//
//   2. The next time somebody reorders a table, id and position diverge again
//      and any hardcoded offset silently stops meaning what it meant.
//
// So: find the entry by its id, and only then write. If the id is not in the
// blob, the helper says so rather than writing somewhere else.

#ifndef AESTRA_TESTS_KEYED_BLOB_H
#define AESTRA_TESTS_KEYED_BLOB_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace AestraTestBlob {

inline constexpr size_t kKeyedHeaderSize = sizeof(uint32_t) * 3;
inline constexpr size_t kKeyedEntrySize = sizeof(uint32_t) + sizeof(float);
inline constexpr size_t kKeyedEntryIdOffset = 0;
inline constexpr size_t kKeyedEntryValueOffset = sizeof(uint32_t);

inline uint32_t readCount(const std::vector<uint8_t>& blob) {
    uint32_t count = 0;
    if (blob.size() >= kKeyedHeaderSize) {
        std::memcpy(&count, blob.data() + sizeof(uint32_t) * 2, sizeof(count));
    }
    return count;
}

inline uint32_t readIdAt(const std::vector<uint8_t>& blob, uint32_t index) {
    uint32_t id = 0;
    std::memcpy(&id, blob.data() + kKeyedHeaderSize + kKeyedEntrySize * index + kKeyedEntryIdOffset,
                sizeof(id));
    return id;
}

inline float readValueAt(const std::vector<uint8_t>& blob, uint32_t index) {
    float value = 0.0f;
    std::memcpy(&value, blob.data() + kKeyedHeaderSize + kKeyedEntrySize * index + kKeyedEntryValueOffset,
                sizeof(value));
    return value;
}

/// Index of the entry carrying @p id, or UINT32_MAX if the blob has no such
/// entry. A miss is a real answer: it means the table changed shape since the
/// blob was written, which is precisely what keyed state is supposed to survive.
inline uint32_t findEntry(const std::vector<uint8_t>& blob, uint32_t id) {
    const uint32_t count = readCount(blob);
    for (uint32_t i = 0; i < count; ++i) {
        if (readIdAt(blob, i) == id)
            return i;
    }
    return UINT32_MAX;
}

/// Overwrite the value of the entry carrying @p id. Returns false and writes
/// nothing if there is no such entry, so a test cannot accidentally corrupt a
/// neighbouring field and still report success.
inline bool setValueForId(std::vector<uint8_t>& blob, uint32_t id, float value) {
    const uint32_t index = findEntry(blob, id);
    if (index == UINT32_MAX)
        return false;
    std::memcpy(blob.data() + kKeyedHeaderSize + kKeyedEntrySize * index + kKeyedEntryValueOffset, &value,
                sizeof(value));
    return true;
}

} // namespace AestraTestBlob

#endif // AESTRA_TESTS_KEYED_BLOB_H
