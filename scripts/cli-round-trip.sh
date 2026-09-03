#!/usr/bin/env bash
#
# Captures a file, verifies the archive, restores it, and reads it back.
#
# The one check that exercises the whole program from outside: nothing here
# knows about any internal type, only about what the command line promises.
# Run on every platform rather than only where the translation is native,
# because the cross-operating-system part is the point of the project.
#
#   scripts/cli-round-trip.sh [BUILD_DIR]

set -euo pipefail

build="${1:-build}"
cli="$build/transmit-cli"
[ -x "$cli" ] || cli="$build/transmit-cli.exe"
if [ ! -x "$cli" ]; then
    echo "there is no transmit-cli in $build" >&2
    exit 1
fi

# Asked for rather than assumed. Pointing HOME at a sample directory works on
# Linux and macOS and does nothing at all on Windows, which resolves its known
# folders through the shell - so the file went one place and the capture looked
# in another.
documents=$("$cli" environment | awk -F'} ' '/^  \{DOCUMENTS/ {print $2}')
if [ -z "$documents" ]; then
    echo "transmit-cli could not say where documents are kept" >&2
    exit 1
fi
echo "documents folder: $documents"

mkdir -p "$documents"
echo "hello" > "$documents/transmit-ci-note.txt"
printf 'passphrase-for-ci\n' > pw.txt

# The dry run first, and it must leave nothing behind: `plan` builds exactly
# the request `export` builds, so if it writes anything at all it is writing it
# during what somebody was told was a preview.
"$cli" plan --out "$PWD/archive.txa" --profile documents --preset fast \
       --passphrase-file pw.txt
if [ -e "$PWD/archive.txa" ]; then
    echo "plan wrote an archive" >&2
    exit 1
fi

"$cli" export --out "$PWD/archive.txa" --profile documents --preset fast \
       --passphrase-file pw.txt
"$cli" verify "$PWD/archive.txa" --deep --passphrase-file pw.txt

# The checksum file next to it is meant to work with the ordinary tool, so that
# is what checks it.
if command -v md5sum >/dev/null 2>&1; then
    ( cd "$PWD" && md5sum -c archive.txa.md5 )
fi

# And the repair path on a sound archive, which must recover nothing and say so
# rather than inventing work.
"$cli" repair "$PWD/archive.txa" --passphrase-file pw.txt
"$cli" import "$PWD/archive.txa" --into "$PWD/restored" --passphrase-file pw.txt
diff "$documents/transmit-ci-note.txt" restored/DOCUMENTS/transmit-ci-note.txt

# The cross-platform translation is the point of the project, so it is
# exercised on every runner rather than only where it is native.
for target in windows macos linux; do
    "$cli" import "$PWD/archive.txa" --emulate-os "$target" --dry-run \
           --passphrase-file pw.txt
done

# The updater answers rather than hangs. There is no signed feed to reach from
# a build machine, so the answer is "could not check" and the exit code says
# so - what is being checked is that it says something and stops.
set +e
"$cli" update --json
status=$?
set -e
case "$status" in
    0|1|3) echo "update reported, exit $status" ;;
    *) echo "transmit-cli update exited $status, which is not one of its answers" >&2; exit 1 ;;
esac

echo "The round trip came back with what went in."
