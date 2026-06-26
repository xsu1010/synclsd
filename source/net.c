#include "net.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/select.h>

#include <3ds.h>
#include <curl/curl.h>

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000

static u32* s_soc_buffer = NULL;
static int s_net_inited = 0;

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} write_ctx_t;

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    write_ctx_t* wc = (write_ctx_t*)userdata;
    size_t total = size * nmemb;

    if (wc->size + total + 1 > wc->cap) {
        size_t ncap = wc->cap ? wc->cap * 2 : 4096;
        while (ncap < wc->size + total + 1) ncap *= 2;
        char* nd = realloc(wc->data, ncap);
        if (!nd) return 0;
        wc->data = nd;
        wc->cap  = ncap;
    }

    memcpy(wc->data + wc->size, ptr, total);
    wc->size += total;
    wc->data[wc->size] = '\0';
    return total;
}

int net_init(char* errbuf, size_t errbufsz)
{
    if (s_net_inited) return 0;

    s_soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!s_soc_buffer) {
        snprintf(errbuf, errbufsz, "soc buffer alloc failed");
        return -1;
    }

    Result rc = socInit(s_soc_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(rc)) {
        snprintf(errbuf, errbufsz, "socInit: 0x%08lX", (unsigned long)rc);
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return -2;
    }

    CURLcode c = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (c != CURLE_OK) {
        snprintf(errbuf, errbufsz, "curl_global_init: %s", curl_easy_strerror(c));
        socExit();
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return -3;
    }

    s_net_inited = 1;
    return 0;
}

void net_exit(void)
{
    if (!s_net_inited) return;
    curl_global_cleanup();
    socExit();
    free(s_soc_buffer);
    s_soc_buffer = NULL;
    s_net_inited = 0;
}

int https_get(const char* url,
              const char* bearer_token,
              char** out_body, size_t* out_body_len,
              long* out_http_code,
              char* errbuf, size_t errbufsz)
{
    *out_body = NULL;
    *out_body_len = 0;
    if (out_http_code) *out_http_code = 0;

    CURL* h = curl_easy_init();
    if (!h) {
        snprintf(errbuf, errbufsz, "curl_easy_init failed");
        return -1;
    }

    write_ctx_t wc = {0, 0, 0};

    struct curl_slist* headers = NULL;
    char auth_hdr[256];
    int n = snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bearer_token);
    if (n < 0 || (size_t)n >= sizeof(auth_hdr)) {
        snprintf(errbuf, errbufsz, "auth header too long");
        curl_easy_cleanup(h);
        return -2;
    }
    headers = curl_slist_append(headers, auth_hdr);
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &wc);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(h, CURLOPT_CAINFO, CA_BUNDLE_PATH);

    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode c = curl_easy_perform(h);

    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    if (out_http_code) *out_http_code = code;

    int rc;
    if (c != CURLE_OK) {
        snprintf(errbuf, errbufsz, "curl: %s", curl_easy_strerror(c));
        free(wc.data);
        rc = -3;
    } else {
        *out_body = wc.data;
        *out_body_len = wc.size;
        rc = 0;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(h);
    return rc;
}
