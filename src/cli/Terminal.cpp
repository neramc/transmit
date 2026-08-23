#include "cli/Terminal.h"

#include <QTextStream>

#include <atomic>
#include <csignal>
#include <string>

#include "core/services/ScanService.h"
#include "format/Bytes.h"

#if defined(_WIN32)
#include <windows.h>

#include <io.h>
#include <stdio.h>
#else
#include <termios.h>
#include <unistd.h>

#include <iostream>
#endif

namespace transmit::cli {
namespace {

/// The token the handler cancels, or null when nothing is running under one.
///
/// A signal handler may touch almost nothing safely; a lock-free atomic
/// pointer and a lock-free atomic flag are among the few things it may.
std::atomic<core::CancelToken*> g_cancelToken{nullptr};
std::atomic_bool g_interrupted{false};

extern "C" void handleInterrupt(int signalNumber) {
    core::CancelToken* token = g_cancelToken.load(std::memory_order_relaxed);
    if (token == nullptr || g_interrupted.exchange(true, std::memory_order_relaxed)) {
        // Nothing to stop, or asked twice. The second Ctrl-C means the person
        // is done waiting for a tidy stop, so give them the usual one.
        std::signal(signalNumber, SIG_DFL);
        std::raise(signalNumber);
        return;
    }
    token->cancel();
}

QTextStream& prompts() {
    // Prompts go to stderr so that `transmit-cli inspect ... > report.txt`
    // still shows the question rather than writing it into the file.
    static QTextStream stream(stderr);
    return stream;
}

/// Reads one line of typed input with the echo off, restoring the terminal
/// afterwards however the read turned out.
std::optional<QString> readLineWithoutEcho() {
#if defined(_WIN32)
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input == INVALID_HANDLE_VALUE || GetConsoleMode(input, &mode) == 0) {
        return std::nullopt;
    }
    SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));

    std::wstring line;
    wchar_t buffer[256];
    DWORD read = 0;
    bool ended = false;
    while (!ended && ReadConsoleW(input, buffer, static_cast<DWORD>(std::size(buffer)), &read,
                                  nullptr) != 0) {
        for (DWORD i = 0; i < read; ++i) {
            if (buffer[i] == L'\r' || buffer[i] == L'\n') {
                ended = true;
                break;
            }
            line.push_back(buffer[i]);
        }
        if (read == 0) {
            break;
        }
    }
    format::secureZero(buffer, sizeof(buffer));

    SetConsoleMode(input, mode);

    QString result = QString::fromWCharArray(line.data(), static_cast<qsizetype>(line.size()));
    format::secureZero(line.data(), line.size() * sizeof(wchar_t));
    return result;
#else
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return std::nullopt;
    }

    termios quiet = original;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) != 0) {
        return std::nullopt;
    }

    std::string line;
    const bool ok = static_cast<bool>(std::getline(std::cin, line));
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);

    if (!ok) {
        format::secureZero(line.data(), line.size());
        return std::nullopt;
    }

    QString result = QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
    format::secureZero(line.data(), line.size());
    return result;
#endif
}

}  // namespace

bool stdinIsATerminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

std::optional<QString> askForPassphrase(const QString& prompt, const bool confirm) {
    if (!stdinIsATerminal()) {
        return std::nullopt;
    }

    prompts() << prompt << QStringLiteral(": ");
    prompts().flush();
    std::optional<QString> first = readLineWithoutEcho();
    // The newline the person typed was swallowed with the echo, so the next
    // thing printed would otherwise land on the prompt line.
    prompts() << Qt::endl;

    if (!first) {
        return std::nullopt;
    }
    if (!confirm) {
        return first;
    }

    prompts() << QStringLiteral("Type it again: ");
    prompts().flush();
    std::optional<QString> second = readLineWithoutEcho();
    prompts() << Qt::endl;

    if (!second || *first != *second) {
        prompts() << QStringLiteral("The two did not match.") << Qt::endl;
        return std::nullopt;
    }
    return first;
}

InterruptHandler::InterruptHandler(core::CancelToken& token) {
    g_interrupted.store(false, std::memory_order_relaxed);
    g_cancelToken.store(&token, std::memory_order_relaxed);
    std::signal(SIGINT, handleInterrupt);
    std::signal(SIGTERM, handleInterrupt);
}

InterruptHandler::~InterruptHandler() {
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g_cancelToken.store(nullptr, std::memory_order_relaxed);
}

bool InterruptHandler::wasInterrupted() noexcept {
    return g_interrupted.load(std::memory_order_relaxed);
}

}  // namespace transmit::cli
