#!/usr/bin/env python3
"""Builds the signed feed the updater reads.

One JSON document listing what has been published, with a digest for every
file, and a detached Ed25519 signature over its exact bytes. The digest is what
a download is checked against and the signature is what makes the digest worth
checking, so both are produced here, from the files that are actually being
uploaded, rather than written by hand.

    scripts/make-update-feed.py --version 0.2.0 --directory release \\
        --out release/updates.json --sign-key key.pem

Without --sign-key it writes the feed and no signature, and every copy of
Transmit that reads it will say a new version exists and install nothing. That
is the intended behaviour for a project that has not set up signing, not a
half-working state: see docs in CONTRIBUTING.md for making a key.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

# Which released file is for which machine. Matched against the file name, and
# a file matching nothing is left out of the feed rather than guessed at - an
# artifact described wrongly is one an updater would download onto the wrong
# machine.
SHAPES = (
    (re.compile(r"-linux-x86_64\.AppImage$"), "linux", "x86_64", "appimage"),
    (re.compile(r"-linux-aarch64\.AppImage$"), "linux", "arm64", "appimage"),
    (re.compile(r"-macos-arm64\.dmg$"), "macos", "arm64", "dmg"),
    (re.compile(r"-macos-x86_64\.dmg$"), "macos", "x86_64", "dmg"),
    (re.compile(r"-windows-x64-setup\.exe$"), "windows", "x86_64", "setup"),
    (re.compile(r"-windows-x64-portable\.zip$"), "windows", "x86_64", "portable"),
)

VERSION = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def digests(path: Path) -> tuple[str, str, int]:
    blake = hashlib.blake2b(digest_size=32)
    sha = hashlib.sha256()
    size = 0
    with path.open("rb") as handle:
        while chunk := handle.read(1 << 20):
            blake.update(chunk)
            sha.update(chunk)
            size += len(chunk)
    return blake.hexdigest(), sha.hexdigest(), size


def artifacts(directory: Path, base_url: str) -> list[dict]:
    found = []
    for path in sorted(directory.iterdir()):
        if not path.is_file():
            continue
        for pattern, platform, arch, kind in SHAPES:
            if not pattern.search(path.name):
                continue
            blake, sha, size = digests(path)
            if size == 0:
                raise SystemExit(f"make-update-feed: {path.name} is empty")
            found.append({
                "platform": platform,
                "arch": arch,
                "kind": kind,
                "url": f"{base_url.rstrip('/')}/{path.name}",
                "size": size,
                "blake2b": blake,
                "sha256": sha,
            })
            break
    return found


def notes_for(version: str, changelog: Path) -> str:
    """The changelog section for this version, if it has one."""
    if not changelog.is_file():
        return ""
    text = changelog.read_text(encoding="utf-8")
    match = re.search(rf"^## {re.escape(version)}\b(.*?)(?=^## |\Z)", text,
                      re.MULTILINE | re.DOTALL)
    return match.group(1).strip() if match else ""


def severity_for(version: str, declaration: Path) -> tuple[str, str | None]:
    """What packaging/release.json says about this version.

    The file names the version it applies to, so a severity written for a
    hotfix cannot outlive it: the next ordinary release finds a version that
    is not its own and is published as normal.
    """
    if not declaration.is_file():
        return "normal", None
    stated = json.loads(declaration.read_text(encoding="utf-8"))
    if stated.get("version") != version:
        return "normal", None

    severity = stated.get("severity", "normal")
    if severity not in {"normal", "important", "critical"}:
        raise SystemExit(f"make-update-feed: '{severity}' is not a severity")

    unsafe_below = stated.get("unsafeBelow")
    if unsafe_below is not None and not VERSION.match(str(unsafe_below)):
        raise SystemExit(f"make-update-feed: unsafeBelow '{unsafe_below}' is not a version")
    return severity, unsafe_below


def sign(document: Path, key: Path, out: Path) -> None:
    """Detached Ed25519 signature, base64, over the exact bytes on disk.

    Shelled out to OpenSSL rather than done here so no signing code lives in
    this repository: the private key is handled by one tool, in one step, and
    never passes through a Python variable.

    The file is named rather than piped because a one-shot Ed25519 signature
    needs to know its length up front, and OpenSSL cannot ask that of a pipe.
    Signing the file that was written is also the stricter thing to do: it is
    the bytes people will download that get signed, not a second copy of them.
    """
    # $OPENSSL rather than whatever is first on the path: macOS installs
    # LibreSSL under that name, and LibreSSL's pkeyutl has no -rawin.
    openssl = os.environ.get("OPENSSL", "openssl")
    signature = subprocess.run(
        [openssl, "pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(document)],
        capture_output=True, check=False)
    if signature.returncode != 0:
        raise SystemExit("make-update-feed: openssl could not sign the feed:\n"
                         + signature.stderr.decode("utf-8", "replace"))
    raw = signature.stdout
    if len(raw) != 64:
        raise SystemExit(f"make-update-feed: the signature is {len(raw)} bytes, not 64 - "
                         f"is the key an Ed25519 key?")

    import base64
    out.write_bytes(base64.b64encode(raw) + b"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--directory", required=True, type=Path,
                        help="where the release files are")
    parser.add_argument("--base-url", required=True,
                        help="where those files will be downloadable from")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--sign-key", type=Path, help="Ed25519 private key, PEM")
    parser.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--declaration", type=Path, default=Path("packaging/release.json"))
    parser.add_argument("--valid-days", type=int, default=365,
                        help="how long the feed may be believed")
    parser.add_argument("--previous", type=Path,
                        help="an existing feed whose releases are carried forward")
    arguments = parser.parse_args()

    version = arguments.version.removeprefix("v")
    if not VERSION.match(version):
        raise SystemExit(f"make-update-feed: '{version}' is not a three-part version")

    files = artifacts(arguments.directory, arguments.base_url)
    if not files:
        raise SystemExit(f"make-update-feed: nothing in {arguments.directory} looks like a "
                         f"release file - the feed would offer an update with nothing to fetch")

    severity, unsafe_below = severity_for(version, arguments.declaration)

    release = {
        "version": version,
        "severity": severity,
        "published": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "notes": notes_for(version, arguments.changelog),
        "artifacts": files,
    }
    if unsafe_below:
        release["unsafeBelow"] = unsafe_below

    releases = [release]
    if arguments.previous and arguments.previous.is_file():
        # Older releases stay listed so somebody far behind still sees a path
        # forward, and so a critical fix keeps applying to versions below it.
        earlier = json.loads(arguments.previous.read_text(encoding="utf-8"))
        for entry in earlier.get("releases", []):
            if entry.get("version") != version:
                releases.append(entry)

    now = datetime.now(timezone.utc)
    feed = {
        "schema": 1,
        "generated": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "expires": (now + timedelta(days=arguments.valid_days)).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "releases": releases,
    }

    # Written once, and signed over exactly those bytes. Re-serialising for the
    # signature is how a feed comes to be signed for something other than what
    # it says.
    document = json.dumps(feed, indent=2, sort_keys=False).encode("utf-8") + b"\n"
    arguments.out.write_bytes(document)
    print(f"wrote {arguments.out} ({len(document)} bytes, {len(files)} files, {severity})")

    if arguments.sign_key:
        signature = Path(str(arguments.out) + ".sig")
        sign(arguments.out, arguments.sign_key, signature)
        print(f"signed into {signature}")
    else:
        print("no signing key given: every copy will report the update and install nothing")

    return 0


if __name__ == "__main__":
    sys.exit(main())
