/*
 * WiiUStation - game covers.
 *
 *  1. Game ID: read SYSTEM.CNF from the disc image (ISO9660 inside .bin /
 *     .iso / .img, following .cue and .m3u), the DISC_ID of a .pbp, an ID in
 *     the file name, or the ID PCSX-ReARMed reports after loading (.chd).
 *     Results are remembered in sd:/wiiu/apps/WiiUStation/covers/ids.txt.
 *  2. Cover: sd:/wiiu/apps/WiiUStation/covers/<ID>.jpg, downloaded in the background from
 *     github.com/xlenore/psx-covers (covers/default/<ID>.jpg) over HTTPS,
 *     verified against Mozilla's root store (content/cacert.pem).
 *     3D boxes (Settings -> Covers: 3D) come from covers/3d/<ID>.png and
 *     are stored in covers/3d/. Without a 3D box the 2D cover is shown.
 *  3. Textures are cached for the menu.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
#include <nn/ac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

#include "platform.h"

#define COVER_DIR   WIIU_COVER_DIR
#define ID_FILE     COVER_DIR "/ids.txt"
#define COVER_URL   "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/%s.jpg"
#define COVER3D_URL "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/3d/%s.png"
#define COVER3D_DIR COVER_DIR "/3d"

/* Covers are tracked by a key: "<ID>" for the 2D cover, "<ID>#3" for the
 * 3D box (a PNG with transparency). */
static int key_is_3d(const char *key) { const char *h = strchr(key, '#'); return h && h[1] == '3'; }
static void key_id(const char *key, char *id, size_t n)
{
	snprintf(id, n, "%s", key);
	char *h = strchr(id, '#');
	if (h) *h = 0;
}
#define CA_FILE     "fs:/vol/content/cacert.pem"

static int file_exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && st.st_size > 0;
}

int covers_normalise_id(const char *in, char *out);
void covers_remember_id(const char *path, const char *id);

/* Called after a game was loaded: the core knows the ID even for .chd. */
void covers_set_loaded_id(const char *path, const char *cdromId)
{
	char id[16];
	if (cdromId && covers_normalise_id(cdromId, id)) {
		covers_remember_id(path, id);
		covers_request(id);
	}
}

/* ------------------------------------------------------------------------ */
/* background downloader                                                    */
/* ------------------------------------------------------------------------ */
#define QSIZE 32
static char queue[QSIZE][16];
static volatile int qhead, qtail;
static SDL_mutex *qlock;
static SDL_sem *qsem;
static SDL_Thread *worker;
static int netReady = -1;

/* Per-ID download state. MISSING (HTTP 404: the repository has no cover for
 * this ID) is remembered on the SD card in covers/missing.txt, so it is never
 * requested again. FAILED (network error) is only kept for this session. */
enum { ST_NONE = 0, ST_PENDING, ST_MISSING, ST_FAILED, ST_DONE };
#define MAXST 512
static struct { char id[16]; int st; } states[MAXST];
static int nstates, missingLoaded;
#define MISSING_FILE COVER_DIR "/missing.txt"

static int *state_of(const char *id, int create)
{
	for (int i = 0; i < nstates; i++)
		if (!strcmp(states[i].id, id)) return &states[i].st;
	if (!create || nstates >= MAXST) return NULL;
	strncpy(states[nstates].id, id, sizeof(states[nstates].id) - 1);
	states[nstates].st = ST_NONE;
	return &states[nstates++].st;
}

static void missing_load(void)
{
	FILE *f;
	char line[32];
	if (missingLoaded) return;
	missingLoaded = 1;
	f = fopen(MISSING_FILE, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		int *st = line[0] ? state_of(line, 1) : NULL;
		if (st) *st = ST_MISSING;
	}
	fclose(f);
}

static void missing_add(const char *id)
{
	FILE *f;
	wiiu_mkdirs(COVER_DIR);
	f = fopen(MISSING_FILE, "a");
	if (!f) return;
	fprintf(f, "%s\n", id);
	fclose(f);
}

static void cover_path(const char *key, char *out, size_t n)
{
	char id[16];
	key_id(key, id, sizeof(id));
	if (key_is_3d(key)) snprintf(out, n, "%s/%s.png", COVER3D_DIR, id);
	else snprintf(out, n, "%s/%s.jpg", COVER_DIR, id);
}

static size_t write_cb(void *p, size_t sz, size_t nm, void *ud)
{
	return fwrite(p, sz, nm, (FILE *)ud);
}

