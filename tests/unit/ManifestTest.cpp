#include <gtest/gtest.h>

#include "format/Manifest.h"

namespace transmit::format {
namespace {

ManifestEntry sampleEntry() {
    ManifestEntry entry;
    entry.id = 42;
    entry.domain = DomainId::AppState;
    entry.type = EntryType::File;
    entry.path = TokenizedPath{PathTokenId::AppConfig, "Mozilla/Firefox/profiles.ini"};
    entry.size = 1234;
    entry.modifiedUnixNs = 1710000000123456789LL;
    entry.createdUnixNs = 1700000000000000000LL;
    entry.posix = PosixMetadata{0644, 1000, 1000, "bob", "bob"};
    entry.windows = WindowsMetadata{0x20};
    entry.contentHash.fill(static_cast<Byte>(0xAB));
    entry.location = BlockLocation{7, 4096, 1234};
    entry.appId = "org.mozilla.firefox";
    entry.captureNote = "read from a VSS snapshot";
    return entry;
}

Manifest sampleManifest() {
    Manifest manifest;
    manifest.archiveId = "8d1f0f21-0000-4000-8000-abcdefabcdef";
    manifest.label = "bob's laptop";
    manifest.preset = CompressionPreset::Maximum;
    manifest.encrypted = true;

    manifest.source.os = OsFamily::Windows;
    manifest.source.osName = "Windows 11 Pro";
    manifest.source.osVersion = "10.0.22631";
    manifest.source.hostName = "BOB-PC";
    manifest.source.userName = "bob";
    manifest.source.homeDirectory = "C:/Users/Bob";
    manifest.source.appVersion = "0.1.0";
    manifest.source.capturedUnix = 1710000000;
    manifest.source.tokenBases[PathTokenId::Home] = "C:/Users/Bob";
    manifest.source.tokenBases[PathTokenId::AppConfig] = "C:/Users/Bob/AppData/Roaming";

    manifest.entries.push_back(sampleEntry());

    ManifestEntry directory;
    directory.id = 43;
    directory.domain = DomainId::UserData;
    directory.type = EntryType::Directory;
    directory.path = TokenizedPath{PathTokenId::Documents, "reports"};
    manifest.entries.push_back(directory);

    ManifestEntry link;
    link.id = 44;
    link.domain = DomainId::UserData;
    link.type = EntryType::Symlink;
    link.path = TokenizedPath{PathTokenId::Home, "shortcut"};
    link.symlinkTarget = "/home/bob/Documents";
    manifest.entries.push_back(link);

    manifest.blocks.push_back(BlockRecord{7, 4096, 65536, 12000, CodecId::Zstd, true});
    manifest.payloads.push_back(
        DomainPayload{DomainId::SystemSettings, "settings.v1", ByteBuffer{Byte{1}, Byte{2}}});
    manifest.totalRawBytes = 65536;
    manifest.totalStoredBytes = 12100;
    manifest.deduplicatedBytes = 512;
    return manifest;
}

TEST(Manifest, RoundTripsEveryField) {
    const Manifest original = sampleManifest();
    const ByteBuffer encoded = original.serialize();

    const auto decoded = Manifest::deserialize(encoded);
    ASSERT_TRUE(decoded) << decoded.error().toString();

    EXPECT_EQ(decoded->archiveId, original.archiveId);
    EXPECT_EQ(decoded->label, original.label);
    EXPECT_EQ(decoded->preset, original.preset);
    EXPECT_TRUE(decoded->encrypted);
    EXPECT_EQ(decoded->totalRawBytes, original.totalRawBytes);
    EXPECT_EQ(decoded->deduplicatedBytes, original.deduplicatedBytes);

    EXPECT_EQ(decoded->source.os, OsFamily::Windows);
    EXPECT_EQ(decoded->source.osName, "Windows 11 Pro");
    EXPECT_EQ(decoded->source.hostName, "BOB-PC");
    EXPECT_EQ(decoded->source.capturedUnix, 1710000000);
    EXPECT_EQ(decoded->source.tokenBases.at(PathTokenId::AppConfig),
              "C:/Users/Bob/AppData/Roaming");

    ASSERT_EQ(decoded->entries.size(), 3u);
    const ManifestEntry& file = decoded->entries[0];
    EXPECT_EQ(file.id, 42u);
    EXPECT_EQ(file.domain, DomainId::AppState);
    EXPECT_EQ(file.path, (TokenizedPath{PathTokenId::AppConfig, "Mozilla/Firefox/profiles.ini"}));
    EXPECT_EQ(file.size, 1234u);
    EXPECT_EQ(file.modifiedUnixNs, 1710000000123456789LL);
    EXPECT_EQ(file.posix.mode, 0644u);
    EXPECT_EQ(file.posix.userName, "bob");
    EXPECT_EQ(file.windows.attributes, 0x20u);
    EXPECT_EQ(file.contentHash, sampleEntry().contentHash);
    EXPECT_EQ(file.location.blockId, 7u);
    EXPECT_EQ(file.location.offset, 4096u);
    EXPECT_EQ(file.appId, "org.mozilla.firefox");
    EXPECT_EQ(file.captureNote, "read from a VSS snapshot");

    EXPECT_EQ(decoded->entries[1].type, EntryType::Directory);
    EXPECT_EQ(decoded->entries[2].symlinkTarget, "/home/bob/Documents");

    ASSERT_EQ(decoded->blocks.size(), 1u);
    EXPECT_EQ(decoded->blocks[0].streamOffset, 4096u);
    EXPECT_EQ(decoded->blocks[0].codec, CodecId::Zstd);
    EXPECT_TRUE(decoded->blocks[0].encrypted);

    ASSERT_EQ(decoded->payloads.size(), 1u);
    EXPECT_EQ(decoded->payloads[0].kind, "settings.v1");
    EXPECT_EQ(decoded->payloads[0].domain, DomainId::SystemSettings);
    EXPECT_EQ(decoded->payloads[0].data.size(), 2u);
}

TEST(Manifest, ReportsPerDomainTotals) {
    Manifest manifest = sampleManifest();
    EXPECT_EQ(manifest.entryCountFor(DomainId::AppState), 1u);
    EXPECT_EQ(manifest.entryCountFor(DomainId::UserData), 2u);
    EXPECT_EQ(manifest.entryCountFor(DomainId::Secrets), 0u);
    EXPECT_EQ(manifest.rawBytesFor(DomainId::AppState), 1234u);
}

TEST(Manifest, FindsBlocksAndPayloads) {
    const Manifest manifest = sampleManifest();
    ASSERT_NE(manifest.findBlock(7), nullptr);
    EXPECT_EQ(manifest.findBlock(999), nullptr);
    ASSERT_NE(manifest.findPayload(DomainId::SystemSettings, "settings.v1"), nullptr);
    EXPECT_EQ(manifest.findPayload(DomainId::SystemSettings, "settings.v2"), nullptr);
}

TEST(Manifest, RefusesAFutureFormatVersion) {
    Manifest manifest;
    manifest.version = Manifest::kCurrentVersion + 1;
    const auto decoded = Manifest::deserialize(manifest.serialize());
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::UnsupportedVersion);
}

TEST(Manifest, HandlesALargeEntryCount) {
    Manifest manifest;
    manifest.archiveId = "bulk";
    for (std::uint64_t i = 0; i < 20000; ++i) {
        ManifestEntry entry;
        entry.id = i;
        entry.path = TokenizedPath{PathTokenId::Documents, "file" + std::to_string(i) + ".txt"};
        entry.size = i;
        entry.location = BlockLocation{1, i * 10, i};
        manifest.entries.push_back(entry);
    }

    const auto decoded = Manifest::deserialize(manifest.serialize());
    ASSERT_TRUE(decoded) << decoded.error().toString();
    ASSERT_EQ(decoded->entries.size(), 20000u);
    EXPECT_EQ(decoded->entries[19999].path.relative, "file19999.txt");
}

TEST(DomainNames, RoundTrip) {
    for (const DomainId domain : allDomains()) {
        const auto parsed = domainFromName(domainName(domain));
        ASSERT_TRUE(parsed) << parsed.error().toString();
        EXPECT_EQ(*parsed, domain);
    }
    EXPECT_FALSE(domainFromName("everything"));
}

}  // namespace
}  // namespace transmit::format
