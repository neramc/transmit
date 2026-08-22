#!/usr/bin/env bash
# Every .qml file has to be listed in its module's CMakeLists.
#
# A file that is not listed is not compiled into the module and not in the
# resources, so it simply does not exist at run time - and nothing says so.
# The failure shows up as a type that cannot be resolved, on whichever page
# happens to use it, which is a long way from the file somebody forgot to add.
set -euo pipefail

cd "$(dirname "$0")/.."

missing=0
for cmakelists in src/ui/qml/*/CMakeLists.txt src/ui/qml/CMakeLists.txt; do
    directory="$(dirname "$cmakelists")"
    for qml in "$directory"/*.qml; do
        [ -e "$qml" ] || continue
        name="$(basename "$qml")"
        if ! grep -q "\\b${name}\\b" "$cmakelists"; then
            echo "$qml is not listed in $cmakelists"
            missing=1
        fi
    done
done

# And the reverse: a name left behind in the list after the file is gone stops
# the build, but only once somebody configures from scratch.
for cmakelists in src/ui/qml/*/CMakeLists.txt src/ui/qml/CMakeLists.txt; do
    directory="$(dirname "$cmakelists")"
    for name in $(grep -oE '[A-Za-z0-9_]+\.qml' "$cmakelists" | sort -u); do
        if [ ! -e "$directory/$name" ]; then
            echo "$cmakelists lists $name, which does not exist"
            missing=1
        fi
    done
done

if [ "$missing" -ne 0 ]; then
    echo
    echo "Add the file to QML_FILES in its module, or remove the stale name."
    exit 1
fi

echo "Every QML file is registered in its module."
