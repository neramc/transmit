# Changelog

Notable changes, newest first. Versions follow [semantic
versioning](https://semver.org): while the major version is 0 the archive
format may still change between minor versions, and when it does, the release
notes say so.

## Unreleased

Nothing yet.

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

### The archive

- Solid blocks with content deduplication, BLAKE2b integrity hashes, and
  multi-volume splitting for filesystems that cannot hold a large file.
- zstd, xz, deflate and store codecs; zstd level 22 with a 128 MiB window is
  the default.
- Optional AES-256-GCM encryption with scrypt key derivation, covering the
  manifest as well as the contents.

### The programs

- `transmit`, a wizard in each direction, which shows a restore in full before
  it touches anything.
- `transmit-cli` for machines with no display, including `--emulate-os` to
  report what a restore onto another system would do from this one. It asks
  the terminal for a passphrase rather than taking one on the command line
  where anyone can read it, and Ctrl-C stops a capture where it can still
  clear up after itself.

### Packaging

- AppImage, macOS disk image, Windows installer and portable archive, built and
  published for each release.
- Recipes for Debian, Fedora, openSUSE, Arch, Alpine, Void, Gentoo, NixOS and
  Flatpak.
