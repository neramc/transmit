# Crashers, kept

Every input that made a fuzz target crash is committed here, under a
directory named after the target. The replay binaries in an ordinary
build run this directory as a plain ctest test, so a bug that was found
once with clang and libFuzzer stays fixed on every machine afterwards,
including the ones that cannot fuzz.

Adding one: copy the file libFuzzer wrote (`crash-<sha1>`), give it a
name that says what it is, and commit it with the fix.
