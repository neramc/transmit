#pragma once

/// The parts of the headless front end that talk to an actual terminal:
/// asking for a passphrase without showing it, and turning Ctrl-C into a
/// clean stop rather than a killed process.

#include <QString>

#include <optional>

namespace transmit::core {
class CancelToken;
}

namespace transmit::cli {

/// Whether there is a person at the other end of stdin.
///
/// False for a pipe, a cron job or a CI step, which is the difference between
/// "ask them" and "fail with an explanation": prompting a script that nobody
/// is watching just hangs it.
[[nodiscard]] bool stdinIsATerminal();

/// Asks for a passphrase on the terminal with the echo turned off.
///
/// This is the only way to give Transmit a passphrase that leaves no trace:
/// `--passphrase` puts it in the process list where every other user on the
/// machine can read it, and `--passphrase-file` puts a copy of it on a disk
/// that is probably the one being captured.
///
/// Returns nothing when there is no terminal to ask, when the input ends, or
/// when `confirm` is set and the two attempts differ.
[[nodiscard]] std::optional<QString> askForPassphrase(const QString& prompt, bool confirm);

/// Routes Ctrl-C to a cancel token for as long as it is alive.
///
/// A capture killed mid-write leaves a half-written archive behind, and one
/// that looks exactly like a finished one until somebody carries it to
/// another machine and tries to restore from it. Cancelling instead lets the
/// run stop where it can clear up after itself. A second Ctrl-C gives up on
/// that and kills the process the usual way, for a run that will not stop.
class InterruptHandler {
public:
    explicit InterruptHandler(core::CancelToken& token);
    ~InterruptHandler();

    InterruptHandler(const InterruptHandler&) = delete;
    InterruptHandler& operator=(const InterruptHandler&) = delete;
    InterruptHandler(InterruptHandler&&) = delete;
    InterruptHandler& operator=(InterruptHandler&&) = delete;

    /// Whether the run was interrupted, so the caller can say so rather than
    /// reporting a plain failure.
    [[nodiscard]] static bool wasInterrupted() noexcept;
};

}  // namespace transmit::cli
