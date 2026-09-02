#!/usr/bin/env bash
#
# Starts the real interface, on a real windowing system, and insists it draws.
#
# Every other interface test in this repository runs under the offscreen
# platform, in the same process as the test. That is fast and it catches QML
# mistakes, but it never asks the one question a person asks first: does the
# program open a window on my machine? It does not load the platform plugin
# their session uses, it does not build a graphics context, and it does not
# involve the scene graph at all. Version 0.1.0 shipped an AppImage that
# aborted the moment it was started on a Wayland desktop, and every check the
# project had was green.
#
# So this one launches the binary the way a person does, waits for it to paint
# a frame, and fails on anything it says along the way that a working launch
# would not say.
#
# Usage: launch-test.sh --binary PATH --mode MODE [options]
#
#   --mode offscreen        no windowing system at all
#   --mode xcb              a real X server (Xvfb)
#   --mode wayland          a real Wayland compositor (weston, headless)
#
#   --without-buffer-integration
#       Runs Wayland with the client buffer integration taken away, which is
#       what a bundle missing plugins/wayland-graphics-integration-client looks
#       like from the inside. The interface has to fall back to drawing in
#       software rather than dying.
#
#   --expect-software / --expect-accelerated
#       Insists on which of the two happened, so a fallback cannot quietly
#       become the normal path.
#
# A mode whose tools are not installed exits 77, which CTest is told to read
# as "skipped" rather than "passed". Set TRANSMIT_LAUNCH_TESTS_REQUIRED=1 to
# turn that into a failure instead: on a machine that is meant to have the
# tools - continuous integration - a silent skip is how a check stops checking.

set -uo pipefail

binary=""
mode="offscreen"
without_buffer_integration=0
expectation=""
timeout_seconds=90

while [ $# -gt 0 ]; do
    case "$1" in
        --binary) binary="$2"; shift 2 ;;
        --mode) mode="$2"; shift 2 ;;
        --without-buffer-integration) without_buffer_integration=1; shift ;;
        --expect-software) expectation="software"; shift ;;
        --expect-accelerated) expectation="accelerated"; shift ;;
        --timeout) timeout_seconds="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$binary" ] || [ ! -x "$binary" ]; then
    echo "no interface to launch: '$binary'" >&2
    exit 2
fi

skip() {
    if [ "${TRANSMIT_LAUNCH_TESTS_REQUIRED:-0}" != "0" ]; then
        echo "FAILED: $* - and this machine was told it must run them" >&2
        exit 1
    fi
    echo "SKIPPED: $*"
    exit 77
}

work="$(mktemp -d)"
compositor_pid=""
cleanup() {
    if [ -n "$compositor_pid" ]; then
        kill "$compositor_pid" 2>/dev/null
        wait "$compositor_pid" 2>/dev/null
    fi
    rm -rf "$work"
}
trap cleanup EXIT

log="$work/launch.log"

# A home of its own. The interface reads settings and writes a log on start,
# and a test that leaves marks in the real home is a test nobody runs twice.
export HOME="$work/home"
mkdir -p "$HOME"
export XDG_CONFIG_HOME="$HOME/.config" XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache" XDG_STATE_HOME="$HOME/.local/state"

# Quits as soon as the window has painted, which is the whole point: an
# interface that starts and then never draws is not a working launch, and
# waiting for a timeout cannot tell the two apart.
export TRANSMIT_STARTUP_BENCHMARK=1
export QT_LOGGING_RULES="transmit.performance.debug=true"

run_it() {
    timeout --signal=KILL "$timeout_seconds" "$binary" > "$log" 2>&1
    echo $?
}

