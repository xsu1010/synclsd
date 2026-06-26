#ifndef SAVESYNC_PAT_H
#define SAVESYNC_PAT_H

#include <stddef.h>

#include "common.h"

#define TOKEN_PATH SD_PREFIX "/3ds/savesync/token.txt"
#define PAT_MAX    256

int pat_read(char* out_token, size_t outsz, char* errbuf, size_t errbufsz);

#endif
