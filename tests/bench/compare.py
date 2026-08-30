#!/usr/bin/env python3
"""Compares a benchmark run against a committed baseline.

Exits non-zero when something got slower by more than the allowed margin.

The margin is measured against the machine, not against the clock. Baselines
are committed from one run and compared against another, and the build
machines are not all the same speed - the pool this runs on has been seen to
vary by a quarter for identical code. A gate on raw milliseconds fires on that
every time it draws a slow machine, and a gate that cries wolf is one everybody
learns to re-run until it passes.

So each run also measures a fixed amount of work that owes nothing to this
project ("machine/fixed-work"). Dividing by it cancels how fast the computer
is and leaves how fast the code is, which is the only thing a baseline can
honestly be about. When a run has no such measurement, the comparison falls
back to raw times with a much looser margin, and says so.

    compare.py baseline.json current.json [--tolerance 15]
"""

import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        return {r["name"]: r for r in json.load(handle)["results"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("current")
    parser.add_argument("--tolerance", type=float, default=20.0,
                        help="percent slower before this fails")
    parser.add_argument("--size-tolerance", type=float, default=5.0,
                        help="percent larger the archive may get")
    parser.add_argument("--uncalibrated-tolerance", type=float, default=45.0,
                        help="percent slower before this fails when the run "
                             "carries no machine measurement to divide by")
    args = parser.parse_args()

    baseline = load(args.baseline)
    current = load(args.current)

    # How much slower this machine is than the one the baseline came from.
    # Everything is divided by it, so what is compared is the code.
    reference = "machine/fixed-work"
    speed = None
    if reference in baseline and reference in current:
        was = baseline[reference]["medianMs"]
        now = current[reference]["medianMs"]
        if was > 0 and now > 0:
            speed = now / was

    if speed is None:
        tolerance = args.uncalibrated_tolerance
        print(f"no machine measurement in both runs - comparing raw times with a "
              f"{tolerance:.0f}% margin")
    else:
        tolerance = args.tolerance
        print(f"this machine is {speed:.2f}x the baseline's, and every measurement "
              f"below is divided by that")

    failures = []
    print(f"{'measurement':<28} {'baseline':>10} {'now':>10} {'change':>9}")

    for name, now in sorted(current.items()):
        # The reference is how the others are judged; judging it against
        # itself would always read as no change and say nothing.
        if name == reference:
            continue
        was = baseline.get(name)
        if was is None:
            print(f"{name:<28} {'-':>10} {now['medianMs']:>10.1f} {'new':>9}")
            continue

        # A measurement too small to time is not a measurement.
        #
        # Five milliseconds, not one: at a millisecond and a half a single
        # scheduling hiccup on a shared runner is a fifteen percent change, and
        # a gate that fires on that is a gate people learn to re-run until it
        # passes. The measurements that matter here - the large-buffer ones -
        # are two orders of magnitude above this.
        if was["medianMs"] < 5.0:
            continue

        # Neither is one that read nothing. A capture pointed at an empty
        # directory finishes instantly and reports a spectacular improvement,
        # and a gate that accepts that is not a gate.
        if was.get("files", 0) > 0 and now.get("files", 0) == 0:
            failures.append(
                f"{name} measured no files at all - the corpus is missing or empty")
            continue

        # What this measurement would have taken on the baseline's machine.
        expected = was["medianMs"] * (speed if speed is not None else 1.0)
        change = (now["medianMs"] - expected) / expected * 100.0
        print(f"{name:<28} {expected:>10.1f} {now['medianMs']:>10.1f} "
              f"{change:>+8.1f}%")
        if change > tolerance:
            failures.append(f"{name} is {change:.1f}% slower than this machine "
                            f"should manage ({expected:.1f} -> {now['medianMs']:.1f} ms)")

        # An archive that grew means a compression setting was weakened,
        # which a wall clock alone would report as an improvement.
        if was.get("bytesOut", 0) > 0 and now.get("bytesOut", 0) > 0:
            grew = (now["bytesOut"] - was["bytesOut"]) / was["bytesOut"] * 100.0
            if grew > args.size_tolerance:
                failures.append(f"{name} writes {grew:.1f}% more bytes")

    if failures:
        print("\nregressions:")
        for line in failures:
            print(f"  {line}")
        print("\nIf the change is intended, re-run the benchmark and commit "
              "the new baseline in the same commit as the change.")
        return 1

    print("\nno regression beyond the tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
