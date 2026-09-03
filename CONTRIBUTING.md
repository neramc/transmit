# Contributing

## Building and testing

```bash
cmake --preset default          # RelWithDebInfo, with the tests
cmake --build --preset default
ctest --preset default
```

Other presets: `debug`, `release`, `cli-only` (skips the interface, for
servers), `asan` (address and undefined-behaviour sanitisers) and `clang`. The
dependency list is in the README.

Before pushing, run what CI runs:

```bash
scripts/format.sh --check          # clang-format-18
scripts/check-qml-modules.sh       # every .qml registered, both directions
scripts/check-design-tokens.sh     # no colours or spacing typed into a page
python3 scripts/check-contrast.py  # the palette is readable in both schemes
ctest --preset default
cmake --build --preset clang       # the warnings gcc has not got
```

The last one matters more than it looks. Linux builds with gcc and macOS with
clang, and they do not warn about the same things - `-Wunused-private-field` is
clang's alone - so a clean gcc build is not evidence of a clean macOS one. The
`clang` preset builds the same tree with the same `-Werror`, and finds those
here rather than eight minutes into a runner.

### Build against the Qt the runners use

CI builds against **Qt 6.8.1**. If your distribution's Qt is older, a clean
build here is not evidence of a clean build there, and the two ways that has
gone wrong were both invisible locally and fatal on every job:

- `QTRY_VERIFY` takes an `int` timeout in Qt 6.4 and `std::chrono::milliseconds`
  in 6.8, where the macro's own expansion narrows a `long` to an `int`. With
  `-Werror` that is a build failure in a macro you did not write.
- Qt's ahead-of-time QML compiler writes string literals straight into generated
  C++. 6.8 compiles bindings 6.4 leaves alone, so a literal that is fine here
  can produce generated C++ that does not parse there.

Neither is a compiler difference - gcc 13 and 14 both accept them - so nothing
but the right Qt finds them. It is one command:

```bash
pip install aqtinstall
python3 -m aqt install-qt linux desktop 6.8.1 linux_gcc_64 -m qtimageformats -O /opt/Qt

cmake -S . -B build-qt68 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.1/gcc_64 \
      -DTRANSMIT_WERROR=ON -DTRANSMIT_BUILD_TESTS=ON
cmake --build build-qt68 && ctest --test-dir build-qt68
```

Worth doing before any push that touches QML or a test macro.

`scripts/format.sh` without `--check` fixes the formatting instead of
reporting it.

For a change of any size, it is worth also asking what the suite did not run:

```bash
scripts/coverage.sh --base main    # the lines this branch adds, and which are untested
```

It builds instrumented, runs everything, prints coverage by area, and then -
the useful part - names the lines the branch adds that no test reached. CI runs
the same script and requires 90% of new lines to be covered, which is a
standard nobody could retrofit across fourteen thousand lines and everybody can
meet on the thirty they just wrote. Twice already it has pointed at a claim the
tests were not actually checking: that the restore fingerprint distinguishes
one set of domains from another, and that an unreadable record is refused
rather than ignored.

The suite is grouped by label, so a slice can be run on its own:

```bash
ctest --preset default -L format         # the archive format, no Qt
ctest --preset default -L property       # randomised round trips
ctest --preset default -L fault          # deliberate breakage - see below
ctest --preset default -L integration    # capture and restore end to end
ctest --preset default -L launch         # the interface, actually started
```

`-L launch` is the only slice that runs the built executable rather than
linking its pieces into a test process. It starts the interface under the
offscreen platform, under a real X server, and under a real Wayland
compositor, and each one has to paint a frame before the test passes. It needs
two programs that are not build dependencies:

```bash
sudo apt-get install -y xvfb weston
```

Without them the cases skip themselves, which is right on a machine that has
no use for them and wrong on a build machine - so CI sets
`TRANSMIT_LAUNCH_TESTS_REQUIRED=1`, which turns a skip into a failure.

These exist because version 0.1.0 shipped an AppImage that aborted the instant
it was started on a Wayland desktop, with the whole suite green. Everything
that touched the interface ran under the offscreen platform, which loads no
platform plugin, builds no graphics context and never involves the scene graph
- so the one thing that was broken was the one thing nothing could see. The
fourth case, `Launch.wayland-no-buffers`, takes the graphics plugin away
deliberately and requires the interface to fall back to drawing in software
instead of dying.

The application catalog is data, and is checked as data:

```bash
python3 -m pip install jsonschema
python3 -c "import json,jsonschema; jsonschema.validate(
    json.load(open('resources/app-catalog.json')),
    json.load(open('resources/app-catalog.schema.json')))"
scripts/migrate-catalog.py --check resources/app-catalog.json
./build/tests/integration/transmit_Catalog_test
```

`scripts/enrich-catalog.py` holds the hand-written half - what is inside each
application's state folder and what has to happen to it when it moves. Adding
an application there and re-running both scripts is the way to extend it; only
put in what is actually known, because an entry guessed at says with the same
confidence as everything else that a file can be dropped when it cannot.

`-L fault` is the one to run after touching anything that reads or writes a
file. It fills up a disk part way through a write, pulls a stick out mid-read,
makes a device store one byte fewer than it was given, cuts an archive off at
every percentage point, and flips a single bit in each of two thousand places
in a finished archive. The standard is not that Transmit succeeds - most of
these cannot succeed - but that it never reports success over data that is not
what went in. The bit-flip case asserts that all two thousand are caught; if a
format change makes some of them land where nothing checks, loosen it and
write the reason next to the new number.

The credential tests skip themselves when no secret service is running; to run
them for real against a throwaway keyring:

```bash
dbus-run-session -- scripts/with-keyring.sh \
    ./build/tests/integration/transmit_SecretStore_test
```

