#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "net.h"
#include "pat.h"
#include "state.h"
#include "status.h"
#include "sync.h"

#define CONFIG_PATH SD_PREFIX "/3ds/savesync/config.toml"

static file_list_t g_files;
static state_t g_state;
static config_t g_cfg;
static char g_token[PAT_MAX];
static int g_cursor = 0;
static int g_net_ok = 0;
static int g_exited_normally = 0;

static const char *basename(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static void render_header(void) {
  printf("\x1b[1;1H");
  printf("suzinho's OFF save sync                    [MVP]\n");
  printf("GitHub: %s/%s  branch=%s\n\n", g_cfg.github.owner, g_cfg.github.repo,
         g_cfg.github.branch);
}

static void render_list(void) {
  int max_rows = 20;
  int start = 0;
  if (g_files.count > (size_t)max_rows &&
      (size_t)g_cursor >= g_files.count - max_rows) {
    start = (int)g_files.count - max_rows;
  }
  if (g_cursor > start + max_rows - 1)
    start = g_cursor - max_rows + 1;
  if (start < 0)
    start = 0;

  printf("  #  Status       File\n");
  for (int i = start; i < (int)g_files.count && i < start + max_rows; i++) {
    const file_entry_t *fe = &g_files.files[i];
    const char *arrow = (i == g_cursor) ? ">" : " ";
    printf("%s%-2d  %s  %.30s\n", arrow, i + 1, status_str(fe->status),
           basename(fe->repo_path));
  }

  printf("\nA: Sync  X: Force-push  Y: Force-pull\n");
  printf("L: Sync All  START: Exit\n");
}

static void handle_conflict(int idx) {
  file_entry_t *fe = &g_files.files[idx];
  printf("\x1b[2J\x1b[1;1H");
  printf("CONFLICT\n%s\n\n", fe->repo_path);
  printf("A: Keep Local  B: Take Remote\n");
  printf("X: Keep Both   Y: Skip\n");

  int got = 0;
  resolve_action_t action = RESOLVE_SKIP;
  while (aptMainLoop() && !got) {
    gspWaitForVBlank();
    hidScanInput();
    u32 k = hidKeysDown();
    if (k & KEY_A) {
      action = RESOLVE_KEEP_LOCAL;
      got = 1;
    } else if (k & KEY_B) {
      action = RESOLVE_TAKE_REMOTE;
      got = 1;
    } else if (k & KEY_X) {
      action = RESOLVE_KEEP_BOTH;
      got = 1;
    } else if (k & KEY_Y) {
      action = RESOLVE_SKIP;
      got = 1;
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
  }

  sync_outcome_t o;
  sync_resolve(&g_cfg.github, g_token, fe->repo_path, fe->sd_path, &g_state,
               action, &o);
  printf("\n-> %s: %s\n", sync_result_str(o.result),
         o.detail[0] ? o.detail : "");
  printf("\npress any key...\n");
  gfxFlushBuffers();
  gfxSwapBuffers();

  int wait = 0;
  while (aptMainLoop() && !wait) {
    gspWaitForVBlank();
    hidScanInput();
    if (hidKeysDown())
      wait = 1;
    gfxFlushBuffers();
    gfxSwapBuffers();
  }

  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
  status_refresh_one(&g_cfg.github, g_token, &g_state, fe);
}

static void sync_selected(void) {
  file_entry_t *fe = &g_files.files[g_cursor];
  sync_outcome_t o;
  int rc = sync_file(&g_cfg.github, g_token, fe->repo_path, fe->sd_path,
                     &g_state, &o);

  if (o.result == SYNC_CONFLICT) {
    handle_conflict(g_cursor);
    return;
  }

  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
  status_refresh_one(&g_cfg.github, g_token, &g_state, fe);
  (void)rc;
}

static void force_push_selected(void) {
  file_entry_t *fe = &g_files.files[g_cursor];
  char ts[32];
  time_t t = time(NULL);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%SZ", gmtime(&t));

  if (!fe->has_remote) {
    char msg[160];
    snprintf(msg, sizeof(msg), "savesync: seed-push %.80s", fe->repo_path);
    char aerr[256];
    api_seed_push(&g_cfg.github, g_token, fe->repo_path, fe->sd_path, msg, aerr,
                  sizeof(aerr));
  } else {
    char msg[160];
    snprintf(msg, sizeof(msg), "savesync: force-push %.80s", fe->repo_path);
    char aerr[256];
    api_update_push(&g_cfg.github, g_token, fe->repo_path, fe->sd_path,
                    fe->remote_sha, msg, aerr, sizeof(aerr));
  }
  state_upsert(&g_state, fe->repo_path, fe->local_sha, ts);
  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
  status_refresh_one(&g_cfg.github, g_token, &g_state, fe);
}

static void force_pull_selected(void) {
  file_entry_t *fe = &g_files.files[g_cursor];
  if (!fe->has_remote)
    return;

  sync_outcome_t o;
  resolve_action_t action = RESOLVE_TAKE_REMOTE;
  sync_resolve(&g_cfg.github, g_token, fe->repo_path, fe->sd_path, &g_state,
               action, &o);
  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
  status_refresh_one(&g_cfg.github, g_token, &g_state, fe);
}

static void sync_all(void) {
  for (size_t i = 0; i < g_files.count; i++) {
    file_entry_t *fe = &g_files.files[i];
    sync_outcome_t o;
    sync_file(&g_cfg.github, g_token, fe->repo_path, fe->sd_path, &g_state, &o);

    if (o.result == SYNC_CONFLICT) {
      g_cursor = (int)i;
      handle_conflict((int)i);
    }

    status_refresh_one(&g_cfg.github, g_token, &g_state, fe);
  }
  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  gfxInitDefault();
  consoleInit(GFX_BOTTOM, NULL);

  char err[256];
  if (config_load(CONFIG_PATH, &g_cfg, err, sizeof(err)) != 0) {
    printf("\x1b[2J\x1b[1;1H");
    printf("suzinho's OFF save sync\n\nconfig error:\n%s\n\nExpected: "
           "%s\n\nPress START "
           "to exit.\n",
           err, CONFIG_PATH);
    goto loop;
  }

  if (pat_read(g_token, sizeof(g_token), err, sizeof(err)) != 0) {
    printf("\x1b[2J\x1b[1;1H");
    printf(
        "suzinho's OFF save sync\n\ntoken error: %s\n\nPress START to exit.\n",
        err);
    config_free(&g_cfg);
    goto loop;
  }

  if (net_init(err, sizeof(err)) != 0) {
    printf("\x1b[2J\x1b[1;1H");
    printf("suzinho's OFF save sync\n\nnet error: %s\n\nPress START to exit.\n",
           err);
    config_free(&g_cfg);
    goto loop;
  }
  g_net_ok = 1;

  if (state_load(&g_state, err, sizeof(err)) != 0) {
    printf("\x1b[2J\x1b[1;1H");
    printf(
        "suzinho's OFF save sync\n\nstate error: %s\n\nPress START to exit.\n",
        err);
    net_exit();
    config_free(&g_cfg);
    goto loop;
  }

  printf("\x1b[2J\x1b[1;1H");
  printf(
      "suzinho's OFF save sync\n\n'mimimimi play on one console like the rest "
      "of us mortals'\n\nnuh uh.\n\nscanning...\n");
  gfxFlushBuffers();
  gfxSwapBuffers();

  status_scan(&g_cfg, g_token, &g_state, &g_files);

  printf("\x1b[2J");
  while (aptMainLoop()) {
    render_header();
    render_list();
    gfxFlushBuffers();
    gfxSwapBuffers();

    gspWaitForVBlank();
    hidScanInput();
    u32 k = hidKeysDown();

    if (k & KEY_START)
      break;
    if (k & KEY_UP) {
      if (g_cursor > 0)
        g_cursor--;
    }
    if (k & KEY_DOWN) {
      if ((size_t)g_cursor + 1 < g_files.count)
        g_cursor++;
    }
    if (k & KEY_A)
      sync_selected();
    if (k & KEY_X)
      force_push_selected();
    if (k & KEY_Y)
      force_pull_selected();
    if (k & KEY_L)
      sync_all();
  }

  g_exited_normally = 1;
  file_list_free(&g_files);
  char serr[256];
  state_save(&g_state, serr, sizeof(serr));
  state_free(&g_state);

loop:
  if (g_net_ok)
    net_exit();
  config_free(&g_cfg);

  if (!g_exited_normally) {
    while (aptMainLoop()) {
      gspWaitForVBlank();
      hidScanInput();
      if (hidKeysDown() & KEY_START)
        break;
      gfxFlushBuffers();
      gfxSwapBuffers();
    }
  }

  gfxExit();
  return 0;
}
