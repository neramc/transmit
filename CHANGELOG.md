# Changelog

Notable changes, newest first. Versions follow [semantic
versioning](https://semver.org): while the major version is 0 the archive
format may still change between minor versions, and when it does, the release
notes say so.

## Unreleased

### Added

- The interface can be in another language. Translations are built into the
  binary rather than shipped beside it, the language follows the machine unless
  a setting says otherwise, and changing it takes effect immediately rather
  than on the next start. Qt's own strings — the buttons in a dialog, the names
  of the standard folders — are loaded too, so a window is not half translated.
  Korean is complete for the shell, the home page and settings; the rest falls
  back to English rather than being guessed at.
- The settings that belong to the machine rather than to the person now travel
  too: its name, the entries somebody added to its hosts file, the server it
  takes the time from, whether the firewall is on and whether it accepts SSH.
  Every one is read where that system keeps it and written into the script a
  restore leaves behind — none is applied, because none can be without rights
  Transmit does not ask for. Where each system keeps them is a table rather
  than three implementations of the same list, which is what lets somebody
  correct the Windows entry without a Windows machine.
- The interface takes its measurements from the desktop it is running on.
  Corner radii, control and row heights, and the order of the buttons in a
  dialog now follow Windows 11, Windows 10, macOS, GNOME, KDE Plasma, Xfce or
  COSMIC — detected, and overridable in settings. The accent follows the
  desktop's own where it can be read without starting a process (Windows and
  Plasma); elsewhere the brand colour is used, because an accent guessed wrong
  is worse than one chosen on purpose.
- Two names for one file arrive as two names for one file. A hard link is not a
  copy: writing through either name changes what the other one sees, so a
  restore that made two independent files out of them changed the data and not
  only how much room it took - and the room matters too, since a package store
  or a backup tree can be almost entirely links. The scan notices a file with
  more than one name from the stat it was already doing on Linux and macOS, and
  from the file's own record on Windows; the capture puts them in a group named
  after the first of them; the restore writes the first and makes the rest
  names for it. Anything that refuses - a destination on another volume, a
  filesystem with no hard links - gets the copy, which is what happened before.
  The archive still holds the contents once, as it did, so an older reader
  ignores the new field and writes each name separately.

- A file that is mostly hole arrives as one. A disk image, a virtual machine or
  a database that has been emptied has a length far larger than the data in it,
  and the machine it came from was not storing those zeroes. Writing them out
  is how a restore of three gigabytes needs forty gigabytes of disk and fails
  at the very end, after everything else has already landed. The restore now
  skips runs of zeroes of 64 KiB and over in any file of a megabyte or more,
  seeking over them, and then asking for the gaps back. Linux leaves a hole
  behind a write that lands past the end of a file, and so does NTFS once the
  file has been marked sparse - which it has to be asked for while the file is
  still empty. APFS fills the gap in, so on macOS the zeroes are handed back
  afterwards instead, which is the same idea under a third name: fallocate,
  F_PUNCHHOLE, FSCTL_SET_ZERO_DATA. Every refusal along the way is a success -
  FAT32 on a stick has no holes to give and writes the zeroes, which reads back
  the same - because failing a restore over how much room the right bytes take
  would be the wrong trade. The length is set explicitly at the end, or a file
  that ends in zeroes would arrive short by exactly that run.

- The tags a filesystem keeps beside a file now travel with it: the colour
  label somebody set, the Finder tag, the comment a file manager wrote. Every
  system Transmit runs on has them and the archive had nowhere to put one, so
  none of them ever arrived. What travels is an allow-list - `user.*` and
  macOS's tag attributes - because two kinds of extended attribute must not:
  `security.capability` grants a binary powers the kernel then honours, so an
  archive able to set it would be a way to hand out privilege by restoring a
  file, and `com.apple.quarantine` is the mark that says "this came from the
  internet", which would arrive on documents that never did.
- Files that are not really on the machine are listed rather than downloaded.
  OneDrive keeps a file's name, size and modification time on disk with the
  contents on its own servers, and iCloud Drive replaces an evicted
  `report.pdf` with a stub called `.report.pdf.icloud`. Reading either one
  fetches it, so a capture of a home directory full of them was a
  several-hundred-gigabyte download nobody agreed to. They are now counted and
  reported - with the size, because that is the number that decides anything -
  and `--fetch-cloud-files`, or the box beside the file limits, asks for them.
