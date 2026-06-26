#include "api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <mbedtls/base64.h>

#include "net.h"

static char* read_file_base64(const char* sd_path, size_t* out_len, char* errbuf, size_t errbufsz)
{
    *out_len = 0;

    FILE* fp = fopen(sd_path, "rb");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot open %s", sd_path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 0) {
        snprintf(errbuf, errbufsz, "cannot size %s", sd_path);
        fclose(fp);
        return NULL;
    }

    unsigned char* raw = malloc((size_t)fsize);
    if (!raw) {
        snprintf(errbuf, errbufsz, "alloc raw failed");
        fclose(fp);
        return NULL;
    }

    size_t got = fread(raw, 1, (size_t)fsize, fp);
    fclose(fp);
    if (got != (size_t)fsize) {
        snprintf(errbuf, errbufsz, "short read %s", sd_path);
        free(raw);
        return NULL;
    }

    size_t b64_len = 0;
    int rc = mbedtls_base64_encode(NULL, 0, &b64_len, raw, (size_t)fsize);
    if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        snprintf(errbuf, errbufsz, "base64 size: %d", rc);
        free(raw);
        return NULL;
    }

    char* b64 = malloc(b64_len + 1);
    if (!b64) {
        snprintf(errbuf, errbufsz, "alloc b64 failed");
        free(raw);
        return NULL;
    }

    rc = mbedtls_base64_encode((unsigned char*)b64, b64_len + 1, &b64_len, raw, (size_t)fsize);
    free(raw);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "base64 encode: %d", rc);
        free(b64);
        return NULL;
    }

    b64[b64_len] = '\0';
    *out_len = b64_len;
    return b64;
}

static int json_escape(const char* in, char* out, size_t outsz)
{
    size_t o = 0;
    for (size_t i = 0; in[i]; i++) {
        char c = in[i];
        const char* esc = NULL;
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    if (o + 6 > outsz) return -1;
                    o += snprintf(out + o, outsz - o, "\\u%04x", (unsigned char)c);
                    continue;
                }
                if (o + 1 > outsz) return -1;
                out[o++] = c;
                continue;
        }
        if (o + 2 > outsz) return -1;
        out[o++] = esc[0];
        out[o++] = esc[1];
    }
    if (o + 1 > outsz) return -1;
    out[o] = '\0';
    return 0;
}

int api_seed_push(const github_config_t* gh,
                  const char* bearer_token,
                  const char* repo_path,
                  const char* sd_path,
                  const char* commit_message,
                  char* errbuf, size_t errbufsz)
{
    size_t b64_len = 0;
    char* b64 = read_file_base64(sd_path, &b64_len, errbuf, errbufsz);
    if (!b64) return -1;

    char url[CFG_STR_MAX * 2];
    int un = snprintf(url, sizeof(url),
                      "https://api.github.com/repos/%s/%s/contents%s?ref=%s",
                      gh->owner, gh->repo, repo_path, gh->branch);
    if (un < 0 || (size_t)un >= sizeof(url)) {
        snprintf(errbuf, errbufsz, "url too long");
        free(b64);
        return -2;
    }

    char msg_esc[512];
    if (json_escape(commit_message, msg_esc, sizeof(msg_esc)) != 0) {
        snprintf(errbuf, errbufsz, "commit message too long");
        free(b64);
        return -3;
    }

    size_t json_cap = b64_len + strlen(msg_esc) + strlen(gh->branch) + 128;
    char* body = malloc(json_cap);
    if (!body) {
        snprintf(errbuf, errbufsz, "alloc json failed");
        free(b64);
        return -4;
    }

    int bn = snprintf(body, json_cap,
                      "{\"message\":\"%s\",\"branch\":\"%s\",\"content\":\"%s\"}",
                      msg_esc, gh->branch, b64);
    free(b64);
    if (bn < 0 || (size_t)bn >= json_cap) {
        snprintf(errbuf, errbufsz, "json body overflow");
        free(body);
        return -5;
    }

    char* resp = NULL;
    size_t resp_len = 0;
    long http_code = 0;
    int rc = https_put(url, bearer_token, body, (size_t)bn,
                       &resp, &resp_len, &http_code, errbuf, errbufsz);
    free(body);

    if (rc != 0) {
        free(resp);
        return rc;
    }

    if (http_code < 200 || http_code >= 300) {
        snprintf(errbuf, errbufsz, "HTTP %ld: %.*s",
                 http_code, (int)(resp_len < 256 ? resp_len : 256), resp ? resp : "");
        free(resp);
        return -6;
    }

    free(resp);
    return 0;
}
