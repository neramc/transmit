/// The tagged binary reader underneath every structure in the format.
///
/// Its contract is narrow and absolute: whatever the bytes say, a read
/// either returns a value or fails, and `skip` never advances past the
/// end. Everything above it - manifest, payloads, the journal later -
/// depends on that being true for input nobody generated.

#include <cstdlib>

#include "format/Serialization.h"

#include "fuzz/FuzzMain.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace transmit::format;

    ByteReader reader(ByteView(reinterpret_cast<const Byte*>(data), size));

    // Walk it as a record stream until it refuses, which is what every
    // real reader does.
    std::size_t previous = reader.offset();
    for (int guard = 0; guard < 4096 && !reader.atEnd(); ++guard) {
        const auto tag = reader.getTag();
        if (!tag) {
            break;
        }
        if (!reader.skip(tag->type)) {
            break;
        }

        // A reader that stops making progress would spin forever on a
        // crafted input; the guard above bounds it, but standing still
        // is itself the bug.
        if (reader.offset() <= previous) {
            std::abort();
        }
        previous = reader.offset();
    }
    return 0;
}
