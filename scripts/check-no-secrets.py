#!/usr/bin/env python3
"""Nothing that looks like a credential is committed.

A key or a token committed once is committed forever: rewriting history does
not reach the clones somebody already made, and a private repository can stop
being one. The only moment it can still be undone is before it is pushed, so
this is cheap and runs on everything git is tracking.

It looks for the shapes credentials come in rather than for any particular
service's, because the next service's format is not in this file yet.

    scripts/check-no-secrets.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# What is being looked for. Each is a shape rather than a service: a PEM block
# is a PEM block whoever issued it.
PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("a private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |PGP |DSA )?PRIVATE KEY")),
    ("a GitHub token", re.compile(r"\bgh[pousr]_[A-Za-z0-9]{36,}")),
    ("an AWS access key", re.compile(r"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b")),
    ("a Slack token", re.compile(r"\bxox[abposr]-[0-9A-Za-z-]{10,}")),
    ("a Google API key", re.compile(r"\bAIza[0-9A-Za-z_-]{35}\b")),
    ("a JSON web token", re.compile(r"\beyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.")),
    ("a bearer token", re.compile(r"(?i)\bauthorization\s*[:=]\s*[\"']?bearer\s+[A-Za-z0-9._-]{20,}")),
    ("a password in a setting",
     re.compile(r"(?i)\b(?:password|passphrase|secret|api[_-]?key)\s*[:=]\s*[\"'][^\"'\s]{8,}[\"']")),
)

# Files that are allowed to contain what looks like one, and why.
#
# Each is a place where the shape is the subject rather than the secret: a test
# that proves a passphrase is refused has to name a passphrase.
ALLOWED = {
    "scripts/check-no-secrets.py": "the patterns themselves are here",
    "tests/integration/UpdateTest.cpp": "signing keys are generated and used in the test",
    "tests/integration/SecretStoreTest.cpp": "the credential store is what it tests",
    "tests/integration/ContinuityRoundTripTest.cpp": "encryption is tested with a passphrase",
    "tests/unit/ContainerTest.cpp": "encryption is tested with a passphrase",
    "tests/property/RoundTripPropertyTest.cpp": "a passphrase is generated from the seed",
    "docs/security.md": "documents what a credential looks like",
    "SECURITY.md": "documents what a credential looks like",
}

# Nothing binary, and nothing that is a test fixture of bytes on purpose.
SKIP_SUFFIXES = {".png", ".ico", ".icns", ".svg", ".dmg", ".txa", ".qm", ".zip", ".gz"}


def tracked() -> list[str]:
    listing = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                             capture_output=True, check=True)
    return [name.decode("utf-8") for name in listing.stdout.split(b"\0") if name]


def main() -> int:
    findings: list[str] = []
    for name in tracked():
        if Path(name).suffix in SKIP_SUFFIXES:
            continue
        path = ROOT / name
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue

        for description, pattern in PATTERNS:
            for match in pattern.finditer(text):
                if name in ALLOWED:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                findings.append(f"{name}:{line}: {description}")

    if findings:
        print(f"{len(findings)} things that look like credentials:\n", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        print("\nIf one of these is deliberate, add the file to ALLOWED in this script "
              "with the reason.", file=sys.stderr)
        return 1

    print(f"Nothing that looks like a credential in {len(tracked())} tracked files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
