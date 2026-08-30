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
    """
    counts = {}
    for segment in entry.get("segments", []):
        line, _column, count, has_count = segment[0], segment[1], segment[2], segment[3]
        if not has_count:
            continue
        counts[line] = max(counts.get(line, 0), count)
    return counts


def added_lines(base):
    """The lines this branch adds, by file, from the merge base with `base`."""
    try:
        merge_base = subprocess.run(
            ["git", "merge-base", base, "HEAD"],
            capture_output=True, text=True, check=True).stdout.strip()
    except subprocess.CalledProcessError:
        print(f"cannot find a merge base with '{base}'", file=sys.stderr)
        return None

    diff = subprocess.run(
        ["git", "diff", "--unified=0", "--no-color", f"{merge_base}...HEAD"],
        capture_output=True, text=True, check=True).stdout

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--coverage", required=True)
    parser.add_argument("--floor", type=float, default=0.0)
    parser.add_argument("--changed-floor", type=float, default=0.0)
    parser.add_argument("--base")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()

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
        added = added_lines(args.base)
        if added is None:
            return 1

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