- What a file is on Windows travels with it. The manifest has had a place for
  the attribute word since the format was written and nothing ever filled it
  in, so a hidden file arrived visible and a read-only one writable. Only the
  bits that say what the file is for are put back; the ones describing how the
  old disk stored it - compressed, a reparse point, still in the cloud - are
  left behind, because on the new machine they would be untrue.
- Both programs now carry a Windows application manifest, so long paths work at
  all. Without it every call in the process is capped at 260 characters
  whatever the machine is configured to allow, and a restore of a tree that
  came off a Linux or macOS home directory failed part way down with what
  looked like a permission error.
- Transmit now says what the system will not let it read, before the capture
  rather than after. macOS refuses Mail, Messages, Safari and the address book
  to a program without Full Disk Access and gives no error when it does - the
  archive is simply short. A Flatpak or Snap build sees only what its sandbox
  was given. Windows will not hand a shadow copy to a process that is not
  elevated. Each is probed rather than assumed, so a machine that has already
  granted what is needed is not nagged about it.
- The catalog now describes 192 programs rather than 73. The new entries are
  mostly the things a machine somebody actually uses has on it and the old list
  did not: game launchers and stores, emulators, and the save folders of games
  themselves — worlds, characters, mods and settings, with the downloaded game
  files marked as what they are so they are left behind rather than carried.
  Four more Firefox forks and Opera join the browser families, and the rest
  fills in the gaps: build tools and cloud command lines (whose credential
  files are named as credentials), more editors, players, cameras and
  note-takers.
- A game bought through a store is in no package manager's list, so it is found
  by its save folder instead. That path already existed; it only ever looked at
  the first place a recipe named, so anyone whose copy was the Flatpak had it
  treated as absent. Every candidate is tried now.
- A `.deb` and a `.rpm`, built by CPack from the same install rules as
  everything else, published beside the AppImage. Both are built against the
  distribution's Qt rather than carrying one, and both leave the updater out:
  a copy `dpkg` or `rpm` owns is theirs to replace.

### Fixed

- A file could be written outside the folder a restore was pointed at. A name
  is refused if it is `..`, and then cut to fit the target filesystem's length
  limit — in that order, so a name three hundred bytes long that began `..`
  passed the check and became `..` when it was cut. The rules now run again
  after the cut, and `.` is refused alongside `..`: it names the folder it is
  already in, so two entries arrive as one file and the second overwrites the
  first without a word. Found by the fuzzer; the input it found it with is
  committed, and replays on every build from now on, fuzzer or no fuzzer.
- The scripts a restore leaves behind are written byte for byte. All three were
  opened in text mode, which on Windows turns every newline into a carriage
  return and a newline — and a shell script with carriage returns in it does
  not run: `sh` reads the `\r` as part of the interpreter's name and reports
  that `/bin/sh` is not there. Which script gets written is decided by the
  operating system and how it got written was decided by Qt's idea of the
  operating system, which is two decisions where there should be one.
- Undoing a rewritten settings file tries harder, and says where the file is
  when it cannot. Putting an original back has to clear what is in its place
  first, so if the move then fails the only copy is under a name the person has
  never heard of - and the message did not say that name. It now tries a copy
  where the move failed, which works across a filesystem boundary and where
  something holds the name open, and if neither works it says where the
  original still is.

- A program name out of an archive can no longer put a command into the install
  script. Three places pasted a name or a package identifier into the generated
  script without quoting it: the message printed when flatpak is missing, the
  list of programs to install by hand in the shell script, and the same list in
  the PowerShell one. A name carrying an apostrophe closed the string it was
  sitting in and everything after it was a line of a script; a name carrying a
  newline ended the comment it was sitting in and did the same. Every name and
  identifier is quoted now - `'\''` for the shell, a doubled quote for
  PowerShell - and a name that goes into a comment has its line breaks taken
  out. All of them come out of the archive's application list, which is the one
  thing in the room somebody else may have written, and the script is a file a
  person reads and then runs having just typed their password.

- A NixOS machine no longer reports packages that are not packages. `nix
  profile list` prints several lines for each one, and the rule read the last
  word of every line - so an index of 0 became a package called "0", and the
  list of things to reinstall filled up with pieces of the listing. It now
  takes the store path, which is the one thing on those lines that names
  something, and which the older one-line-per-package format ends with too.

