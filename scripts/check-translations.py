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
old translation until somebody notices the English coming through. That half
used to be checked only in CI, which is how eleven sentences came to be
untranslatable in every language without anything going red: lupdate cannot
parse a static_cast where a plural's count belongs, so it skipped those calls
in silence and the strings never reached the catalogue at all. It is checked
here now, so it fails on the machine the change was written on.

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


def sources_in(path: Path) -> set[tuple[str, str]]:
    """Every (context, source) the catalogue has an entry for."""
    found: set[tuple[str, str]] = set()
    root = ElementTree.parse(path).getroot()
    for context in root.findall("context"):
        name = (context.findtext("name") or "?").strip()
        for message in context.findall("message"):
            found.add((name, message.findtext("source") or ""))
    return found


def find_lupdate() -> str | None:
    """lupdate, wherever this machine keeps it."""
    from shutil import which

    import os

    candidates = [os.environ.get("LUPDATE"), which("lupdate"), which("lupdate-qt6")]
    for root in (os.environ.get("QT_ROOT_DIR"), "/opt/Qt/6.8.1/gcc_64", "/usr/lib/qt6"):
        if root:
            candidates.append(str(Path(root) / "bin" / "lupdate"))

    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def disagreements_with(path: Path, lupdate: str) -> list[str]:
    """Where this catalogue and the source no longer say the same thing.

    Asked by running lupdate into a copy and comparing which strings came out,
    rather than by diffing the file. Two versions of lupdate can write the same
    catalogue differently, and a check that fails on a tool's formatting is one
    people learn to ignore; what matters is whether a sentence somebody will
    read has a place to be translated into. Qt 6.4 and 6.8 were compared over
    this tree and offer exactly the same 669 strings, so the comparison does
    not depend on which Qt is installed.

    Both directions fail, and they mean different things. A string the source
    has and the catalogue lacks cannot be translated at all. A string the
    catalogue has and the source no longer offers is either a sentence that was
    reworded without the catalogue being brought along - so the old translation
    is still being shown for text nobody writes any more - or, and this is the
    one that cost eleven sentences, a call lupdate stopped being able to see.
    """
    import shutil
    import subprocess
    import tempfile

    with tempfile.TemporaryDirectory() as work:
        regenerated = Path(work) / path.name
        shutil.copyfile(path, regenerated)

        run = subprocess.run(
            [lupdate, "-locations", "none", "-no-obsolete", "-recursive",
             str(ROOT / "src"), "-ts", str(regenerated)],
            capture_output=True, text=True)
        if run.returncode != 0:
            return [f"{path.name}: lupdate would not run ({run.stderr.strip()[:200]})"]

        try:
            offered = sources_in(regenerated)
        except ElementTree.ParseError as failure:
            return [f"{path.name}: lupdate wrote something that is not valid XML ({failure})"]

    have = sources_in(path)
    absent = sorted(offered - have)
    stale = sorted(have - offered)
    if not absent and not stale:
        return []

    lines: list[str] = []
    if absent:
        lines.append(f"{path.name}: {len(absent)} string(s) in the source have no entry, so "
                     f"they cannot be translated in any language:")
        lines.extend(_listed(absent))
    if stale:
        lines.append(f"{path.name}: {len(stale)} entry/entries are for text the source no "
                     f"longer offers - either it was reworded, or lupdate can no longer see "
                     f"the call that produces it:")
        lines.extend(_listed(stale))

    lines.append("      Run: lupdate -locations none -no-obsolete -recursive src \\")
    lines.append(f"                   -ts translations/{path.name}")
    lines.append("      and commit the result. If a string is still missing after that, the "
                 "call is one lupdate cannot parse - a static_cast where a plural's count "
                 "goes is the one that has happened here.")
    return lines


def _listed(pairs: list[tuple[str, str]]) -> list[str]:
    shown = [f"      {context}: {source[:100]!r}" for context, source in pairs[:20]]
    if len(pairs) > 20:
        shown.append(f"      ... and {len(pairs) - 20} more")
    return shown


def main() -> int:
    problems: list[str] = []
    if not CATALOGUES.is_dir():
        print(f"no {CATALOGUES.relative_to(ROOT)} directory", file=sys.stderr)
        return 1

    lupdate = find_lupdate()
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

        if lupdate is None:
            print(f"{path.name}: no lupdate here, so whether it is behind the source was "
                  f"not checked - CI still checks it")
        else:
            drift = disagreements_with(path, lupdate)
            if drift:
                problems.append("\n  ".join(drift))

    if problems:
        print(f"\n{len(problems)} problems:\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nEvery translation's placeholders match its source.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
