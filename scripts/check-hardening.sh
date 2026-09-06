#!/usr/bin/env bash
#
# The mitigations the binaries are supposed to carry, read back off them.
#
# cmake/Hardening.cmake asks the compiler for these. Whether the compiler did
# it, whether the linker kept it, and whether a later change quietly turned one
# off are three different questions, and only the finished file answers them.
#
#   scripts/check-hardening.sh BUILD_DIR [more...]
#
# Every binary is checked and every missing property is reported before this
# exits, so one run says everything rather than the first thing.

set -uo pipefail

problems=0

check() {
    local binary="$1" name="$2" ok="$3"
    if [ "$ok" = "yes" ]; then
        printf '  %-22s %s\n' "$name" "yes"
    else
        printf '  %-22s %s\n' "$name" "MISSING"
        echo "FAILED: $binary was built without $name" >&2
        problems=1
    fi
}

# Searched with a here-string rather than through a pipe.
#
# `something | grep -q` is wrong in a script with `pipefail`: grep stops at the
# first match, whatever is upstream dies of SIGPIPE, and the pipeline reports
# 141 - so a property that is present reads as missing. This script said the
# binaries had no stack guard and no fortified calls while `readelf -sW` listed
# __stack_chk_fail, __memcpy_chk and __snprintf_chk in them.
has() { grep -qE -- "$2" <<<"$1"; }

examine() {
    local binary="$1"
    echo "$binary"

    local header segments dynamic symbols
    header=$(readelf -hW "$binary" 2>/dev/null)
    segments=$(readelf -lW "$binary" 2>/dev/null)
    dynamic=$(readelf -dW "$binary" 2>/dev/null)
    symbols=$(readelf -sW "$binary" 2>/dev/null)

    # Position independent: the loader is free to place it anywhere, which is
    # what makes address space layout randomisation worth anything.
    check "$binary" "position independent" \
        "$(has "$header" 'DYN \(' && echo yes || echo no)"

    # Full RELRO: relocations resolved at load and the table made read-only.
    check "$binary" "read-only relocations" \
        "$(has "$segments" 'GNU_RELRO' && echo yes || echo no)"
    check "$binary" "bound at load" \
        "$(has "$dynamic" 'BIND_NOW|FLAGS.*NOW' && echo yes || echo no)"

    # A stack the processor will refuse to execute.
    local stack
    stack=$(printf '%s' "$segments" | grep GNU_STACK || true)
    check "$binary" "non-executable stack" \
        "$(has "$stack" 'RWE' && echo no || echo yes)"

    # Stack cookies, and the fortified string and memory calls that check a
    # size the compiler can work out.
    check "$binary" "stack guard" \
        "$(has "$symbols" '__stack_chk_fail' && echo yes || echo no)"
    check "$binary" "fortified calls" \
        "$(has "$symbols" '__[a-z]+_chk' && echo yes || echo no)"
}

if [ "$#" -eq 0 ]; then
    echo "usage: check-hardening.sh BUILD_DIR [more...]" >&2
    exit 2
fi

if ! command -v readelf >/dev/null 2>&1; then
    echo "readelf is not installed, so nothing was checked" >&2
    exit 2
fi

found=0
for directory in "$@"; do
    for name in transmit transmit-cli; do
        binary="$directory/$name"
        [ -f "$binary" ] || continue
        # Skip anything that is not an ELF: a macOS bundle's executable, a
        # wrapper script, a stray file with the right name.
        head -c 4 "$binary" | grep -q ELF || continue
        found=$((found + 1))
        examine "$binary"
    done
done

if [ "$found" -eq 0 ]; then
    echo "FAILED: no binaries to check in: $*" >&2
    echo "  A check that finds nothing is a check that cannot fail." >&2
    exit 1
fi

echo
if [ "$problems" -ne 0 ]; then
    echo "$found binaries checked, and the build is not hardened as it says it is." >&2
    exit 1
fi
echo "$found binaries checked; every mitigation is present."
