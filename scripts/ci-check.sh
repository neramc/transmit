#!/usr/bin/env bash
#
# Runs one check, remembers what happened, and always succeeds.
#
# A run that stops at the first failure tells you about one problem and hides
# however many others are there, so the next run finds the second one, and the
# one after that finds the third. Every check here records its result and
# returns 0, and scripts/ci-report.py reads the whole pile at the end, prints
# the cause of everything that failed, and fails the job once.
#
#   scripts/ci-check.sh "Formatting" scripts/format.sh --check
#
# Results go to $TRANSMIT_CI_RESULTS, or ci-results/ beside the working
# directory. Deleting that directory starts a fresh run.

set -uo pipefail

if [ $# -lt 2 ]; then
    echo "usage: ci-check.sh NAME COMMAND [ARGUMENT...]" >&2
    exit 2
fi

name="$1"
shift

results="${TRANSMIT_CI_RESULTS:-ci-results}"
mkdir -p "$results"

# Numbered so the report reads in the order the checks ran, and slugged so two
# checks cannot land on one file.
slug=$(printf '%s' "$name" | tr -c 'A-Za-z0-9' '-' | tr -s '-' | sed 's/^-//; s/-$//')
index=$(printf '%03d' "$(find "$results" -maxdepth 1 -name '*.json' | wc -l)")
record="$results/$index-$slug.json"
log="$results/$index-$slug.log"

printf '::group::%s\n' "$name"
started=$(date +%s)

"$@" > >(tee "$log") 2> >(tee -a "$log" >&2)
status=$?

finished=$(date +%s)
printf '::endgroup::\n'

if [ "$status" -eq 0 ]; then
    printf '%s: passed (%ss)\n' "$name" "$((finished - started))"
else
    # Said here as well as in the report, so somebody reading the log as it
    # scrolls past sees it at the point it happened.
    printf '::warning::%s failed (exit %s) - the run carries on, see the report at the end\n' \
        "$name" "$status"
fi

# The record is JSON so the report does not have to parse prose. Written with
# python because quoting a log file into JSON with shell tools is how a report
# comes to be unreadable exactly when it is needed.
python=python3
command -v "$python" >/dev/null 2>&1 || python=python
"$python" - "$record" "$name" "$status" "$((finished - started))" "$log" "$*" <<'PY'
import json, sys

record, name, status, seconds, log, command = sys.argv[1:7]
try:
    with open(log, "r", encoding="utf-8", errors="replace") as handle:
        output = handle.read()
except OSError:
    output = ""

with open(record, "w", encoding="utf-8") as handle:
    json.dump({
        "name": name,
        "status": "passed" if status == "0" else "failed",
        "exit": int(status),
        "seconds": int(seconds),
        "command": command,
        "output": output,
    }, handle)
PY

exit 0
