#!/usr/bin/env python3
"""Turns llvm-cov's export into a report somebody can act on, and two floors.

The overall floor answers "is the project still about as tested as it was".
The changed-line floor answers the more useful question: "are the lines this
change adds tested". A project of fourteen thousand lines can absorb a great
deal of untested new code before its overall percentage moves, and by the time
it does the untested code is everywhere.
"""

import argparse
import collections
import json
import re
import subprocess
import sys


def executable_lines(entry):
    """Which lines of a file the compiler emitted code for, and whether each ran.

    llvm-cov reports segments rather than lines: a segment starts at a line and
    column and carries an execution count. A line is covered when any segment
    starting on it ran, and a line with no segment at all is not code - a
    comment, a blank, a declaration - and is not counted either way.

    Gap regions are left out. They are llvm-cov's filler for the space between
    real regions - the `switch (x) {` line above the first case, the tail of a
    `return` that spans two lines - and they always carry a count of zero. Read
    as code they turn every switch in the project into an uncovered line: this
    file's own report showed three `return` statements as never reached in a
    function the tests had just called three times, which is how a coverage
    gate comes to be argued with rather than fixed.
    """
    counts = {}
    for segment in entry.get("segments", []):
        line, count, has_count = segment[0], segment[2], segment[3]
        is_gap = len(segment) > 5 and segment[5]
        if not has_count or is_gap:
            continue
        counts[line] = max(counts.get(line, 0), count)
    return counts


def unreachable_base(base):
    """Why `base` cannot be diffed against, or None if it can.

    Two different things go wrong here and they deserve different sentences.
    The commit may simply not be in this clone - the ordinary cause is a
    force-push, after which the commit the push event names as "before" is
    orphaned on the server and never arrives, however deep the fetch. Or it may
    be present but share no history with HEAD, which is what an unrelated
    branch looks like.

    `git rev-parse --verify` answers neither question: given forty hex digits
    it parses them and returns them, existing object or not, so it will happily
    wave through the one case this is here to catch.
    """
    present = subprocess.run(["git", "cat-file", "-e", f"{base}^{{commit}}"],
                             capture_output=True)
    if present.returncode != 0:
        return f"the commit '{base}' is not in this clone (a force-push leaves one behind)"

    if subprocess.run(["git", "merge-base", base, "HEAD"], capture_output=True).returncode != 0:
        return f"'{base}' shares no history with HEAD"

    return None


def added_lines(base):
    """The lines this branch adds, by file, from the merge base with `base`.

    The diff is asked for `src/` alone and decoded loosely on purpose. Only
    this project's own sources are ever scored, so a diff of everything else is
    work thrown away - and a repository picks up files that are not text: the
    fuzzer's committed crashers are bytes chosen to break a parser, and one of
    them is a byte no decoder will accept. Read strictly, the gate died on it
    with a UnicodeDecodeError, which arrives looking exactly like a coverage
    regression. The lines this function reads are the diff's own headers and
    they are ASCII whatever the file contains.
    """
    merge_base = subprocess.run(
        ["git", "merge-base", base, "HEAD"],
        capture_output=True, text=True, check=True).stdout.strip()

    diff = subprocess.run(
        ["git", "diff", "--unified=0", "--no-color", f"{merge_base}...HEAD", "--", "src"],
        capture_output=True, check=True).stdout.decode("utf-8", errors="replace")

    by_file = collections.defaultdict(set)
    current = None
    hunk = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
        elif line.startswith("@@") and current:
            match = hunk.match(line)
            if match:
                start = int(match.group(1))
                length = int(match.group(2) or 1)
                by_file[current].update(range(start, start + length))
    return by_file


