#ifndef SAVESYNC_API_H
#define SAVESYNC_API_H

#include <stddef.h>

#include "config.h"
#include "common.h"

typedef struct {
    int       present;
    char      sha[GIT_SHA1_HEX_LEN];
    char*     content_b64;
    size_t    content_b64_len;
} remote_file_t;

int api_seed_push(const github_config_t* gh,
                  const char* bearer_token,
                  const char* repo_path,
                  const char* sd_path,
                  const char* commit_message,
                  char* errbuf, size_t errbufsz);

int api_update_push(const github_config_t* gh,
                    const char* bearer_token,
                    const char* repo_path,
                    const char* sd_path,
                    const char* remote_sha,
                    const char* commit_message,
                    char* errbuf, size_t errbufsz);

int api_get_remote(const github_config_t* gh,
                   const char* bearer_token,
                   const char* repo_path,
                   remote_file_t* out,
                   char* errbuf, size_t errbufsz);

void remote_file_free(remote_file_t* rf);

#endif
