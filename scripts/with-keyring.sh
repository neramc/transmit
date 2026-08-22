#!/usr/bin/env bash
# Runs a command with a throwaway login keyring.
#
# The credential tests need a real secret service. This starts one against a
# temporary home directory so nothing touches the keyring you actually use,
# and so the test machine does not need a desktop session.
#
#   dbus-run-session -- scripts/with-keyring.sh ./build/tests/integration/transmit_SecretStore_test
#
# Skips nothing and fails loudly if the pieces are missing: the tests
# themselves already skip when there is no store to talk to.
set -euo pipefail

if [ $# -eq 0 ]; then
    echo "usage: dbus-run-session -- $0 <command> [args...]" >&2
    exit 2
fi

if ! command -v gnome-keyring-daemon >/dev/null 2>&1; then
    echo "gnome-keyring-daemon is not installed" >&2
    exit 1
fi
if ! command -v secret-tool >/dev/null 2>&1; then
    echo "secret-tool is not installed (apt install libsecret-tools)" >&2
    exit 1
fi
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    echo "no session bus - run this under dbus-run-session" >&2
    exit 1
fi

keyring_home="$(mktemp -d)"
trap 'rm -rf "$keyring_home"' EXIT

export HOME="$keyring_home"
export XDG_DATA_HOME="$keyring_home/.local/share"

# The password is for a keyring that exists for the length of this command.
eval "$(printf 'transmit-test\n' | gnome-keyring-daemon --unlock --components=secrets 2>/dev/null)"
export GNOME_KEYRING_CONTROL

# --unlock does not create the default collection, and in a home directory
# this new there is none to unlock. Anything asking for the default collection
# then gets "no object at /org/freedesktop/secrets/collection/login", and the
# daemon tries to raise a graphical prompt that cannot appear. Storing one
# entry is what brings the collection into existence.
printf 'bootstrap' | secret-tool store --label='transmit-test bootstrap' \
    transmit-test bootstrap >/dev/null 2>&1 || {
        echo "could not create the default keyring collection" >&2
        exit 1
    }

"$@"
