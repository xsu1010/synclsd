#include "status.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "blobsha.h"
#include "api.h"

const char* status_str(file_status_t s)
{
    switch (s) {
        case ST_IN_SYNC:   return "in-sync  ";
        case ST_WILL_PUSH: return "will-push ";
        case ST_WILL_PULL: return "will-pull ";
        case ST_CONFLICT:  return "CONFLICT  ";
        case ST_NEW_SEED:  return "new-seed  ";
        case ST_ERROR:     return "ERROR     ";
        default:           return "?         ";
    }
}

static file_status_t compute_status(const char* local_sha, const char* base_sha,
                                    const remote_file_t* rf)
{
    int has_base = base_sha && base_sha[0];
    int local_changed = has_base ? (strcmp(local_sha, base_sha) != 0) : 1;

    if (!rf->present) {
        if (!has_base) return ST_NEW_SEED;
        if (!local_changed) return ST_IN_SYNC;
        return ST_WILL_PUSH;
    }

    int remote_changed = has_base ? (strcmp(rf->sha, base_sha) != 0) : 1;
    int remote_eq_local = (strcmp(rf->sha, local_sha) == 0);

    if (!has_base) {
        return remote_eq_local ? ST_IN_SYNC : ST_CONFLICT;
    }

    if (!local_changed && !remote_changed) return ST_IN_SYNC;
    if (local_changed && !remote_changed)  return ST_WILL_PUSH;
    if (!local_changed && remote_changed)  return ST_WILL_PULL;
    if (local_changed && remote_changed)   return remote_eq_local ? ST_IN_SYNC : ST_CONFLICT;
    return ST_ERROR;
}

void status_refresh_one(const github_config_t* gh, const char* bearer_token,
                        state_t* st, file_entry_t* fe)
{
    char berr[256];
    if (blobsha_file(fe->sd_path, fe->local_sha, berr, sizeof(berr)) != 0) {
        fe->status = ST_ERROR;
        return;
    }

    state_entry_t* e = state_find(st, fe->repo_path);
    const char* base_sha = e ? e->base_sha : NULL;

    remote_file_t rf;
    memset(&rf, 0, sizeof(rf));
    char aerr[256];
    if (api_get_remote(gh, bearer_token, fe->repo_path, &rf, aerr, sizeof(aerr)) != 0) {
        fe->status = ST_ERROR;
        fe->has_remote = 0;
        remote_file_free(&rf);
        return;
    }

    fe->has_remote = rf.present;
    if (rf.present) {
        strcpy(fe->remote_sha, rf.sha);
    } else {
        fe->remote_sha[0] = '\0';
    }

    fe->status = compute_status(fe->local_sha, base_sha, &rf);
    remote_file_free(&rf);
}

int status_scan(const config_t* cfg, const char* bearer_token, state_t* st,
                file_list_t* out)
{
    out->files = NULL;
    out->count = 0;

    size_t total = 0;
    for (size_t i = 0; i < cfg->watch_count; i++) {
        matched_file_t* files = NULL;
        size_t count = 0;
        char serr[256];
        if (sdscan_watch(&cfg->watches[i], &files, &count, serr, sizeof(serr)) != 0) {
            continue;
        }
        total += count;
        sdscan_free(files);
    }

    if (total == 0) return 0;

    out->files = calloc(total, sizeof(file_entry_t));
    if (!out->files) return -1;

    size_t idx = 0;
    for (size_t i = 0; i < cfg->watch_count; i++) {
        matched_file_t* files = NULL;
        size_t count = 0;
        char serr[256];
        if (sdscan_watch(&cfg->watches[i], &files, &count, serr, sizeof(serr)) != 0) {
            continue;
        }
        for (size_t j = 0; j < count; j++) {
            file_entry_t* fe = &out->files[idx++];
            strcpy(fe->repo_path, files[j].repo_path);
            strcpy(fe->sd_path, files[j].sd_path);
            status_refresh_one(&cfg->github, bearer_token, st, fe);
        }
        sdscan_free(files);
    }
    out->count = idx;
    return 0;
}

void file_list_free(file_list_t* fl)
{
    free(fl->files);
    fl->files = NULL;
    fl->count = 0;
}
