#!/usr/bin/env bash
# Line coverage, and the two questions worth asking about it.
#
# The first is whether the project as a whole is still tested about as well as
# it was, which a single floor answers. The second, and the one that actually
# keeps a codebase honest, is whether the lines being *added* are tested -
# because a large well-tested project can absorb a great deal of untested new
# code before its overall number moves at all, and by then the untested code is
# everywhere.
#
#   scripts/coverage.sh                 # build, run, report, enforce the floor
#   scripts/coverage.sh --base main     # also require the changed lines be covered
#   scripts/coverage.sh --report-only   # no floors, just the numbers
set -euo pipefail

cd "$(dirname "$0")/.."

# Measured at 74.0% when this was written; the floor is two points under, so
# ordinary movement does not fire it and a real drop does.
FLOOR=${TRANSMIT_COVERAGE_FLOOR:-72.0}

# New lines are held to a higher standard than old ones, deliberately. Nobody
# can retrofit tests for fourteen thousand lines in an afternoon; everybody can
# test the thirty they just wrote.
CHANGED_FLOOR=${TRANSMIT_CHANGED_COVERAGE_FLOOR:-90.0}

BASE=""
REPORT_ONLY=0
SKIP_BUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --base) BASE="$2"; shift 2;;
        --report-only) REPORT_ONLY=1; shift;;
        --no-build) SKIP_BUILD=1; shift;;
        *) echo "unknown option: $1" >&2; exit 2;;
    esac
done

PROFDATA=$(command -v llvm-profdata-18 || command -v llvm-profdata)
COV=$(command -v llvm-cov-18 || command -v llvm-cov)
BUILD=build-coverage

if [ "$SKIP_BUILD" -eq 0 ]; then
    cmake --preset coverage -DTRANSMIT_WERROR=ON >/dev/null
    cmake --build --preset coverage
fi

rm -rf "$BUILD/profiles"
mkdir -p "$BUILD/profiles"

# One profile per test process. ctest runs each case as its own process, so a
# single fixed path would have them overwrite each other and the report would
# be of whichever finished last.
LLVM_PROFILE_FILE="$PWD/$BUILD/profiles/%p.profraw" QT_QPA_PLATFORM=offscreen \
    ctest --preset coverage

"$PROFDATA" merge -sparse "$BUILD"/profiles/*.profraw -o "$BUILD/all.profdata"

objects=()
while IFS= read -r binary; do
    objects+=(-object "$binary")
done < <(find "$BUILD/tests" "$BUILD/src" -type f -executable -name 'transmit_*test*' 2>/dev/null)

if [ "${#objects[@]}" -eq 0 ]; then
    echo "no test binaries found under $BUILD" >&2
    exit 1
fi

# Tests are excluded from their own coverage, as are everything Qt generates
# and anything outside this project.
IGNORE='(tests/|build-|/usr/|_autogen|moc_|qrc_)'

"$COV" export "${objects[@]}" -instr-profile="$BUILD/all.profdata" \
    -format=text -ignore-filename-regex="$IGNORE" > "$BUILD/coverage.json"

python3 scripts/coverage_report.py \
    --coverage "$BUILD/coverage.json" \
    --floor "$FLOOR" \
    --changed-floor "$CHANGED_FLOOR" \
    ${BASE:+--base "$BASE"} \
    $([ "$REPORT_ONLY" -eq 1 ] && echo --report-only)
