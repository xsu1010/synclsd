#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <3ds.h>

#include "config.h"
#include "sdscan.h"
#include "blobsha.h"
#include "net.h"
#include "pat.h"

#define CONFIG_PATH SD_PREFIX "/3ds/savesync/config.toml"
#define API_BASE     "https://api.github.com/repos/"

static void print_config(const config_t* cfg)
{
    printf("\x1b[1;1H");
    printf("3DS Save Sync\n");
    printf("M3: HTTPS GET\n\n");
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

    printf("-- M3: HTTPS GET test --\n");

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
        char url[CFG_STR_MAX * 2];
        const watch_config_t* w0 = &cfg.watches[0];
        int un = snprintf(url, sizeof(url),
                          "%s%s/%s/contents%s?ref=%s",
                          API_BASE, cfg.github.owner, cfg.github.repo,
                          w0->path, cfg.github.branch);
        if (un < 0 || (size_t)un >= sizeof(url)) {
            printf("url: too long\n");
        } else {
            printf("GET %s\n", url);
            char* body = NULL;
            size_t body_len = 0;
            long http_code = 0;
            printf("... fetching ...\n");
            gfxFlushBuffers();
            gfxSwapBuffers();

            if (https_get(url, token, &body, &body_len, &http_code, nerr, sizeof(nerr)) == 0) {
                printf("HTTP %ld, %zu bytes\n", http_code, body_len);
                size_t show = body_len < 240 ? body_len : 240;
                printf("--- body (first %zu) ---\n", show);
                for (size_t i = 0; i < show; i++) {
                    char c = body[i];
                    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
                    putchar(c);
                }
                printf("\n--- end ---\n");
                free(body);
            } else {
                printf("https_get failed: %s\n", nerr);
            }
        }
    } else {
        printf("no watch to test\n");
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
