# 3DS Save Sync — Agent Guide

On-3DS homebrew that syncs **loose SD-card save files** to a **private GitHub repo**,
manual two-way with conflict detection. First target: EasyRPG `.lsd` saves.

- **Full design + build plan:** `docs/PLAN.md` — the source of truth. Read it before coding.
- **Current state + milestones:** `PROGRESS.md` — read at session start, update before ending.

## Two repos (don't conflate)

- **This repo = the homebrew source** (C/C++, libctru). It's what you build.
- The user's **saves** are pushed at runtime to a **separate** private "saves" repo named in
  `config.toml`. This source repo never contains saves or tokens.

## Hard rules

- **Secrets:** never commit the GitHub PAT or any token. On-device it lives at
  `sdmc:/3ds/savesync/token.txt`, never in any repo. `.gitignore` must keep honoring this.
- **Scope:** sync LOOSE SD FILES only. Do **not** add FS save-archive / Checkpoint-style
  access for commercial titles — it was explicitly scoped out. Don't reintroduce it.
- **TLS:** certificate verification stays **ON**. Never disable it to "make HTTPS work."
- **Hardware boundary:** you can build, but you cannot run on the 3DS. Milestones touching the
  network, real SD, or RTC (M3, M4, and every sync path) need the human to flash + test.
  Build, then **STOP and request hardware verification** — don't mark them done yourself.

## Architecture invariants (easy to get wrong)

- Conflict logic keys on git **blob shas** — content-addressed,
  `sha1("blob " + <len> + "\0" + <bytes>)` — **never** on timestamps. Timestamps are
  display-only (the 3DS RTC drifts).
- Repo paths **mirror SD paths verbatim**, so restore == copy. No path rewriting.
- **One file per save** in the repo — no tar/zip. Saves are already loose single files.
- Overwrites: **temp file → swap, plus a pre-overwrite local backup** under
  `sdmc:/3ds/savesync/backups/`. Never write directly over a save.
- Per-file decision uses { local blob sha, base sha from `state.json`, remote sha }.
  See `docs/PLAN.md` §2 for the full table.

## Stack & build

- Greenfield C/C++ on **libctru / devkitPro**. Targets: `.3dsx` (dev) + `.cia` (ship).
- HTTPS via **libcurl + mbedtls** (portlibs); mbedtls also supplies **sha1** and **base64**,
  so no extra hashing/encoding deps.
- TOML config parsed with **`tomlc99`**.
- Build commands (established at M0): `export DEVKITPRO=/opt/devkitpro && export DEVKITARM=/opt/devkitpro/devkitARM && make` → `savesync.3dsx` (+ `savesync.smdh`). devkitPro v6 ships no env script, so the two exports are required per shell. Portlibs (`3ds-curl`, `3ds-mbedtls`, `3ds-zlib`) are a separate `pacman -S` install needed from M3 onward. Full setup in `README.md`.

## Workflow

- Work **one milestone at a time** per `PROGRESS.md`. Do not sprint ahead.
- A milestone is "done" only after **end-to-end verification** (on hardware where relevant),
  not when the code compiles.
- Update `PROGRESS.md` (status + log entry) before ending a session.

## Conventions

- C11, 4-space indent, no tabs. Functions return error codes — check them; free what you alloc.
- No secrets or SD-absolute paths hardcoded in source — read paths from `config.toml`.
