#!/bin/sh
# Regenerates the icons the build needs from resources/icons/transmit-512.png.
#
# That file is the master: 512x536, where the artwork is a 512 square and the
# extra 24 pixels are the shadow it casts. Everything derived here is padded to
# 536x536 first, because icon themes, window managers and .icns all want a
# square and cropping would cut the shadow off the drawing.
#
# The sizes are checked in, so nobody needs ImageMagick to build Transmit. Run
# this when the artwork changes, and commit what it writes.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
icons="$root/resources/icons"
master="$icons/transmit-512.png"

if [ ! -f "$master" ]; then
    echo "No master icon at $master" >&2
    exit 1
fi

if ! command -v convert >/dev/null 2>&1; then
    echo "ImageMagick's 'convert' is not on PATH." >&2
    exit 1
fi

square() {
    convert "$master" -background none -gravity center -extent 536x536 \
        -filter Lanczos -resize "$1x$1" -strip "$2"
}

# The sizes the hicolor theme installs and the window icon carries.
for size in 16 32 48 64 128 256; do
    square "$size" "$icons/transmit-$size.png"
    echo "wrote transmit-$size.png"
done

# macOS wants one file holding every size. Its container is trivial - a header
# and eight bytes per member - but nothing here writes one, so it is assembled
# by hand from PNGs the sizes above already prove render correctly.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for size in 16 32 64 128 256 512; do
    square "$size" "$work/$size.png"
done

python3 - "$work" "$icons/transmit.icns" <<'PYTHON'
import struct
import sys

work, output = sys.argv[1], sys.argv[2]

# Each OSType names both a pixel size and what Finder should use it for; the
# @2x entries are the same pixels under a different name, which is how a
# retina display picks the sharper one.
members = [
    (b"icp4", 16),   # 16x16
    (b"icp5", 32),   # 32x32
    (b"ic11", 32),   # 16x16@2x
    (b"ic12", 64),   # 32x32@2x
    (b"ic07", 128),  # 128x128
    (b"ic13", 256),  # 128x128@2x
    (b"ic08", 256),  # 256x256
    (b"ic14", 512),  # 256x256@2x
    (b"ic09", 512),  # 512x512
]

body = b""
for ostype, size in members:
    with open(f"{work}/{size}.png", "rb") as image:
        payload = image.read()
    body += ostype + struct.pack(">I", len(payload) + 8) + payload

with open(output, "wb") as icns:
    icns.write(b"icns" + struct.pack(">I", len(body) + 8) + body)

print(f"wrote {output.rsplit('/', 1)[-1]} with {len(members)} sizes")
PYTHON
