#!/usr/bin/env python3
"""Reads everything scripts/ci-check.sh recorded and says what went wrong.

One report, at the end, with the cause of every failure rather than the first
one. A run that stops at the first problem costs a full round trip for each
subsequent problem, and the second problem is usually the same shape as the
first, so finding them together is the difference between one fix and four.

Prints to stdout and, in continuous integration, into the job summary. Exits 1
if anything failed, which is the single place the job's result is decided.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

# How much of a failing check's output to show. Enough to hold a compiler's
# error and the lines around it; a full build log in a summary is a way of
# saying nothing at length.
HEAD_LINES = 60
TAIL_LINES = 120


def interesting(output: str) -> str:
    """The part of the output worth reading, with the middle dropped."""
    lines = output.splitlines()
    if len(lines) <= HEAD_LINES + TAIL_LINES:
        return "\n".join(lines)
    hidden = len(lines) - HEAD_LINES - TAIL_LINES
    return "\n".join([
        *lines[:HEAD_LINES],
        "",
        f"    ... {hidden} lines omitted ...",
        "",
        *lines[-TAIL_LINES:],
    ])


def load(directory: Path) -> list[dict]:
    records = []
    for path in sorted(directory.glob("*.json")):
        try:
            records.append(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError) as failure:
            records.append({
                "name": f"(unreadable record {path.name})",
                "status": "failed",
                "exit": -1,
                "seconds": 0,
                "command": "",
                "output": str(failure),
            })
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", type=Path,
                        default=Path(os.environ.get("TRANSMIT_CI_RESULTS", "ci-results")))
    parser.add_argument("--title", default="Checks")
    arguments = parser.parse_args()

    if not arguments.results.is_dir():
        print(f"ci-report: no results in {arguments.results} - did any check run?",
              file=sys.stderr)
        return 1

    records = load(arguments.results)
    if not records:
        print(f"ci-report: {arguments.results} is empty - did any check run?", file=sys.stderr)
        return 1

    failed = [record for record in records if record["status"] != "passed"]
    lines: list[str] = []

    lines.append(f"## {arguments.title}")
    lines.append("")
    lines.append("| | Check | Time |")
    lines.append("| --- | --- | --- |")
    for record in records:
        mark = "✅" if record["status"] == "passed" else "❌"
        lines.append(f"| {mark} | {record['name']} | {record['seconds']}s |")
    lines.append("")

    if not failed:
        lines.append(f"All {len(records)} checks passed.")
    else:
        lines.append(f"**{len(failed)} of {len(records)} checks failed.** "
                     f"Every one of them is below, with what it was running and what it said. "
                     f"They all ran: nothing here was skipped because something earlier failed.")
        lines.append("")
        for number, record in enumerate(failed, start=1):
            lines.append(f"### {number}. {record['name']}")
            lines.append("")
            lines.append(f"Exit status **{record['exit']}** after {record['seconds']}s.")
            lines.append("")
            if record["command"]:
                lines.append("Ran:")
                lines.append("")
                lines.append("```")
                lines.append(record["command"])
                lines.append("```")
                lines.append("")
            lines.append("Said:")
            lines.append("")
            lines.append("```")
            lines.append(interesting(record["output"]) or "(it printed nothing)")
            lines.append("```")
            lines.append("")

    report = "\n".join(lines)
    print(report)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(report + "\n")

    for record in failed:
        print(f"::error::{record['name']} failed (exit {record['exit']})")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
