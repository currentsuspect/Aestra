// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class TakeManager {
public:
    struct TakeEntry {
        std::string id;
        std::string name;
        std::string parentId;
        std::string snapshotPath;
        uint64_t createdAtMs{0};
        uint64_t updatedAtMs{0};
        bool active{false};
    };

    struct Manifest {
        bool ok{false};
        std::string errorMessage;
        std::string projectPath;
        std::string activeTakeId;
        std::vector<TakeEntry> takes;

        const TakeEntry* findTake(const std::string& id) const;
        const TakeEntry* activeTake() const;
    };

    struct Result {
        bool ok{false};
        std::string errorMessage;
        TakeEntry take;
        Manifest manifest;
    };

    static std::string getTakesDirectory(const std::string& projectPath);
    static std::string getManifestPath(const std::string& projectPath);
    static std::string resolveSnapshotPath(const std::string& projectPath, const TakeEntry& take);

    static Manifest loadManifest(const std::string& projectPath);

    static Result ensureManifest(const std::string& projectPath, const std::string& currentProjectContents,
                                 const std::string& initialTakeName = "Main");

    static Result saveActiveTake(const std::string& projectPath, const std::string& currentProjectContents);

    static Result createTake(const std::string& projectPath, const std::string& currentProjectContents,
                             const std::string& name, const std::string& parentId = "");

    static Result setActiveTake(const std::string& projectPath, const std::string& takeId);
};