static int download(const char *key)
{
	char id[16], url[256], path[256], tmp[260];
	CURL *c;
	FILE *f;
	long code = 0;
	CURLcode rc;

	if (netReady < 0) {
		netReady = NNResult_IsSuccess(ACInitialize()) && NNResult_IsSuccess(ACConnect());
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	if (!netReady) return 0;

	key_id(key, id, sizeof(id));
	snprintf(url, sizeof(url), key_is_3d(key) ? COVER3D_URL : COVER_URL, id);
	cover_path(key, path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	wiiu_mkdirs(key_is_3d(key) ? COVER3D_DIR : COVER_DIR);
	f = fopen(tmp, "wb");
	if (!f) return 0;
	c = curl_easy_init();
	if (!c) { fclose(f); return 0; }
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_CAINFO, CA_FILE);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "WiiUStation");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 4L * 1024 * 1024);
	rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	fclose(f);
	if (rc == CURLE_OK && code == 200) {
		rename(tmp, path);
		return 1;
	}
	remove(tmp);
	return rc == CURLE_OK && code == 404 ? -1 : 0;
}

static int worker_main(void *ud)
{
	(void)ud;
	for (;;) {
		char id[16];
		SDL_SemWait(qsem);
		SDL_LockMutex(qlock);
		if (qhead == qtail) { SDL_UnlockMutex(qlock); continue; }
		strcpy(id, queue[qhead]);
		qhead = (qhead + 1) % QSIZE;
		SDL_UnlockMutex(qlock);
		if (!strcmp(id, "QUIT")) break;
		int r = download(id);
		SDL_LockMutex(qlock);
		int *st = state_of(id, 1);
		if (st) *st = r > 0 ? ST_DONE : r < 0 ? ST_MISSING : ST_FAILED;
		SDL_UnlockMutex(qlock);
		if (r < 0) missing_add(id);
	}
	return 0;
}

static void request_key(const char *id)
{
	char path[256];
	if (!id || !id[0]) return;
	cover_path(id, path, sizeof(path));
	if (file_exists(path)) return;
	if (!qlock) {
		qlock = SDL_CreateMutex();
		qsem = SDL_CreateSemaphore(0);
		worker = SDL_CreateThread(worker_main, "covers", NULL);
	}
	SDL_LockMutex(qlock);
	missing_load();
	int *st = state_of(id, 1);
	if (st && *st == ST_NONE && (qtail + 1) % QSIZE != qhead) {
		*st = ST_PENDING;          /* queued once; never every frame */
		strcpy(queue[qtail], id);
		qtail = (qtail + 1) % QSIZE;
		SDL_SemPost(qsem);
	}
	SDL_UnlockMutex(qlock);
}

static int state_get(const char *id)
{
	int r;
	if (!qlock) { qlock = SDL_CreateMutex(); qsem = SDL_CreateSemaphore(0);
	              worker = SDL_CreateThread(worker_main, "covers", NULL); }
	SDL_LockMutex(qlock);
	missing_load();
	int *st = state_of(id, 0);
	r = st ? *st : ST_NONE;
	SDL_UnlockMutex(qlock);
	return r;
}

static void state_set(const char *id, int v)
{
	SDL_LockMutex(qlock);
	int *st = state_of(id, 1);
	if (st) *st = v;
	SDL_UnlockMutex(qlock);
}

/* the key to show for this ID: the 3D box when chosen and available,
 * else the 2D cover */
static void show_key(const char *id, char *key, size_t n)
{
	if (cfg.cover3d) {
		snprintf(key, n, "%s#3", id);
		int st = state_get(key);
		if (st != ST_MISSING) return;          /* have it, or still trying */
	}
	snprintf(key, n, "%s", id);
}

/* ---- custom covers chosen in the game options: covers/custom/<hash>.png,
 * shown through the id "@<hash>" (hash of the game's file name) ---- */
#define CUSTOM_DIR COVER_DIR "/custom"

static unsigned fnv(const char *s)
{
	unsigned h = 2166136261u;
	while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
	return h;
}

static void custom_path(const char *id, char *out, size_t n)
{
	snprintf(out, n, "%s/%s.png", CUSTOM_DIR, id + 1);
}

int covers_custom_id(const char *gameKey, char *out)
{
	char p[256];
	snprintf(out, 16, "@%08X", fnv(gameKey));
	custom_path(out, p, sizeof(p));
	return file_exists(p);
}

int covers_set_custom(const char *gameKey, const char *image)
{
	char id[16], p[256], buf[8192];
	size_t n;
	int ok = 1;
	covers_custom_id(gameKey, id);
	custom_path(id, p, sizeof(p));
	wiiu_mkdirs(CUSTOM_DIR);
	FILE *in = fopen(image, "rb"), *out = in ? fopen(p, "wb") : NULL;
	if (!in || !out) ok = 0;
	while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n) ok = 0;
	if (in) fclose(in);
	if (out) fclose(out);
	if (!ok) remove(p);
	covers_flush();
	return ok;
}

void covers_remove_custom(const char *gameKey)
{
	char id[16], p[256];
	covers_custom_id(gameKey, id);
	custom_path(id, p, sizeof(p));
	remove(p);
	covers_flush();
}

