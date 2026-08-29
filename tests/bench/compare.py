#!/usr/bin/env python3
"""Compares a benchmark run against a committed baseline.

Exits non-zero when something got slower by more than the allowed
margin. The margin is generous on purpose: a shared build machine has a
noise floor around ten percent, and a gate that fires on noise is a gate
everyone learns to ignore.

    compare.py baseline.json current.json [--tolerance 20]
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
    args = parser.parse_args()

    baseline = load(args.baseline)
    current = load(args.current)

    failures = []
    print(f"{'measurement':<28} {'baseline':>10} {'now':>10} {'change':>9}")

    for name, now in sorted(current.items()):
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

        change = (now["medianMs"] - was["medianMs"]) / was["medianMs"] * 100.0
        print(f"{name:<28} {was['medianMs']:>10.1f} {now['medianMs']:>10.1f} "
              f"{change:>+8.1f}%")
        if change > args.tolerance:
            failures.append(f"{name} is {change:.1f}% slower "
                            f"({was['medianMs']:.1f} -> {now['medianMs']:.1f} ms)")

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
