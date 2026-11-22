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
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <unistd.h>

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

static char latestTag[32], latestUrl[512], latestBody[4096];

/* a JSON string with its escapes decoded (\n, \", \uXXXX...) */
static int json_text(const char *js, const char *key, char *out, size_t n)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(js, pat);
	size_t o = 0;
	out[0] = 0;
	if (!p) return 0;
	p = strchr(p + strlen(pat), ':');
	if (!p) return 0;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	if (*p != '"') return 0;
	p++;
	while (*p && *p != '"' && o + 4 < n) {
		if (*p != '\\') { out[o++] = *p++; continue; }
		p++;
		switch (*p) {
		case 'n': out[o++] = '\n'; p++; break;
		case 'r': p++; break;
		case 't': out[o++] = ' '; p++; break;
		case 'u': {
			unsigned u = 0;
			for (int i = 1; i <= 4 && p[i]; i++) u = u * 16 + (unsigned)((p[i] <= '9' ? p[i] - '0' : (p[i] | 32) - 'a' + 10) & 15);
			p += 5;
			if (u < 0x80) out[o++] = (char)u;
			else if (u < 0x800) { out[o++] = (char)(0xC0 | (u >> 6)); out[o++] = (char)(0x80 | (u & 63)); }
			else if (u >= 0xD800 && u < 0xE000) out[o++] = '?';
			else { out[o++] = (char)(0xE0 | (u >> 12)); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
			break;
		}
		default: if (*p) out[o++] = *p++; break;
		}
	}
	out[o] = 0;
	return 1;
}

/* the text of the newest release found by update_check ("" when none) */
const char *update_notes(void) { return latestBody; }

static size_t file_write_cb(void *p, size_t sz, size_t n, void *ud)
{
	return fwrite(p, sz, n, (FILE *)ud);
}

/* 1 newer version available, 0 up to date, -1 error (message in err) */
int update_check(char *tag, size_t tn, char *err, size_t en)
{
	mem_t m = { 0 };
	long code = 0;
	latestTag[0] = latestUrl[0] = latestBody[0] = 0;
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
		wlog("update check failed: curl %d, http %ld", (int)rc, code);
		snprintf(err, en, "%s", code == 404 ? tr("No release has been published yet.") : tr("Could not reach GitHub."));
		free(m.buf);
		return -1;
	}
	json_str(m.buf, "tag_name", NULL, latestTag, sizeof(latestTag));
	json_text(m.buf, "body", latestBody, sizeof(latestBody));
	{   /* GitHub adds "**Full Changelog**: <url>" at the end: not shown */
		char *fc = strstr(latestBody, "Full Changelog");
		if (fc) {
			while (fc > latestBody && fc[-1] == '*') fc--;
			*fc = 0;
		}
		size_t l = strlen(latestBody);
		while (l > 0 && (latestBody[l - 1] == '\n' || latestBody[l - 1] == ' ')) latestBody[--l] = 0;
	}
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

/* a small file to disk: 1 ok, -1 not found (404), 0 failed */
int net_download(const char *url, const char *path)
{
	char tmp[600];
	long code = 0;
	if (!net_init()) return 0;
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return 0;
	CURL *c = new_handle(url);
	if (!c) { fclose(f); remove(tmp); return 0; }
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 4L * 1024 * 1024);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	fclose(f);
	if (rc == CURLE_OK && code == 200) {
		remove(path);
		if (rename(tmp, path) == 0) return 1;
	}
	remove(tmp);
	return rc == CURLE_OK && code == 404 ? -1 : 0;
}

/* ---- the download ---- */
static volatile int dlState;           /* 0 idle, 1 running, 2 done, -1 failed, 3 cancelled */
static volatile int dlCancel;          /* the player asked to stop */
static volatile double dlNow, dlTotal;

static int progress_cb(void *ud, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un)
{
	(void)ud; (void)ut; (void)un;
	dlTotal = (double)total;
	dlNow = (double)now;
	return dlCancel ? 1 : 0;               /* nonzero stops the transfer */
}



static char dlErr[160];               /* why the download failed */

