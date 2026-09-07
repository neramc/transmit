#!/usr/bin/env bash
#
# Run inside a Fedora container by scripts/check-linux-packages.sh.
#
# Kept in a file of its own rather than passed as a string, because a shell
# script quoted through two shells and a container is a script nobody can read
# and everybody is afraid to change.
#
# Every step announces itself. The output of a container comes back through
# docker interleaved with its own progress reporting, and a failure in here
# used to arrive as one line - "the .rpm did not install and start on Fedora" -
# with nothing to say which half of that sentence was the problem.

set -euo pipefail

package="$1"

say() {
    # To stderr, unbuffered, so the order survives the trip out of the
    # container and past docker's own chatter.
    echo "[fedora] $*" >&2
}

say "installing $package"
install_log=$(mktemp)
if ! dnf install -y "$package" > "$install_log" 2>&1; then
    say "dnf could not install the package:"
    cat "$install_log" >&2
    exit 1
fi
say "installed"

for program in /usr/bin/transmit /usr/bin/transmit-cli; do
    if [ ! -x "$program" ]; then
        say "$program is not there after installing"
        exit 1
    fi
done

say "the command line tool says: $(/usr/bin/transmit-cli --version 2>&1)"

say "starting the interface"
log=$(mktemp)
status=0
QT_QPA_PLATFORM=offscreen \
TRANSMIT_STARTUP_BENCHMARK=1 \
TRANSMIT_NO_UPDATE_CHECK=1 \
QT_LOGGING_RULES=transmit.performance.debug=true \
QT_DEBUG_PLUGINS=1 \
    timeout 90 /usr/bin/transmit > "$log" 2>&1 || status=$?

say "the interface exited with $status and said $(wc -c < "$log") bytes:"
# The plugin trace is long and only interesting when something failed, so the
# tail is enough to say which library was not found.
tail -n 60 "$log" >&2

if ! grep -q "first frame after" "$log"; then
    say "it never painted a frame"
    exit 1
fi

say "the .rpm installed on Fedora and the program painted a frame"
