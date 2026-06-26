#ifndef SAVESYNC_SDSCAN_H
#define SAVESYNC_SDSCAN_H

#include <stddef.h>

#include "common.h"
#include "config.h"

typedef struct {
    char sd_path[CFG_STR_MAX * 2];
    char repo_path[CFG_STR_MAX];
} matched_file_t;

int sdscan_watch(const watch_config_t* watch,
                 matched_file_t** out_files, size_t* out_count,
                 char* errbuf, size_t errbufsz);

void sdscan_free(matched_file_t* files);

#endif
