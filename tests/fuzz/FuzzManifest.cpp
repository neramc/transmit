/// The manifest is the most attacker-controlled thing Transmit parses.
///
/// It arrives inside a block, so on an unencrypted archive anyone can
/// rewrite it, and `readEntry` copies out of it into fixed-size arrays.
/// Whatever it holds, deserialising must either succeed or fail - never
/// read out of bounds, and never allocate an amount somebody else chose.

#include "format/Manifest.h"
#include "fuzz/FuzzMain.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace transmit::format;

    const ByteView bytes(reinterpret_cast<const Byte*>(data), size);

    auto parsed = Manifest::deserialize(bytes);
    if (!parsed) {
        return 0;
    }
    const Manifest& manifest = *parsed;

    // A manifest that parsed must be self-consistent enough to walk
    // without the caller checking anything first.
    for (const ManifestEntry& entry : manifest.entries) {
        static_cast<void>(entry.hasContent());
        static_cast<void>(entry.path.toDisplayString());
        static_cast<void>(manifest.findBlock(entry.location.blockId));
    }
    for (const BlockRecord& block : manifest.blocks) {
        static_cast<void>(block.blockId);
    }
    static_cast<void>(manifest.entryCountFor(DomainId::UserData));

    // And it must survive a round trip through its own writer.
    const ByteBuffer written = manifest.serialize();
    static_cast<void>(Manifest::deserialize(written));
    return 0;
}
