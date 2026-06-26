#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "toml.h"

static int copy_string(char* dst, size_t dstsz, const char* src)
{
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    if (strlen(src) >= dstsz) {
        return -1;
    }
    strcpy(dst, src);
    return 0;
}

int config_load(const char* path, config_t* cfg, char* errbuf, size_t errbufsz)
{
    memset(cfg, 0, sizeof(*cfg));

    FILE* fp = fopen(path, "r");
    if (!fp) {
        snprintf(errbuf, errbufsz, "cannot open %s", path);
        return -1;
    }

    char err[256];
    toml_table_t* root = toml_parse_file(fp, err, sizeof(err));
    fclose(fp);
    if (!root) {
        snprintf(errbuf, errbufsz, "parse error: %s", err);
        return -2;
    }

    toml_table_t* gh = toml_table_in(root, "github");
    if (!gh) {
        snprintf(errbuf, errbufsz, "missing [github] table");
        toml_free(root);
        return -3;
    }

    toml_datum_t d_owner = toml_string_in(gh, "owner");
    toml_datum_t d_repo = toml_string_in(gh, "repo");
    toml_datum_t d_branch = toml_string_in(gh, "branch");

    if (!d_owner.ok || !d_repo.ok || !d_branch.ok) {
        snprintf(errbuf, errbufsz, "[github] requires owner, repo, branch");
        if (d_owner.ok) free(d_owner.u.s);
        if (d_repo.ok) free(d_repo.u.s);
        if (d_branch.ok) free(d_branch.u.s);
        toml_free(root);
        return -4;
    }

    int rc = 0;
    if (copy_string(cfg->github.owner, sizeof(cfg->github.owner), d_owner.u.s)) rc = -5;
    if (copy_string(cfg->github.repo, sizeof(cfg->github.repo), d_repo.u.s)) rc = -5;
    if (copy_string(cfg->github.branch, sizeof(cfg->github.branch), d_branch.u.s)) rc = -5;

    free(d_owner.u.s);
    free(d_repo.u.s);
    free(d_branch.u.s);

    if (rc) {
        snprintf(errbuf, errbufsz, "[github] field too long");
        toml_free(root);
        return rc;
    }

    toml_array_t* watches = toml_array_in(root, "watch");
    if (watches) {
        int n = toml_array_nelem(watches);
        if (n < 0) n = 0;
        cfg->watches = calloc(n, sizeof(watch_config_t));
        if (!cfg->watches) {
            snprintf(errbuf, errbufsz, "out of memory");
            toml_free(root);
            return -6;
        }
        cfg->watch_count = 0;

        for (int i = 0; i < n; i++) {
            toml_table_t* w = toml_table_at(watches, i);
            if (!w) continue;

            watch_config_t* wc = &cfg->watches[cfg->watch_count];

            toml_datum_t d_name = toml_string_in(w, "name");
            toml_datum_t d_wpath = toml_string_in(w, "path");
            toml_datum_t d_glob = toml_string_in(w, "glob");

            if (!d_wpath.ok || !d_glob.ok) {
                if (d_name.ok) free(d_name.u.s);
                if (d_wpath.ok) free(d_wpath.u.s);
                if (d_glob.ok) free(d_glob.u.s);
                continue;
            }

            if (copy_string(wc->name, sizeof(wc->name), d_name.ok ? d_name.u.s : NULL)) rc = -5;
            if (copy_string(wc->path, sizeof(wc->path), d_wpath.u.s)) rc = -5;
            if (copy_string(wc->glob, sizeof(wc->glob), d_glob.u.s)) rc = -5;

            if (d_name.ok) free(d_name.u.s);
            free(d_wpath.u.s);
            free(d_glob.u.s);

            if (rc) {
                snprintf(errbuf, errbufsz, "[[watch]] field too long");
                toml_free(root);
                return rc;
            }
            cfg->watch_count++;
        }
    }

    toml_free(root);
    return 0;
}

void config_free(config_t* cfg)
{
    free(cfg->watches);
    cfg->watches = NULL;
    cfg->watch_count = 0;
}