static long file_size(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* writes src over dst in place (same file, new contents, cut to the new
 * length): works on a file that is open and can't be replaced */
static int overwrite_file(const char *src, const char *dst)
{
	FILE *in = fopen(src, "rb");
	if (!in) return 0;
	FILE *out = fopen(dst, "r+b");
	if (!out) { fclose(in); return 0; }
	static char buf[64 * 1024];
	size_t n;
	long total = 0;
	int ok = 1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
		total += (long)n;
	}
	fclose(in);
	if (fflush(out) != 0) ok = 0;
	if (ok && ftruncate(fileno(out), total) != 0) ok = 0;
	fclose(out);
	return ok && file_size(dst) == total;
}

static int download_thread(void *ud)
{
	(void)ud;
	char tmp[300];
	long code = 0;
	dlErr[0] = 0;
	/* downloaded into the data folder first (a .bin, so the Wii U menu
	 * doesn't list it as an app) */
	snprintf(tmp, sizeof(tmp), "%s/update.bin", WIIU_APP_DIR);
	wiiu_mkdirs(WIIU_APP_DIR);
	remove(tmp);
	FILE *f = fopen(tmp, "wb");
	if (!f) { snprintf(dlErr, sizeof(dlErr), "%s", tr("Could not write to the SD card.")); wlog("update download failed: %s", dlErr); dlState = -1; return 0; }
	CURL *c = new_handle(latestUrl);
	if (!c) { fclose(f); remove(tmp); snprintf(dlErr, sizeof(dlErr), "%s", tr("Could not start the download.")); wlog("update download failed: %s", dlErr); dlState = -1; return 0; }
	char cerr[CURL_ERROR_SIZE] = "";
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 64L * 1024 * 1024);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, cerr);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);      /* give up after 60 s without data */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	int wok = fflush(f) == 0;
	fclose(f);
	long got = file_size(tmp);
	if (dlCancel) {                       /* stopped by the player: nothing is kept */
		remove(tmp);
		wlog("update download cancelled");
		dlState = 3;
		return 0;
	}
	if (rc != CURLE_OK || code != 200) {
		remove(tmp);
		if (rc != CURLE_OK) snprintf(dlErr, sizeof(dlErr), tr("Download error %d: %s"), (int)rc, cerr[0] ? cerr : curl_easy_strerror(rc));
		else snprintf(dlErr, sizeof(dlErr), tr("The server answered %ld."), code);
		wlog("update download failed: %s", dlErr);
		dlState = -1;
		return 0;
	}
	if (!wok || got < 1024 * 1024) {
		remove(tmp);
		snprintf(dlErr, sizeof(dlErr), tr("The downloaded file is incomplete (%ld bytes)."), got);
		wlog("update download failed: %s", dlErr);
		dlState = -1;
		return 0;
	}
	/* ready: the file is swapped when the app closes (update_finish) */
	wlog("update %s downloaded (%ld bytes)", latestTag, got);
	dlState = 2;
	return 0;
}

const char *update_error(void) { return dlErr; }

/* stop the download that is running (update_progress then returns 3) */
void update_cancel(void) { if (dlState == 1) dlCancel = 1; }

int update_start(void)
{
	if (!latestUrl[0] || dlState == 1) return 0;
	dlCancel = 0;
	dlState = 1;
	dlNow = dlTotal = 0;
	/* curl with TLS needs a large stack */
	SDL_Thread *t = SDL_CreateThreadWithStackSize(download_thread, "update", 256 * 1024, NULL);
	if (!t) { wlog("update download failed: %s", dlErr); dlState = -1; return 0; }
	SDL_DetachThread(t);
	return 1;
}

/* state: 1 running, 2 downloaded (installed when the app closes), -1 failed; *frac = 0..1 */
int update_progress(float *frac)
{
	*frac = dlTotal > 0 ? (float)(dlNow / dlTotal) : 0;
	return dlState;
}

/* ---- a check in the background when the menu starts, for the notice on
 * the home screen ---- */
static volatile int bgState;           /* 0 not started, 1 checking, 2 newer, 3 current or error */
static char bgTag[32];

static int bg_thread(void *ud)
{
	(void)ud;
	char tag[32], err[128];
	SDL_Delay(4000);                    /* leave the start-up to the menu */
	int r = update_check(tag, sizeof(tag), err, sizeof(err));
	if (r == 1) snprintf(bgTag, sizeof(bgTag), "%s", tag);
	bgState = r == 1 ? 2 : 3;
	return 0;
}

