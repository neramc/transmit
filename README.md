<img src="resources/icons/icon-dark.png#gh-dark-mode-only" width="670" alt="">
<img src="resources/icons/icon-light.png#gh-light-mode-only" width="670" alt="">

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

## Getting it

Built for each release on the [releases page](https://github.com/neramc/transmit/releases):

| File | For |
| --- | --- |
| `Transmit-*-linux-x86_64.AppImage` | Any modern Linux. `chmod +x` it and run it. |
| `Transmit-*-macos-arm64.dmg` | macOS 14 and later, Apple silicon. |
| `Transmit-*-windows-x64-setup.exe` | Windows 10 1809 and later. |
| `Transmit-*-windows-x64-portable.zip` | Windows, without installing anything. |

Each one carries both programs: the window, and `transmit-cli` for machines
with no display. Check what you downloaded against `SHA256SUMS` on the same
page.

The macOS and Windows builds are **not signed** — signing needs credentials a
public build has no business holding. macOS will refuse the first launch:
right-click the application and choose Open. Windows SmartScreen warns once.

Distribution packages are in `packaging/`, and building from source is
[below](#building).

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

> **On verification.** Transmit is developed on Linux. All three platforms now
> build and pass their tests in CI, which is what makes the Windows and macOS
> code more than a reading of the documentation — it caught a folder table
> built from two different homes, an Objective-C++ layer compiled without the
> reference counting it was written for, and several pieces of shared state
> that two worker threads could read at once.
>
> What each platform actually runs differs, and the suites say so rather than
> pretending otherwise. The round-trip tests build an isolated home by
> redirecting `HOME`; Windows resolves its known folders through the shell and
> takes no notice, so those suites skip there with a reason instead of running
> against the real user profile. Windows still covers the archive format, path
> tokenisation, name rules, the codecs, the interface, and the cross-OS
> translation through `--emulate-os`.

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
into the encrypted archive, and re-stored on the new one, with the attributes
the program that saved them uses to find them again — a browser login without
its realm is present in the keyring and invisible to the browser. This is
refused outright without a passphrase, and the report says plainly that the
drive contains passwords.

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
- **Integrity.** Every block carries a BLAKE2b hash and every file carries both
  a BLAKE2b hash and an MD5, all checked on the way out. After writing, the
  archive is read back off the drive - with a new reader, and with the page
  cache dropped first where the system allows it, so what is checked is what
  the drive kept rather than what is still in memory. A capture that does not
  read back correctly fails; `--no-verify-after` turns it off.
- **Carrying on.** A capture onto a stick can take twenty minutes, and until the
  last moment the archive cannot be opened: the manifest and the footer are
  written at the end. So a record of what has actually reached the drive is
  kept beside it as it goes. If the drive fills up, fails a write, or is pulled
  out, what was written stays where it is and the same command with `--resume`
  finishes it instead of starting the twenty minutes again:

  ```
  transmit-cli export --out /media/usb/laptop.txa --profile full
  # error: No space left on device
  # What was written is still on the drive. Run the same command again
  # with --resume to carry on from where it stopped.

  transmit-cli export --out /media/usb/laptop.txa --profile full --resume
  # Carried on from an interrupted capture: 42 files (820 MiB) were
  # already on the drive
  ```

  It is refused rather than guessed at. The record carries a fingerprint of the
  machine, the packaging settings and every scanned file's path, size and
  modification time, and all of it has to match: half of Tuesday and half of
  Thursday is not a capture of either. A capture you cancel yourself still
  leaves nothing behind - you asked for it to stop, not to pause - and
  `--no-journal` opts out of keeping the record at all.

  A restore keeps the same kind of record, for a reason that is about more
  than saving time. Running an interrupted restore again is safe, because a
  file that is already byte for byte what the archive holds is recognised and
  left alone. But when the folder already has a file of that name holding
  something else - the ordinary case of restoring onto a machine somebody is
  already using - `keep-both` saves the archive's copy alongside under a name
  it invents, and only the record remembers which name. Without it the second
  run meets its own earlier work, does not recognise it, and invents another:

  ```
  transmit-cli import /media/usb/laptop.txa --into ~ --conflict keep-both
  # warning: 19 file(s) could not be restored.
  # what did land has been noted: run the same command with --resume to
  # settle only what is left

  transmit-cli import /media/usb/laptop.txa --into ~ --conflict keep-both --resume
  # carried on from an interrupted restore: 22 item(s) were already in place
  ```
- **Checking it elsewhere.** A `.md5` file is written beside the archive in
  `md5sum`'s own format, so a drive can be checked with a tool that has never
  heard of Transmit:

  ```
  md5sum -c machine-20260829-1130.txa.md5
  ```

  The files inside are listed as comments, which `md5sum` skips - except on an
  encrypted archive, where listing every path beside the thing that was
  encrypted to hide them would defeat the point.
- **Repair.** When a drive damages an archive, `transmit-cli repair` reads the
  affected files off the machine they came from and writes them into
  `name.txa.repair`, which every reader picks up on its own. The damaged
  archive is never modified - its footer and part lengths are computed over the
  whole set, so writing a corrected file back into it would invalidate the
  thing being fixed - and a repair may only supply bytes that hash to what the
  archive already recorded, so a file that has changed since the capture is
  refused rather than quietly substituted.
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

Other presets: `debug`, `release`, `cli-only` (skips the interface, for servers),
`asan` (address and undefined-behaviour sanitisers) and `clang` (the same build
with clang, which warns about things gcc does not).

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

`transmit` opens a wizard in each direction.

The capture side asks which programs to close before it starts, rather than
telling you afterwards that one was open — a program holding its data while it
is read gives you a half-written copy, and being told at the end costs you the
whole wait.

The restore side shows you exactly what it would do — where every folder lands,
what gets renamed, which settings files would be rewritten and to what — before
it touches anything. When it has finished it offers you the two answers worth
having: undo it, which puts back what was replaced and removes what was added,
or keep it, which deletes the undo point and the copies of the files whose
paths were corrected. Either way nothing of Transmit's is left behind.

### The command line

```bash
transmit-cli environment          # what this machine looks like to Transmit
transmit-cli drives               # where an archive could be written
transmit-cli profiles             # the built-in capture profiles
transmit-cli apps                 # what is installed, and how much of it travels
                    [--carries-data-only]

transmit-cli export --out ARCHIVE [--profile full|documents|developer]
                    [--preset fast|balanced|maximum|extreme]
                    [--split 3584M] [--passphrase-file FILE]
                    [--domains userdata,appstate,settings,apps]

                    # which applications
                    [--apps all|none|ID,ID] [--no-app-data ID,ID] [--app-roots ID,ID]

                    # which files
                    [--max-file-size 2G] [--min-file-size 1K]
                    [--modified-since 6m] [--modified-before 2024-01-01]
                    [--include-ext txt,md] [--exclude-ext iso,vmdk]
                    [--exclude PATTERN] [--no-hidden] [--follow-symlinks]

                    # how it is packed
                    [--block-size 64M] [--workers 4] [--sync-every 32M]
                    [--no-verify-after] [--no-md5] [--no-md5-sidecar]

                    # a capture the drive interrupted
                    [--resume] [--no-journal]

                    # and all of the above, written down
                    [--selection-file FILE] [--save-selection FILE]

transmit-cli inspect ARCHIVE      # where it came from and what is inside
transmit-cli verify ARCHIVE       # check every block against its hash
                    [--deep]      # and every file against its hash and MD5
                    [--json]      # exit 0 all of it, 2 some of it, 1 none of it
transmit-cli plan   --out X       # what a capture would do, writing nothing
                                  # (takes every option export takes)
transmit-cli repair ARCHIVE       # recover the damaged files from this machine
                    [--from-report verify.json]

# any capture can be asked where its time went
transmit-cli export --out X --timings
transmit-cli import ARCHIVE [--into DIR] [--dry-run] [--verify]
                    [--conflict skip|overwrite|newer|keep-both]
                    [--emulate-os windows|macos|linux]
                    [--resume] [--no-journal]
transmit-cli rollback UNDO-POINT  # reverse a restore
```

`--save-selection` writes every choice to a JSON file and `--selection-file`
reads one back, so a capture that took some working out can be repeated next
month, or handed to somebody else, without repeating the working out. Options
given alongside `--selection-file` are applied on top of it, which is how one
file holds the settled choices while a flag varies the one thing that differs
today.

`transmit-cli apps` is where the ids for `--apps` come from, and it says which
applications' data can actually travel as opposed to only being noted as
installed. Choosing an application and leaving its data behind are separate:
`--apps all --no-app-data com.spotify.client` still records that Spotify was
there, which costs a few hundred bytes and is what lets a restore offer to
install it again.

A passphrase can come from three places. `--passphrase-file FILE` reads it from
a file; `--ask-passphrase` asks the terminal for it without showing it, which
is also what happens by itself when one is needed and none was given.
`--passphrase TEXT` exists for scripts that have nowhere else to put it, and
should be avoided: every other user on the machine can read it out of the
process list.

Ctrl-C stops a capture where it can clear up after itself, and exits 130 the
way a shell expects. It removes the part-written archive, because one that
stops half way cannot be restored from and looks exactly like a finished one
until somebody carries it to another machine and tries. A second Ctrl-C kills
the process outright, part-written file and all.

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
expensive happens off the interface thread — reading a drive that has spun
down, enumerating what is mounted, unpacking an archive to undo a restore.

Pages are built the first time they are opened rather than all at startup,
which is the difference between a window in 68 ms and one in 107 ms. Set
`TRANSMIT_STARTUP_BENCHMARK=1` with
`QT_LOGGING_RULES=transmit.performance.debug=true` to measure it yourself: the
application reports both numbers and quits once it has painted.

---

## What checks it

A tool that can turn somebody's computer into a brick has to be able to say
what evidence there is that it will not. This is that evidence.

On every push:

| | |
| --- | --- |
| Linux, Windows, macOS | The whole suite on all three. Windows and macOS cannot be built anywhere else this project is developed, and every fault this matrix has found has been a real one |
| Formatting and static analysis | clang-format, clang-tidy over every source file, QML module registration, design tokens, palette contrast |
| Sanitisers | Address, undefined behaviour and thread, the last for the compression pool and the controllers |
| Fault injection | A drive that fills, stops, writes short, reads wrong, or is pulled out mid-write. The gate is that every one of two thousand injected bit flips is detected |
| Fuzzing | Manifests, containers, serialisation and path tokens, plus every input that has ever crashed one of them |
| Application catalog | Schema, invariants, and that reading the old format still produces the same thing field for field |
| Coverage | By area, with a floor under the whole and a higher one under the lines a change adds |
| Speed | Micro and macro benchmarks against committed baselines, so a change that quietly halves the throughput fails rather than ships |

Nightly, because they are too slow to sit in front of a push: the whole suite
twenty times over, the property suites with twenty times the cases and a new
seed, fifteen minutes a fuzz target instead of one, and Windows and macOS run
twice to catch a test that depends on what the run before it left behind.

Weekly and on main: CodeQL, which follows values across files - a length read
out of an archive reaching an allocation - rather than reading one file at a
time.

Nothing is published from a commit whose checks did not pass. The release
workflow asks what each required job concluded on exactly those bytes and
refuses otherwise, naming the jobs rather than trusting the run's overall
result: a run can be green with a job cancelled, and "nothing failed" is not
the same claim as "the sanitisers passed".

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
