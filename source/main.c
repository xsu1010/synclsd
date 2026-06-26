#include <stdio.h>
#include <3ds.h>

#include "config.h"
#include "sdscan.h"
#include "blobsha.h"

#define CONFIG_PATH SD_PREFIX "/3ds/savesync/config.toml"

static void print_config(const config_t* cfg)
{
    printf("\x1b[1;1H");
    printf("3DS Save Sync\n");
    printf("M1: config + file list\n\n");
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
