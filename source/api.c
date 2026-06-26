#include "api.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <mbedtls/base64.h>

#include "net.h"

static const char* find_field(const char* json, const char* field)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char* p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')) p++;
    return p;
}

static int extract_string_field(const char* json, const char* field,
                                char* out, size_t outsz)
{
    const char* p = find_field(json, field);
    if (!p || *p != '"') return -1;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            if (o + 1 >= outsz) return -1;
            out[o++] = p[1];
            p += 2;
        } else {
            if (o + 1 >= outsz) return -1;
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return (*p == '"') ? 0 : -1;
}

static char* extract_b64_field(const char* json, const char* field, size_t* out_len)
{
    *out_len = 0;
    const char* p = find_field(json, field);
    if (!p || *p != '"') return NULL;
    p++;
    const char* start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p += 2;
        else p++;
    }
    if (*p != '"') return NULL;
    size_t raw_len = (size_t)(p - start);

    char* out = malloc(raw_len + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (const char* q = start; q < p; q++) {
        if (*q == '\\' && q + 1 < p) {
            if (q[1] == 'n' || q[1] == 'r') {
                q++;
                continue;
            }
            out[o++] = q[1];
            q++;
        } else {
            out[o++] = *q;
        }
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

static char* b64_strip_newlines(const char* in, size_t in_len, size_t* out_len)
{
    *out_len = 0;
    char* out = malloc(in_len + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (in[i] != '\n' && in[i] != '\r' && in[i] != ' ' && in[i] != '\t') {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

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

static int build_and_put(const char* url, const char* bearer_token,
                         const char* branch, const char* commit_message,
                         const char* b64_content, const char* remote_sha,
                         char* errbuf, size_t errbufsz)
{
    char msg_esc[512];
    if (json_escape(commit_message, msg_esc, sizeof(msg_esc)) != 0) {
        snprintf(errbuf, errbufsz, "commit message too long");
        return -1;
    }

    size_t b64_len = strlen(b64_content);
    size_t sha_len = remote_sha ? strlen(remote_sha) : 0;
    size_t json_cap = b64_len + strlen(msg_esc) + strlen(branch) + sha_len + 160;
    char* body = malloc(json_cap);
    if (!body) {
        snprintf(errbuf, errbufsz, "alloc json failed");
        return -2;
    }

    int bn;
    if (remote_sha) {
        bn = snprintf(body, json_cap,
                      "{\"message\":\"%s\",\"branch\":\"%s\",\"content\":\"%s\",\"sha\":\"%s\"}",
                      msg_esc, branch, b64_content, remote_sha);
    } else {
        bn = snprintf(body, json_cap,
                      "{\"message\":\"%s\",\"branch\":\"%s\",\"content\":\"%s\"}",
                      msg_esc, branch, b64_content);
    }
    if (bn < 0 || (size_t)bn >= json_cap) {
        snprintf(errbuf, errbufsz, "json body overflow");
        free(body);
        return -3;
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
        return -4;
    }

    free(resp);
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

    int rc = build_and_put(url, bearer_token, gh->branch, commit_message, b64, NULL,
                           errbuf, errbufsz);
    free(b64);
    return rc;
}

int api_update_push(const github_config_t* gh,
                    const char* bearer_token,
                    const char* repo_path,
                    const char* sd_path,
                    const char* remote_sha,
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

    int rc = build_and_put(url, bearer_token, gh->branch, commit_message, b64, remote_sha,
                           errbuf, errbufsz);
    free(b64);
    return rc;
}

int api_get_remote(const github_config_t* gh,
                   const char* bearer_token,
                   const char* repo_path,
                   remote_file_t* out,
                   char* errbuf, size_t errbufsz)
{
    memset(out, 0, sizeof(*out));

    char url[CFG_STR_MAX * 2];
    int un = snprintf(url, sizeof(url),
                      "https://api.github.com/repos/%s/%s/contents%s?ref=%s",
                      gh->owner, gh->repo, repo_path, gh->branch);
    if (un < 0 || (size_t)un >= sizeof(url)) {
        snprintf(errbuf, errbufsz, "url too long");
        return -1;
    }

    char* body = NULL;
    size_t body_len = 0;
    long http_code = 0;
    int rc = https_get(url, bearer_token, &body, &body_len, &http_code, errbuf, errbufsz);

    if (rc != 0) {
        free(body);
        return rc;
    }

    if (http_code == 404) {
        out->present = 0;
        free(body);
        return 0;
    }

    if (http_code < 200 || http_code >= 300) {
        snprintf(errbuf, errbufsz, "HTTP %ld: %.*s",
                 http_code, (int)(body_len < 256 ? body_len : 256), body ? body : "");
        free(body);
        return -2;
    }

    out->present = 1;
    if (extract_string_field(body, "sha", out->sha, sizeof(out->sha)) != 0) {
        snprintf(errbuf, errbufsz, "no sha in response");
        free(body);
        return -3;
    }

    size_t raw_len = 0;
    char* raw_b64 = extract_b64_field(body, "content", &raw_len);
    free(body);
    if (!raw_b64) {
        snprintf(errbuf, errbufsz, "no content in response");
        return -4;
    }

    out->content_b64 = b64_strip_newlines(raw_b64, raw_len, &out->content_b64_len);
    free(raw_b64);
    if (!out->content_b64) {
        snprintf(errbuf, errbufsz, "oom");
        return -5;
    }

    return 0;
}

void remote_file_free(remote_file_t* rf)
{
    free(rf->content_b64);
    rf->content_b64 = NULL;
    rf->content_b64_len = 0;
}
