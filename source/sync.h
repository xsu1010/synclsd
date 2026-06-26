#ifndef SAVESYNC_SYNC_H
#define SAVESYNC_SYNC_H

#include "config.h"
#include "state.h"

typedef enum {
    SYNC_IN_SYNC = 0,
    SYNC_PUSHED,
    SYNC_PULLED,
    SYNC_SEEDED,
    SYNC_CONFLICT,
    SYNC_SKIPPED,
    SYNC_ERROR
} sync_result_t;

typedef enum {
    RESOLVE_KEEP_LOCAL = 1,
    RESOLVE_TAKE_REMOTE = 2,
    RESOLVE_KEEP_BOTH = 3,
    RESOLVE_SKIP = 4
} resolve_action_t;

typedef struct {
    sync_result_t result;
    char detail[256];
} sync_outcome_t;

int sync_file(const github_config_t* gh,
              const char* bearer_token,
              const char* repo_path,
              const char* sd_path,
              state_t* st,
              sync_outcome_t* out);

int sync_resolve(const github_config_t* gh,
                 const char* bearer_token,
                 const char* repo_path,
                 const char* sd_path,
                 state_t* st,
                 resolve_action_t action,
                 sync_outcome_t* out);

const char* sync_result_str(sync_result_t r);

#endif
