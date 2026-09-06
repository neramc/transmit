#!/usr/bin/env python3
"""Every string a person reads goes through a translation.

A literal that never reaches qsTr() or QCoreApplication::translate() is a
sentence that cannot be translated, and nothing about it looks wrong until
somebody runs the program in their own language and finds one English line in
the middle of a page. There is no way to notice that by reading the code, so it
is counted here instead.

    scripts/check-translatable.py            # every untranslated string
    scripts/check-translatable.py --list     # one per line, for fixing

What counts as a string a person reads: a literal assigned to a property that
puts text on the screen. Identifiers, colours, icon names, urls, and the
strings that are compared rather than shown are not.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Properties whose value is read by a person. Deliberately a list rather than
# "anything that looks like a sentence": the second kind of rule reports the
# name of an icon and stays quiet about a two-word label.
SHOWN = (
    "text", "title", "description", "placeholderText", "label", "heading",
    "subtitle", "message", "hint", "toolTip", "tooltip", "caption",
    "summary", "note", "detail", "action", "emptyText", "errorText",
    "confirmText", "cancelText", "acceptText", "rejectText", "buttonText",
)

ASSIGNMENT = re.compile(
    r"^\s*(?:readonly\s+)?(?:property\s+string\s+)?(" + "|".join(SHOWN) + r")\s*:\s*(.+)$"
)

# A literal in the value, not preceded by qsTr( on the same line.
LITERAL = re.compile(r"""(?<![\w.])(["'])((?:\\.|(?!\1)[^\\])*)\1""")

# Values that are a string but not a sentence.
NOT_FOR_READING = re.compile(
    r"""^\s*(?:                     # the whole value is one of these
        ["'][^"']*["']\s*$          # handled below by the content rules
    )""",
    re.VERBOSE,
)

# Content that is a name rather than a phrase: an icon, a colour, a url, a
# format string with nothing to read, a single punctuation mark.
NOT_A_PHRASE = re.compile(
    r"""^(?:
        |\s*                        # empty or whitespace
        |[#][0-9A-Fa-f]{3,8}        # a colour
        |[a-z][a-z0-9-]*            # one lowercase word: an icon or an id
        |[a-z]+://.*                # a url
        |[^A-Za-z]*                 # no letters at all: symbols, digits
        |qrc:.*|:/.*                # a resource path
        |[A-Za-z]:[\\/].*           # a path
    )$""",
    re.VERBOSE,
)


def tracked(pattern: str) -> list[Path]:
    listing = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", pattern],
                             capture_output=True, check=True)
    return [ROOT / name.decode("utf-8") for name in listing.stdout.split(b"\0") if name]


def without_translated(text: str) -> str:
    """The file with every qsTr(...) blanked out, offsets preserved.

    Blanked rather than removed, and by whole calls rather than by line,
    because a call is often spread over four lines - a sentence built by
    concatenation. Matching "qsTr" and a closing bracket on one line reports
    every one of those as untranslated, which is how this script's first
    version found twenty strings that were already translated.
    """
    out = list(text)

    # Comments first: a commented-out line is not a string anybody reads, and
    # a bracket inside one would throw off the counting below.
    for match in re.finditer(r"//[^\n]*", text):
        for index in range(match.start(), match.end()):
            out[index] = " "
    for match in re.finditer(r"/\*.*?\*/", text, re.DOTALL):
        for index in range(match.start(), match.end()):
            if out[index] != "\n":
                out[index] = " "

    blanked = "".join(out)
    for call in re.finditer(r"\bqsTr(?:anslate|Id)?\s*\(", blanked):
        depth = 0
        index = call.end() - 1
        while index < len(blanked):
            character = blanked[index]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        for position in range(call.start(), min(index + 1, len(blanked))):
            if out[position] != "\n":
                out[position] = " "

    return "".join(out)


def untranslated_in(path: Path) -> list[tuple[int, str, str]]:
    found: list[tuple[int, str, str]] = []
    text = without_translated(path.read_text(encoding="utf-8"))
    for number, line in enumerate(text.splitlines(), start=1):
        match = ASSIGNMENT.match(line)
        if not match:
            continue
        prop, value = match.group(1), match.group(2)

        for literal in LITERAL.finditer(value):
            content = literal.group(2)
            # An escape is not a letter. "\u2713" is a tick and "\n" is a line
            # break, and both would otherwise read as words because of the
            # letters in the escape itself.
            plain = re.sub(r"\\u[0-9A-Fa-f]{4}|\\.", "", content)
            if NOT_A_PHRASE.match(plain):
                continue
            found.append((number, prop, content))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true",
                        help="print one finding per line and nothing else")
    arguments = parser.parse_args()

    findings: list[str] = []
    for path in tracked("*.qml"):
        for number, prop, content in untranslated_in(path):
            findings.append(f"{path.relative_to(ROOT)}:{number}: {prop}: \"{content}\"")

    if arguments.list:
        for finding in findings:
            print(finding)
        return 0

    if findings:
        print(f"{len(findings)} strings a person reads that cannot be translated:\n",
              file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        print("\nWrap each in qsTr().", file=sys.stderr)
        return 1

    print("Every string a person reads goes through a translation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
