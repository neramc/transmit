#!/usr/bin/env bash
#
# Run inside a Fedora container by scripts/check-linux-packages.sh.
#
# Kept in a file of its own rather than passed as a string, because a shell
# script quoted through two shells and a container is a script nobody can read
# and everybody is afraid to change.

set -euo pipefail

package="$1"

# Quiet when it works, and not when it does not. Sending this to /dev/null
# outright meant a failure here printed nothing at all: the run said "the .rpm
# did not install and start on Fedora" and left whoever read it to guess which
# of the two it was.
install_log=$(mktemp)
if ! dnf install -y "$package" > "$install_log" 2>&1; then
    echo "dnf could not install the package:"
    cat "$install_log"
    exit 1
fi

test -x /usr/bin/transmit
test -x /usr/bin/transmit-cli
/usr/bin/transmit-cli --version

log=$(mktemp)
QT_QPA_PLATFORM=offscreen \
TRANSMIT_STARTUP_BENCHMARK=1 \
TRANSMIT_NO_UPDATE_CHECK=1 \
QT_LOGGING_RULES=transmit.performance.debug=true \
    timeout 90 /usr/bin/transmit > "$log" 2>&1 || true
cat "$log"
grep -q "first frame after" "$log"

echo "the .rpm installed on Fedora and the program painted a frame"
