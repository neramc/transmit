#!/usr/bin/env python3
"""Adds the hand-written half of the catalog: what is inside each application's
state folder, and what has to happen to it when it moves.

The migration script does what a script can do - reshaping paths, giving every
state root an id. This holds the part no script can work out: that a Firefox
profile's installs.ini is keyed by a hash of the old installation directory, or
that a Chromium profile's saved passwords are sealed to the account that wrote
them. It is kept as code rather than typed straight into the JSON so that the
same knowledge is written once and applied to every application that shares a
layout - the five Chromium browsers between them are one description.

Only applications whose layout is actually known are here. An entry guessed at
would be worse than none: the catalog would then say, with the same confidence
as everything else, that a file can be dropped when it cannot.

  scripts/enrich-catalog.py
  scripts/migrate-catalog.py resources/app-catalog.json   # normalises afterwards
"""
import json, sys
from pathlib import Path

CATALOG = Path("resources/app-catalog.json")

MOZILLA_CONTENTS = [
    {"path": "profiles.ini", "role": "index", "format": "ini", "portable": "rewrite",
     "note": "Names every profile folder and whether the name is relative."},
    {"path": "installs.ini", "role": "index", "format": "ini", "portable": "never",
     "note": "Keyed by a hash of the installation directory, which is different on the new machine."},
    {"path": "*/compatibility.ini", "role": "index", "format": "ini", "portable": "never",
     "note": "Records where the last-run build lived; rebuilt on first start."},
    {"path": "*/prefs.js", "role": "settings", "format": "text", "portable": "rewrite"},
    {"path": "*/user.js", "role": "settings", "format": "text", "portable": "rewrite"},
    {"path": "*/places.sqlite", "role": "database", "format": "sqlite", "portable": "always",
     "live": True, "note": "Bookmarks and history."},
    {"path": "*/favicons.sqlite", "role": "database", "format": "sqlite", "portable": "always",
     "live": True},
    {"path": "*/cookies.sqlite", "role": "database", "format": "sqlite", "portable": "always",
     "live": True},
    {"path": "*/formhistory.sqlite", "role": "database", "format": "sqlite", "portable": "always",
     "live": True},
    {"path": "*/logins.json", "role": "credentials", "format": "json", "portable": "always",
     "sensitive": True,
     "note": "Encrypted with key4.db, which travels beside it, so these keep working anywhere."},
    {"path": "*/key4.db", "role": "credentials", "format": "sqlite", "portable": "always",
     "sensitive": True},
    {"path": "*/cert9.db", "role": "database", "format": "sqlite", "portable": "always"},
    {"path": "*/extensions", "role": "extension", "portable": "always"},
    {"path": "*/extensions.json", "role": "index", "format": "json", "portable": "rewrite",
     "note": "Records where each extension file is."},
    {"path": "*/storage", "role": "content", "portable": "always",
     "note": "Site storage: what pages have saved locally."},
    {"path": "*/sessionstore-backups", "role": "state", "portable": "always"},
    {"path": "*/cache2", "role": "cache", "portable": "never"},
    {"path": "*/startupCache", "role": "cache", "portable": "never"},
    {"path": "*/thumbnails", "role": "cache", "portable": "never"},
    {"path": "*/lock", "role": "lock", "portable": "never"},
    {"path": "*/.parentlock", "role": "lock", "portable": "never"},
    {"path": "*/crashes", "role": "log", "portable": "never"},
    {"path": "*/minidumps", "role": "log", "portable": "never"},
]

MOZILLA_MOVES = [
    {"when": {"from": "*", "to": "*"}, "root": "profile", "file": "profiles.ini",
     "format": "ini", "action": "rewrite",
     "set": [{"key": "*/IsRelative", "value": "1"}],
     "note": "Forced to relative so the profile is found wherever the folder ends up, rather than at the path it had on the old machine."},
    {"when": {"from": "*", "to": "*"}, "root": "profile", "file": "installs.ini",
     "action": "regenerate",
     "note": "Its section names are a hash of the old installation directory, so every entry in it is stale."},
    {"when": {"from": "*", "to": "*"}, "root": "profile", "file": "*/compatibility.ini",
     "action": "regenerate"},
    {"when": {"from": "*", "to": "*"}, "root": "profile", "file": "*/prefs.js",
     "format": "text", "action": "drop-keys",
     "keys": ["browser.startup.homepage_override.buildID",
              "browser.startup.homepage_override.mstone",
              "gfx.blacklist.",
              "media.gmp-.abi",
              "app.update.lastUpdateTime."],
     "note": "Removes the settings that describe the old build and the old graphics hardware. Left in place they make the application believe it has already dealt with this profile."},
]

