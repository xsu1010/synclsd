#include "pat.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

int pat_read(char* out_token, size_t outsz, char* errbuf, size_t errbufsz)
{
    FILE* fp = fopen(TOKEN_PATH, "r");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot open %s", TOKEN_PATH);
        return -1;
    }

    char buf[PAT_MAX];
    size_t got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[got] = '\0';

    char* start = buf;
    while (*start && isspace((unsigned char)*start)) start++;

    char* end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) end--;
    *end = '\0';

    if (end == start) {
        snprintf(errbuf, errbufsz, "token file empty");
        return -2;
    }

    if (strlen(start) >= outsz) {
        snprintf(errbuf, errbufsz, "token too long");
        return -3;
    }

    strcpy(out_token, start);
    return 0;
}