case "$mode" in
    offscreen)
        export QT_QPA_PLATFORM=offscreen
        status="$(run_it)"
        ;;

    xcb)
        command -v xvfb-run >/dev/null 2>&1 || skip "Xvfb is not installed"
        export QT_QPA_PLATFORM=xcb
        status="$(xvfb-run -a -s '-screen 0 1440x900x24' \
                    timeout --signal=KILL "$timeout_seconds" "$binary" \
                    > "$log" 2>&1; echo $?)"
        ;;

    wayland)
        command -v weston >/dev/null 2>&1 || skip "weston is not installed"

        export XDG_RUNTIME_DIR="$work/runtime"
        mkdir -p "$XDG_RUNTIME_DIR"
        chmod 700 "$XDG_RUNTIME_DIR"
        socket="transmit-launch-$$"

        # weston named its backends "headless-backend.so" until version 11 and
        # "headless" after, and both spellings are still in use on the systems
        # this runs on. Rather than parse a version, ask for one and then the
        # other: a wrong name fails immediately and costs nothing.
        start_compositor() {
            weston --backend="$1" --socket="$socket" \
                   --width=1440 --height=900 >> "$work/weston.log" 2>&1 &
            compositor_pid=$!

            # The socket appears a moment after the process does, and
            # connecting before it exists fails in a way that looks like the
            # interface's fault.
            for _ in $(seq 1 100); do
                [ -S "$XDG_RUNTIME_DIR/$socket" ] && return 0
                kill -0 "$compositor_pid" 2>/dev/null || break
                sleep 0.1
            done

            kill "$compositor_pid" 2>/dev/null
            wait "$compositor_pid" 2>/dev/null
            compositor_pid=""
            return 1
        }

        if ! start_compositor headless && ! start_compositor headless-backend.so; then
            echo "the compositor never came up:" >&2
            cat "$work/weston.log" >&2
            skip "weston could not start on this machine"
        fi

        export WAYLAND_DISPLAY="$socket"
        export QT_QPA_PLATFORM=wayland
        if [ "$without_buffer_integration" -eq 1 ]; then
            # No integration by this name exists, so Qt finds none at all -
            # exactly the state of a bundle that forgot to carry the plugins.
            export QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=deliberately-absent
        fi
        status="$(run_it)"
        ;;

    *)
        echo "unknown mode: $mode" >&2
        exit 2
        ;;
esac

echo "----- $mode -----"
cat "$log"
echo "-----------------"

fail() { echo "FAILED: $*" >&2; exit 1; }

[ "$status" -eq 0 ] || fail "the interface exited with $status instead of finishing its first frame"

grep -q "first frame after" "$log" || fail "the interface never painted a frame"

# Anything Qt says about a broken window, a missing type or a binding that
# does not resolve. The list is of things a healthy launch never prints, so
# adding to it is how a new class of silent breakage becomes a failure.
while IFS= read -r pattern; do
    [ -z "$pattern" ] && continue
    if grep -qF "$pattern" "$log"; then
        fail "the interface printed \"$pattern\""
    fi
done <<'PATTERNS'
is not a type
Failed to initialize graphics backend
Failed to create RHI
ASSERT:
QQmlApplicationEngine failed to load component
Cannot assign to non-existent property
Unable to assign
Type error
File not found
module "
PATTERNS

# The one the crash of 0.1.0 would have been caught by. Deliberately induced
# in the --without-buffer-integration run, so it is only an error elsewhere.
if [ "$without_buffer_integration" -eq 0 ]; then
    if grep -q "Failed to load client buffer integration" "$log"; then
        fail "the Wayland client buffer integration is missing from this build"
    fi
    if grep -q 'Available client buffer integrations: QList()' "$log"; then
        fail "this build carries no Wayland client buffer integration at all"
    fi
fi

drew_in_software=0
grep -q "drawing in software instead" "$log" && drew_in_software=1

case "$expectation" in
    software)
        [ "$drew_in_software" -eq 1 ] || fail "expected the software fallback and it did not happen"
        ;;
    accelerated)
        [ "$drew_in_software" -eq 0 ] || fail "fell back to software drawing on a machine that has OpenGL"
        ;;
esac

echo "PASSED: $mode launch painted a frame"
