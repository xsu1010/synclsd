#ifndef SAVESYNC_NET_H
#define SAVESYNC_NET_H

#include <stddef.h>

#include "common.h"

#define CA_BUNDLE_PATH SD_PREFIX "/3ds/savesync/cacert.pem"
#define USER_AGENT     "savesync/0.1 (3DS homebrew)"

int net_init(char* errbuf, size_t errbufsz);
void net_exit(void);

int https_get(const char* url,
              const char* bearer_token,
              char** out_body, size_t* out_body_len,
              long* out_http_code,
              char* errbuf, size_t errbufsz);

int https_put(const char* url,
              const char* bearer_token,
              const char* body, size_t body_len,
              char** out_resp, size_t* out_resp_len,
              long* out_http_code,
              char* errbuf, size_t errbufsz);

#endif
