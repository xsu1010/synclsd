#ifndef SAVESYNC_BLOBSHA_H
#define SAVESYNC_BLOBSHA_H

#include <stddef.h>

#include "common.h"

int blobsha_file(const char* sd_path, char* hex_out, char* errbuf, size_t errbufsz);

#endif
