# Contributing

## Building and testing

```bash
cmake --preset default          # RelWithDebInfo, with the tests
cmake --build --preset default
ctest --preset default
```

Other presets: `debug`, `release`, `cli-only` (skips the interface, for
servers), `asan` (address and undefined-behaviour sanitisers). The dependency
list is in the README.

Before pushing, run what CI runs:

```bash
scripts/format.sh --check          # clang-format-18
scripts/check-qml-modules.sh       # every .qml registered, both directions
ctest --preset default
```

`scripts/format.sh` without `--check` fixes the formatting instead of
reporting it. The credential tests skip themselves when no secret service is
running; to run them for real against a throwaway keyring:

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

## Security-sensitive changes

Credential handling has rules that are not negotiable: see `SECURITY.md`.
Anything touching `src/core/secrets/`, `src/platform/*SecretStore*` or
`src/format/crypto/` should be read with them in hand.

## Releasing

Bump `VERSION` in `CMakeLists.txt`, note the changes in `CHANGELOG.md`, then
push a tag that matches: `v0.1.0` for version `0.1.0`. The release workflow
refuses a tag that disagrees with `CMakeLists.txt`, builds all three platforms,
tests them, and publishes the installers with their checksums.
