#!/usr/bin/env python3
"""The one place that knows where a version number is written down.

A release used to fail when the tag and CMakeLists.txt disagreed, which is
honest but not helpful: it tells somebody at the end of a release that they
should have edited eight files at the beginning of it. The version now travels
one way - the tag says what is being built, this rewrites everything to match,
and the build carries on.

Between releases the direction is reversed: CMakeLists.txt is what everything
else has to agree with, and --check says so in continuous integration, because
a packaging file left behind is only discovered by whoever builds the package.

Every place is named explicitly. Nothing here searches for a version-shaped
string and replaces it, because a version-shaped string also appears in a
comment about the release it went wrong in, and rewriting history in a comment
is how a note about 0.1.0 silently becomes a note about 0.3.2.

    scripts/version.py --print          what CMakeLists.txt declares
    scripts/version.py --check          every file agrees, or say where not
    scripts/version.py --set 0.2.0      rewrite every file
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# A version is three numbers. Anything else - a date, a git description, a
# "v" that wandered in from a tag - is rejected rather than written into eight
# files and discovered by a package build a week later.
VERSION = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


@dataclass(frozen=True)
class Place:
    """One line in one file that carries the version."""

    path: str
    #: Two groups: everything before the version, and the version itself.
    pattern: str
    #: What the line should look like. `{version}` is substituted.
    describe: str
    #: How many times the pattern must match. A pattern that stops matching is
    #: a silent failure otherwise: --check would pass because it found no
    #: disagreement, having found nothing at all.
    occurrences: int = 1


PLACES: tuple[Place, ...] = (
    Place("CMakeLists.txt", r"^(    VERSION )(\S+)$", "    VERSION {version}"),
    Place("packaging/linux/APKBUILD", r"^(pkgver=)(\S+)$", "pkgver={version}"),
    Place("packaging/linux/PKGBUILD", r"^(pkgver=)(\S+)$", "pkgver={version}"),
    Place("packaging/linux/template", r"^(version=)(\S+)$", "version={version}"),
    Place("packaging/linux/transmit.spec", r"^(Version:\s+)(\S+)$", "Version:        {version}"),
    Place("packaging/linux/flake.nix", r'^(\s*version = ")([^"]+)";$', '          version = "{version}";'),
    Place("packaging/linux/io.github.neramc.Transmit.yml", r"^(\s*tag: v)(\S+)$", "        tag: v{version}"),
    Place("packaging/windows/transmit.nsi", r'^(\s*!define VERSION ")([^"]+)"$',
          '    !define VERSION "{version}"'),
)

# Reset to 1 whenever the version moves: a package revision counts rebuilds of
# one version, so carrying it forward tells a package manager that the new
# version has already been rebuilt several times.
REVISIONS: tuple[Place, ...] = (
    Place("packaging/linux/APKBUILD", r"^(pkgrel=)(\S+)$", "pkgrel=0"),
    Place("packaging/linux/PKGBUILD", r"^(pkgrel=)(\S+)$", "pkgrel=1"),
    Place("packaging/linux/template", r"^(revision=)(\S+)$", "revision=1"),
)


@dataclass
class Finding:
    place: Place
    line_number: int
    found: str
    problem: str


@dataclass
class Report:
    findings: list[Finding] = field(default_factory=list)

    def add(self, place: Place, line_number: int, found: str, problem: str) -> None:
        self.findings.append(Finding(place, line_number, found, problem))

    def __bool__(self) -> bool:
        return bool(self.findings)


# Splits on newlines and nothing else, keeping each line's ending attached.
# str.splitlines would also break on a form feed and on the Unicode line
# separators, and a file rewritten along a boundary it never had is a file
# somebody has to repair by hand.
LINES = re.compile(r"[^\n]*\n|[^\n]+$")


def read(path: str) -> list[str]:
    """Every line of the file, endings intact.

    newline="" turns off the translation in both directions. Without it a
    Windows checkout - where .gitattributes asks for CRLF in the NSIS
    installer script - would be read as LF and written back as LF, so setting
    a version would rewrite every line in the file as a side effect.
    """
    full = ROOT / path
    if not full.is_file():
        raise SystemExit(f"version.py: {path} is not there any more - update PLACES")
    with full.open("r", encoding="utf-8", newline="") as handle:
        return LINES.findall(handle.read())


def body(line: str) -> tuple[str, str]:
    """The line without its ending, and the ending on its own."""
    stripped = line.rstrip("\r\n")
    return stripped, line[len(stripped):]


def matches(place: Place) -> list[tuple[int, str, str]]:
    """Every line the place's pattern matches: (index, whole line, version)."""
    found = []
    for index, line in enumerate(read(place.path)):
        hit = re.match(place.pattern, body(line)[0])
        if hit:
            found.append((index, line, hit.group(2)))
    return found