## Where things go

```
src/format/     The archive format. No Qt: pure computation, tested alone.
src/platform/   The only place operating system differences live.
src/core/       Capture and restore, recipes, path rewriting, settings.
src/app/        Controllers and models the interface binds to.
src/ui/qml/     The interface.
src/cli/        The headless front end.
```

Dependencies point one way. The interface knows about the backend; the backend
has never heard of the interface. A platform difference that leaks out of
`src/platform/` is a bug even when it works.

Anything that can take time runs off the interface thread — reading a drive
that has spun down, enumerating what is mounted, unpacking an archive.

## Style

- C++20. Errors are `Result<T>`, not exceptions; `TRANSMIT_TRY` and
  `TRANSMIT_CHECK` are how they propagate.
- Comments say **why**, not what. If a line needs explaining, explain the
  reason it is that way — the code already says what it does.
- Names read as English. `applicationsToClose`, not `getAppList`.
- New behaviour comes with a test. A bug fix comes with the test that would
  have caught it.
- Anything user-visible goes through `QCoreApplication::translate` or `tr`.

## Adding a program to the catalog

`resources/app-catalog.json` describes where each program keeps its state on
each platform, what is cache, and which files hold paths that need correcting.
Adding one is usually a data change with no code behind it. Test it with a
sample tree and `transmit-cli import --dry-run`, which prints every rewrite it
would make before making any of them.

Users can override or extend the catalog from
`~/.config/Transmit/catalog.d/` without rebuilding.

## Icons

`resources/icons/transmit-512.png` is the master. Everything else the build
uses - the theme sizes, the `.icns` for the macOS bundle - is generated from it
by `scripts/make-icons.sh` and committed, so building Transmit needs no image
tooling. Change the artwork, run the script, commit what it writes.

The Windows `.ico` and the two README banners are drawn separately and are not
generated.

## Security-sensitive changes

Credential handling has rules that are not negotiable: see `SECURITY.md`.
Anything touching `src/core/secrets/`, `src/platform/*SecretStore*` or
`src/format/crypto/` should be read with them in hand.

## Updates

Transmit can replace itself, which makes the update path the most dangerous
code in the project: everything else can lose a copy of somebody's files, and
this can run somebody else's program on their machine. Three rules hold it
down, and none of them is a preference.

**Nothing is installed that cannot be authenticated.** The feed is signed with
an Ed25519 key and the public half is compiled into the build. A build given no
key still checks, still says a new version exists, and downloads nothing. That
is the default, and a project that has not set up signing gets exactly half the
feature working rather than a broken one.

**Nothing replaces a copy something else owns.** A Flatpak, a Snap, anything
under `/usr`, a Homebrew cask - the updater says there is a new version and
stops. The packaging recipes also build with `-DTRANSMIT_WITH_UPDATER=OFF`, so
in those builds the code that could replace a program is not there at all.

**What is downloaded is checked twice.** Once when it arrives, against the
BLAKE2b digest in the signed feed, read back off the disk rather than hashed on
the way past. Again at the moment it is used, because between those two moments
it is a file in a cache directory that anything running as that user could
write to.

Setting it up:

```bash
scripts/make-update-key.sh transmit-update-key.pem
```

Put the private key in the repository's `UPDATE_SIGNING_KEY` secret and build
with the printed public key in `TRANSMIT_UPDATE_KEYS`. Several keys separated
by `;` are allowed, which is how a key is rotated: ship a build that trusts
both, then start signing with the new one.

### Hotfixes

A release marked **critical** is installed without waiting to be asked. Use it
for something that loses data or lets somebody in, and not for anything else -
the setting a person chose covers features and ordinary fixes, and spending it
on a convenience is how people turn updates off.

Before tagging, put the version in `packaging/release.json`:

```json
{
  "version": "0.2.1",
  "severity": "critical",
  "unsafeBelow": "0.2.1"
}
```

The version is named there so the setting cannot outlive the release it was
written for: the next ordinary release finds a version that is not its own and
publishes as normal. `unsafeBelow` narrows it to the versions actually exposed,
so somebody already past the problem is offered the update rather than given
it. Leave it out and everything below the release is treated as exposed.

Then tag as usual. Copies that can replace themselves take it on their next
check; copies that cannot say so loudly and link to the release page. It still
goes nowhere without the signature.

## Releasing

The tag decides the version. Note the changes in `CHANGELOG.md` and tag the
commit:

```bash
git tag v0.2.0 && git push origin v0.2.0     # the workflow writes the release
```

or fill in the form under **Releases → Draft a new release**, naming the same
tag; the workflow uploads into what you wrote.

Every build job rewrites the tree to the tag's version before it compiles
anything, so the binary, the installer, the disk image and the seven packaging
recipes cannot report different versions from each other or from the tag. It
used to refuse a tag that disagreed with `CMakeLists.txt`, which is a true
statement made at the worst possible moment - at the end of a release, about
files that should have been edited at the beginning of one.

Between releases the direction is reversed: `CMakeLists.txt` is what the rest
of the tree has to agree with, and CI says so.

```bash
scripts/version.py --print        # what the tree declares
scripts/version.py --check        # every file agrees, or exactly where not
scripts/version.py --set 0.2.0    # rewrite all nine of them
```

Add a place to `PLACES` in that script when a new file starts carrying the
version. It insists each pattern still matches, so a file that changes shape
fails the check rather than quietly dropping out of it.

The release builds all three platforms, tests them before packaging, starts
each packaged bundle to be sure it opens a window, and attaches the installers
with their checksums. **Actions → release → Run workflow** builds the same
things without publishing anything, for checking a change to the packaging
itself.
