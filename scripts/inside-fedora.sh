#!/usr/bin/env bash
#
# Run inside a Fedora container by scripts/check-linux-packages.sh.
#
# Kept in a file of its own rather than passed as a string, because a shell
# script quoted through two shells and a container is a script nobody can read
# and everybody is afraid to change.

set -euo pipefail

package="$1"

dnf install -y "$package" > /dev/null

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
