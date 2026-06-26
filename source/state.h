#ifndef SAVESYNC_STATE_H
#define SAVESYNC_STATE_H

#include <stddef.h>

#include "common.h"
#include "config.h"

#define STATE_PATH    SD_PREFIX "/3ds/savesync/state.json"
#define STATE_MAX_ENTRIES 256

typedef struct {
    char repo_path[CFG_STR_MAX];
    char base_sha[GIT_SHA1_HEX_LEN];
    char last_synced_utc[32];
} state_entry_t;

typedef struct {
    state_entry_t* entries;
    size_t count;
} state_t;

int state_load(state_t* st, char* errbuf, size_t errbufsz);
int state_save(const state_t* st, char* errbuf, size_t errbufsz);
void state_free(state_t* st);

state_entry_t* state_find(state_t* st, const char* repo_path);
int state_upsert(state_t* st, const char* repo_path, const char* base_sha,
                 const char* last_synced_utc);

#endif
