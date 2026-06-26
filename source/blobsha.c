#include "blobsha.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <mbedtls/sha1.h>

#define CHUNK_SIZE 4096

static void hex_encode(const unsigned char* bin, size_t len, char* hex)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2]     = digits[(bin[i] >> 4) & 0xF];
        hex[i * 2 + 1] = digits[bin[i] & 0xF];
    }
    hex[len * 2] = '\0';
}

int blobsha_file(const char* sd_path, char* hex_out, char* errbuf, size_t errbufsz)
{
    FILE* fp = fopen(sd_path, "rb");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot open %s", sd_path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 0) {
        snprintf(errbuf, errbufsz, "cannot size %s", sd_path);
        fclose(fp);
        return -2;
    }

    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);

    int rc = mbedtls_sha1_starts_ret(&ctx);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "sha1 starts: %d", rc);
        goto done;
    }

    char header[32];
    int hlen = snprintf(header, sizeof(header), "blob %ld", fsize);
    if (hlen < 0 || hlen >= (int)sizeof(header)) {
        snprintf(errbuf, errbufsz, "header overflow");
        rc = -3;
        goto done;
    }

    rc = mbedtls_sha1_update_ret(&ctx, (const unsigned char*)header, (size_t)hlen);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "sha1 hdr: %d", rc);
        goto done;
    }
    rc = mbedtls_sha1_update_ret(&ctx, (const unsigned char*)"\0", 1);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "sha1 nul: %d", rc);
        goto done;
    }

    unsigned char chunk[CHUNK_SIZE];
    size_t remaining = (size_t)fsize;
    while (remaining > 0) {
        size_t want = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
        size_t got = fread(chunk, 1, want, fp);
        if (got != want) {
            snprintf(errbuf, errbufsz, "short read %s", sd_path);
            rc = -4;
            goto done;
        }
        rc = mbedtls_sha1_update_ret(&ctx, chunk, got);
        if (rc != 0) {
            snprintf(errbuf, errbufsz, "sha1 update: %d", rc);
            goto done;
        }
        remaining -= got;
    }

    unsigned char digest[20];
    rc = mbedtls_sha1_finish_ret(&ctx, digest);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "sha1 finish: %d", rc);
        goto done;
    }

    hex_encode(digest, 20, hex_out);
    rc = 0;

done:
    mbedtls_sha1_free(&ctx);
    fclose(fp);
    return rc;
}
