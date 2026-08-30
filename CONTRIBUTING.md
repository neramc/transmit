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
```

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

## Releasing

Bump `VERSION` in `CMakeLists.txt`, note the changes in `CHANGELOG.md`, then
tag the commit `v0.1.0` for version `0.1.0` — either way round works:

```bash
git tag v0.1.0 && git push origin v0.1.0     # the workflow writes the release
```

or fill in the form under **Releases → Draft a new release**, naming the same
tag; the workflow uploads into what you wrote.

Either way it refuses a tag that disagrees with `CMakeLists.txt`, builds all
three platforms, tests them before packaging, and attaches the installers with
their checksums. **Actions → release → Run workflow** builds the same things
without publishing anything, for checking a change to the packaging itself.