CHROMIUM_CONTENTS = [
    {"path": "Local State", "role": "index", "format": "json", "portable": "rewrite",
     "note": "Lists the profiles and, on Windows, holds the key the saved passwords are encrypted with."},
    {"path": "*/Preferences", "role": "settings", "format": "json", "portable": "rewrite"},
    {"path": "*/Secure Preferences", "role": "settings", "format": "json", "portable": "never",
     "note": "Signed against this installation; a copied one is rejected and rebuilt."},
    {"path": "*/Bookmarks", "role": "content", "format": "json", "portable": "always"},
    {"path": "*/History", "role": "database", "format": "sqlite", "portable": "always",
     "live": True},
    {"path": "*/Login Data", "role": "credentials", "format": "sqlite", "portable": "same-os",
     "sensitive": True,
     "note": "Encrypted with a key held by the operating system's own keystore, which does not travel."},
    {"path": "*/Cookies", "role": "database", "format": "sqlite", "portable": "same-os",
     "sensitive": True, "live": True,
     "note": "Encrypted the same way as the saved passwords."},
    {"path": "*/Extensions", "role": "extension", "portable": "always"},
    {"path": "*/Local Storage", "role": "content", "portable": "always"},
    {"path": "*/IndexedDB", "role": "content", "portable": "always"},
    {"path": "*/Cache", "role": "cache", "portable": "never"},
    {"path": "*/Code Cache", "role": "cache", "portable": "never"},
    {"path": "*/GPUCache", "role": "cache", "portable": "never"},
    {"path": "ShaderCache", "role": "cache", "portable": "never"},
    {"path": "GrShaderCache", "role": "cache", "portable": "never"},
    {"path": "Crashpad", "role": "log", "portable": "never"},
    {"path": "SingletonLock", "role": "lock", "portable": "never"},
    {"path": "SingletonSocket", "role": "lock", "portable": "never"},
    {"path": "SingletonCookie", "role": "lock", "portable": "never"},
]

CHROMIUM_MOVES = [
    {"when": {"from": "*", "to": "*"}, "root": "profile", "file": "*/Secure Preferences",
     "action": "regenerate",
     "note": "Signed against the installation that wrote it; rebuilt on first start."},
    {"when": {"from": "windows", "to": "*"}, "root": "profile", "file": "Local State",
     "format": "json", "action": "drop-keys", "keys": ["os_crypt"],
     "note": "Holds a key sealed to the old Windows account. Kept, it would be tried and fail; dropped, the browser starts a new one."},
    {"when": {"from": "*", "to": "windows"}, "root": "profile", "file": "Local State",
     "format": "json", "action": "drop-keys", "keys": ["os_crypt"]},
]

