# Security

Transmit is asked to carry the contents of a computer, and — if you turn it on
— its saved passwords. This is what it does with them.

## Reporting a vulnerability

Report privately through
[GitHub's advisory form](https://github.com/neramc/transmit/security/advisories/new)
rather than in a public issue. Please include what you did, what happened, and
what you expected. A first reply should come within a week.

## The rules the code follows

These are properties of the implementation, not aspirations. A change that
breaks one is a bug even if everything still works.

**Credentials are opt-in.** Nothing reads the keyring unless the credential
domain is selected. It is off in every profile that does not name it.

**Credentials require encryption.** A capture that includes them and has no
passphrase is refused outright, in the interface and on the command line. There
is no fallback that writes them in the clear.

**Plaintext lives in memory only.** Decrypted secrets are never written to a
temporary file, a log, or a generated script. `SecretRecord::clear()` and
`format::secureZero` overwrite the buffers that held them.

**Passphrases never become command arguments.** Anything Transmit runs that
needs one gets it on standard input. A passphrase in `argv` is readable by
every other user on the machine, which is why `--passphrase` carries a warning
and the terminal prompt exists.

**Transmit never runs the scripts it writes.** Installing software and changing
privileged settings are your decision and your password. It writes
`install-apps.sh` / `.ps1` and `apply-settings.sh` / `.ps1`, tells you they are
there, and stops.

**The archive protects names as well as contents.** Encryption is AES-256-GCM
with scrypt key derivation, and the manifest is encrypted too — a file list is
not a small thing to leak. A wrong passphrase is rejected on the header rather
than after a failed decryption.

**The Flatpak manifest grants no network access.** Transmit has no reason to
talk to anything.

## What is on the drive

An encrypted archive is only as good as where the passphrase is. If credentials
were included, the archive contains passwords for everything they unlock: keep
the drive and the passphrase apart, and erase the drive when the move is done.

An archive with no passphrase is a readable copy of a home directory. That is
not a defect — it is faster, and most captures do not need protecting — but it
is worth knowing before it goes in a drawer.

## What is out of scope

- A machine that is already compromised. Transmit reads what the user can read;
  malware on either side can do the same.
- The strength of a chosen passphrase. scrypt makes guessing expensive; it
  cannot make `password` a good one.
- Anything sealed to hardware — a TPM, a Secure Enclave. Those keys cannot be
  exported by design, and Transmit reports them as not portable rather than
  pretending.
