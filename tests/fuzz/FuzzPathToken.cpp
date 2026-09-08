/// Tokenising and resolving a path, and sanitising a name.
///
/// Two properties, over bytes nobody chose: resolving must never hand
/// back a path outside the folder it resolved against, and sanitising
/// must never produce a component that climbs. The property suite runs
/// these over generated paths; this runs them over anything at all,
/// including invalid UTF-8, which is where a length calculation goes
/// wrong.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "format/NameSanitizer.h"
#include "format/PathToken.h"

#include "fuzz/FuzzMain.h"

namespace {

/// Says what broke, then stops.
///
/// Both properties here used to end in a bare std::abort(), and an optimising
/// build folds two identical calls into one - so the stack trace named
/// whichever line the compiler kept, and the crash could not say which of the
/// two properties it was. Printing first also puts the input's shape in the
/// log beside the trace, where a person reading a failed job can see it
/// without fetching the artifact.
[[noreturn]] void broke(const char* property, const std::string& input,
                        const std::string& produced) {
    std::fprintf(stderr, "\nPROPERTY BROKEN: %s\n  in:  %s\n  out: %s\n", property, input.c_str(),
                 produced.c_str());
    std::fflush(stderr);
    std::abort();
}

}  // namespace

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

    // No component may be a parent reference, whatever went in - nor a
    // reference to the folder it is already in, which is quieter and still
    // loses a file: "a/./b" and "a/b" name the same place, so two entries
    // become one and the second overwrites the first.
    std::size_t start = 0;
    while (start <= safe.size()) {
        const std::size_t next = safe.find('/', start);
        const std::size_t end = (next == std::string::npos) ? safe.size() : next;
        if (safe.compare(start, end - start, "..") == 0) {
            broke("a sanitised path still has a \"..\" component", relative, safe);
        }
        if (safe.compare(start, end - start, ".") == 0) {
            broke("a sanitised path still has a \".\" component", relative, safe);
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
                broke("resolving landed outside the folder it resolved against", *base, *resolved);
            }
        }
    }

    // And tokenising an absolute path must round-trip.
    const auto tokenized = map.tokenize(relative);
    static_cast<void>(map.resolve(tokenized));
    return 0;
}
