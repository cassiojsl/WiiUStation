/*
 * WiiUStation - update from GitHub releases.
 *
 * Asks the GitHub API for the latest release of the project, compares its
 * tag (v1.2.3) with this build's version and downloads the release's .wuhb
 * over the installed one (sd:/wiiu/apps/WiiUStation.wuhb). The download
 * runs in a thread so the menu can show its progress.
 */
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include <nn/ac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif
#define UPDATE_API  "https://api.github.com/repos/cassiojsl/WiiUStation/releases/latest"
#define CA_FILE     "fs:/vol/content/cacert.pem"
#define WUHB_PATH   WIIU_SD_ROOT "/wiiu/apps/WiiUStation.wuhb"

const char *app_version(void) { return APP_VERSION; }

static int net_init(void)
{
	static int ready = -1;
	if (ready < 0) {
		ready = NNResult_IsSuccess(ACInitialize()) && NNResult_IsSuccess(ACConnect());
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	return ready;
}

static CURL *new_handle(const char *url)
{
	CURL *c = curl_easy_init();
	if (!c) return NULL;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, CA_FILE);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "WiiUStation-updater");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	return c;
}

/* ---- the release information ---- */
typedef struct { char *buf; size_t len; } mem_t;

static size_t mem_write(void *p, size_t sz, size_t n, void *ud)
{
	mem_t *m = ud;
	size_t add = sz * n;
	if (m->len + add > 256 * 1024) return 0;      /* far more than a release needs */
	char *nb = realloc(m->buf, m->len + add + 1);
	if (!nb) return 0;
	memcpy(nb + m->len, p, add);
	m->buf = nb;
	m->len += add;
	m->buf[m->len] = 0;
	return add;
}

/* the string value of "key": "..." after position from */
static int json_str(const char *js, const char *key, const char *from, char *out, size_t n)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(from ? from : js, pat);
	if (!p) return 0;
	p = strchr(p + strlen(pat), ':');
	if (!p) return 0;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	if (*p != '"') return 0;
	p++;
	size_t o = 0;
	while (*p && *p != '"' && o + 1 < n) {
		if (*p == '\\' && p[1]) p++;
		out[o++] = *p++;
	}
	out[o] = 0;
	return 1;
}

/* "v1.2.3" -> 1002003; -1 if it isn't a release number */
static long vnum(const char *v)
{
	int a = 0, b = 0, c = 0;
	if (*v == 'v' || *v == 'V') v++;
	if (sscanf(v, "%d.%d.%d", &a, &b, &c) < 2) return -1;
	return a * 1000000L + b * 1000L + c;
}

static char latestTag[32], latestUrl[512];

/* 1 newer version available, 0 up to date, -1 error (message in err) */
int update_check(char *tag, size_t tn, char *err, size_t en)
{
	mem_t m = { 0 };
	long code = 0;
	latestTag[0] = latestUrl[0] = 0;
	if (!net_init()) { snprintf(err, en, "%s", tr("No internet connection.")); return -1; }
	CURL *c = new_handle(UPDATE_API);
	if (!c) { snprintf(err, en, "%s", tr("Could not start the download.")); return -1; }
	struct curl_slist *h = curl_slist_append(NULL, "Accept: application/vnd.github+json");
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	curl_slist_free_all(h);
	if (rc != CURLE_OK || code != 200 || !m.buf) {
		snprintf(err, en, "%s", code == 404 ? tr("No release has been published yet.") : tr("Could not reach GitHub."));
		free(m.buf);
		return -1;
	}
	json_str(m.buf, "tag_name", NULL, latestTag, sizeof(latestTag));
	/* the first asset whose download URL ends in .wuhb */
	const char *p = m.buf;
	while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
		char url[512];
		if (json_str(m.buf, "browser_download_url", p, url, sizeof(url))) {
			size_t l = strlen(url);
			if (l > 5 && !strcmp(url + l - 5, ".wuhb")) { snprintf(latestUrl, sizeof(latestUrl), "%s", url); break; }
		}
		p++;
	}
	free(m.buf);
	snprintf(tag, tn, "%s", latestTag);
	if (!latestTag[0] || !latestUrl[0]) { snprintf(err, en, "%s", tr("The latest release has no .wuhb file.")); return -1; }
	long have = vnum(APP_VERSION), want = vnum(latestTag);
	/* a build between releases ("v1.2.0-5-gabc") counts as that release */
	if (want < 0) { snprintf(err, en, "%s", tr("Could not read the release version.")); return -1; }
	return have < 0 || want > have ? 1 : 0;
}

/* ---- the download ---- */
static volatile int dlState;           /* 0 idle, 1 running, 2 done, -1 failed */
static volatile double dlNow, dlTotal;

static int progress_cb(void *ud, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un)
{
	(void)ud; (void)ut; (void)un;
	dlTotal = (double)total;
	dlNow = (double)now;
	return 0;
}

static size_t file_write(void *p, size_t sz, size_t n, void *ud)
{
	return fwrite(p, sz, n, (FILE *)ud);
}

static int download_thread(void *ud)
{
	(void)ud;
	char tmp[300];
	long code = 0;
	snprintf(tmp, sizeof(tmp), "%s.part", WUHB_PATH);
	wiiu_mkdirs(WIIU_SD_ROOT "/wiiu/apps");
	FILE *f = fopen(tmp, "wb");
	CURL *c = f ? new_handle(latestUrl) : NULL;
	if (!c) { if (f) fclose(f); dlState = -1; return 0; }
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 64L * 1024 * 1024);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	fclose(f);
	if (rc != CURLE_OK || code != 200 || dlNow < 1024 * 1024) { remove(tmp); dlState = -1; return 0; }
	/* replace the installed app; it takes effect the next time it starts */
	remove(WUHB_PATH);
	dlState = rename(tmp, WUHB_PATH) == 0 ? 2 : -1;
	return 0;
}

int update_start(void)
{
	if (!latestUrl[0] || dlState == 1) return 0;
	dlState = 1;
	dlNow = dlTotal = 0;
	SDL_Thread *t = SDL_CreateThread(download_thread, "update", NULL);
	if (!t) { dlState = -1; return 0; }
	SDL_DetachThread(t);
	return 1;
}

/* state: 1 running, 2 done, -1 failed; *frac = 0..1 */
int update_progress(float *frac)
{
	*frac = dlTotal > 0 ? (float)(dlNow / dlTotal) : 0;
	return dlState;
}