- A size too large to hold is refused rather than wrapped round. `--max-file-size
  18446744073709551G` used to come back as a limit of about fourteen exabytes,
  because the multiplication overflowed with nothing to notice it by: not what
  anybody asked for, and it does not look wrong anywhere afterwards.

- A date off the end of the calendar is refused rather than guessed at.
  `--modified-since 400000000w` multiplied the count by seven inside an int,
  which is undefined behaviour reached by typing a number, and
  `--modified-since 2000000000y` came back as the first of January 1970 - a
  date, before now, and nothing to do with what was asked for. Both are
  refused, and the counting is done in 64 bits.

- A report that names a script for the networks needing administrator rights
  now names the networks too. It said where the commands were and left the one
  question somebody actually has - which networks? - answerable only by opening
  the file.

- The coverage gate no longer fails a build because a commit was amended. Its
  changed-line check diffs against the commit the push event names as
  "before", and after a force-push that commit is orphaned on the server and
  never arrives however deep the fetch - so the diff could not be taken and
  the job failed, which reads like untested code rather than like a missing
  commit. The guard meant to catch this used `git rev-parse --verify`, which
  given forty hex digits parses them and hands them back whether or not the
  object exists, so it said yes to precisely the case it was written for. Both
  ends now check with `git cat-file -e`, a base that cannot be diffed against
  is a stated skip of that one check rather than a failure, and the overall
  floor still decides the exit status. The rules themselves are now a test:
  `Coverage.rules` builds a repository, orphans a commit in it, and asks.

- The .rpm was working and the check said it was not. The two lines that prove
  a launch reached a painted window went through a logging category, so whether
  the proof appeared depended on the machine's logging configuration rather
  than on the program: on Fedora the package installed, the window was built, a
  frame was painted and the process exited cleanly - and printed nothing, which
  the check read as "it never started". Reproduced exactly here by silencing
  the categories, and fixed by saying those two lines on standard output when
  the benchmark is asked for, where nothing can filter them.

- A rule set for the whole capture is no longer cancelled by a folder that says
  nothing. Each root carries a scope of its own and the stricter of the two was
  taken, which is right for a flag whose default is permissive and wrong for
  one whose default is already the strict answer: every root that was never
  given a rule carried a default-built one saying no, so `--follow-symlinks`
  did nothing whatsoever on a user folder and said nothing about it. A root can
  still narrow what is taken by size, date, type or pattern; it cannot overrule
  a policy set for the whole capture.


- Restoring into a chosen folder now keeps everything inside it. Files an
  archive gave an absolute path for were the exception: the folder table was
  built with every location rooted at the destination except that one, and the
  code that resolved it answered before it ever looked at the table. A restore
  could therefore write outside the folder somebody pointed at, on the word of
  the archive. Such a path was also written relative to wherever the program
  had been started, so where a file landed depended on how it was launched.

### Testing

- A fuzzer that crashes says which property it broke. Both of the path
  fuzzer's properties ended in a bare `abort()`, and an optimising build folds
  two identical calls into one — so the stack trace named whichever line the
  compiler kept, and the crash could not say whether a path had climbed out of
  its folder or merely failed to resolve. It now prints the property, the input
  and what came out, before it stops.
- A suite that dies is asked what happened, whichever suite it is. When ctest
  reports a failure it loses the test's own output, so the run asks QtTest to
  write its report to a file instead — but only for six suites named in the
  script. `InstallScript` was the four hundred and sixty-second test, was not
  among the six, and arrived from Windows as a name, a number and nothing at
  all. The list is now the failures the run just had, and a suite that wrote no
  report at all is run again in the open, because that is what a crash before
  the first line looks like.
- A security suite, checking the properties SECURITY.md states rather than
  leaving them as prose: every shape of path an archive could use to get out of
  the folder it was pointed at, on both path styles, and the refusals the
  updater is supposed to make.
- The binaries are checked for the mitigations the build asks for — position
  independence, read-only relocations, a non-executable stack, stack guards,
  fortified calls — read back off the finished files rather than assumed from
  the flags. Those flags are now asked for explicitly instead of inherited from
  whichever distribution happened to build it.
