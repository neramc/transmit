// What a file's attributes say about whether it is really here.
//
// Two systems hide the same thing in two different places and both do it
// silently: OneDrive leaves a name, a size and a modification time on disk with
// no contents behind them, and iCloud Drive replaces the file with a stub named
// after it. Reading either one downloads it. A capture that gets this wrong
// does not fail - it quietly pulls a few hundred gigabytes over whatever
// connection the machine happens to be on, which is worse than failing.

#include <gtest/gtest.h>

#include "format/FileTraits.h"

namespace transmit::format {
namespace {

TEST(StoredInTheCloud, AnOrdinaryFileIsHere) {
    EXPECT_FALSE(storedInTheCloud(0));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kArchive));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kHidden | file_attribute::kReadOnly |
                                  file_attribute::kSystem));
}

TEST(StoredInTheCloud, EachOfTheThreeBitsIsEnoughOnItsOwn) {
    EXPECT_TRUE(storedInTheCloud(file_attribute::kOffline));
    EXPECT_TRUE(storedInTheCloud(file_attribute::kRecallOnOpen));
    EXPECT_TRUE(storedInTheCloud(file_attribute::kRecallOnDataAccess));

    // And in the company they actually keep: OneDrive sets the recall bit
    // beside the ordinary ones rather than instead of them.
    EXPECT_TRUE(storedInTheCloud(file_attribute::kArchive | file_attribute::kReparsePoint |
                                 file_attribute::kRecallOnDataAccess));
}

TEST(StoredInTheCloud, TheNeighbouringBitsDoNotCount) {
    // Nearest neighbours in the word, and none of them means the contents are
    // elsewhere. A mask written one bit wide of the mark would pass every test
    // above and skip every compressed or sparse file on the disk.
    EXPECT_FALSE(storedInTheCloud(file_attribute::kCompressed));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kSparseFile));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kNotContentIndexed));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kEncrypted));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kVirtual));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kNoScrubData));
    EXPECT_FALSE(storedInTheCloud(file_attribute::kIntegrityStream));
}

TEST(EvictedICloudFile, TheStubIsRecognisedAndTheRealNameComesBack) {
    EXPECT_TRUE(isEvictedICloudFile(".report.pdf.icloud"));
    EXPECT_EQ(nameBehindICloudStub(".report.pdf.icloud"), "report.pdf");

    EXPECT_TRUE(isEvictedICloudFile(".a.icloud"));
    EXPECT_EQ(nameBehindICloudStub(".a.icloud"), "a");

    // A name with no extension of its own, and one with several.
    EXPECT_EQ(nameBehindICloudStub(".Budget.icloud"), "Budget");
    EXPECT_EQ(nameBehindICloudStub(".archive.tar.gz.icloud"), "archive.tar.gz");
}

TEST(EvictedICloudFile, AFileThatMerelyLooksLikeOneIsAFile) {
    // Both ends have to be right. Getting either wrong means refusing to carry
    // somebody's real file and telling them it is in the cloud.
    EXPECT_FALSE(isEvictedICloudFile("report.pdf.icloud"));  // not hidden
    EXPECT_FALSE(isEvictedICloudFile(".report.pdf"));        // not the suffix
    EXPECT_FALSE(isEvictedICloudFile("notes.icloud"));
    EXPECT_FALSE(isEvictedICloudFile(".icloud"));  // nothing behind it
    EXPECT_FALSE(isEvictedICloudFile("."));
    EXPECT_FALSE(isEvictedICloudFile(""));
    EXPECT_FALSE(isEvictedICloudFile(".icloud.icloud.txt"));

    EXPECT_TRUE(nameBehindICloudStub("report.pdf.icloud").empty());
    EXPECT_TRUE(nameBehindICloudStub(".icloud").empty());
}

TEST(EvictedICloudFile, TheStubOfAStubIsStillReadOnce) {
    // ".icloud.icloud" is a hidden file called ".icloud" that has itself been
    // evicted. Absurd, and the answer still has to be the one that round-trips.
    EXPECT_TRUE(isEvictedICloudFile(".icloud.icloud"));
    EXPECT_EQ(nameBehindICloudStub(".icloud.icloud"), "icloud");
}

TEST(AttributesWorthCarrying, WhatThePersonSetTravels) {
    EXPECT_EQ(attributesWorthCarrying(file_attribute::kHidden), file_attribute::kHidden);
    EXPECT_EQ(attributesWorthCarrying(file_attribute::kReadOnly), file_attribute::kReadOnly);
    EXPECT_EQ(attributesWorthCarrying(file_attribute::kSystem), file_attribute::kSystem);
    EXPECT_EQ(attributesWorthCarrying(file_attribute::kHidden | file_attribute::kReadOnly),
              file_attribute::kHidden | file_attribute::kReadOnly);
}

