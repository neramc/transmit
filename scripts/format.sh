#!/bin/sh
# Formats every source file, or checks them, using the same file list and the
# same formatter version continuous integration uses. Running this before a
# push is the whole point: a formatting failure in CI wastes a round trip on
# something a script can settle.
#
#   scripts/format.sh          format in place
#   scripts/format.sh --check  report violations and fail, changing nothing

set -eu

cd "$(dirname "$0")/.."

# Pinned so the result does not depend on which version happens to be installed.
FORMATTER="${CLANG_FORMAT:-}"
if [ -z "$FORMATTER" ]; then
    for candidate in clang-format-18 clang-format; do
        if command -v "$candidate" >/dev/null 2>&1; then
            FORMATTER="$candidate"
            break
        fi
    done
fi

if [ -z "$FORMATTER" ]; then
    echo "No clang-format found. Install clang-format-18." >&2
    exit 1
fi

# The .mm files are Objective-C++ and are easy to forget; they are in the list
# CI checks, so they are in this one.
files=$(git ls-files '*.cpp' '*.h' '*.mm')

if [ "${1:-}" = "--check" ]; then
    # shellcheck disable=SC2086
    $FORMATTER --dry-run --Werror $files
    echo "Formatting is clean ($FORMATTER)."
else
    # shellcheck disable=SC2086
    $FORMATTER -i $files
    echo "Formatted $(echo "$files" | wc -l) files with $FORMATTER."
fi
