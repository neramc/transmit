#!/usr/bin/env bash
#
# Runs the suite, and makes a failure readable.
#
# ctest buffers a test's output and loses it when the process dies rather than
# reporting, so a crash arrives as a name and a number. This runs the suite,
# and on failure runs the failures again verbosely and then asks QtTest to
# write its own report to a file - which on Windows is the only way the output
# has ever survived the trip.
#
#   scripts/run-tests.sh [BUILD_DIR]

set -uo pipefail

build="${1:-build}"

# Kept so the names of the failures can be repeated at the very end. A long
# suite's summary sits in the middle of thousands of lines, and the middle is
# exactly what any excerpt of a log drops.
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

ctest --test-dir "$build" --output-on-failure 2>&1 | tee "$log"
[ "${PIPESTATUS[0]}" -eq 0 ] && exit 0

echo "::group::Re-running the failures verbosely"
ctest --test-dir "$build" --rerun-failed --output-on-failure --verbose || true
echo "::endgroup::"

echo "::group::Reports written straight to a file"
for suite in UiSmoke RestoreUndo ContinuityRoundTrip PathRewrite SecretStore Update; do
    for binary in "$build/tests/ui/transmit_${suite}_test" \
                  "$build/tests/integration/transmit_${suite}_test" \
                  "$build/transmit_${suite}_test"; do
        for candidate in "$binary" "$binary.exe"; do
            [ -x "$candidate" ] || continue
            report="$suite-report.txt"
            QT_QPA_PLATFORM=offscreen TRANSMIT_NO_UPDATE_CHECK=1 \
                "$candidate" -o "$report,txt" >/dev/null 2>&1 || true
            if [ -s "$report" ]; then
                echo "----- $suite -----"
                cat "$report"
            fi
            break 2
        done
    done
done
echo "::endgroup::"

# Last, and outside every group, so it is the final thing anything reading
# this sees: which tests failed, by name. Everything above is the evidence;
# this is the answer.
echo
echo "The tests that failed:"
if ! sed -n '/The following tests FAILED:/,$p' "$log" | grep -v '^Errors while running CTest$'; then
    echo "  (ctest named none - the suite itself did not finish; look above)"
fi

exit 1
