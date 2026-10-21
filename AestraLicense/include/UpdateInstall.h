// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Getting an update onto the disk, safely, before anything is replaced:
//
//   download   resumable, to a ".part" file beside the final name; accepted only when its size AND SHA-256 match the signed
//              feed (the feed's signature covers both, so a package that matches them is the one we published)
//   extract    into a staging folder: no symlinks, nothing outside the folder, the main program present
//
// Nothing here touches the installed copy. The swap is the updater program's job (tools/updater).

#include "HttpTransport.h"
#include "UpdateClient.h"

#include <string>

namespace Aestra {
namespace License {

enum class DownloadStatus {
    Ok,
    Cancelled,     ///< stopped by the user; the part file stays so the next try resumes
    Unauthorized,  ///< the host refused the founder key
    NetworkError,  ///< could not finish; the part file stays so the next try resumes
    SizeMismatch,  ///< finished, but not the size the feed promised: deleted
    HashMismatch,  ///< finished, but not the file the feed promised: deleted
    DiskError,     ///< no room, or cannot write
    BadConfig,     ///< not https / not the update host / no founder key / an unsafe file name: nothing was requested
};

struct DownloadOutcome {
    DownloadStatus status = DownloadStatus::NetworkError;
    std::string path;    ///< the finished package when status == Ok
    std::string detail;  ///< short, for the log and the page
};

/// Lower-case hex SHA-256 of a file; empty (and @p error set) if it cannot be read.
std::string sha256FileHex(const std::string& path, std::string* error = nullptr);

/// The package's file name: the last part of its URL, only if it is a plain safe name; empty otherwise.
std::string updatePackageFileName(const UpdateManifest& manifest);

/// Download (or finish downloading) the package into @p directory and verify it.
DownloadOutcome downloadUpdatePackage(IHttpTransport& transport, const UpdateConfig& config, const UpdateManifest& manifest,
                                      const std::string& directory, const DownloadProgress& progress = nullptr);

/// Unpack a .zip or .tar.gz into @p stagingDir (replaced if it exists) and check it. A single wrapping folder is
/// stripped. @p executableName must be there afterwards. False, with @p error set, leaves nothing half-staged.
bool extractUpdatePackage(const std::string& packagePath, const std::string& stagingDir, const std::string& executableName,
                          std::string* error);

} // namespace License
} // namespace Aestra
