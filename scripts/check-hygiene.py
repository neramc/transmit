#!/usr/bin/env python3
"""The small things that are only ever noticed by the person they inconvenience.

Every rule reports every place it applies before the script exits, for the same
reason the whole run does: finding four of something together is one fix, and
finding them one per run is four.

    scripts/check-hygiene.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Files git is tracking, so a build directory or somebody's notes are never
# read. Anything not tracked is not this script's business.
def tracked() -> list[Path]:
    listing = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                             capture_output=True, check=True)
    return [ROOT / name.decode("utf-8")
            for name in listing.stdout.split(b"\0") if name]


TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".mm", ".qml", ".py", ".sh", ".json",
    ".md", ".yml", ".yaml", ".txt", ".cmake", ".nix", ".spec", ".ebuild",
    ".desktop", ".rc", ".nsi", ".in",
}
TEXT_NAMES = {"APKBUILD", "PKGBUILD", "template", "rules", "control", "CMakeLists.txt"}

# A conflict left in a file compiles surprisingly often and means something
# nobody wrote.
MERGE_MARKERS = re.compile(r"^(<<<<<<< |>>>>>>> |={7}$)", re.MULTILINE)

# Nothing tracked here should be this big. A binary that slipped in is one
# everybody clones forever.
LARGEST_TRACKED = 4 * 1024 * 1024


def is_text(path: Path) -> bool:
    return path.suffix in TEXT_SUFFIXES or path.name in TEXT_NAMES


def reportable_job_names(workflow: Path) -> set[str] | None:
    """Every name a job in this workflow can report to GitHub.

    A job whose name mentions a matrix value reports one name per entry, so the
    matrix is expanded rather than the name compared literally. Without that,
    "Sanitisers (asan-ubsan)" looks missing while it is running.
    """
    try:
        import yaml
    except ImportError:
        # Nothing rather than an empty set: the caller subtracts this from the
        # names the release insists on, so "I could not read the file" and
        # "the file names no jobs" must not look the same. Returning an empty
        # set once reported all seven required jobs as missing.
        return None

    try:
        parsed = yaml.safe_load(workflow.read_text(encoding="utf-8"))
    except Exception:  # a workflow that will not parse is another check's problem
        return None

    names: set[str] = set()
    for key, job in (parsed.get("jobs") or {}).items():
        template = str(job.get("name", key))
        placeholders = re.findall(r"\$\{\{\s*matrix\.(\w+)\s*\}\}", template)
        if not placeholders:
            names.add(template)
            continue
        for entry in ((job.get("strategy") or {}).get("matrix") or {}).get("include", []):
            filled = template
            for placeholder in placeholders:
                filled = re.sub(r"\$\{\{\s*matrix\." + placeholder + r"\s*\}\}",
                                str(entry.get(placeholder, "")), filled)
            names.add(filled)
    return names


def declared_version() -> str:
    """What CMakeLists.txt says, without importing the version script."""
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"^ {4}VERSION (\S+)$", text, re.MULTILINE)
    return found.group(1) if found else ""


def main() -> int:
    problems: list[str] = []

    for path in tracked():
        if not path.is_file():
            continue

        relative = path.relative_to(ROOT)
        size = path.stat().st_size
        if size > LARGEST_TRACKED:
            problems.append(f"{relative}: {size // 1024} KB is too large to keep in the "
                            f"repository forever")

        if not is_text(path):
            continue

        try:
            raw = path.read_bytes()
        except OSError as failure:
            problems.append(f"{relative}: could not be read ({failure})")
            continue

        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            problems.append(f"{relative}: is not valid UTF-8")
            continue

        if raw and not raw.endswith(b"\n"):
            problems.append(f"{relative}: does not end with a newline")

        if MERGE_MARKERS.search(text):
            for number, line in enumerate(text.splitlines(), start=1):
                if re.match(r"^(<<<<<<< |>>>>>>> |={7}$)", line):
                    problems.append(f"{relative}:{number}: a merge conflict marker is still here")

        for number, line in enumerate(text.splitlines(), start=1):
            if line.rstrip("\r\n") != line.rstrip():
                problems.append(f"{relative}:{number}: trailing whitespace")
            if "\t" in line and path.suffix in {".cpp", ".h", ".qml", ".py"}:
                problems.append(f"{relative}:{number}: a tab, where this project uses spaces")

    # Every script a workflow calls has to exist, because a workflow that calls
    # a script that was renamed fails at the moment it is needed most.
    for workflow in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        text = workflow.read_text(encoding="utf-8")
        for reference in sorted(set(re.findall(r"(?:^|[\s\"'(])(scripts/[\w.-]+)", text))):
            if not (ROOT / reference).exists():
                problems.append(f"{workflow.relative_to(ROOT)}: calls {reference}, "
                                f"which is not there")

    # Every test source is registered with CMake. One that is not builds
    # nothing, runs nothing, and looks exactly like one that passes.
    for directory in sorted((ROOT / "tests").iterdir()):
        cmake = directory / "CMakeLists.txt"
        if not directory.is_dir() or not cmake.is_file():
            continue
        listed = cmake.read_text(encoding="utf-8")
        for source in sorted(directory.glob("*Test.cpp")):
            stem = source.stem.removesuffix("Test")
            if stem not in listed and source.name not in listed:
                problems.append(f"{source.relative_to(ROOT)}: is not registered in "
                                f"{cmake.relative_to(ROOT)}, so it never runs")

    # Every source file is in a build. One that is not compiles nowhere,
    # is analysed by nothing, and looks exactly like one that works.
    for directory, cmake_name in ((ROOT / "src", "CMakeLists.txt"),):
        for source in sorted(directory.rglob("*.cpp")) + sorted(directory.rglob("*.mm")):
            listed = False
            for level in list(source.parents)[:4]:
                cmake = level / cmake_name
                if cmake.is_file() and source.name in cmake.read_text(encoding="utf-8"):
                    listed = True
                    break
            if not listed:
                problems.append(f"{source.relative_to(ROOT)}: is in no CMakeLists.txt, "
                                f"so nothing compiles it")

    # A script a person is told to run has to be runnable.
    for script in sorted(ROOT.glob("scripts/*")):
        if script.suffix in {".sh", ".py"} and not script.stat().st_mode & 0o111:
            problems.append(f"{script.relative_to(ROOT)}: is not executable")

    # A link in the documentation that points at nothing is a paragraph that
    # tells somebody to go and look at a file that was renamed.
    link = re.compile(r"\[[^\]]*\]\(([^)#\s]+)")
    for document in sorted(ROOT.glob("*.md")) + sorted(ROOT.glob("docs/*.md")):
        text = document.read_text(encoding="utf-8")
        for target in sorted(set(link.findall(text))):
            if target.startswith(("http://", "https://", "mailto:")):
                continue
            if not (document.parent / target).exists():
                problems.append(f"{document.relative_to(ROOT)}: links to {target}, "
                                f"which is not there")

    # The release refuses to publish unless a named set of jobs passed. A job
    # renamed or, as happened once, deleted by an edit to the file above it,
    # takes the release with it and nothing says so until somebody tags.
    ci = ROOT / ".github" / "workflows" / "ci.yml"
    release = ROOT / ".github" / "workflows" / "release.yml"
    if ci.is_file() and release.is_file():
        gate = re.search(r"^\s*required=\((.*?)\)\s*$",
                         release.read_text(encoding="utf-8"), re.MULTILINE | re.DOTALL)
        if gate:
            wanted = set(re.findall(r'"([^"]+)"', gate.group(1)))
            declared = reportable_job_names(ci)
            if declared is None:
                print("PyYAML is not installed, so the jobs the release insists on were not "
                      "checked against the ones ci.yml defines.")
            else:
                for job in sorted(wanted - declared):
                    problems.append(f".github/workflows/ci.yml: has no job named '{job}', "
                                    f"which release.yml refuses to publish without")

    # packaging/release.json decides whether the next release installs itself
    # on every machine without being asked. It is small, and it is read by a
    # script at the one moment nobody is watching, so it is checked here.
    declaration = ROOT / "packaging" / "release.json"
    if declaration.is_file():
        import json
        try:
            stated = json.loads(declaration.read_text(encoding="utf-8"))
        except json.JSONDecodeError as failure:
            problems.append(f"packaging/release.json: is not JSON ({failure})")
        else:
            version = str(stated.get("version", ""))
            if not re.match(r"^\d+\.\d+\.\d+$", version):
                problems.append(f"packaging/release.json: version '{version}' is not a "
                                f"three-part version")
            severity = stated.get("severity")
            if severity not in {"normal", "important", "critical"}:
                problems.append(f"packaging/release.json: '{severity}' is not a severity")
            floor = stated.get("unsafeBelow")
            if floor is not None and not re.match(r"^\d+\.\d+\.\d+$", str(floor)):
                problems.append(f"packaging/release.json: unsafeBelow '{floor}' is not a version")
            if severity == "critical" and version == declared_version():
                # Not a fault, but the one setting worth saying out loud: this
                # release will install itself on every machine that can take it.
                print(f"packaging/release.json marks {version} critical: it will install "
                      f"itself without asking.")

    if problems:
        print(f"{len(problems)} things to tidy:\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("Nothing untidy.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
