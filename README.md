# Transmit

Move a computer's environment onto another computer running a different
operating system.

Transmit captures your files, the data and settings your programs keep, your
desktop preferences and the list of what you have installed; compresses all of
it onto a USB drive; and puts it back on the other side — translating whatever
has to be translated, and telling you honestly about whatever cannot make the
trip.

```
transmit-cli export --out /media/usb/laptop.txa --profile full --passphrase-file pw
# ... carry the drive to the other machine ...
transmit-cli import /media/usb/laptop.txa --passphrase-file pw
```

---

## Why this is not just a zip file

Copying files between operating systems is easy. Making them *mean the same
thing* on the other side is the actual problem, and it has several parts:

| Problem | What Transmit does |
| --- | --- |
| `C:\Users\me\Documents` and `/home/me/문서` are the same place with different names | Stores locations as meanings — `{DOCUMENTS}`, `{APPCONFIG}` — and resolves them on arrival |
| Firefox reads its profile from `~/.mozilla/firefox` on Linux but `%APPDATA%\Mozilla\Firefox` on Windows | Moves application state to where that program looks for it here |
| Settings files are full of absolute paths that no longer exist | Rewrites them, using per-application rules that name the exact field |
| `Notes.txt` and `notes.txt` cannot coexist on Windows | Detects the collision, renames one, and repairs references to it |
| A running browser's database is copied mid-write and arrives corrupt | Copies live databases through SQLite's online backup, which produces a consistent snapshot |
| Program binaries cannot cross an OS boundary at all | Writes you an install script for the target's package manager, and says so |
| Saved passwords are sealed by the OS keychain | Reports them as needing your attention rather than pretending they came across |

Every item is graded in a report at the end: **Full** (byte for byte),
**Adapted** (translated for this system), **Needs you** (a script to run or a
setting to click), or **Not portable** (physically cannot cross).

---

## Supported systems

| System | Status |
| --- | --- |
| Windows 10 (1809) and later | Supported |
| macOS 14 Sonoma and later | Supported |
| Linux — Debian, Ubuntu, Mint, Pop!_OS, Raspberry Pi OS | Supported |
| Linux — Fedora, RHEL and derivatives, openSUSE | Supported |
| Linux — Arch, Gentoo, NixOS, Alpine, Void, Slackware | Supported |
| ChromeOS — inside the Linux (Crostini) container | Supported |
| Windows 8.1, ReactOS | **Not supported** — Qt 6 requires Windows 10 1809 |

> **On verification.** Transmit is developed on Linux, and the Linux paths are
> exercised end to end. The Windows and macOS platform code is written against
> those systems' documented interfaces but has not yet run on them; the CI
> matrix in `.github/workflows/ci.yml` is what will establish that. Treat those
> platforms as unproven until a run goes green.

---

## What travels

Capture is organised into five kinds of thing, each of which you can include or
leave out.

**Your files** — documents, pictures, music, video, downloads, desktop.

**Application data and settings** — the whole configuration tree travels, so a
program Transmit has never heard of keeps its settings. On top of that, 73
programs are described in the shipped catalog: browsers, editors, terminals,
shells, messengers, media players, graphics tools, office suites and utilities.
For those, Transmit also knows where their state lives on *each* platform, so it
can move it to the right place; which parts are cache worth leaving behind;
which files hold paths that need correcting; and what it should warn you about.

**Desktop preferences** — appearance and accent colour, wallpaper (including
the image itself), language, formats, time zone, keyboard layouts, default
browser and mail program, sleep and screen timeouts, text scale, high contrast,
reduced motion, scroll direction, clock format and hidden files.

**The list of installed programs** — matched against the catalog and turned
into an install script for the target's package manager. Transmit writes the
script; it never runs it. Installing software is your decision and your
password.

**Saved credentials** — opt-in, and off unless you turn it on. Wireless
passphrases and credential-store entries are decrypted on the old machine, put
into the encrypted archive, and re-stored on the new one. This is refused
outright without a passphrase, and the report says plainly that the drive
contains passwords.

### What deliberately stays behind

Caches, thumbnails, trash, build output, `node_modules`, virtual environments,
virtual machine disks and swap files are excluded by default. They are large,
they are regenerable, and carrying them would cost you USB space and minutes
for nothing.

### What cannot travel at all

Program binaries. Hardware drivers. Licence activations and DRM. Anything a
machine sealed to its own TPM or Secure Enclave. Signal's message history and
Element's encryption keys, both of which are deliberately bound to one device.
These are reported, not silently dropped.

---

## The archive

