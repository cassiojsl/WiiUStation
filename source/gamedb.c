/*
 * WiiUStation - game details (release date, genre, developer, publisher, players, description).
 *
 * The data is libretro-database's "Sony - PlayStation.dat" (CC BY-SA 4.0), built from the PSX Data Center.
 * It is downloaded once to the SD card (about 7 MB) and read from there; it is not part of the app.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>
#include "platform.h"

#define DB_DIR   WIIU_APP_DIR "/db"
#define DB_FILE  DB_DIR "/psx.dat"
#define DB_URL   "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/Sony%20-%20PlayStation.dat"
#define CA_FILE  "fs:/vol/content/cacert.pem"

int gamedb_present(void)
{
	struct stat st;
	return stat(DB_FILE, &st) == 0 && st.st_size > 100000;
}

static size_t write_cb(void *p, size_t sz, size_t nm, void *ud) { return fwrite(p, sz, nm, (FILE *)ud); }

/* ---- the download: a thread, with progress and cancel (like the update download) ---- */
static volatile int dlState;            /* 0 idle, 1 running, 2 done, -1 failed, 3 cancelled */
static volatile int dlCancel;
static volatile double dlNow, dlTotal;

static int progress_cb(void *ud, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un)
{
	(void)ud; (void)ut; (void)un;
	dlTotal = (double)total;
	dlNow = (double)now;
	return dlCancel ? 1 : 0;              /* nonzero stops the transfer */
}

static int download_thread(void *ud)
{
	(void)ud;
	const char *tmp = DB_FILE ".part";
	if (!covers_net_ready()) { wlog("game details: no network"); dlState = -1; return 0; }
	wiiu_mkdirs(DB_DIR);
	FILE *f = fopen(tmp, "wb");
	if (!f) { dlState = -1; return 0; }
	CURL *c = curl_easy_init();
	if (!c) { fclose(f); remove(tmp); dlState = -1; return 0; }
	long code = 0;
	curl_easy_setopt(c, CURLOPT_URL, DB_URL);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, CA_FILE);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "WiiUStation");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);        /* give up after 60 s without data */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 40L * 1024 * 1024);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	fclose(f);
	if (dlCancel) { remove(tmp); wlog("game details: download cancelled"); dlState = 3; return 0; }
	if (rc == CURLE_OK && code == 200) {
		remove(DB_FILE);
		if (rename(tmp, DB_FILE) == 0 && gamedb_present()) { wlog("game details downloaded"); dlState = 2; return 0; }
	}
	wlog("game details: download failed (curl %d, http %ld)", (int)rc, code);
	remove(tmp);
	dlState = -1;
	return 0;
}

int gamedb_start(void)
{
	if (dlState == 1) return 0;
	dlCancel = 0;
	dlNow = dlTotal = 0;
	dlState = 1;
	SDL_Thread *t = SDL_CreateThreadWithStackSize(download_thread, "gamedb", 256 * 1024, NULL);   /* TLS needs a big stack */
	if (!t) { dlState = -1; return 0; }
	SDL_DetachThread(t);
	return 1;
}

void gamedb_cancel(void) { if (dlState == 1) dlCancel = 1; }

/* state: 1 running, 2 done, -1 failed, 3 cancelled; *frac = 0..1 (the file is about 6.7 MB when the server does not say) */
int gamedb_progress(float *frac)
{
	double total = dlTotal > 0 ? dlTotal : 6900000.0;
	double f = dlNow / total;
	*frac = f < 0 ? 0 : f > 1 ? 1 : (float)f;
	return dlState;
}

/* the text between the first and the last quote of a line */
static int quoted(const char *line, char *out, size_t n)
{
	const char *a = strchr(line, '"');
	const char *b = strrchr(line, '"');
	if (!a || b <= a) return 0;
	size_t l = (size_t)(b - a - 1);
	if (l >= n) l = n - 1;
	memcpy(out, a + 1, l);
	out[l] = 0;
	return 1;
}

int gamedb_lookup(const char *serial, gamedb_t *out)
{
	/* the last 32 answers (also "not found"), for a list that is scrolled back and forth */
	enum { NC = 32 };
	static struct { char serial[16]; int found; gamedb_t d; } cache[NC];
	static int next;
	if (!serial || !serial[0]) return 0;
	for (int i = 0; i < NC; i++)
		if (cache[i].serial[0] && !strcmp(cache[i].serial, serial)) { if (cache[i].found) *out = cache[i].d; return cache[i].found; }

	FILE *f = fopen(DB_FILE, "r");
	if (!f) return 0;
	static char buf[65536];
	setvbuf(f, buf, _IOFBF, sizeof(buf));
	static char line[4096];
	gamedb_t cur;
	char val[1500];
	int inGame = 0, match = 0, found = 0;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "game (", 6)) { memset(&cur, 0, sizeof(cur)); inGame = 1; match = 0; continue; }
		if (!inGame) continue;
		if (line[0] == ')') {
			if (match) { found = 1; break; }
			inGame = 0;
			continue;
		}
		if (line[0] != '\t' || line[1] == '\t') continue;       /* only the fields of the game itself */
		const char *k = line + 1;
		if (!strncmp(k, "serial ", 7)) {
			if (quoted(k, val, sizeof(val)) && !strcmp(val, serial)) match = 1;
			continue;
		}
		if (!match) continue;
		if (!strncmp(k, "description ", 12) && quoted(k, val, sizeof(val))) snprintf(cur.description, sizeof(cur.description), "%s", val);
		else if (!strncmp(k, "developer ", 10) && quoted(k, val, sizeof(val))) snprintf(cur.developer, sizeof(cur.developer), "%s", val);
		else if (!strncmp(k, "publisher ", 10) && quoted(k, val, sizeof(val))) snprintf(cur.publisher, sizeof(cur.publisher), "%s", val);
		else if (!strncmp(k, "genre ", 6) && quoted(k, val, sizeof(val))) snprintf(cur.genre, sizeof(cur.genre), "%s", val);
		else if (!strncmp(k, "releaseyear ", 12) && quoted(k, val, sizeof(val))) cur.year = atoi(val);
		else if (!strncmp(k, "releasemonth ", 13) && quoted(k, val, sizeof(val))) cur.month = atoi(val);
		else if (!strncmp(k, "releaseday ", 11) && quoted(k, val, sizeof(val))) cur.day = atoi(val);
		else if (!strncmp(k, "users ", 6) && quoted(k, val, sizeof(val))) cur.users = atoi(val);
	}
	fclose(f);
	snprintf(cache[next].serial, sizeof(cache[next].serial), "%s", serial);
	cache[next].found = found;
	if (found) { cache[next].d = cur; *out = cur; }
	next = (next + 1) % NC;
	return found;
}