void update_check_background(void)
{
	if (bgState) return;
	bgState = 1;
	SDL_Thread *t = SDL_CreateThread(bg_thread, "updchk", NULL);
	if (!t) { bgState = 3; return; }
	SDL_DetachThread(t);
}

/* the newer version found by the background check, or NULL */
const char *update_available(void)
{
	return bgState == 2 ? bgTag : NULL;
}

/* ---- installing: the running app's file is locked while its bundle is
 * mounted, so the swap happens as the app closes: the bundle is unmounted
 * through Aroma's RPX loader, the new file takes the old one's place and
 * the app is started again. ---- */
#include <coreinit/dynload.h>
#include <coreinit/title.h>

static int installPending;
void update_install_on_exit(void) { installPending = 1; }
int update_pending(void) { return installPending; }

typedef int (*rl_fn0)(void);
typedef int (*rl_path)(const char *);
typedef int (*rl_buf)(char *, uint32_t);
typedef int (*rl_ver)(uint32_t *);
static OSDynLoad_Module rlMod;

static void *rl_find(const char *name)
{
	void *f = NULL;
	if (!rlMod && OSDynLoad_Acquire("homebrew_rpx_loader", &rlMod) != OS_DYNLOAD_OK) { rlMod = NULL; return NULL; }
	if (OSDynLoad_FindExport(rlMod, OS_DYNLOAD_EXPORT_FUNC, name, &f) != OS_DYNLOAD_OK) return NULL;
	return f;
}

/* the running app's file, relative to the SD card ("wiiu/apps/...") */
static int running_path(char *out, size_t n)
{
	rl_ver ver = (rl_ver)rl_find("RL_GetVersion");
	rl_buf get = (rl_buf)rl_find("RL_GetPathOfRunningExecutable");
	uint32_t v = 0;
	if (!ver || ver(&v) != 0 || v < 2 || !get) return 0;
	if (get(out, (uint32_t)n) != 0 || !out[0]) return 0;
	while (out[0] == '/') memmove(out, out + 1, strlen(out));
	size_t l = strlen(out);
	return l > 5 && !strcasecmp(out + l - 5, ".wuhb");
}

/* 1 = the new version is in place and is being started */
int update_finish(void)
{
	char staged[300], rel[256] = "", full[400];
	if (!installPending) return 0;
	installPending = 0;
	snprintf(staged, sizeof(staged), "%s/update.bin", WIIU_APP_DIR);
	if (file_size(staged) < 1024 * 1024) return 0;
	if (!running_path(rel, sizeof(rel))) snprintf(rel, sizeof(rel), "wiiu/apps/WiiUStation.wuhb");
	snprintf(full, sizeof(full), "%s/%s", WIIU_SD_ROOT, rel);
	rl_fn0 unmount = (rl_fn0)rl_find("RL_UnmountCurrentRunningBundle");
	if (unmount) unmount();                      /* frees the file */
	int ok = 0;
	if (remove(full) == 0 || file_size(full) < 0) ok = rename(staged, full) == 0;
	if (!ok) ok = overwrite_file(staged, full) && remove(staged) == 0;
	if (!ok) { wlog("update: could not replace %s", full); return 0; }
	wlog("update installed: %s", full);
	rl_path launch = (rl_path)rl_find("RL_LaunchHomebrew");
	return launch && launch(rel) == 0;
}

/* on start: leftovers of earlier update attempts */
void update_cleanup(void)
{
	char rel[256] = "";
	running_path(rel, sizeof(rel));
	remove(WUHB_PATH ".old");
	remove(WUHB_PATH ".part");
	DIR *d = opendir(WIIU_SD_ROOT "/wiiu/apps");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		/* WiiUStation-v1.2.3.wuhb saved next to the app by 1.0.1 - 1.0.3 */
		if (strncmp(e->d_name, "WiiUStation-v", 13)) continue;
		size_t l = strlen(e->d_name);
		if (l < 6 || strcasecmp(e->d_name + l - 5, ".wuhb")) continue;
		char p[400];
		snprintf(p, sizeof(p), "wiiu/apps/%s", e->d_name);
		if (!strcmp(p, rel)) continue;           /* the one running */
		snprintf(p, sizeof(p), "%s/wiiu/apps/%s", WIIU_SD_ROOT, e->d_name);
		remove(p);
	}
	closedir(d);
}
