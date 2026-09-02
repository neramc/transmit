#!/usr/bin/env bash
#
# Makes the key pair that lets Transmit install updates.
#
# Until a build is given a public key it will report that a new version exists
# and download nothing, which is the safe half of the updater working on its
# own. This is the other half.
#
#   scripts/make-update-key.sh transmit-update-key.pem
#
# It prints the public key. Two things then have to happen:
#
#   1. Put the private key in the repository's UPDATE_SIGNING_KEY secret
#      (Settings -> Secrets and variables -> Actions), whole file, including
#      the BEGIN and END lines. The release workflow signs the feed with it and
#      removes it before the step ends.
#
#   2. Build with the public key:
#        cmake --preset release -DTRANSMIT_UPDATE_KEYS=<the printed key>
#      or add it to CMakePresets.json so every build carries it.
#
# Keep the private key somewhere it survives losing this machine, and nowhere
# it can be read by anything else. Anybody holding it can make every copy of
# Transmit install whatever they like, without asking.
#
# Rotating: make a second key, add its public half to TRANSMIT_UPDATE_KEYS
# alongside the first (separated by ';'), ship that build, and only then start
# signing with the new private key. Builds that only know the old key keep
# working until they have updated past it.

set -euo pipefail

destination="${1:-transmit-update-key.pem}"

if [ -e "$destination" ]; then
    echo "refusing to overwrite $destination" >&2
    exit 1
fi

if ! command -v openssl >/dev/null 2>&1; then
    echo "openssl is needed to make an Ed25519 key" >&2
    exit 1
fi

umask 077
openssl genpkey -algorithm ED25519 -out "$destination"

# The raw 32 bytes are the last 32 of the DER public key: a 12-byte header
# describing the algorithm, then the key itself.
public=$(openssl pkey -in "$destination" -pubout -outform DER | tail -c 32 | base64 | tr -d '\n')

cat <<MESSAGE

Private key: $destination   (keep it; anybody with it controls every update)
Public key:  $public

Put the private key in the UPDATE_SIGNING_KEY secret, and build with:

    -DTRANSMIT_UPDATE_KEYS=$public

MESSAGE
