#!/usr/bin/env bash
#
# Starts the interface once and insists it paints a frame.
#
# TRANSMIT_STARTUP_BENCHMARK makes it quit as soon as the window has drawn, so
# finishing is the pass and a timeout is the failure - the opposite way round
# from waiting to see whether it dies. That is the difference between "it did
# not crash in thirty seconds" and "it opened a window".
#
#   scripts/start-once.sh [BUILD_DIR] [SECONDS]

set -uo pipefail

build="${1:-build}"
patience="${2:-90}"

# macOS builds an application bundle; the other two put the executable at the
# top of the build directory.
for candidate in "$build/transmit.app/Contents/MacOS/transmit" \
                 "$build/transmit" "$build/transmit.exe"; do
    if [ -x "$candidate" ]; then
        interface="$candidate"
        break
    fi
done
if [ -z "${interface:-}" ]; then
    echo "there is no interface to start in $build" >&2
    exit 1
fi
echo "starting $interface"

export TRANSMIT_STARTUP_BENCHMARK=1
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-transmit.performance.debug=true}"
# Qt on Windows builds the interface for the windowed subsystem, and its
# default message handler writes to the debugger rather than to stderr when the
# process has no console. Started from a script it has none.
export QT_LOGGING_TO_CONSOLE=1
export TRANSMIT_NO_UPDATE_CHECK=1

log=started.log
"$interface" > "$log" 2>&1 &
pid=$!

waited=0
while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt "$patience" ]; do
    sleep 1
    waited=$((waited + 1))
done

if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
    cat "$log"
    echo "the interface was still running after ${patience}s without painting a frame" >&2
    exit 1
fi

wait "$pid"
status=$?
cat "$log"

if [ "$status" -ne 0 ]; then
    echo "the interface exited with $status" >&2
    exit 1
fi
if ! grep -q "first frame after" "$log"; then
    echo "the interface finished without drawing anything" >&2
    exit 1
fi
if grep -qE "is not a type|failed to load|not installed" "$log"; then
    echo "the interface started with QML errors" >&2
    exit 1
fi

echo "The interface opened a window and drew into it."
