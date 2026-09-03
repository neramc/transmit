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

# A QML string holding an escaped NUL breaks the build, and not here.
#
# Qt compiles QML bindings ahead of time into C++, and the generated source
# embeds string literals verbatim. An embedded NUL terminates the literal in
# the generated file rather than in the string, so what comes out is C++ that
# does not parse - "error: missing terminating \" character" in a file nobody
# wrote. Worse, the compiler that does this is version-dependent: Qt 6.4 here
# accepted the binding and Qt 6.8 on the runners did not, so it cost a full
# matrix and looked like a Qt bug rather than a line somebody typed.
#
# There is always another separator. This costs one grep to never spend that
# afternoon again.
literals=0
while IFS= read -r file; do
    if grep -nE '"[^"]*\\u0000' "$file"; then
        echo "  ^ $file has an escaped NUL in a string literal"
        literals=1
    fi
done < <(find src/ui/qml -name '*.qml')

if [ "$literals" -ne 0 ]; then
    echo
    echo "Qt's ahead-of-time QML compiler writes string literals straight into"
    echo "generated C++, where a NUL ends the literal and the generated file"
    echo "stops parsing. Use a separator that is not a control character."
    exit 1
fi

echo "No QML string literal holds a character that breaks the generated C++."