CHROMIUM_PAIRS = [
    {"from": "windows", "to": "linux", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old account and cannot be read here."},
    {"from": "windows", "to": "macos", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old account and cannot be read here."},
    {"from": "linux", "to": "windows", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old keyring and cannot be read here."},
    {"from": "macos", "to": "windows", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old keychain and cannot be read here."},
    {"from": "linux", "to": "macos", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old keyring and cannot be read here."},
    {"from": "macos", "to": "linux", "grade": "adapted",
     "why": "Bookmarks, history and extensions come across. Saved passwords and cookies are sealed to the old keychain and cannot be read here."},
]

ENRICHMENT = {
    "org.mozilla.firefox": {
        "profile": {"contents": MOZILLA_CONTENTS,
                    "extra_paths": {"linux": ["{HOME}/.var/app/org.mozilla.firefox/.mozilla/firefox",
                                              "{HOME}/snap/firefox/common/.mozilla/firefox"]}},
        "move": MOZILLA_MOVES,
    },
    "org.mozilla.thunderbird": {
        "profile": {"contents": MOZILLA_CONTENTS + [
            {"path": "*/Mail", "role": "content", "portable": "always",
             "note": "Locally stored mail."},
            {"path": "*/ImapMail", "role": "content", "portable": "always"},
            {"path": "*/global-messages-db.sqlite", "role": "cache", "format": "sqlite",
             "portable": "never",
             "note": "The search index, rebuilt from the mail itself and often the largest file in the profile."},
        ], "extra_paths": {"linux": ["{HOME}/.var/app/org.mozilla.Thunderbird/.thunderbird"]}},
        "move": MOZILLA_MOVES + [
            {"when": {"from": "*", "to": "*"}, "root": "profile",
             "file": "*/global-messages-db.sqlite", "action": "regenerate"},
        ],
    },
    # Deliberately not Tor Browser. Its state root is the bundle directory,
    # not a Mozilla profile root - the profile is several levels inside it - so
    # the rules above would name files that are not where they say.
    "org.chromium.chromium": {
        "profile": {"contents": CHROMIUM_CONTENTS,
                    "extra_paths": {"linux": ["{HOME}/.var/app/org.chromium.Chromium/config/chromium"]}},
        "move": CHROMIUM_MOVES, "pairs": CHROMIUM_PAIRS,
    },
    "com.google.chrome": {"profile": {"contents": CHROMIUM_CONTENTS}, "move": CHROMIUM_MOVES,
                          "pairs": CHROMIUM_PAIRS},
    "com.brave.browser": {"profile": {"contents": CHROMIUM_CONTENTS}, "move": CHROMIUM_MOVES,
                          "pairs": CHROMIUM_PAIRS},
    "com.microsoft.edge": {"profile": {"contents": CHROMIUM_CONTENTS}, "move": CHROMIUM_MOVES,
                           "pairs": CHROMIUM_PAIRS},
    "com.vivaldi.browser": {"profile": {"contents": CHROMIUM_CONTENTS}, "move": CHROMIUM_MOVES,
                            "pairs": CHROMIUM_PAIRS},
    "com.microsoft.vscode": {
        "config": {"contents": [
            {"path": "settings.json", "role": "settings", "format": "json", "portable": "rewrite"},
            {"path": "keybindings.json", "role": "settings", "format": "json", "portable": "always"},
            {"path": "snippets", "role": "content", "portable": "always"},
            {"path": "tasks.json", "role": "settings", "format": "json", "portable": "rewrite"},
            {"path": "globalStorage/state.vscdb", "role": "database", "format": "sqlite",
             "portable": "always", "live": True},
            {"path": "globalStorage/state.vscdb.backup", "role": "cache", "portable": "never"},
            {"path": "workspaceStorage", "role": "state", "portable": "never",
             "note": "Keyed by the folder each workspace was opened from, so none of it applies on the new machine."},
            {"path": "History", "role": "state", "portable": "never",
             "note": "The editor's own local file history."},
        ]},
        "data": {"contents": [
            {"path": "*", "role": "extension", "portable": "rewrite"},
            {"path": ".obsolete", "role": "index", "format": "json", "portable": "never"},
            {"path": "extensions.json", "role": "index", "format": "json", "portable": "rewrite"},
        ]},
        "move": [
            {"when": {"from": "*", "to": "*"}, "root": "config", "file": "workspaceStorage",
             "action": "skip"},
            {"when": {"from": "*", "to": "*"}, "root": "data", "file": ".obsolete",
             "action": "regenerate"},
        ],
    },
    "org.openssh.client": {
        "config": {"contents": [
            {"path": "config", "role": "settings", "format": "text", "portable": "rewrite"},
            {"path": "id_*", "role": "credentials", "portable": "always", "sensitive": True,
             "note": "Private keys. Their file permissions matter and are restored."},
            {"path": "*.pub", "role": "credentials", "portable": "always"},
            {"path": "known_hosts", "role": "state", "format": "text", "portable": "always"},
            {"path": "authorized_keys", "role": "credentials", "portable": "always"},
            {"path": "*.sock", "role": "lock", "portable": "never"},
            {"path": "known_hosts.old", "role": "cache", "portable": "never"},
        ]},
    },
    "org.git-scm.git": {
        "config": {"contents": [
            {"path": ".gitconfig", "role": "settings", "format": "ini", "portable": "rewrite"},
        ]},
        "config2": {"contents": [
            {"path": "config", "role": "settings", "format": "ini", "portable": "rewrite"},
            {"path": "ignore", "role": "settings", "format": "text", "portable": "always"},
            {"path": "attributes", "role": "settings", "format": "text", "portable": "always"},
        ]},
        "move": [
            {"when": {"from": "*", "to": "windows"}, "root": "config", "file": ".gitconfig",
             "format": "ini", "action": "drop-keys",
             "keys": ["credential/helper", "core/sshCommand"],
             "note": "The credential helper and any ssh command named there are programs at paths that do not exist on Windows."},
            {"when": {"from": "windows", "to": "*"}, "root": "config", "file": ".gitconfig",
             "format": "ini", "action": "drop-keys",
             "keys": ["credential/helper", "core/autocrlf", "core/sshCommand"],
             "note": "Windows line-ending handling is wrong elsewhere, and the credential helper is a Windows program."},
        ],
    },
}


def enrich(entry: dict) -> dict:
    plan = ENRICHMENT.get(entry["id"])
    if plan is None:
        return entry

    for state in entry.get("state", []):
        rules = plan.get(state["id"])
        if rules is None:
            continue
        if "contents" in rules:
            state["contents"] = rules["contents"]
        for system, extra in rules.get("extra_paths", {}).items():
            existing = state.setdefault("paths", {}).setdefault(system, [])
            for candidate in extra:
                if candidate not in existing:
                    existing.append(candidate)

    if "move" in plan:
        entry["move"] = plan["move"]
    if "pairs" in plan:
        entry.setdefault("portability", {})["pairs"] = plan["pairs"]
    return entry


def main() -> int:
    document = json.loads(CATALOG.read_text(encoding="utf-8"))
    touched = 0
    for entry in document["apps"]:
        if entry["id"] in ENRICHMENT:
            enrich(entry)
            touched += 1
    CATALOG.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"enriched {touched} recipes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
