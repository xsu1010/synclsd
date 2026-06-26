#include "sdscan.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

static int glob_match(const char* pattern, const char* name)
{
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            for (; *name; name++) {
                if (glob_match(pattern, name)) return 1;
            }
            return 0;
        } else if (*pattern == '?') {
            if (!*name) return 0;
            pattern++;
            name++;
        } else {
            if (*pattern != *name) return 0;
            pattern++;
            name++;
        }
    }
    return *name == '\0';
}

int sdscan_watch(const watch_config_t* watch,
                 matched_file_t** out_files, size_t* out_count,
                 char* errbuf, size_t errbufsz)
{
    *out_files = NULL;
    *out_count = 0;

    char dir_path[sizeof(SD_PREFIX) + CFG_STR_MAX];
    int n = snprintf(dir_path, sizeof(dir_path), "%s%s", SD_PREFIX, watch->path);
    if (n < 0 || (size_t)n >= sizeof(dir_path)) {
        snprintf(errbuf, errbufsz, "watch path too long");
        return -1;
    }

    DIR* d = opendir(dir_path);
    if (!d) {
        snprintf(errbuf, errbufsz, "cannot open %s", dir_path);
        return -2;
    }

    size_t cap = 8;
    size_t count = 0;
    matched_file_t* files = malloc(cap * sizeof(matched_file_t));
    if (!files) {
        closedir(d);
        snprintf(errbuf, errbufsz, "out of memory");
        return -3;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_DIR) continue;
        if (ent->d_type != DT_REG && ent->d_type != DT_UNKNOWN) continue;

        if (!glob_match(watch->glob, ent->d_name)) continue;

        if (count == cap) {
            size_t ncap = cap * 2;
            matched_file_t* nf = realloc(files, ncap * sizeof(matched_file_t));
            if (!nf) {
                free(files);
                closedir(d);
                snprintf(errbuf, errbufsz, "out of memory");
                return -3;
            }
            files = nf;
            cap = ncap;
        }

        matched_file_t* mf = &files[count];

        int m = snprintf(mf->repo_path, sizeof(mf->repo_path), "%s/%s",
                         watch->path, ent->d_name);
        if (m < 0 || (size_t)m >= sizeof(mf->repo_path)) {
            continue;
        }

        m = snprintf(mf->sd_path, sizeof(mf->sd_path), "%s/%s",
                     dir_path, ent->d_name);
        if (m < 0 || (size_t)m >= sizeof(mf->sd_path)) {
            continue;
        }

        count++;
    }

    closedir(d);
    *out_files = files;
    *out_count = count;
    return 0;
}

void sdscan_free(matched_file_t* files)
{
    free(files);
}
