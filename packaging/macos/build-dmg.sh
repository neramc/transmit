#!/bin/sh
# Builds a disk image from an already-built bundle.
#
# Signing and notarisation are deliberately not done here: they need
# credentials, and a script that quietly expects them would fail confusingly on
# anyone else's machine. Sign the bundle before running this if you need to.

set -eu

BUILD_DIR="${1:-build}"
BUNDLE="$BUILD_DIR/transmit.app"
STAGING="$BUILD_DIR/dmg-staging"
OUTPUT="$BUILD_DIR/Transmit.dmg"

if [ ! -d "$BUNDLE" ]; then
    echo "No bundle at $BUNDLE. Build with the release preset first." >&2
    exit 1
fi

# The command line tool travels inside the bundle, where it shares the Qt
# frameworks that are about to be copied in. Somebody restoring onto a machine
# with no display still needs a way to run this.
CLI="$BUILD_DIR/transmit-cli"
if [ -x "$CLI" ]; then
    cp "$CLI" "$BUNDLE/Contents/MacOS/"
fi

# macdeployqt copies in the Qt frameworks and QML modules the bundle needs.
# The second executable has to be named, or its library paths are left
# pointing at a Qt that is only on the machine that built it.
if command -v macdeployqt >/dev/null 2>&1; then
    if [ -x "$BUNDLE/Contents/MacOS/transmit-cli" ]; then
        macdeployqt "$BUNDLE" -qmldir="$(dirname "$0")/../../src/ui/qml" \
            -executable="$BUNDLE/Contents/MacOS/transmit-cli"
    else
        macdeployqt "$BUNDLE" -qmldir="$(dirname "$0")/../../src/ui/qml"
    fi
else
    echo "macdeployqt not on PATH; the bundle will only run where Qt is installed." >&2
fi

rm -rf "$STAGING" "$OUTPUT"
mkdir -p "$STAGING"
cp -R "$BUNDLE" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

hdiutil create -volname "Transmit" -srcfolder "$STAGING" -ov -format UDZO "$OUTPUT"
rm -rf "$STAGING"

echo "Wrote $OUTPUT"
