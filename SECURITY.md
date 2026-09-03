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

**Transmit reaches the network for exactly one thing.** Nothing it captures or
restores goes anywhere: every byte it reads ends up on the drive you chose. The
one exception is the updater, which asks a published feed whether there is a
newer version. It is the only outbound request in the program, it fetches only
that feed and the release file it names, and it can be built out entirely with
`-DTRANSMIT_WITH_UPDATER=OFF`.

**The Flatpak manifest grants no network access.** So the Flatpak has no
updater either — it is built without one, and Flathub does that job.

**An update that cannot be authenticated is not installed.** The feed carries a
detached Ed25519 signature, checked against public keys compiled into the
build. A build given no key reports that a new version exists and downloads
nothing. There is no configuration, no environment variable and no interface
control that turns this off: an updater that installs what it cannot
authenticate is a way to run arbitrary code on every machine that has this
program.

**A download is checked against the signed feed, twice.** Once when it arrives,
by reading it back off the disk rather than hashing it on the way past — so a
file that arrived intact and failed to land intact is caught. Again at the
moment it replaces the running program, because in between it is a file in a
cache directory that anything running as that user could write to.

**A critical release installs itself, and only a critical release does.**
Severity comes from the signed feed, so it cannot be set by anything that
cannot sign. It overrides the update preference and nothing else: it still
requires the signature, still refuses a copy a package manager owns, and still
verifies the download. A version that is already past the stated exposure is
offered the update rather than given it.

**Nothing replaces a copy something else owns.** Flatpak, Snap, anything under
a system prefix, a Homebrew cask: the updater says there is a new version and
stops. Self-updating those would leave the package manager describing files
that are no longer there.

**A release file is only ever fetched over HTTPS, from a known host.**
Redirects are followed — a release download is one — but only onto the hosts
the release is published on, and never down to plain HTTP. A feed that names an
address anywhere else is refused before anything is requested.

**MD5 confirms a transfer and decides nothing.** Every file also carries an MD5,
and the `.md5` file beside an archive lets somebody check a drive with a tool
that has never heard of Transmit. That is all it is for. MD5 collisions can be
constructed in seconds, so it is never an identity: whether two files are the
same, which block a file deduplicates onto, whether a block came back intact
and whether an encrypted archive is authentic are decided by BLAKE2b and
AES-256-GCM, and a matching MD5 alone is never accepted as proof of anything.
It is implemented in this repository rather than called through OpenSSL because
a FIPS-mode build refuses MD5, and a check that quietly becomes optional
depending on how the runtime was configured is not a check.

**A repair archive cannot change what an archive holds.** `name.txa.repair`
supplies bytes for files the drive damaged, and every reader picks it up
without being asked. It may only supply bytes that hash to what the archive's
own manifest already recorded for that path, so dropping a crafted `.repair`
file next to somebody's archive changes nothing about what restoring it puts
on their machine. The original archive is never modified.

**An encrypted archive's `.md5` file does not list its contents.** The point of
encrypting one is that the names of somebody's files are not readable from the
drive; a sidecar listing every path beside it would hand them over in plain
text. The parts are still listed, so the drive can still be checked.
`--md5-sidecar-names` overrides this, and says so.

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
