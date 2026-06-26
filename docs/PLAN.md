# 3DS Save Sync — Design & Build Plan

A homebrew utility for the Nintendo 3DS that syncs **loose SD-card save files** to a
**private GitHub repository**, manual two-way with conflict detection. First target is
EasyRPG Player's `.lsd` saves; the same machinery extends to any save that lives as a
file on the SD card (RetroArch SRAM/states, other homebrew, etc.).

> Assumes a Luma3DS CFW console. This tool reads/writes files on the SD card — it does
> **not** touch FS save archives of commercial titles (that branch was explicitly scoped out).

---

## 1. Decision log (locked)

| Area             | Decision                                                                           |
| ---------------- | ---------------------------------------------------------------------------------- |
| Sync model       | Manual two-way push/pull with conflict **detection** (human resolves)              |
| Transport        | Cloud/self-hosted middle (PC need not be awake)                                    |
| Backend          | Git — the 3DS drives the host's HTTP API, not the git wire protocol                |
| Host             | GitHub (private repo). Portable to self-hosted Gitea later (same API)              |
| Scope            | All **loose SD-file saves**; EasyRPG `.lsd` is case zero                           |
| Auth             | Fine-grained PAT, single-repo, Contents-only, plaintext on SD, outside watch rules |
| Sync set         | TOML config on SD (folder + glob rules). File picker writes it later               |
| Path mapping     | **Mirror full SD path** verbatim into the repo (restore = copy)                    |
| Save format      | One file per save at its mirrored path (no tar — already a single file)            |
| Conflict UX      | Prompt per-file: Keep Local · Take Remote · Keep Both · Skip                       |
| Overwrite safety | Atomic-ish write (temp + swap) **plus** pre-overwrite local backup                 |
| Stack            | Greenfield C/C++ on libctru / devkitPro                                            |
| Packaging        | `.3dsx` for dev iteration, `.cia` for daily use (same source)                      |
| UI               | Text-console MVP → citro2d graphical later                                         |
| Operation        | Status list on launch + **Sync All**; per-file force-push/force-pull overrides     |
| API mechanics    | Contents API primary; Git Data **blobs fallback** for files >~1 MB                 |
| Recovery         | PC clone + copy now (free via mirrored paths); on-console restore post-MVP         |

---

## 2. How it works

### State the tool keeps

- **`state.json`** (on SD, `sdmc:/3ds/savesync/state.json`): maps each repo path →
  `{ base_sha, last_synced_utc }`. `base_sha` is the git **blob** sha the tool last
  synchronized for that file — the common ancestor for three-way reasoning.
- The **git blob sha** of a local file is content-addressed:
  `sha1("blob " + <byte_length> + "\0" + <raw_bytes>)`. Because it depends only on
  content, it is **invariant under history rewriting** — important for the pruning note
  in §9.

### The three-way decision (per file)

For each tracked file the tool has three facts: `local` (blob sha of the current SD
file), `base` (from `state.json`), and `remote` (the file's blob sha on GitHub, or
absent).

| `local` vs `base`        | `remote` vs `base`             | Result                                    |
| ------------------------ | ------------------------------ | ----------------------------------------- |
| unchanged                | unchanged                      | **In sync** — no-op                       |
| changed                  | unchanged                      | **Push** (`PUT` with `sha = remote`)      |
| unchanged                | changed                        | **Pull** (write via temp + backup + swap) |
| changed                  | changed (and `local ≠ remote`) | **Conflict** → prompt                     |
| new (no base, no remote) | —                              | **Seed-push** (create)                    |
| new local, remote exists | —                              | Treated as conflict (adopt vs overwrite)  |

The `sha` passed on `PUT` is the version being replaced; a stale `sha` returns
`409 Conflict`, which is the server-side backstop if the remote moved between the
status scan and the write.

### Sync All is idempotent

