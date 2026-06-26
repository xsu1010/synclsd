#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <3ds.h>

#include "config.h"
#include "sdscan.h"
#include "blobsha.h"
#include "net.h"
#include "pat.h"
#include "state.h"
#include "sync.h"

#define CONFIG_PATH SD_PREFIX "/3ds/savesync/config.toml"

static void print_config(const config_t* cfg)
{
    printf("\x1b[1;1H");
    printf("3DS Save Sync\n");
    printf("M5: three-way sync\n\n");
    printf("GitHub: %s/%s  branch=%s\n\n",
           cfg->github.owner, cfg->github.repo, cfg->github.branch);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    printf("\x1b[2J");

    config_t cfg;
    char err[256];
    if (config_load(CONFIG_PATH, &cfg, err, sizeof(err)) != 0) {
        printf("\x1b[1;1H");
        printf("3DS Save Sync\n\n");
        printf("config error:\n%s\n\n", err);
        printf("Expected: %s\n\n", CONFIG_PATH);
        printf("Press START to exit.\n");
        goto loop;
    }

    print_config(&cfg);

    size_t total = 0;
    for (size_t i = 0; i < cfg.watch_count; i++) {
        const watch_config_t* w = &cfg.watches[i];
        printf("[%s] %s  %s\n", w->name, w->path, w->glob);

        matched_file_t* files = NULL;
        size_t count = 0;
        char serr[256];
        if (sdscan_watch(w, &files, &count, serr, sizeof(serr)) == 0) {
            for (size_t j = 0; j < count; j++) {
                char sha[GIT_SHA1_HEX_LEN];
                char berr[256];
                if (blobsha_file(files[j].sd_path, sha, berr, sizeof(berr)) == 0) {
                    printf("  %s  %s\n", sha, files[j].repo_path);
                } else {
                    printf("  [sha err] %s  (%s)\n", files[j].repo_path, berr);
                }
            }
            if (count == 0) {
                printf("  (no matches)\n");
            }
            printf("  -> %zu file(s)\n\n", count);
            sdscan_free(files);
            total += count;
        } else {
            printf("  ! %s\n\n", serr);
        }
    }

    printf("Total: %zu file(s)\n\n", total);

    printf("-- M5: Sync All --\n");

    char token[PAT_MAX];
    char terr[256];
    if (pat_read(token, sizeof(token), terr, sizeof(terr)) != 0) {
        printf("token: %s\n", terr);
        config_free(&cfg);
        goto loop;
    }
    printf("token: read OK (%zu chars)\n", strlen(token));

    char nerr[256];
    if (net_init(nerr, sizeof(nerr)) != 0) {
        printf("net: %s\n", nerr);
        config_free(&cfg);
        goto loop;
    }
    printf("net: SOC + curl init OK\n\n");

    state_t st;
    char serr2[256];
    if (state_load(&st, serr2, sizeof(serr2)) != 0) {
        printf("state: %s\n", serr2);
        net_exit();
        config_free(&cfg);
        goto loop;
    }

    int counts[8] = {0};
    int errors = 0;

    for (size_t i = 0; i < cfg.watch_count; i++) {
        const watch_config_t* w = &cfg.watches[i];
        matched_file_t* files = NULL;
        size_t count = 0;
        char scanerr[256];
        if (sdscan_watch(w, &files, &count, scanerr, sizeof(scanerr)) != 0) {
            printf("[%s] scan error: %s\n", w->name, scanerr);
            continue;
        }
        for (size_t j = 0; j < count; j++) {
            printf("%s\n", files[j].repo_path);
            gfxFlushBuffers();
            gfxSwapBuffers();

            sync_outcome_t o;
            int rc = sync_file(&cfg.github, token, files[j].repo_path,
                               files[j].sd_path, &st, &o);
            const char* tag = sync_result_str(o.result);

            if (o.result == SYNC_CONFLICT) {
                printf("  CONFLICT: %s\n", o.detail);
                printf("  1 Keep Local  2 Take Remote  3 Keep Both  4 Skip\n");
                gfxFlushBuffers();
                gfxSwapBuffers();

                resolve_action_t action = RESOLVE_SKIP;
                int got = 0;
                while (aptMainLoop() && !got) {
                    gspWaitForVBlank();
                    hidScanInput();
                    u32 k = hidKeysDown();
                    if (k & KEY_A)      { action = RESOLVE_KEEP_LOCAL;  got = 1; }
                    else if (k & KEY_B) { action = RESOLVE_TAKE_REMOTE; got = 1; }
                    else if (k & KEY_X) { action = RESOLVE_KEEP_BOTH;   got = 1; }
                    else if (k & KEY_Y) { action = RESOLVE_SKIP;        got = 1; }
                    gfxFlushBuffers();
                    gfxSwapBuffers();
                }

                sync_outcome_t ro;
                int rrc = sync_resolve(&cfg.github, token, files[j].repo_path,
                                       files[j].sd_path, &st, action, &ro);
                tag = sync_result_str(ro.result);
                if (rrc == 0 && ro.detail[0]) {
                    printf("  -> %s: %s\n", tag, ro.detail);
                } else if (rrc == 0) {
                    printf("  -> %s\n", tag);
                } else {
                    printf("  -> %s: %s\n", tag, ro.detail);
                    errors++;
                }
                counts[ro.result]++;
            } else if (rc == 0 && o.detail[0]) {
                printf("  %s: %s\n", tag, o.detail);
                counts[o.result]++;
            } else if (rc == 0) {
                printf("  %s\n", tag);
                counts[o.result]++;
            } else {
                printf("  %s: %s\n", tag, o.detail);
                errors++;
                counts[o.result]++;
            }
        }
        sdscan_free(files);
    }

    char saveerr[256];
    if (state_save(&st, saveerr, sizeof(saveerr)) != 0) {
        printf("\nstate save FAILED: %s\n", saveerr);
    } else {
        printf("\nstate saved.\n");
    }
    state_free(&st);

    printf("\nSync All done:\n");
    printf("  in-sync: %d  pushed: %d  pulled: %d  seeded: %d\n",
           counts[SYNC_IN_SYNC], counts[SYNC_PUSHED], counts[SYNC_PULLED], counts[SYNC_SEEDED]);
    printf("  conflicts: %d  skipped: %d  errors: %d\n",
           counts[SYNC_CONFLICT], counts[SYNC_SKIPPED], errors);

    net_exit();
    config_free(&cfg);

loop:
    printf("Press START to exit.\n");

    while (aptMainLoop())
    {
        gspWaitForVBlank();
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    gfxExit();
    return 0;
}