One file, or several numbered parts when the destination cannot hold a large
one — a FAT32 USB stick caps files below 4 GB, which Transmit detects and
splits for automatically.

- **Compression.** Small files are concatenated into large blocks before
  compression, so the codec can find matches between them; this is what turns a
  home directory full of similar configuration files into a small archive.
  Identical files are stored once. The default is zstd at its highest level with
  a 128 MiB window, which lands within a few percent of xz while decompressing
  far faster — and restoring is the half you are waiting on.
- **Integrity.** Every block carries a BLAKE2b hash, checked on the way out.
  `transmit-cli verify` walks the whole archive.
- **Encryption.** Optional, AES-256-GCM with scrypt key derivation. The manifest
  is encrypted too, so file names are protected, not just contents. A wrong
  passphrase is rejected immediately rather than after a failed decryption.

---

## Building

Needs a C++20 compiler, CMake 3.21, Qt 6.4 or later, zstd, zlib and SQLite3.
liblzma, OpenSSL and libsecret are optional. Without OpenSSL the build cannot
read or write encrypted archives, and says so rather than falling back to
plaintext. Without libsecret it can write to the Linux login keyring but not
read it, so application passwords stay on the old machine.

```bash
# Debian, Ubuntu and derivatives
sudo apt install qt6-base-dev qt6-declarative-dev libzstd-dev liblzma-dev \
                 libssl-dev libsqlite3-dev libsecret-1-dev ninja-build

cmake --preset default
cmake --build --preset default
ctest --preset default
```

Other presets: `debug`, `release`, `cli-only` (skips the interface, for servers)
and `asan` (address and undefined-behaviour sanitisers).

The credential tests need a running secret service, which a build machine
usually has not got, so they skip themselves when there is none. To run them for
real against a throwaway keyring:

```bash
dbus-run-session -- scripts/with-keyring.sh \
    ./build/tests/integration/transmit_SecretStore_test
```

---

## Using it

### The interface

`transmit` opens a wizard in each direction. The restore side will show you
exactly what it would do — where every folder lands, what gets renamed, which
settings files would be rewritten and to what — before it touches anything.

### The command line

```bash
transmit-cli environment          # what this machine looks like to Transmit
transmit-cli drives               # where an archive could be written
transmit-cli profiles             # the built-in capture profiles

transmit-cli export --out ARCHIVE [--profile full|documents|developer]
                    [--preset fast|balanced|maximum|extreme]
                    [--split 3584M] [--passphrase-file FILE]
                    [--domains userdata,appstate,settings,apps]

transmit-cli inspect ARCHIVE      # where it came from and what is inside
transmit-cli verify ARCHIVE       # check every block against its hash
transmit-cli import ARCHIVE [--into DIR] [--dry-run] [--verify]
                    [--conflict skip|overwrite|newer|keep-both]
                    [--emulate-os windows|macos|linux]
transmit-cli rollback UNDO-POINT  # reverse a restore
```

Before a restore replaces anything, it saves what it is about to replace and
notes what it is about to add. `transmit-cli rollback` puts the first back and
removes the second, so a restore you did not want can be reversed without
re-running anything.

`--emulate-os` reports what a restore onto another system would do, from this
one. It is how the cross-platform translation is tested without three machines,
and it is worth running before a real move.

### Extending the catalog

Drop a JSON file into `~/.config/Transmit/catalog.d/` to describe a program
Transmit does not know, or to correct one it does. Same schema as
`resources/app-catalog.json`; later definitions win.

---

## How it is put together

```
src/format/     The archive: container, codecs, encryption, path tokens,
                name rules. No Qt — pure computation, tested on its own.
src/platform/   The only place operating system differences live.
src/core/       Capture and restore, recipes, path rewriting, settings.
src/app/        Controllers and models the interface binds to.
src/ui/qml/     The interface: a design system, components, pages.
src/cli/        The headless front end.
```

The layering rule is that dependencies point one way: the interface knows about
the backend, and the backend has never heard of the interface. Anything
expensive happens off the interface thread.

---

## Not built yet

Honest list of what the design covers but the code does not:

- **Volume snapshots on Windows and macOS.** Both fall back to consistent
  database copies, which covers the common case; VSS and APFS snapshots are not
  wired up.

---

## Packaging

Recipes for Debian, Fedora and openSUSE, Arch, Alpine, Void, Gentoo, NixOS,
Flatpak, a Windows installer and a macOS disk image are in `packaging/`. They
all build from a normal `cmake --install`, so none of them duplicates build
logic.

---

## Licence

GNU Affero General Public License v3.0. See `LICENSE.md`.
