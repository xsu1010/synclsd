# 3DS Save Sync

Homebrew for the Nintendo 3DS that syncs **loose SD-card save files** to a **private GitHub
repository**, manual two-way with conflict detection. First target is EasyRPG Player's `.lsd`
saves; the same machinery handles any save that lives as a file on the SD card.

> Requires a Luma3DS CFW console. This tool works on **files on the SD card** — it does not
> touch the FS save archives of commercial titles (explicitly out of scope).

**This repository is the homebrew source.** Your saves are pushed at runtime to a _separate_
private "saves" repo that you name in `config.toml`. The two are not the same repo.

- Design + build plan: [`docs/PLAN.md`](docs/PLAN.md)
- Build progress / milestones: [`PROGRESS.md`](PROGRESS.md)
- Agent instructions (Claude Code): [`CLAUDE.md`](CLAUDE.md)

## Prerequisites

- Luma3DS CFW with the Homebrew Launcher.
- A **private GitHub repo for your saves**, created empty (the tool will not create it), plus a
  **fine-grained PAT** scoped to that one repo with **Contents: Read and write**.
- devkitPro + the 3DS toolchain, with portlibs: `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`,
  `citro2d`/`citro3d`, plus a small TOML parser (`tomlc99`).

## On-device setup (SD card)

Place these under `sdmc:/3ds/savesync/`:

- `config.toml` — watch rules (folder + glob). See plan §4 for the schema.
- `token.txt` — your fine-grained PAT. **Never committed; lives only on the SD card.**
- `cacert.pem` — CA bundle, so TLS to GitHub can be verified.

`state.json` and `backups/` are created and managed by the tool.

## Build

Requires devkitPro with the 3DS toolchain. One-time setup (macOS, Apple Silicon):

```sh
# install devkitPro pacman (v6.0.2 pkg), then the 3DS toolchain group
sudo installer -pkg devkitpro-pacman-installer.pkg -target /
sudo /opt/devkitpro/pacman/bin/pacman -Sy --noconfirm 3ds-dev
```

The portlibs needed from M3 onward are a separate install (not needed for M0):

```sh
sudo /opt/devkitpro/pacman/bin/pacman -Sy --noconfirm 3ds-curl 3ds-mbedtls 3ds-zlib
```

Build the `.3dsx` (set the env vars once per shell — devkitPro v6 ships no env script):

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
make            # -> savesync.3dsx (+ savesync.smdh)
make clean
# .cia target is added at M8
```

Deploy: Open Homebrew Launcher, press Y, then run:

```sh
make send IP=<3ds ip>`
```
