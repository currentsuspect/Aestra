// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/ProjectSerializer.h"

namespace Aestra {

enum class MuseProjectLoadOrigin {
    Canonical,
    Recovery,
    Snapshot
};

/**
 * @brief Translate the loader's existing result into Muse's observational schema.
 *
 * This function does not recover, retry, mutate, or reinterpret a project. It
 * only gives stable names to facts ProjectSerializer already recorded.
 */
JSON makeMuseProjectLoadReport(const ProjectSerializer::LoadResult& result,
                               MuseProjectLoadOrigin origin);

} // namespace Aestra
