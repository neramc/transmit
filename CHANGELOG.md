# Changelog

Notable changes, newest first. Versions follow [semantic
versioning](https://semver.org): while the major version is 0 the archive
format may still change between minor versions, and when it does, the release
notes say so.

## Unreleased

### Fixed

- The Linux AppImage aborted on start on a Wayland desktop. The bundle was
  built without `plugins/wayland-graphics-integration-client`, so Qt could not
  give the window an OpenGL context, and Qt Quick's response to that is to end
  the process. The plugin is now carried, and the release build refuses to
  publish a bundle that is missing it.
- Transmit no longer dies when there is no graphics backend at all. It asks
  before it draws, falls back to rendering in software, and says so — a remote
  desktop, a virtual machine without OpenGL, or a broken driver now costs
  speed rather than the whole program.

### Testing

- The interface is now launched for real, on a real X server and a real Wayland
  compositor, and has to paint a frame. Every interface check before this ran
  under the offscreen platform, which loads no platform plugin and builds no
  graphics context — which is why a build that could not start on Wayland
  passed everything. The packaged bundles on all three systems are started too.

## 0.1.0

First release. Everything below is new.

### Capturing and restoring

- Captures user files, application data and settings, desktop preferences, the
  list of installed programs, and — only when asked — saved credentials.
- Restores onto a different operating system, resolving locations by meaning
  (`{DOCUMENTS}`, `{APPCONFIG}`) rather than by path.
- Moves application state to where each program looks for it on the target
  system, for the 73 programs in the shipped catalog, and rewrites the absolute
  paths inside their settings files field by field rather than by search and
  replace. Users can extend the catalog from `~/.config/Transmit/catalog.d/`.
- Detects name collisions a target filesystem cannot hold (`Notes.txt` beside
  `notes.txt`), renames one, and repairs references to it.
- Copies live SQLite databases through the online backup API, so a browser that
  was open does not arrive corrupt.
- Grades every item Full, Adapted, Needs you, or Not portable, and reports it.
- Writes an install script for the target's package manager, and a script for
  the settings a program is not allowed to change. It never runs either.
- A restore can be undone: what it replaced goes back, what it added is
  removed.

### Choosing what travels

- Per-folder selection: take Documents but not four hundred gigabytes of video.
  Each folder is listed with its size and file count, measured in one walk, so
  the choice is made knowing what it costs. `--folders documents,pictures` on
  the command line.
- Per-application selection, with each program marked according to whether its
  data can travel or only its name can be recorded for the reinstall script.
- Scope limits: maximum and minimum file size, modified-since and
  modified-before, extensions to include or leave behind, exclusion patterns,
  hidden files and symbolic links. Every one of them reachable from both the
  window and the command line.
- A selection can be saved to a file and replayed, so the same capture can be
  repeated or scripted.

### The archive

- Format version 2. The footer commits to the whole 32-byte hash of the
  manifest rather than its first eight bytes; each block header carries sixteen
  bytes of its own hash rather than twelve; and each part carries a checksum of
  the bytes it holds, so a damaged drive is found in one sequential pass rather
  than by decompressing everything. Version 1 archives are still read, verified
  and restored — a real one is committed to the test suite and opened on every
  build.
- Solid blocks with content deduplication, BLAKE2b integrity hashes, and
  multi-volume splitting for filesystems that cannot hold a large file.
- zstd, xz, deflate and store codecs; zstd level 22 with a 128 MiB window is
  the default.
- Optional AES-256-GCM encryption with scrypt key derivation, covering the
  manifest as well as the contents.
- An MD5 for every file, in the manifest and in a `.md5` file beside the
  archive in `md5sum`'s own format, so a drive can be checked with a tool that
  has never heard of Transmit.
- Written to the drive with real device syncs, and read back afterwards with a
  new reader and the page cache dropped where the system allows it — so what is
  checked is what the drive kept, not what is still in memory.

### Interrupted transfers

- A capture that is interrupted — a full drive, a failed write, a stick pulled
  out — leaves what it wrote and a record of it, and `--resume` finishes it
  instead of starting again.
- A restore does the same, and for a reason beyond speed: when the destination
  already holds files of the same name, "keep both" saves the archive's copy
  under a name it invents, and only the record remembers which. Without it a
  second run invents another and you get two copies of one file.
- Both are refused rather than guessed at when the machine, the settings or the
  files have changed since.

### The programs

- `transmit`, a wizard in each direction, which shows a restore in full before
  it touches anything.
- `transmit-cli` for machines with no display, including `--emulate-os` to
  report what a restore onto another system would do from this one. It asks
  the terminal for a passphrase rather than taking one on the command line
  where anyone can read it, and Ctrl-C stops a capture where it can still
  clear up after itself.
- `verify --deep` checks every part, block and file and says which files a
  damaged drive cost; `repair` rewrites the affected ones from the machine they
  came from without touching the original archive.
- Safe removal: the window offers to eject the drive when a capture finishes,
  rather than leaving somebody to pull out a stick with pages still unwritten.

### What checks it

- Every push builds and tests on Linux, Windows and macOS, under the address,
  undefined-behaviour and thread sanitisers, with clang-tidy over every source
  file, fuzzing, fault injection — the gate is that all two thousand injected
  bit flips are detected — coverage floors on the whole and on the lines a
  change adds, and benchmarks against committed baselines.
- Nightly: the whole suite twenty times over, the property suites with twenty
  times the cases, longer fuzzing, and Windows and macOS run twice.
- Nothing is published from a commit whose checks did not pass: the release
  workflow asks what each required job concluded on exactly those bytes.

### The window

- Built to the design specification: a collapsing sidebar, a command palette,
  toasts, inline messages, empty and error states, and a type and spacing scale
  used through tokens rather than typed into each page — enforced by a linter
  that fails a build for a colour or a margin written as a number.
- Checked for layout faults automatically at six resolutions, in light and dark:
  nothing overflowing its parent, no siblings overlapping, no text silently
  truncated, every control reachable by keyboard and large enough to hit.

### Speed

- Every stage of a capture and a restore is timed and reported, so "it was
  slow" becomes "three and a half minutes of it were hashing".
- Sort keys computed once instead of per comparison, a real LRU block cache,
  fewer copies through the compression pipeline, cached user and group lookups,
  and worker budgets set from the machine rather than guessed.
- Benchmarks with committed baselines fail the build on a regression, including
  one on archive size, so compression cannot quietly get weaker.

### Packaging

- AppImage, macOS disk image, Windows installer and portable archive, built and
  published for each release.
- Recipes for Debian, Fedora, openSUSE, Arch, Alpine, Void, Gentoo, NixOS and
  Flatpak.
