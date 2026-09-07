#pragma once

#include <cstdint>
#include <string_view>

namespace transmit::format {

/// Windows file attribute bits, by their real names.
///
/// The manifest has carried a `WindowsMetadata::attributes` word since the
/// format was written and nothing ever put anything in it, so every capture
/// made on Windows quietly lost whether a file was hidden, read-only or
/// system. Naming the bits here is what lets the capture record them, the
/// restore put back the ones that mean something, and both sides agree about
/// which ones those are.
///
/// The values are FILE_ATTRIBUTE_* from the Windows headers. They are written
/// out rather than included so this file compiles anywhere - the decisions
/// below are made on Linux and macOS too, about an archive that came from
/// Windows.
namespace file_attribute {

inline constexpr std::uint32_t kReadOnly = 0x00000001;
inline constexpr std::uint32_t kHidden = 0x00000002;
inline constexpr std::uint32_t kSystem = 0x00000004;
inline constexpr std::uint32_t kDirectory = 0x00000010;
inline constexpr std::uint32_t kArchive = 0x00000020;
inline constexpr std::uint32_t kTemporary = 0x00000100;
inline constexpr std::uint32_t kSparseFile = 0x00000200;
inline constexpr std::uint32_t kReparsePoint = 0x00000400;
inline constexpr std::uint32_t kCompressed = 0x00000800;

/// The file's contents are not on this disk. Set by anything that moves data
/// off to slower storage; for a home directory that means a cloud client.
inline constexpr std::uint32_t kOffline = 0x00001000;

inline constexpr std::uint32_t kNotContentIndexed = 0x00002000;
inline constexpr std::uint32_t kEncrypted = 0x00004000;
inline constexpr std::uint32_t kIntegrityStream = 0x00008000;
inline constexpr std::uint32_t kVirtual = 0x00010000;
inline constexpr std::uint32_t kNoScrubData = 0x00020000;

/// Opening the file fetches it. OneDrive's "Files On-Demand" sets this on
/// everything it has not downloaded yet.
inline constexpr std::uint32_t kRecallOnOpen = 0x00040000;

/// Reading the file's bytes fetches it, though opening it does not - which is
/// why a scan can see the name and the size without paying for the contents.
inline constexpr std::uint32_t kRecallOnDataAccess = 0x00400000;

}  // namespace file_attribute

/// Whether the file's bytes live somewhere else and reading them would fetch
/// them over the network.
///
/// This is the difference between a capture that takes twenty minutes and one
/// that quietly downloads a three-hundred-gigabyte OneDrive onto a laptop over
/// a hotel connection. The names and sizes are still known - Windows keeps
/// those locally - so such a file can be listed without being read.
[[nodiscard]] constexpr bool storedInTheCloud(std::uint32_t attributes) noexcept {
    return (attributes & (file_attribute::kOffline | file_attribute::kRecallOnOpen |
                          file_attribute::kRecallOnDataAccess)) != 0;
}

/// Whether this name is the stub macOS leaves behind for a file iCloud Drive
/// has evicted: `report.pdf` becomes `.report.pdf.icloud`, a few hundred bytes
/// of metadata standing in for the real thing.
///
/// macOS has no attribute bit for it, so the name is the only evidence without
/// asking Foundation - and the answer has to be available to a plain scan.
[[nodiscard]] bool isEvictedICloudFile(std::string_view name) noexcept;

/// The name the stub stands for, or an empty view when it is not a stub. What
/// the report should say is missing is `report.pdf`, not `.report.pdf.icloud`.
[[nodiscard]] std::string_view nameBehindICloudStub(std::string_view name) noexcept;

/// The attributes worth putting back on a restored file.
///
/// Two kinds of bit share this word. Some say what the file is for - hidden,
/// read-only, system - and belong to the person who set them, so they travel.
/// The rest describe how this particular disk happens to be storing it:
/// whether NTFS compressed it, whether it is a reparse point, whether the
/// contents are still in the cloud. Putting those back on the far side would
/// be a lie about the new machine, and setting kOffline on a real local file
/// is one Windows itself acts on.
[[nodiscard]] constexpr std::uint32_t attributesWorthCarrying(std::uint32_t attributes) noexcept {
    constexpr std::uint32_t carried = file_attribute::kReadOnly | file_attribute::kHidden |
                                      file_attribute::kSystem | file_attribute::kArchive |
                                      file_attribute::kNotContentIndexed;
    return attributes & carried;
}

}  // namespace transmit::format
