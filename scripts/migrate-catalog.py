#!/usr/bin/env python3
"""Converts the application catalog from schema 1 to schema 2.

Schema 1 could say where an application keeps its state and which paths inside
it to correct. It could not say what is *in* that folder - which parts are
settings, which are caches nobody should carry, which are credentials - nor
what has to happen to a file when it moves between two systems. Schema 2 can.

This does the mechanical part for all of them: one path per system becomes a
list of candidates, every state root gets a stable id so a move step can refer
to it, and each application says whether its data travels at all. The
interesting part - the contents of a browser profile, the three things that
have to happen to a Firefox profile before it will open somewhere else - is
written by hand afterwards, because no script can know it.

Idempotent: running it on a file that is already version 2 changes nothing,
which is what makes it safe to run again after the hand-written parts are in.

  scripts/migrate-catalog.py resources/app-catalog.json
  scripts/migrate-catalog.py --check resources/app-catalog.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SYSTEMS = ("windows", "macos", "linux")


def unique_id(preferred: str, taken: set[str]) -> str:
    if preferred not in taken:
        taken.add(preferred)
        return preferred
    for suffix in range(2, 100):
        candidate = f"{preferred}{suffix}"
        if candidate not in taken:
            taken.add(candidate)
            return candidate
    raise RuntimeError(f"could not find a free id near {preferred!r}")


def migrate_state(entry: dict) -> list[dict]:
    taken: set[str] = set()
    migrated = []

    for state in entry.get("state", []):
        role = state.get("role", "config")
        moved = {"id": state.get("id") or unique_id(role, taken), "role": role}

        # Already version 2: a "paths" object with lists. Left exactly as it is,
        # including any hand-written contents.
        if "paths" in state:
            moved["paths"] = {
                system: value if isinstance(value, list) else [value]
                for system, value in state["paths"].items()
            }
        else:
            paths = {}
            for system in SYSTEMS:
                value = state.get(system)
                if isinstance(value, str) and value:
                    paths[system] = [value]
                elif isinstance(value, list) and value:
                    paths[system] = list(value)
            moved["paths"] = paths

        for carried in ("exclude", "contents"):
            if carried in state:
                moved[carried] = state[carried]

        migrated.append(moved)

    return migrated


def migrate_entry(entry: dict) -> dict:
    migrated = dict(entry)
    if "state" in entry:
        migrated["state"] = migrate_state(entry)

    # An application that names somewhere its settings live can carry them.
    # Defaulted rather than left to each of the seventy-three entries to
    # repeat, which is only a way to get it wrong in a few of them.
    portability = dict(entry.get("portability", {}))
    portability.setdefault("carries_data", bool(migrated.get("state")))
    migrated["portability"] = portability

    # A stable key order, so a hand edit shows up in a diff as itself rather
    # than as the whole entry moving about.
    order = ["id", "name", "detect", "install", "state", "rewrite", "move", "quiesce",
             "portability", "grade", "note"]
    return {key: migrated[key] for key in order if key in migrated} | {
        key: value for key, value in migrated.items() if key not in order
    }


def migrate(document: dict) -> dict:
    migrated = {
        "$comment": document.get("$comment", ""),
        "schemaVersion": 2,
        "apps": [migrate_entry(entry) for entry in document.get("apps", [])],
    }
    if not migrated["$comment"]:
        del migrated["$comment"]
    return migrated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="report whether the file is already migrated, change nothing")
    arguments = parser.parse_args()

    original = json.loads(arguments.catalog.read_text(encoding="utf-8"))
    migrated = migrate(original)
    text = json.dumps(migrated, indent=2, ensure_ascii=False) + "\n"

    if arguments.check:
        if text == arguments.catalog.read_text(encoding="utf-8"):
            print(f"{arguments.catalog} is migrated and stable.")
            return 0
        print(f"{arguments.catalog} is not what the migration would produce.", file=sys.stderr)
        return 1

    arguments.catalog.write_text(text, encoding="utf-8")
    print(f"migrated {len(migrated['apps'])} recipes to schema {migrated['schemaVersion']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