def declared() -> str:
    hits = matches(PLACES[0])
    if not hits:
        raise SystemExit("version.py: CMakeLists.txt does not declare a VERSION")
    return hits[0][2]


def check(expected: str) -> Report:
    report = Report()
    for place in PLACES:
        hits = matches(place)
        if len(hits) != place.occurrences:
            report.add(place, 0, f"{len(hits)} lines matched",
                       f"expected {place.occurrences} - the pattern no longer "
                       f"describes this file, so nothing here is being checked")
            continue
        for index, _, version in hits:
            if version != expected:
                report.add(place, index + 1, version,
                           f"should be {expected}, which is what CMakeLists.txt declares")
    return report


def apply(version: str) -> list[str]:
    changed = []
    for place in (*PLACES, *REVISIONS):
        lines = read(place.path)
        touched = False
        for index, line in enumerate(lines):
            text, ending = body(line)
            if re.match(place.pattern, text):
                replacement = place.describe.format(version=version) + ending
                if replacement != line:
                    lines[index] = replacement
                    touched = True
        if touched:
            # newline="" again: on Windows the default would turn every \n
            # written here into \r\n, including in the files that must keep LF.
            with (ROOT / place.path).open("w", encoding="utf-8", newline="") as handle:
                handle.write("".join(lines))
            changed.append(place.path)
    return sorted(set(changed))


def changelog_mentions(version: str) -> bool:
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    return re.search(rf"^## {re.escape(version)}\b", text, re.MULTILINE) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--print", action="store_true", help="print the declared version")
    action.add_argument("--check", action="store_true", help="every file agrees")
    action.add_argument("--set", metavar="X.Y.Z", help="rewrite every file")
    parser.add_argument("--require-changelog", action="store_true",
                        help="also insist CHANGELOG.md has a section for the version")
    arguments = parser.parse_args()

    if arguments.print:
        print(declared())
        return 0

    if arguments.set:
        wanted = arguments.set.removeprefix("v")
        if not VERSION.match(wanted):
            print(f"version.py: '{arguments.set}' is not a three-part version", file=sys.stderr)
            return 2

        changed = apply(wanted)
        print(f"version {wanted}")
        for path in changed:
            print(f"  rewrote {path}")
        if not changed:
            print("  everything already said so")

        # Rewriting is not the same as agreeing. A pattern that has rotted
        # would rewrite nothing and report nothing, so the result is checked.
        report = check(wanted)
        if report:
            print("\nbut the files still do not agree:", file=sys.stderr)
            for finding in report.findings:
                print(f"  {finding.place.path}:{finding.line_number}: "
                      f"{finding.found} - {finding.problem}", file=sys.stderr)
            return 1
        return 0

    expected = declared()
    report = check(expected)
    if report:
        print(f"The version is {expected} in CMakeLists.txt, and these disagree:\n",
              file=sys.stderr)
        for finding in report.findings:
            where = f"{finding.place.path}:{finding.line_number}" if finding.line_number \
                else finding.place.path
            print(f"  {where}", file=sys.stderr)
            print(f"      found {finding.found}", file=sys.stderr)
            print(f"      {finding.problem}", file=sys.stderr)
        print(f"\nRun: scripts/version.py --set {expected}", file=sys.stderr)
        return 1

    if arguments.require_changelog and not changelog_mentions(expected):
        print(f"CHANGELOG.md has no '## {expected}' section. A release with no "
              f"notes is a release nobody can read.", file=sys.stderr)
        return 1

    print(f"Every version in the tree says {expected}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