Each file is synced independently and its `state.json` entry is updated immediately on
success. An interrupted Sync All is simply re-run: completed files now read **in sync**
and are skipped; failed files are retried. No global transaction needed. Pushes are
atomic server-side (a commit lands or it doesn't); pulls are atomic-ish locally
(temp + swap) with a backup as the real safety net.

---

## 3. Filesystem & repo layout

### On the SD card

```
sdmc:/3ds/savesync/
├── config.toml          # watch rules (may be committed; see below)
├── token.txt            # fine-grained PAT — NEVER committed, outside all watch rules
├── state.json           # base_sha + timestamp per repo path
├── cacert.pem           # CA bundle for TLS verification
└── backups/
    └── 3ds/easyrpg-player/games/MyGame/Save01.lsd@2026-06-26T14-03-12Z
```

### In the GitHub repo (paths mirror the SD root)

```
3ds/easyrpg-player/games/MyGame/Save01.lsd
3ds/easyrpg-player/games/MyGame/Save02.lsd
3ds/retroarch/saves/SomeGame.srm
```

Restore to a wiped card is therefore `git clone` + `cp -r clone/* /sdcard/` — the repo
path _is_ the SD path.

---

## 4. Config schema (TOML)

```toml
# sdmc:/3ds/savesync/config.toml

[github]
owner  = "your-username"
repo   = "3ds-saves"
branch = "main"
# token is read from sdmc:/3ds/savesync/token.txt (kept separate so config is safe to commit)

# One block per save location. `glob` filters within `path`.
# Omit `dest` to mirror the SD path verbatim (the chosen default).
[[watch]]
name = "easyrpg-mygame"
path = "/3ds/easyrpg-player/games/MyGame"
glob = "*.lsd"

[[watch]]
name = "retroarch-sram"
path = "/3ds/retroarch/saves"
glob = "*.srm"
```

---

## 5. One-time setup

1. **Console**: Luma3DS CFW with the Homebrew Launcher (assumed already in place).
2. **GitHub**: create an **empty private repo** (the tool will not create repos — a
   contents-only fine-grained PAT can't, by design). Mint a **fine-grained PAT** scoped
   to that one repo with **Contents: Read and write**. Set a long expiry; note the
   rotation date.
3. **SD**: drop `config.toml`, `token.txt`, and `cacert.pem` into
   `sdmc:/3ds/savesync/`.
4. **Dev box**: install devkitPro + the 3DS toolchain; add `3ds-curl`, `3ds-mbedtls`,
   `3ds-zlib`, `citro2d`/`citro3d`, and a small TOML parser (`tomlc99`).

---

## 6. Build roadmap

Sliced so the scariest integration (TLS on the 3DS) is proven early, and so a working
tool exists at the MVP line before any polish.

- **M0 — Toolchain & boot.** Build a `.3dsx` that boots and prints to the text console.
  Confirms the build → deploy loop. (An emulator like Azahar speeds iteration, but
  networking and FAT/RTC behaviour are only trustworthy on real hardware.)
- **M1 — Config + file list.** Parse `config.toml`, resolve folder+glob rules against
  the SD, print matched files. Pure local, no network.
- **M2 — Local blob shas.** Compute the git blob sha of each matched file (sha1 via the
  already-bundled mbedtls). Print them.
- **M3 — HTTPS GET.** Bundle `cacert.pem`; wire libcurl + mbedtls; read the PAT; `GET`
  one file's contents from the repo with **TLS verification on**. The riskiest piece —
  isolated and validated by itself.
- **M4 — Seed push.** Contents API `PUT` (base64 body, commit message, `sha` when
  updating). Push one `.lsd` to the empty repo; confirm the commit on GitHub.
  _First end-to-end "my save is on GitHub" moment._
- **M5 — Three-way sync + `state.json`.** Persist `base_sha`; implement the decision
  table; wire **Sync All** over the whole list (happy path). Pulls write via
  temp + backup + swap.
- **M6 — Conflict prompt.** Detect both-changed; numbered menu
  (`1 Keep Local · 2 Take Remote · 3 Keep Both · 4 Skip`); implement each action.
- **M7 — Status list (text).** Auto-scan on launch; render badges
  (in-sync / will-push / will-pull / conflict); cursor nav; Sync All key; per-file
  force-push/pull. **← MVP cutline: a fully working two-way sync tool.**
- **M8 — `.cia` target.** Add banner, icon, title ID, and FS-access exheader so it
  installs to the Home Menu. Same source.
- **M9 — citro2d UI.** Rebuild the interface graphically: touch buttons, a polished
  conflict card.
- **M10 — File picker.** Browse the SD, tap to add watch rules, write them into
  `config.toml`.
- **M11 — On-console restore.** Repo-driven enumeration (Git Trees API,
  `recursive=1`) → restore selected/all files to mirrored SD paths.
- **M12 — Blobs fallback.** Handle >1 MB save-states via the Git Data API
  (blob → tree → commit → ref).

Optional later: a PC-side retention/pruning script; migration to self-hosted Gitea
(a config change, not a rewrite).

---

## 7. Libraries

- **libctru / devkitPro** — core 3DS SDK, SD file I/O, input, text console.
- **libcurl + mbedtls** (devkitPro portlibs) — HTTPS to GitHub; mbedtls also supplies
  **sha1** (blob hashing) and **base64** (Contents API body), so no extra deps there.
- **citro2d / citro3d** — graphical UI from M9 onward.
- **tomlc99** (MIT) — config parsing.
- No tar/zip library needed: saves are loose single files pushed directly.

---

## 8. Risks & mitigations

- **TLS on the 3DS** is the single most likely thing to fight. Mitigated by isolating it
  at M3, bundling a known-good `cacert.pem`, and keeping verification **on** (disabling
  it would hand the PAT to anyone on the network).
- **FAT `rename()` may not be atomic** via devkitPro/newlib, so the temp→swap has a tiny
  window. This is exactly why a **pre-overwrite local backup** exists — it's the real
  safety net; the swap just shrinks the corruption window.
- **Token at rest on SD** can't be encrypted meaningfully on this hardware. Mitigated by
  _scope_: a single-repo, contents-only PAT is near-worthless if stolen. It also lives
  outside every watch rule, so it can never be committed.
- **Repo growth**: binary saves don't delta well, so size grows ~linearly with
  syncs × save size. At `.lsd`/SRAM sizes this is trivial for years. Pruning later is
  **safe** because blob shas are content-addressed — a PC-side history squash doesn't
  change the current file's blob sha, so `state.json` stays valid.
- **3DS RTC clock skew**: never used in the sync _logic_ (which keys on content shas);
  timestamps are display-only.
- **GitHub API requires a `User-Agent` header** and returns rate-limit info — set a UA;
  the 5,000 req/hr authenticated limit is far beyond this tool's needs.

---

## 9. Deferred decisions (defaults applied)

These were low-stakes; sensible defaults were taken so the plan is complete. Each is
cheap to revisit.

- **History retention** → _keep full history, don't prune._ Small saves, slow growth,
  full rollback. Pruning available later (safe per §8) via an optional PC-side script.
- **TOML schema** → as in §4 (`[github]` + repeated `[[watch]]` blocks, `dest` optional).
- **Interrupted Sync All** → _per-file idempotent re-run_ (§2), no global transaction.
- **Empty-repo first run** → _seed-push every watched file_; user creates the repo + PAT
  manually as a one-time step (§5).
- **Build order** → §6, MVP at M7.