def self_test():
    """Check the two rules that decide whether this gate can be believed.

    Both were wrong at once and neither showed as a wrong answer: the job
    failed, which reads like a coverage regression, and the reason was a base
    commit that a force-push had orphaned. So the rules are exercised here
    against a repository built for the purpose, rather than left to be found
    the next time somebody amends a commit.
    """
    import os
    import pathlib
    import tempfile

    failures = []

    def check(what, condition):
        print(f"  {'ok  ' if condition else 'FAIL'}  {what}")
        if not condition:
            failures.append(what)

    here = os.getcwd()
    with tempfile.TemporaryDirectory() as work:
        os.chdir(work)
        try:
            git = ["git", "-c", "user.email=t@t", "-c", "user.name=t"]
            subprocess.run(["git", "init", "-q", "-b", "main", "."], check=True)
            pathlib.Path("a.txt").write_text("one\n", encoding="utf-8")
            subprocess.run(git + ["add", "-A"], check=True)
            subprocess.run(git + ["commit", "-q", "-m", "first"], check=True)
            first = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                                   text=True, check=True).stdout.strip()

            pathlib.Path("a.txt").write_text("one\ntwo\n", encoding="utf-8")
            subprocess.run(git + ["commit", "-qam", "second"], check=True)

            print("A base commit that is not in the clone:")
            absent = "0" * 40
            check("git rev-parse --verify waves it through, so it cannot be the check",
                  subprocess.run(["git", "rev-parse", "--verify", absent],
                                 capture_output=True).returncode == 0)
            check("unreachable_base names it",
                  "not in this clone" in (unreachable_base(absent) or ""))

            print("A base that shares no history:")
            subprocess.run(git + ["checkout", "-q", "--orphan", "elsewhere"], check=True)
            subprocess.run(git + ["commit", "-q", "--allow-empty", "-m", "unrelated"],
                           check=True)
            orphan = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                                    text=True, check=True).stdout.strip()
            subprocess.run(git + ["checkout", "-q", "main"], check=True)
            check("unreachable_base names that too",
                  "shares no history" in (unreachable_base(orphan) or ""))

            print("A base that can be diffed against:")
            check("unreachable_base says nothing is wrong", unreachable_base(first) is None)
            pathlib.Path("src").mkdir()
            pathlib.Path("src/a.txt").write_text("one\ntwo\n", encoding="utf-8")
            subprocess.run(git + ["add", "-A"], check=True)
            subprocess.run(git + ["commit", "-q", "-m", "a source file"], check=True)
            check("and the added line is found",
                  added_lines(first).get("src/a.txt") == {1, 2})

            print("A file whose bytes are not text:")
            # The fuzzer's committed crashers are exactly this, and git calls a
            # file text unless it finds a NUL, so the diff carries the bytes.
            pathlib.Path("src/crasher").write_bytes(b"..\"" + bytes([0x80]) * 253)
            subprocess.run(git + ["add", "-A"], check=True)
            subprocess.run(git + ["commit", "-q", "-m", "a crasher"], check=True)
            try:
                found = added_lines(first).get("src/a.txt") == {1, 2}
            except UnicodeDecodeError:
                found = False
            check("the gate still runs, and still finds the line", found)
        finally:
            os.chdir(here)

    print("\nA gap region is not code:")
    entry = {"segments": [[10, 1, 3, True, True, False],
                          [11, 1, 0, True, True, True],
                          [12, 1, 0, True, True, False],
                          [13, 1, 0, False, False, False]]}
    counts = executable_lines(entry)
    check("a line that ran is counted as covered", counts.get(10) == 3)
    check("the gap region is left out entirely", 11 not in counts)
    check("a real line that never ran is counted as missed", counts.get(12) == 0)
    check("a line with no count at all is not code", 13 not in counts)

    if failures:
        print(f"\n{len(failures)} of these rules do not hold.")
        return 1
    print("\nEvery rule holds.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--coverage")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--floor", type=float, default=0.0)
    parser.add_argument("--changed-floor", type=float, default=0.0)
    parser.add_argument("--base")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.coverage:
        parser.error("--coverage is required")

    with open(args.coverage, encoding="utf-8") as handle:
        data = json.load(handle)
    export = data["data"][0]

    # ---------------------------------------------------------- by area
    areas = collections.defaultdict(lambda: [0, 0])
    per_file = {}
    for entry in export["files"]:
        name = entry["filename"]
        relative = name.split("/src/", 1)[-1] if "/src/" in name else name
        parts = relative.split("/")
        area = "/".join(parts[:2]) if len(parts) > 2 else parts[0]
        summary = entry["summary"]["lines"]
        areas[area][0] += summary["covered"]
        areas[area][1] += summary["count"]
        per_file[relative] = entry

    print("Line coverage by area, least covered first:")
    for area, (covered, total) in sorted(areas.items(), key=lambda kv: kv[1][0] / max(kv[1][1], 1)):
        share = 100.0 * covered / max(total, 1)
        print(f"  {share:6.1f}%  {covered:>6}/{total:<6}  {area}")

    totals = export["totals"]["lines"]
    overall = 100.0 * totals["covered"] / max(totals["count"], 1)
    print(f"\nOverall: {overall:.2f}%  ({totals['covered']}/{totals['count']} lines)")

    failed = False
    if not args.report_only:
        if overall < args.floor:
            print(f"::error::Line coverage {overall:.2f}% is below the floor of {args.floor}%.")
            failed = True
        else:
            print(f"At or above the floor of {args.floor}%.")

    # ------------------------------------------------------ changed lines
    if args.base and not args.report_only:
        # A base that cannot be diffed against is not a coverage regression, and
        # failing the job for one teaches people that a red Coverage means
        # nothing. Say the gate did not run, loudly enough to be read, and let
        # the overall floor decide the exit status on its own.
        unreachable = unreachable_base(args.base)
        if unreachable is not None:
            print(f"\n::warning::The changed-line check did not run: {unreachable}. "
                  f"Only the overall floor was enforced.")
            return 1 if failed else 0

        added = added_lines(args.base)

        covered = 0
        total = 0
        missed = collections.defaultdict(list)
        for path, lines in added.items():
            # Only this project's own sources; a change to a test or a workflow
            # has no coverage to have.
            if not path.startswith("src/"):
                continue
            relative = path[4:]
            entry = per_file.get(relative)
            if entry is None:
                continue
            counts = executable_lines(entry)
            for line in sorted(lines):
                if line not in counts:
                    continue  # not code
                total += 1
                if counts[line] > 0:
                    covered += 1
                else:
                    missed[path].append(line)

        if total == 0:
            print("\nNo new executable lines to check.")
        else:
            share = 100.0 * covered / total
            print(f"\nLines this change adds: {share:.1f}% covered ({covered}/{total})")
            if missed:
                print("Not reached by any test:")
                for path in sorted(missed):
                    lines = missed[path]
                    shown = ", ".join(str(n) for n in lines[:20])
                    more = f" (+{len(lines) - 20} more)" if len(lines) > 20 else ""
                    print(f"  {path}: {shown}{more}")
            if share < args.changed_floor:
                print(f"::error::Only {share:.1f}% of the lines this change adds are "
                      f"covered; {args.changed_floor}% is required.")
                failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
