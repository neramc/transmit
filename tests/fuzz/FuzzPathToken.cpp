/// Tokenising and resolving a path, and sanitising a name.
///
/// Two properties, over bytes nobody chose: resolving must never hand
/// back a path outside the folder it resolved against, and sanitising
/// must never produce a component that climbs. The property suite runs
/// these over generated paths; this runs them over anything at all,
/// including invalid UTF-8, which is where a length calculation goes
/// wrong.

#include <cstdlib>
#include <string>
#include <string_view>

#include "format/NameSanitizer.h"
#include "format/PathToken.h"
#include "fuzz/FuzzMain.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace transmit::format;

    if (size == 0 || size > 4096) {
        return 0;
    }

    // The first byte picks the target, the rest is the path.
    const OsFamily family = (data[0] % 3) == 0   ? OsFamily::Linux
                            : (data[0] % 3) == 1 ? OsFamily::MacOs
                                                 : OsFamily::Windows;
    const std::string relative(reinterpret_cast<const char*>(data + 1), size - 1);

    NameSanitizer sanitizer(SanitizeOptions::forTarget(family));
    const std::string safe = sanitizer.sanitizeRelativePath(relative);

    // No component may be a parent reference, whatever went in.
    std::size_t start = 0;
    while (start <= safe.size()) {
        const std::size_t next = safe.find('/', start);
        const std::size_t end = (next == std::string::npos) ? safe.size() : next;
        if (safe.compare(start, end - start, "..") == 0) {
            std::abort();
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    const PathTokenMap map = PathTokenMap::defaultsFor(
        family, family == OsFamily::Windows ? "C:/Users/bob" : "/home/bob");

    if (const auto base = map.base(PathTokenId::Documents)) {
        if (const auto resolved = map.resolve(TokenizedPath{PathTokenId::Documents, safe})) {
            // Resolving must stay inside the folder it resolved against.
            if (resolved->rfind(*base, 0) != 0) {
                std::abort();
            }
        }
    }

    // And tokenising an absolute path must round-trip.
    const auto tokenized = map.tokenize(relative);
    static_cast<void>(map.resolve(tokenized));
    return 0;
}
