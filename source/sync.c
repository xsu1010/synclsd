#include "sync.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include <mbedtls/base64.h>

#include "blobsha.h"
#include "net.h"
#include "api.h"
#include "common.h"

#define BACKUP_DIR SD_PREFIX "/3ds/savesync/backups"

static void iso_timestamp(char* out, size_t outsz)
{
    time_t t = time(NULL);
    struct tm* gm = gmtime(&t);
    strftime(out, outsz, "%Y-%m-%dT%H-%M-%SZ", gm);
}

static int ensure_backup_dir(void)
{
    DIR* d = opendir(BACKUP_DIR);
    if (d) { closedir(d); return 0; }
    return mkdir(BACKUP_DIR, 0777);
}

static int make_backup(const char* sd_path, char* errbuf, size_t errbufsz)
{
    if (ensure_backup_dir() != 0) {
        snprintf(errbuf, errbufsz, "cannot create %s", BACKUP_DIR);
        return -1;
    }

    FILE* src = fopen(sd_path, "rb");
    if (!src) return 0;

    const char* basename = strrchr(sd_path, '/');
    basename = basename ? basename + 1 : sd_path;

    char ts[32];
    iso_timestamp(ts, sizeof(ts));

    char bakpath[CFG_STR_MAX * 2];
    int n = snprintf(bakpath, sizeof(bakpath), "%s/%s@%s", BACKUP_DIR, basename, ts);
    if (n < 0 || (size_t)n >= sizeof(bakpath)) {
        snprintf(errbuf, errbufsz, "backup path too long");
        fclose(src);
        return -2;
    }

    FILE* dst = fopen(bakpath, "wb");
    if (!dst) {
        snprintf(errbuf, errbufsz, "cannot write %.200s", bakpath);
        fclose(src);
        return -3;
    }

    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, got, dst) != got) {
            snprintf(errbuf, errbufsz, "backup write failed");
            fclose(src);
            fclose(dst);
            return -4;
        }
    }

    fclose(src);
    if (fclose(dst) != 0) {
        snprintf(errbuf, errbufsz, "backup close failed");
        return -5;
    }
    return 0;
}

static int write_remote_to_sd(const char* sd_path, const remote_file_t* rf,
                              char* errbuf, size_t errbufsz)
{
    if (make_backup(sd_path, errbuf, errbufsz) != 0) return -1;

    size_t dec_len = 0;
    int rc = mbedtls_base64_decode(NULL, 0, &dec_len,
                                   (const unsigned char*)rf->content_b64,
                                   rf->content_b64_len);
    if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        snprintf(errbuf, errbufsz, "b64 size: %d", rc);
        return -2;
    }

    unsigned char* raw = malloc(dec_len ? dec_len : 1);
    if (!raw) {
        snprintf(errbuf, errbufsz, "oom");
        return -3;
    }

    size_t wrote = 0;
    rc = mbedtls_base64_decode(raw, dec_len, &wrote,
                               (const unsigned char*)rf->content_b64,
                               rf->content_b64_len);
    if (rc != 0) {
        snprintf(errbuf, errbufsz, "b64 decode: %d", rc);
        free(raw);
        return -4;
    }

    char tmp[CFG_STR_MAX * 2];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", sd_path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        snprintf(errbuf, errbufsz, "tmp path too long");
        free(raw);
        return -5;
    }

    FILE* fp = fopen(tmp, "wb");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot write %.200s", tmp);
        free(raw);
        return -6;
    }

    if (fwrite(raw, 1, wrote, fp) != wrote) {
        snprintf(errbuf, errbufsz, "write failed");
        fclose(fp);
        free(raw);
        return -7;
    }
    free(raw);

    if (fclose(fp) != 0) {
        snprintf(errbuf, errbufsz, "close failed");
        return -8;
    }

    remove(sd_path);
    if (rename(tmp, sd_path) != 0) {
        snprintf(errbuf, errbufsz, "rename failed");
        return -9;
    }

    return 0;
}

