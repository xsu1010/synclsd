#ifndef SAVESYNC_CONFIG_H
#define SAVESYNC_CONFIG_H

#include <stddef.h>

#define CFG_STR_MAX 512

typedef struct {
    char owner[128];
    char repo[128];
    char branch[64];
} github_config_t;

typedef struct {
    char name[128];
    char path[CFG_STR_MAX];
    char glob[64];
} watch_config_t;

typedef struct {
    github_config_t github;
    watch_config_t* watches;
    size_t watch_count;
} config_t;

int config_load(const char* path, config_t* cfg, char* errbuf, size_t errbufsz);
void config_free(config_t* cfg);

#endif
