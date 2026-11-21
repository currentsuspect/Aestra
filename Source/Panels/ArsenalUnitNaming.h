// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <vector>

namespace Aestra::ArsenalUnitNaming {

/**
 * @brief The name for a new unit: the prefix plus the lowest number no existing unit already uses.
 *
 * Counting units and adding one repeats a name as soon as anything but the last unit is deleted (1,2,3,5,6 then a
 * new unit made a second "6"). Filling the lowest free number never repeats a name and reuses the gap that a delete
 * left. Only names of exactly the form "<prefix><digits>" take a number; anything else, like a renamed unit, does not.
 */
inline std::string nextUnitName(const std::string& prefix, const std::vector<std::string>& existingNames) {
    std::vector<bool> taken;
    for (const std::string& name : existingNames) {
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string digits = name.substr(prefix.size());
        if (digits.size() > 6 || digits.find_first_not_of("0123456789") != std::string::npos)
            continue;
        const size_t number = static_cast<size_t>(std::stoul(digits));
        if (number == 0)
            continue;
        if (taken.size() < number)
            taken.resize(number, false);
        taken[number - 1] = true;
    }
    size_t lowest = 1;
    while (lowest <= taken.size() && taken[lowest - 1])
        ++lowest;
    return prefix + std::to_string(lowest);
}

} // namespace Aestra::ArsenalUnitNaming
