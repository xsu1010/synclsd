#include "state.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char* skip_ws(const char* p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static int parse_string_value(const char** pp, char* out, size_t outsz)
{
    const char* p = skip_ws(*pp);
    if (*p != '"') return -1;
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
    if (*p != '"') return -1;
    p++;
    out[o] = '\0';
    *pp = p;
    return 0;
}

int state_load(state_t* st, char* errbuf, size_t errbufsz)
{
    st->entries = calloc(16, sizeof(state_entry_t));
    if (!st->entries) {
        snprintf(errbuf, errbufsz, "oom");
        return -1;
    }
    st->count = 0;

    FILE* fp = fopen(STATE_PATH, "r");
    if (!fp) {
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 0 || fsize > 1024 * 1024) {
        snprintf(errbuf, errbufsz, "state.json too large");
        fclose(fp);
        state_free(st);
        return -1;
    }

    char* buf = malloc((size_t)fsize + 1);
    if (!buf) {
        snprintf(errbuf, errbufsz, "oom");
        fclose(fp);
        state_free(st);
        return -2;
    }
    size_t got = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    buf[got] = '\0';

    size_t cap = 16;

    const char* p = skip_ws(buf);
    if (*p != '{') {
        free(buf);
        free(st->entries);
        st->entries = NULL;
        return 0;
    }
    p++;
    p = skip_ws(p);

    while (*p && *p != '}') {
        if (*p == ',') { p++; p = skip_ws(p); }
        if (*p != '"') break;

        char repo_path[CFG_STR_MAX];
        if (parse_string_value(&p, repo_path, sizeof(repo_path)) != 0) break;
        p = skip_ws(p);
        if (*p != ':') break;
        p++;
        p = skip_ws(p);
        if (*p != '{') break;
        p++;
        p = skip_ws(p);

        state_entry_t e;
        memset(&e, 0, sizeof(e));
        snprintf(e.repo_path, sizeof(e.repo_path), "%s", repo_path);

        while (*p && *p != '}') {
            if (*p == ',') { p++; p = skip_ws(p); }
            if (*p != '"') break;
            char key[32];
            if (parse_string_value(&p, key, sizeof(key)) != 0) break;
            p = skip_ws(p);
            if (*p != ':') break;
            p++;
            p = skip_ws(p);
            if (*p == '"') {
                char val[64];
                if (parse_string_value(&p, val, sizeof(val)) == 0) {
                    if (strcmp(key, "base_sha") == 0) strcpy(e.base_sha, val);
                    else if (strcmp(key, "last_synced_utc") == 0) strcpy(e.last_synced_utc, val);
                }
            } else {
                while (*p && *p != ',' && *p != '}') p++;
            }
            p = skip_ws(p);
        }
        if (*p == '}') p++;
        p = skip_ws(p);

        if (st->count == cap) {
            size_t ncap = cap * 2;
            state_entry_t* ne = realloc(st->entries, ncap * sizeof(state_entry_t));
            if (!ne) { snprintf(errbuf, errbufsz, "oom"); free(buf); state_free(st); return -4; }
            st->entries = ne;
            cap = ncap;
        }
        st->entries[st->count++] = e;
    }

    free(buf);
    return 0;
}

int state_save(const state_t* st, char* errbuf, size_t errbufsz)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", STATE_PATH);

    FILE* fp = fopen(tmp, "w");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot write %s", tmp);
        return -1;
    }

    fputs("{\n", fp);
    for (size_t i = 0; i < st->count; i++) {
        const state_entry_t* e = &st->entries[i];
        fprintf(fp, "  \"%s\": { \"base_sha\": \"%s\", \"last_synced_utc\": \"%s\" }%s\n",
                e->repo_path, e->base_sha, e->last_synced_utc,
                (i + 1 < st->count) ? "," : "");
    }
    fputs("}\n", fp);

    if (fclose(fp) != 0) {
        snprintf(errbuf, errbufsz, "close failed");
        return -2;
    }

    remove(STATE_PATH);
    if (rename(tmp, STATE_PATH) != 0) {
        snprintf(errbuf, errbufsz, "rename failed");
        return -3;
    }
    return 0;
}

void state_free(state_t* st)
{
    free(st->entries);
    st->entries = NULL;
    st->count = 0;
}

state_entry_t* state_find(state_t* st, const char* repo_path)
{
    for (size_t i = 0; i < st->count; i++) {
        if (strcmp(st->entries[i].repo_path, repo_path) == 0) {
            return &st->entries[i];
        }
    }
    return NULL;
}

int state_upsert(state_t* st, const char* repo_path, const char* base_sha,
                 const char* last_synced_utc)
{
    if (!st->entries) {
        st->entries = calloc(16, sizeof(state_entry_t));
        if (!st->entries) return -1;
    }
    state_entry_t* e = state_find(st, repo_path);
    if (!e) {
        if (st->count >= STATE_MAX_ENTRIES) return -1;
        e = &st->entries[st->count++];
        strncpy(e->repo_path, repo_path, sizeof(e->repo_path) - 1);
        e->repo_path[sizeof(e->repo_path) - 1] = '\0';
    }
    strncpy(e->base_sha, base_sha, sizeof(e->base_sha) - 1);
    e->base_sha[sizeof(e->base_sha) - 1] = '\0';
    strncpy(e->last_synced_utc, last_synced_utc, sizeof(e->last_synced_utc) - 1);
    e->last_synced_utc[sizeof(e->last_synced_utc) - 1] = '\0';
    return 0;
}
