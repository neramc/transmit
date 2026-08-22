# Packaging

Recipes for the systems Transmit targets. All of them build from a normal
`cmake --install`, so nothing here duplicates build logic — they only describe
dependencies and metadata for each distribution's conventions.

| File | For |
| --- | --- |
| `linux/debian/` | Debian, Ubuntu, Mint, Pop!_OS, Raspberry Pi OS |
| `linux/transmit.spec` | Fedora, RHEL and derivatives, openSUSE |
| `linux/PKGBUILD` | Arch and derivatives |
| `linux/APKBUILD` | Alpine |
| `linux/template` | Void |
| `linux/transmit.ebuild` | Gentoo |
| `linux/flake.nix` | NixOS |
| `linux/io.github.neramc.Transmit.yml` | Flatpak, for any distribution |
| `windows/transmit.nsi` | Windows installer |
| `macos/build-dmg.sh` | macOS disk image |

Slackware has no packaging format of its own beyond a build script; use the
`cmake --install` output directly, or wrap it with `makepkg`.
