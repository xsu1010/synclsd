# Progress

Recovery artifact for multi-session builds.
**Read this and `docs/PLAN.md` at the start of every session. Update status + log before ending.**

## Session protocol

1. Read `docs/PLAN.md` (design) and this file (state).
2. Work the single milestone marked **▶ current**. One at a time — do not sprint ahead.
3. Build it. For milestones marked _[hw]_, **STOP** and ask the human to flash + test on a real 3DS.
4. Mark a milestone done only after end-to-end verification — not when it merely compiles.
5. Append a log entry below and move ▶ to the next milestone.

## Milestones

MVP cutline is **M7** — a working text-mode two-way sync for EasyRPG `.lsd`. M8+ is polish/expansion.

- [x] **M0** — Toolchain & boot: a `.3dsx` that boots and prints to the text console. Record build commands in README + CLAUDE.md.
- [x] **M1** — Parse `config.toml`, resolve folder + glob rules against the SD, print matched files. Local only.
- [ ] ▶ **M2** — Compute the git blob sha of each matched file (mbedtls sha1). Print them. _(built; **awaiting hardware verification** — boot, confirm 40-hex shas print per file; cross-check one with `git hash-object`)_
- [ ] **M3** _[hw]_ — HTTPS `GET` to the GitHub Contents API with TLS verification ON. Riskiest piece — isolate and prove it.
- [ ] **M4** _[hw]_ — Seed-push one `.lsd` via Contents API `PUT`; confirm the commit on GitHub. First end-to-end success.
- [ ] **M5** _[hw]_ — Three-way sync + `state.json`; wire Sync All (happy path); pulls write via temp + backup + swap.
- [ ] **M6** _[hw]_ — Conflict prompt (Keep Local / Take Remote / Keep Both / Skip) and each action.
- [ ] **M7** _[hw]_ — Text status-list UI: auto-scan on launch, status badges, Sync All, per-file force-push/pull. **← MVP.**
- [ ] **M8** — `.cia` target: banner, icon, title ID, FS-access exheader.
- [ ] **M9** — citro2d graphical UI: touch buttons, polished conflict card.
- [ ] **M10** — File picker: browse SD, tap to add watch rules, write them into `config.toml`.
- [ ] **M11** _[hw]_ — On-console restore-from-repo: Git Trees API (recursive) → write to mirrored SD paths.
- [ ] **M12** _[hw]_ — Git Data blobs fallback for files >~1 MB (save-states).

## Log

- _(init)_ Scaffolded from `docs/PLAN.md`. No code yet. Current focus: **M0**.
- _(M0 build)_ Installed devkitPro pacman **v6.0.2** (macOS pkg) + `3ds-dev` group: devkitARM 16.1.0, libctru (with citro2d/3d), `3dsxtool`, `smdhtool`. **Portlibs `3ds-curl`/`3ds-mbedtls`/`3ds-zlib` NOT yet installed** — needed at M3; install command recorded in README. Wrote `source/main.c` (gfx + console init, prints banner, exits on START) and `Makefile` (devkitPro 3DS template, C11, `-Wextra`, libctru, `.3dsx` + `.smdh`). Note: source dir is `source/` (devkitPro convention), not `src/` — README layout updated. Build verified clean (zero warnings):
  ```sh
  export DEVKITPRO=/opt/devkitpro && export DEVKITARM=/opt/devkitpro/devkitARM && make
  # -> savesync.3dsx (122K) + savesync.smdh (14K)
  ```
  Build commands recorded in `README.md` + `CLAUDE.md`. **STOP — awaiting human to flash `savesync.3dsx` to `sdmc:/3ds/savesync/` and confirm it boots + prints on a real 3DS (or Azahar) before M0 is marked done.**
- _(M0 verified)_ Human flashed `savesync.3dsx` and confirmed it boots and prints the banner + "Press START to exit" on the bottom screen, exiting cleanly on START. **M0 done.** Moving to **M1**.
- _(M1 build)_ Vendored **tomlc99** (CK Tan, MIT) into `libs/tomlc99/` (toml.h/toml.c/LICENSE); wired into Makefile (`SOURCES`/`INCLUDES`). Added `source/config.[ch]` (parse `[github]` + `[[watch]]` array-of-tables via tomlc99 → `config_t`/`github_config_t`/`watch_config_t`; `config_load`/`config_free`; error-code return + errbuf). Added `source/sdscan.[ch]` (resolve a watch: prepend `sdmc:` to config path, `opendir`/`readdir`, skip dirs, match `glob` via a hand-written `glob_match` (`*`/`?`/literal — `fnmatch` has no symbol in devkitARM libc), collect `matched_file_t{sd_path, repo_path}`; `sdscan_free`). Rewrote `source/main.c` to load `sdmc:/3ds/savesync/config.toml`, print GitHub config + each watch's matched files + counts, with a clear error path if config is missing/malformed. Added `config.example.toml` (copy to SD + edit). Build clean for our code (`config.c`/`main.c`/`sdscan.c` zero warnings); the 8 `-Wchar-subscripts` warnings are inside vendored `toml.c` (`isdigit` on `char`) — benign (ASCII-only) and left unmodified to ease upstream updates. Artifact: `savesync.3dsx` (179K) + `savesync.smdh`.   **STOP — awaiting human to drop `config.toml` (from `config.example.toml`) + real saves onto SD, boot, and confirm the watch list + matched files print correctly.**
- _(M1 verified)_ Added `make send IP=<3DS-IP>` target (calls `3dslink`; netloader enabled in Homebrew Launcher with Y). Initial netloader attempt hit `fopen:-929020708` on the 3DS (stale hbmenu state); a 3DS restart cleared it. Human sent the `.3dsx` via netloader and confirmed the watch list + matched `.lsd` files print correctly (`Total: 2 file(s)`). **M1 done.** Moving to **M2**.
- _(M2 build)_ Installed portlibs: `3ds-mbedtls`, `3ds-curl`, `3ds-zlib` (pacman needed `sudo env PATH=/opt/devkitpro/pacman/bin:$PATH` for gpgme to find gpg). Added `source/blobsha.[ch]`: `blobsha_file()` streams the file in 4 KB chunks through mbedtls `sha1_starts_ret`/`update_ret`/`finish_ret`, hashing the git blob header (`"blob <len>\0"`) + raw bytes → 20-byte digest → 40-char hex. Updated Makefile: `LIBS += -lmbedcrypto`, `LIBDIRS += $(PORTLIBS)`. Updated `main.c` to print `<sha>  <repo_path>` per file (or `[sha err]` on failure). Build clean for our code; only the 8 benign `-Wchar-subscripts` in vendored `toml.c`. `savesync.3dsx` 186K. **STOP — awaiting human to boot and confirm 40-hex blob shas print per file; cross-check one with `git hash-object <file>` on a PC (must match).**
