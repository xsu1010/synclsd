#ifndef SAVESYNC_BLOBSHA_H
#define SAVESYNC_BLOBSHA_H

#include <stddef.h>

#define GIT_SHA1_HEX_LEN 41

int blobsha_file(const char* sd_path, char* hex_out, char* errbuf, size_t errbufsz);

#endif