int sync_file(const github_config_t* gh,
              const char* bearer_token,
              const char* repo_path,
              const char* sd_path,
              state_t* st,
              sync_outcome_t* out)
{
    out->result = SYNC_ERROR;
    out->detail[0] = '\0';

    char local_sha[GIT_SHA1_HEX_LEN];
    char berr[256];
    if (blobsha_file(sd_path, local_sha, berr, sizeof(berr)) != 0) {
        snprintf(out->detail, sizeof(out->detail), "local sha: %.200s", berr);
        return -1;
    }

    state_entry_t* e = state_find(st, repo_path);
    const char* base_sha = e ? e->base_sha : NULL;

    int local_changed = base_sha ? (strcmp(local_sha, base_sha) != 0) : 1;

    remote_file_t rf;
    memset(&rf, 0, sizeof(rf));
    char aerr[512];
    if (api_get_remote(gh, bearer_token, repo_path, &rf, aerr, sizeof(aerr)) != 0) {
        snprintf(out->detail, sizeof(out->detail), "get remote: %.200s", aerr);
        out->result = SYNC_ERROR;
        return -2;
    }

    int remote_changed = 0;
    int remote_eq_local = 0;
    if (rf.present) {
        remote_changed = base_sha ? (strcmp(rf.sha, base_sha) != 0) : 1;
        remote_eq_local = (strcmp(rf.sha, local_sha) == 0);
    }

    char ts[32];
    iso_timestamp(ts, sizeof(ts));

    if (!base_sha && !rf.present) {
        char msg[160];
        snprintf(msg, sizeof(msg), "savesync: seed-push %.80s", repo_path);
        if (api_seed_push(gh, bearer_token, repo_path, sd_path, msg, aerr, sizeof(aerr)) != 0) {
            snprintf(out->detail, sizeof(out->detail), "seed: %.200s", aerr);
            out->result = SYNC_ERROR;
            remote_file_free(&rf);
            return -3;
        }
        state_upsert(st, repo_path, local_sha, ts);
        out->result = SYNC_SEEDED;
        remote_file_free(&rf);
        return 0;
    }

    if (!base_sha && rf.present) {
        if (remote_eq_local) {
            state_upsert(st, repo_path, local_sha, ts);
            out->result = SYNC_IN_SYNC;
        } else {
            out->result = SYNC_CONFLICT;
            snprintf(out->detail, sizeof(out->detail),
                     "new local vs existing remote");
        }
        remote_file_free(&rf);
        return 0;
    }

    if (!local_changed && !remote_changed) {
        out->result = SYNC_IN_SYNC;
        remote_file_free(&rf);
        return 0;
    }

    if (local_changed && !remote_changed) {
        char msg[160];
        snprintf(msg, sizeof(msg), "savesync: push %.80s", repo_path);
        if (api_update_push(gh, bearer_token, repo_path, sd_path, rf.sha, msg,
                            aerr, sizeof(aerr)) != 0) {
            snprintf(out->detail, sizeof(out->detail), "push: %.200s", aerr);
            out->result = SYNC_ERROR;
            remote_file_free(&rf);
            return -4;
        }
        state_upsert(st, repo_path, local_sha, ts);
        out->result = SYNC_PUSHED;
        remote_file_free(&rf);
        return 0;
    }

    if (!local_changed && remote_changed) {
        char werr[256];
        if (write_remote_to_sd(sd_path, &rf, werr, sizeof(werr)) != 0) {
            snprintf(out->detail, sizeof(out->detail), "pull write: %.200s", werr);
            out->result = SYNC_ERROR;
            remote_file_free(&rf);
            return -5;
        }
        state_upsert(st, repo_path, rf.sha, ts);
        out->result = SYNC_PULLED;
        remote_file_free(&rf);
        return 0;
    }

    if (local_changed && remote_changed) {
        if (remote_eq_local) {
            state_upsert(st, repo_path, local_sha, ts);
            out->result = SYNC_IN_SYNC;
        } else {
            out->result = SYNC_CONFLICT;
            snprintf(out->detail, sizeof(out->detail),
                     "both changed (local vs remote)");
        }
        remote_file_free(&rf);
        return 0;
    }

    out->result = SYNC_SKIPPED;
    remote_file_free(&rf);
    return 0;
}

const char* sync_result_str(sync_result_t r)
{
    switch (r) {
        case SYNC_IN_SYNC:  return "in-sync";
        case SYNC_PUSHED:   return "pushed";
        case SYNC_PULLED:   return "pulled";
        case SYNC_SEEDED:   return "seeded";
        case SYNC_CONFLICT: return "CONFLICT";
        case SYNC_SKIPPED:  return "skipped";
        case SYNC_ERROR:    return "ERROR";
        default:            return "?";
    }
}
