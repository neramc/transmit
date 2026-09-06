#!/usr/bin/env python3
"""The translation catalogues are valid, and their placeholders line up.

Two things go wrong with a .ts file and neither is visible by reading it.

A translation can carry different placeholders from its source. "%1 of %2"
translated as "%1 중" drops a value silently, and "%2 of %1" swaps two - which
in a sentence about how many files were copied out of how many is a report that
says the opposite of what happened. Qt will not warn: it substitutes what it is
given and leaves the rest.

And a catalogue can drift from the source. lupdate is what keeps them together,
and nothing runs it on its own, so a sentence reworded six months ago keeps its
old translation until somebody notices the English coming through.

    scripts/check-translations.py          # placeholders, validity, coverage
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOGUES = ROOT / "translations"

PLACEHOLDER = re.compile(r"%(\d+|n)")


def placeholders(text: str) -> list[str]:
    return sorted(PLACEHOLDER.findall(text or ""))


def main() -> int:
    problems: list[str] = []
    if not CATALOGUES.is_dir():
        print(f"no {CATALOGUES.relative_to(ROOT)} directory", file=sys.stderr)
        return 1

    files = sorted(CATALOGUES.glob("*.ts"))
    if not files:
        print("no catalogues to check, which is not the same as all of them being right",
              file=sys.stderr)
        return 1

    for path in files:
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError as failure:
            problems.append(f"{path.name}: is not valid XML ({failure})")
            continue

        total = 0
        done = 0
        for context in root.findall("context"):
            name = (context.findtext("name") or "?").strip()
            for message in context.findall("message"):
                source = message.findtext("source") or ""
                element = message.find("translation")
                translation = (element.text if element is not None else "") or ""
                unfinished = element is not None and element.get("type") == "unfinished"

                total += 1
                if translation and not unfinished:
                    done += 1
                else:
                    continue

                if placeholders(source) != placeholders(translation):
                    problems.append(
                        f"{path.name}: {name}: placeholders differ\n"
                        f"      source:      {source!r}\n"
                        f"      translation: {translation!r}")

        share = (done * 100.0 / total) if total else 0.0
        print(f"{path.name}: {done} of {total} translated ({share:.0f}%)")

    if problems:
        print(f"\n{len(problems)} problems:\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nEvery translation's placeholders match its source.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
