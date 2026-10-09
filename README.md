<p align="center"><a href="https://github.com/wiidev/usbloadergx/" title="USB Loader GX"><img src="data/web/logo.png"></a></p>
<p align="center">
<a href="https://github.com/wiidev/usbloadergx/releases" title="Releases"><img src="https://img.shields.io/github/v/release/wiidev/usbloadergx?logo=github"></a>
<a href="https://github.com/wiidev/usbloadergx/actions" title="Actions"><img src="https://img.shields.io/github/actions/workflow/status/wiidev/usbloadergx/main.yml?branch=enhanced&logo=github"></a>
</p>

## Description
USB Loader GX allows you to play Wii and GameCube games from a USB storage device or an SD card, launch other homebrew apps, create backups, use cheats in games, and a whole lot more.

## Installation
1. Extract the apps folder to the root of your SD card and replace any existing files.
2. Install the [latest d2x cIOS](https://github.com/wiidev/d2x-cios/releases).
3. Optional: Update wiitdb.xml by selecting the update option within the loaders settings menu.
4. Optional: Install the loaders [forwarder channel](https://raw.githubusercontent.com/wiidev/usbloadergx/updates/USBLoaderGX_forwarder%5BUNEO%5D.wad), then go into `Loader Settings` and set `Return To` to `UNEO`.

## d2x cIOS
1. Use the correct cIOS package for your console — e.g., `d2x-v11-beta3` for the Wii and `d2x-v11-beta3-vWii` for the Wii U.
2. When using the d2x cIOS installer, set the cIOS to the version that you downloaded — e.g., `d2x-v11-beta3`.
3. Install the cIOS into each slot with the following settings.

````
Slot 248 base 38
Slot 249 base 56
Slot 250 base 57
Slot 251 base 58
````

## Building
The project is built with devkitPPC r50 and libogc 3. The `toolchain` stage of the `Dockerfile` defines the whole toolchain: the image `devkitpro/devkitppc:20260503`, `devkitppc-crtls` 2.1.0 (the image does not have it yet), and the libraries `deps/build.sh` builds from pinned upstream sources (wolfSSL, pugixml, minizip and the Homebrew Channel agent). CI, the dev container and `scripts/*.sh` all build in that stage. [deps/README.md](deps/README.md) lists every library and where it comes from. Use any of the following. They compile with the same flags; builds made outside CI are tagged as unofficial in the version string.

1. **Docker** (Linux, macOS, or Windows via WSL2 with Docker Engine installed):
   ````
   docker build -o . .
   ````
   This compiles inside the pinned image and writes `usbloader_gx.zip` to the repository root. `scripts/build.sh` wraps the same command and finds Docker through WSL on Windows.
2. **VS Code Dev Container**: open the repository, choose *Reopen in Container*, then run `make release`.
3. **Native devkitPro**: install the `wii-dev` group and the `ppc-*` portlibs listed in [deps/README.md](deps/README.md) with `dkp-pacman`, update `devkitppc-crtls` to 2.1.0 or later, run `deps/build.sh` once, then run `make release`. With an older `devkitppc-crtls` the DOL sections are not padded to 32 bytes and Dolphin refuses `boot.dol`; `sh scripts/check-dol.sh boot.dol` tells. Match the toolchain version used by CI if you hit header or link errors.

Before opening a pull request, run the static checks in `tests/` (for example `sh tests/check-nintendont-loader-path.sh`) and the host tests (`sh tests/host/run.sh`); CI runs all of them, and fails the build on any compiler warning outside the vendored `source/libs` code. `scripts/diag.sh warnings` and `scripts/cppcheck.sh` run the extra diagnostics used for code review, and `scripts/verify-build.sh` checks the build system itself after a Makefile change.

### Testing

- **Host tests** (`tests/host/`): each test compiles the exact source file the Wii build uses, with the host C compiler under AddressSanitizer and UndefinedBehaviorSanitizer. Add one for any pure function that handles data from a file, a drive or the network. On Windows, `run.sh` uses WSL when Git Bash has no working compiler.
- **Dolphin**: `scripts/dolphin_run.sh <outdir> [seconds]` boots the build unattended in a throwaway profile, keeps the last frames, and saves what the loader prints to its USB Gecko as `gecko.log`. It ends with a verdict read from that log. `scripts/dolphin_start.sh` starts it for you to drive and writes the same log live to `.dev/gecko-live.log`. Dolphin has no USB mass storage and no cIOS, so the emulated SD card is the only storage the loader sees there.
- **A real Wii**: `scripts/wii_install.py` puts a build on a Wii's SD card through a Homebrew Channel that speaks the [hbc-reborn](https://github.com/Monsterray/hbc-reborn) developer protocol, into `apps/usbloader_gx_review` so an installed release and its settings stay as they are.

## Versions
The loader shows a `MAJOR.MINOR.PATCH` version on its start-up screen, in its info window and in the Homebrew Channel. It comes from the last release tag, `vMAJOR.MINOR.PATCH` (for example `v5.2.0`), through `git describe` in `makexml.sh`:

- `5.2.0`: a build of the tagged commit.
- `5.2.0+4.g1a2b3c4`: 4 commits after `v5.2.0`, at commit `1a2b3c4`; `.dirty` is added when the tree has uncommitted changes.
- `0.0.0+g1a2b3c4`: no release tag is reachable (a shallow clone, for example).

Builds made outside the project's CI add "/ Unofficial". To release, tag the commit and push the tag:

````
git tag -a v5.3.0 -m "USB Loader GX 5.3.0"
git push origin v5.3.0
````

Raise MAJOR when settings files, saves or caches of the previous release stop working, MINOR for a new feature, and PATCH for fixes only. The old revision number in `version.txt` (r1283) stays: the settings files still record it.

## Updates
Settings > Update Menu > Update USB Loader GX installs the latest [release of this fork](https://github.com/Monsterray/usbloadergx/releases), not upstream's. Pushing a `vMAJOR.MINOR.PATCH` tag runs `.github/workflows/release.yml`, which builds the release, checks that the build shows exactly that version, and publishes a GitHub release with `boot.dol`, `meta.xml`, `icon.png`, a zip for the SD card and `update.txt`.

`update.txt` names the version and the size and SHA-256 of every file, and ends with an Ed25519 signature (format: `source/network/UpdateManifest.h`). The loader downloads with no certificate check, so it installs a file only when the signature verifies with the public key compiled into it (`source/network/UpdateKey.h`) and the file matches its SHA-256, and only when the release is newer than the running version (semver; `5.2.0+4.g1a2b3c4` counts as `5.2.0`). The private key is the repository secret `UPDATE_SIGNING_KEY`; `scripts/update-keygen.sh` makes a key pair, and `scripts/update-manifest.sh` writes and signs `update.txt`, refusing a key that does not match the loader's. Replacing the key pair locks out every loader built with the old public key: those have to be updated by hand once.

The channel build (`make channel`) installs `usbloadergx.wad` from the release to NAND with ES (`source/wad/WadInstall.cpp`), only for title `00010001-ULNR`. The release workflow makes no WAD: to add one, put it next to the release's other files, run `scripts/update-manifest.sh` again, and upload the WAD and the new `update.txt` to the release. A fakesigned WAD installs only on an IOS that accepts fakesigned titles (a cIOS). The language files, cheats, GameTDB and Nintendont still come from their own upstreams.
