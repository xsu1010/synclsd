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

Deploy: copy `savesync.3dsx` to `sdmc:/3ds/savesync/` on the SD card and launch it
from the Homebrew Launcher.

## Repo layout

```
.
├── CLAUDE.md            # agent contract — Claude Code reads it each session
├── PROGRESS.md          # milestone tracker / multi-session recovery
├── README.md
├── docs/
│   └── PLAN.md          # full design + build plan
├── source/              # C/C++ sources (libctru) — devkitPro convention
├── Makefile             # devkitPro build (added at M0)
└── .gitignore
```

## .gitignore (create this)

```gitignore
# secrets — never commit
**/token.txt
*.pat

# build artifacts
build/
*.3dsx
*.cia
*.elf
*.smdh
```

## Handing this to Claude Code

1. Put these files in the repo (`PLAN.md` → `docs/PLAN.md`), `git init`, add `.gitignore`.
2. Start `claude` in the directory.
3. Tell it: _read `docs/PLAN.md` and `PROGRESS.md`, then begin at M0; do one milestone at a
   time, stop after each for me to verify, and update `PROGRESS.md` before ending._

Claude Code builds on your machine but cannot run on the 3DS, so milestones flagged _[hw]_ in
`PROGRESS.md` are verified by you flashing and testing on hardware.