TEST(AttributesWorthCarrying, WhatTheOldDiskWasDoingStaysBehind) {
    // Every one of these describes storage rather than intent, and putting it
    // back on the far side would be a claim about the new machine that is not
    // true. kOffline is the one that matters most: set on a real local file,
    // Windows itself acts on it.
    for (const std::uint32_t bit :
         {file_attribute::kDirectory, file_attribute::kSparseFile, file_attribute::kReparsePoint,
          file_attribute::kCompressed, file_attribute::kOffline, file_attribute::kEncrypted,
          file_attribute::kVirtual, file_attribute::kTemporary, file_attribute::kRecallOnOpen,
          file_attribute::kRecallOnDataAccess, file_attribute::kIntegrityStream,
          file_attribute::kNoScrubData}) {
        EXPECT_EQ(attributesWorthCarrying(bit), 0u) << "bit " << bit << " should not travel";
    }
}

TEST(AttributesWorthCarrying, AMixedWordKeepsOnlyTheHalfThatTravels) {
    const std::uint32_t asStored = file_attribute::kHidden | file_attribute::kReadOnly |
                                   file_attribute::kCompressed | file_attribute::kOffline |
                                   file_attribute::kRecallOnDataAccess;
    EXPECT_EQ(attributesWorthCarrying(asStored),
              file_attribute::kHidden | file_attribute::kReadOnly);
}

TEST(ExtendedAttributes, WhatSomebodyPutThereTravels) {
    EXPECT_TRUE(extendedAttributeTravels("user.xdg.tags"));
    EXPECT_TRUE(extendedAttributeTravels("user.xdg.comment"));
    EXPECT_TRUE(extendedAttributeTravels("user.dublincore.title"));
    EXPECT_TRUE(extendedAttributeTravels("user.anything.at.all"));

    // Finder tags and the Spotlight comment: the same idea, where macOS keeps
    // it.
    EXPECT_TRUE(extendedAttributeTravels("com.apple.metadata:_kMDItemUserTags"));
    EXPECT_TRUE(extendedAttributeTravels("com.apple.metadata:kMDItemFinderComment"));
}

TEST(ExtendedAttributes, NothingThatGrantsPrivilegeTravels) {
    // The one that matters: security.capability is honoured by the kernel, so
    // an archive able to set it would be a way to hand a binary powers by
    // restoring it. The other two namespaces need privilege to write and
    // describe the filesystem rather than the file.
    EXPECT_FALSE(extendedAttributeTravels("security.capability"));
    EXPECT_FALSE(extendedAttributeTravels("security.selinux"));
    EXPECT_FALSE(extendedAttributeTravels("security.ima"));
    EXPECT_FALSE(extendedAttributeTravels("system.posix_acl_access"));
    EXPECT_FALSE(extendedAttributeTravels("system.nfs4_acl"));
    EXPECT_FALSE(extendedAttributeTravels("trusted.overlay.opaque"));
}

TEST(ExtendedAttributes, TheMarkOfTheInternetIsLeftBehind) {
    // Carrying this forward would make every restored document arrive with a
    // warning it did not have before - and the archive it arrived in is not
    // the download the mark is about.
    EXPECT_FALSE(extendedAttributeTravels("com.apple.quarantine"));
    EXPECT_FALSE(extendedAttributeTravels("com.apple.provenance"));
    EXPECT_FALSE(extendedAttributeTravels("com.apple.macl"));

    // Not a tag: a file's worth of data behind the same interface.
    EXPECT_FALSE(extendedAttributeTravels("com.apple.ResourceFork"));
    EXPECT_FALSE(extendedAttributeTravels("com.apple.FinderInfo"));
}

TEST(ExtendedAttributes, NothingElseIsCarriedByAccident) {
    // An allow-list, so a namespace nobody here has thought about stays where
    // it is rather than travelling because it looked harmless.
    EXPECT_FALSE(extendedAttributeTravels("os2.type"));
    EXPECT_FALSE(extendedAttributeTravels("btrfs.compression"));
    EXPECT_FALSE(extendedAttributeTravels("com.apple.lastuseddate#PS"));
    EXPECT_FALSE(extendedAttributeTravels("userfoo.bar"));
    EXPECT_FALSE(extendedAttributeTravels("user"));
    EXPECT_FALSE(extendedAttributeTravels(""));

    // A refusal wins over an allowance, whichever order a future name matches
    // in.
    EXPECT_FALSE(extendedAttributeTravels("security.user.tags"));
}

}  // namespace
}  // namespace transmit::format
