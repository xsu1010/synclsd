#ifndef SAVESYNC_API_H
#define SAVESYNC_API_H

#include "config.h"

int api_seed_push(const github_config_t* gh,
                  const char* bearer_token,
                  const char* repo_path,
                  const char* sd_path,
                  const char* commit_message,
                  char* errbuf, size_t errbufsz);

#endif
