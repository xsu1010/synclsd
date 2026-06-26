#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <3ds.h>

#include "config.h"
#include "sdscan.h"
#include "blobsha.h"
#include "net.h"
#include "pat.h"
#include "api.h"

#define CONFIG_PATH SD_PREFIX "/3ds/savesync/config.toml"
#define API_BASE     "https://api.github.com/repos/"

static void print_config(const config_t* cfg)
{
    printf("\x1b[1;1H");
    printf("3DS Save Sync\n");
    printf("M4: seed-push\n\n");
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

    printf("-- M4: seed-push --\n");

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
    printf("net: SOC + curl init OK\n");

    if (cfg.watch_count > 0 && cfg.watches[0].path[0]) {
        const watch_config_t* w0 = &cfg.watches[0];
        matched_file_t* files = NULL;
        size_t count = 0;
        char serr[256];
        if (sdscan_watch(w0, &files, &count, serr, sizeof(serr)) != 0) {
            printf("scan: %s\n", serr);
        } else if (count == 0) {
            printf("no files in first watch to push\n");
        } else {
            const matched_file_t* f = &files[0];
            printf("pushing: %s\n", f->repo_path);
            printf("  from: %s\n", f->sd_path);
            printf("  ... uploading ...\n");
            gfxFlushBuffers();
            gfxSwapBuffers();

            char msg[160];
            snprintf(msg, sizeof(msg), "savesync: seed-push %.80s", f->repo_path);
            char aerr[512];
            int rc = api_seed_push(&cfg.github, token, f->repo_path, f->sd_path,
                                   msg, aerr, sizeof(aerr));
            if (rc == 0) {
                printf("  PUSHED OK — check GitHub for the commit!\n");
            } else {
                printf("  push FAILED: %s\n", aerr);
            }
        }
        sdscan_free(files);
    } else {
        printf("no watch to push\n");
    }

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
