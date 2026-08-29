#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "format/Manifest.h"
#include "format/Result.h"

namespace transmit::format {

/// Writes the `.md5` file that sits beside an archive.
///
/// The point of it is that it works without Transmit. The part files are
/// listed in `md5sum` format, so somebody who has been handed a drive can run
///
///     md5sum -c machine-20260829-1130.txa.md5
///
/// in the folder and be told, by a tool that has been on every Unix machine
/// for thirty years, whether the drive holds what was written to it. That is a
/// different question from "is the archive internally consistent", which the
/// archive's own hashes answer, and it is the question somebody actually has
/// when a copy has just finished.
///
/// The files inside the archive are listed too, as comment lines - `md5sum -c`
/// skips those, so they cost nothing to a checker and give a person something
/// to grep when one file is in doubt.

struct SidecarOptions {
    /// List the files inside the archive as well as the parts.
    ///
    /// Off for an encrypted archive: the whole point of encrypting one is that
    /// the names of somebody's files are not readable from the drive, and a
    /// sidecar listing every path would hand them over in plain text beside
    /// it. Turning it on for an encrypted archive is a deliberate choice a
    /// person has to make, not a default.
    bool includeEntries = true;

    /// Goes into the header comment, so the file says which archive it is for.
    std::string archiveName;
};

/// Hashes each part and writes the sidecar. Returns the path written.
Result<std::filesystem::path> writeChecksumSidecar(const std::filesystem::path& sidecarPath,
                                                   const std::vector<std::filesystem::path>& parts,
                                                   const Manifest& manifest,
                                                   const SidecarOptions& options = {});

/// One line of a sidecar that has been read back.
struct SidecarPart {
    std::string fileName;
    Digest128 md5{};
};

/// Reads the part lines back, for `transmit-cli verify`. Comment lines are
/// ignored, which is the same thing `md5sum -c` does with them.
Result<std::vector<SidecarPart>> readChecksumSidecar(const std::filesystem::path& sidecarPath);

/// MD5 of a whole file, read in pieces so a 4 GB part does not have to fit in
/// memory to be checked.
Result<Digest128> md5OfFile(const std::filesystem::path& path);

}  // namespace transmit::format