/* the cover of this ID (2D or 3D, as chosen) is already on the SD card */
int covers_have(const char *id)
{
	char key[16], p[256];
	if (!id || !id[0]) return 0;
	show_key(id, key, sizeof(key));
	cover_path(key, p, sizeof(p));
	return file_exists(p);
}

/* the repository has no cover for it (remembered in missing.txt) */
int covers_missing(const char *id)
{
	char key[16];
	if (!id || !id[0]) return 0;
	show_key(id, key, sizeof(key));
	return state_get(key) == ST_MISSING;
}

void covers_request(const char *id)
{
	char key[16];
	if (!id || !id[0] || id[0] == '@') return;
	show_key(id, key, sizeof(key));
	request_key(key);
}

/* 2 = cover available, 1 = being downloaded, 0 = no cover (no SD access) */
int covers_status(const char *id)
{
	char key[16];
	if (!id || !id[0]) return 0;
	if (id[0] == '@') { char p[256]; custom_path(id, p, sizeof(p)); return file_exists(p) ? 2 : 0; }
	show_key(id, key, sizeof(key));
	int st = state_get(key);
	return st == ST_DONE ? 2 : st == ST_PENDING ? 1 : 0;
}

void covers_shutdown(void)
{
	if (!worker) return;
	SDL_LockMutex(qlock);
	qhead = qtail = 0;
	strcpy(queue[0], "QUIT");
	qtail = 1;
	SDL_UnlockMutex(qlock);
	SDL_SemPost(qsem);
	SDL_WaitThread(worker, NULL);
	worker = NULL;
	curl_global_cleanup();
	if (netReady > 0) ACClose();
}

/* ---- network status for the menu (Wi-Fi icon) ----
 * The connection is started in the background the first time; after that
 * the state is asked again at most every two seconds. */
int net_status(void)
{
	static int started, last = -1;
	static uint32_t when;
	uint32_t now = SDL_GetTicks();
	if (!started) {
		started = 1;
		if (NNResult_IsSuccess(ACInitialize())) ACConnectAsync();
		when = now;
		return -1;
	}
	if (now - when < 2000 && last >= 0) return last;
	when = now;
	BOOL c = FALSE;
	if (NNResult_IsSuccess(ACIsApplicationConnected(&c))) last = c ? 1 : 0;
	return last;
}

/* ------------------------------------------------------------------------ */
/* textures                                                                 */
/* ------------------------------------------------------------------------ */
#define TEXN 48                    /* enough for every cover on screen */
static struct { char id[16]; SDL_Texture *t; int w, h; uint32_t used; } texc[TEXN];
static uint32_t texclock;

void covers_flush(void)
{
	for (int i = 0; i < TEXN; i++)
		if (texc[i].t) { SDL_DestroyTexture(texc[i].t); texc[i].t = NULL; }
}

void *covers_texture(void *renderer, const char *gameId, int *w, int *h)
{
	char path[256], id[16];
	SDL_Surface *s;
	int victim = 0;
	if (!gameId || !gameId[0]) return NULL;
	if (gameId[0] == '@') snprintf(id, sizeof(id), "%s", gameId);
	else show_key(gameId, id, sizeof(id));
	for (int i = 0; i < TEXN; i++) {
		if (texc[i].t && !strcmp(texc[i].id, id)) {
			texc[i].used = ++texclock;
			*w = texc[i].w; *h = texc[i].h;
			return texc[i].t;
		}
		if (!texc[i].t) victim = i;
		else if (texc[victim].t && texc[i].used < texc[victim].used) victim = i;
	}
	/* Only touch the SD card when the state is unknown or freshly
	 * downloaded; pending / missing / failed covers cost nothing. */
	if (id[0] == '@') {
		custom_path(id, path, sizeof(path));
		s = file_exists(path) ? IMG_Load(path) : NULL;
		if (!s) return NULL;
	} else {
		int st = state_get(id);
		if (st == ST_PENDING || st == ST_MISSING || st == ST_FAILED) return NULL;
		cover_path(id, path, sizeof(path));
		if (!file_exists(path)) { request_key(id); return NULL; }
		s = IMG_Load(path);
		if (!s) { state_set(id, ST_FAILED); return NULL; }   /* unreadable file */
		state_set(id, ST_DONE);
	}
	if (texc[victim].t) SDL_DestroyTexture(texc[victim].t);
	texc[victim].t = SDL_CreateTextureFromSurface((SDL_Renderer *)renderer, s);
	if (texc[victim].t) SDL_SetTextureBlendMode(texc[victim].t, SDL_BLENDMODE_BLEND);
	texc[victim].w = s->w;
	texc[victim].h = s->h;
	SDL_FreeSurface(s);
	strcpy(texc[victim].id, id);
	texc[victim].used = ++texclock;
	*w = texc[victim].w; *h = texc[victim].h;
	return texc[victim].t;
}
