#ifndef SAVESYNC_STATUS_H
#define SAVESYNC_STATUS_H

#include "config.h"
#include "sdscan.h"
#include "state.h"

typedef enum {
    ST_IN_SYNC = 0,
    ST_WILL_PUSH,
    ST_WILL_PULL,
    ST_CONFLICT,
    ST_NEW_SEED,
    ST_ERROR
} file_status_t;

typedef struct {
    char repo_path[CFG_STR_MAX];
    char sd_path[CFG_STR_MAX * 2];
    char local_sha[GIT_SHA1_HEX_LEN];
    file_status_t status;
    int has_remote;
    char remote_sha[GIT_SHA1_HEX_LEN];
} file_entry_t;

typedef struct {
    file_entry_t* files;
    size_t count;
} file_list_t;

int status_scan(const config_t* cfg, const char* bearer_token, state_t* st,
                file_list_t* out);

void status_refresh_one(const github_config_t* gh, const char* bearer_token,
                        state_t* st, file_entry_t* fe);

void file_list_free(file_list_t* fl);

const char* status_str(file_status_t s);

#endif