- Every page is laid out again in each of the eight desktop profiles at the
  tightest window size, because a taller control and a wider corner are only a
  problem where there was no room to begin with. And each profile's
  measurements are read back out of the design system after being asked for,
  so a table that stopped being consulted is a failure rather than a look
  nobody notices is missing.
- The rules that differ per operating system are now run against a fixture on
  whatever machine happens to be building, rather than only on the one that has
  the thing they are about: which evidence says a file is kept online is a
  parameter, so the macOS rule is exercised on a Linux runner and the Windows
  one everywhere. Each of the three - the attribute bits, the name of an
  evicted iCloud file, and the policy merge - was shown to fail when broken on
  purpose.
- A finding about what the system will not let Transmit read has to reach the
  report. Each of the three real ones needs a machine in a particular state - a
  macOS without Full Disk Access, a build inside a Flatpak, a Windows process
  that is not elevated - so the finding is the platform's job and the fake
  platform can now put one in the way, which is what makes the half that runs
  everywhere testable anywhere. Data that will not be there is graded
  differently from a copy that is merely less careful, and both were shown to
  fail when broken.
- The rule that decides what a single folder may change about the whole
  capture's scope is a named function with a suite of its own now, because it
  is the one that was wrong: nine cases covering the size limits at both ends,
  the dates, the intersection of file types, hidden files and the two policy
  flags a folder may not overrule, each shown to fail when the merge is broken.
- Five more things the catalog has to be true about, each shown to fail when
  broken on purpose: a file inside a state folder cannot be written as an
  absolute path, no state root names the same place twice, anything whose role
  is "credentials" is marked sensitive, every recipe can be found by one of the
  two means there are, and a sandboxed install is found even when it is not the
  first place the recipe names.
- Nothing that looks like a credential can be committed.
- Every string a person reads has to go through a translation, and every
  translation has to carry the same placeholders as its source — `"%1 of %2"`
  translated as `"%2 of %1"` reports the opposite of what happened and nothing
  else would catch it. The catalogues are also checked against the source on
  every commit, because `lupdate` is what keeps them together and nothing runs
  it on its own.
- Every release check now starts what people actually download. Linux runs the
  `.AppImage` itself rather than the program unpacked out of it; macOS attaches
  the disk image, copies the bundle out and starts that; Windows installs the
  installer and starts the program from where it put itself, then unpacks the
  portable archive and starts that too. Each also asks the packaged command
  line tool its version and fails if it is not the tag's.
- The two Linux packages are built, installed and started on every commit, not
  only at release: the `.deb` on the runner, the `.rpm` in a Fedora container.

## 0.1.1 - 2026-09-03

### Added

- Transmit can update itself. It asks a signed feed what has been published,
  checks what it downloads against the digest in that feed, and replaces the
  running copy through two renames so there is never a half-written program on
  disk. `transmit-cli update` does the same from a terminal, and the settings
  page holds the choice between being told, being updated, and being left
  alone.
- A release can be marked **critical**, and one is installed without waiting to
  be asked — the setting covers features and ordinary fixes, not a hole
  somebody could be walking through. It still only happens when the feed's
  signature checks out against a key the build was compiled to trust, and never
  to a copy a package manager owns.
- A build given no signing key gets an updater that reports a new version and
  installs nothing. That is the safe half working on its own: nothing is ever
  installed that could not be authenticated.

### Changed

- Every packaging recipe — Flatpak, Arch, Alpine, RPM, Void, Gentoo, Nix —
  builds with `-DTRANSMIT_WITH_UPDATER=OFF`. Those copies belong to a package
  manager, and the code that could replace a program is left out of the builds
  that must never replace one.
- The release takes its version from the tag and rewrites the tree to match
  before compiling anything, instead of refusing a tag that disagreed with
  `CMakeLists.txt`. Nine files carry a version; one of them is the tag.

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

## 0.1.0 - 2026-08-30

First release. Everything below is new.

### Capturing and restoring

- Captures user files, application data and settings, desktop preferences, the
  list of installed programs, and — only when asked — saved credentials.
- Restores onto a different operating system, resolving locations by meaning
  (`{DOCUMENTS}`, `{APPCONFIG}`) rather than by path.
- Moves application state to where each program looks for it on the target
  system, for the programs in the shipped catalog, and rewrites the absolute
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
