// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "../NomadCore/include/NomadJSON.h"
#include <string>
#include <memory>

class ProjectSerializer {
public:
    struct LoadResult {
        bool ok{false};
        double tempo{120.0};
        double playhead{0.0};
    };

    static bool save(const std::string& path,
                     const std::shared_ptr<Nomad::Audio::TrackManager>& trackManager,
                     double tempo,
                     double playheadSeconds);

    static LoadResult load(const std::string& path,
                           const std::shared_ptr<Nomad::Audio::TrackManager>& trackManager);
};
