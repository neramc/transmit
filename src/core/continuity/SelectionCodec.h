#pragma once

#include <QByteArray>
#include <QString>

#include "core/continuity/ContinuityTypes.h"

namespace transmit::core {

/// Everything a capture needs, written down.
///
/// A selection is a lot of choices - which folders, which applications, which
/// of their state roots, what size range, what file types, how the archive is
/// packed - and any of them changes what ends up on the drive. Somebody who
/// gets it right once should not have to get it right again next month, and
/// somebody comparing two runs needs to be able to see which of the choices
/// differed. So it is a document.
///
/// The interface writes one and `transmit-cli export --selection-file` repeats
/// it exactly, which is also what makes a capture reproducible in a test.
struct CaptureDocument {
    CaptureSelection selection;
    PackagingOptions packaging;
    QString label;
};

/// Reads and writes that document as JSON.
///
/// Deliberately not the archive's own binary format. This is a file people
/// edit by hand, put in version control, and send to somebody else to say
/// "use these settings" - all of which want text.
class SelectionCodec {
public:
    /// The shape this writer produces. A reader accepts this and anything
    /// older; a newer document is refused with a message rather than being
    /// read half-correctly.
    static constexpr int kVersion = 1;

    [[nodiscard]] static QByteArray encode(const CaptureDocument& document);

    /// Returns false and fills `errorMessage` when the document cannot be
    /// read. A field that is simply absent takes its default, so a short file
    /// naming only what somebody cares about is valid.
    [[nodiscard]] static bool decode(const QByteArray& json, CaptureDocument& document,
                                     QString* errorMessage = nullptr);
};

}  // namespace transmit::core
