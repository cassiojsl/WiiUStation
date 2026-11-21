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
static char queue[QSIZE][12];
static volatile int qhead, qtail;
static SDL_mutex *qlock;
static SDL_sem *qsem;
static SDL_Thread *worker;
static char failed[64][12];
static int nfailed;
static int netReady = -1;

static void cover_path(const char *id, char *out, size_t n)
{
	snprintf(out, n, "%s/%s.jpg", COVER_DIR, id);
}

static size_t write_cb(void *p, size_t sz, size_t nm, void *ud)
{
	return fwrite(p, sz, nm, (FILE *)ud);
}

static int download(const char *id)
{
	char url[256], path[256], tmp[260];
	CURL *c;
	FILE *f;
	long code = 0;
	CURLcode rc;

	if (netReady < 0) {
		netReady = NNResult_IsSuccess(ACInitialize()) && NNResult_IsSuccess(ACConnect());
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	if (!netReady) return 0;

	snprintf(url, sizeof(url), COVER_URL, id);
	cover_path(id, path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	wiiu_mkdirs(COVER_DIR);
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
	return 0;
}

static int worker_main(void *ud)
{
	(void)ud;
	for (;;) {
		char id[12];
		SDL_SemWait(qsem);
		SDL_LockMutex(qlock);
		if (qhead == qtail) { SDL_UnlockMutex(qlock); continue; }
		strcpy(id, queue[qhead]);
		qhead = (qhead + 1) % QSIZE;
		SDL_UnlockMutex(qlock);
		if (!strcmp(id, "QUIT")) break;
		if (!download(id) && nfailed < 64) strcpy(failed[nfailed++], id);
	}
	return 0;
}

void covers_request(const char *id)
{
	char path[256];
	if (!id || !id[0]) return;
	cover_path(id, path, sizeof(path));
	if (file_exists(path)) return;
	for (int i = 0; i < nfailed; i++) if (!strcmp(failed[i], id)) return;
	if (!qlock) {
		qlock = SDL_CreateMutex();
		qsem = SDL_CreateSemaphore(0);
		worker = SDL_CreateThread(worker_main, "covers", NULL);
	}
	SDL_LockMutex(qlock);
	for (int i = qhead; i != qtail; i = (i + 1) % QSIZE)
		if (!strcmp(queue[i], id)) { SDL_UnlockMutex(qlock); return; }
	if ((qtail + 1) % QSIZE != qhead) {
		strcpy(queue[qtail], id);
		qtail = (qtail + 1) % QSIZE;
		SDL_SemPost(qsem);
	}
	SDL_UnlockMutex(qlock);
	/* remember the attempt so we do not queue it every frame */
	if (nfailed < 64) {
		/* cleared again only by an app restart; a successful download makes
		 * the file exist, which is checked first above */
		strcpy(failed[nfailed++], id);
	}
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

/* ------------------------------------------------------------------------ */
/* textures                                                                 */
/* ------------------------------------------------------------------------ */
#define TEXN 12
static struct { char id[12]; SDL_Texture *t; int w, h; uint32_t used; } texc[TEXN];
static uint32_t texclock;

void *covers_texture(void *renderer, const char *id, int *w, int *h)
{
	char path[256];
	SDL_Surface *s;
	int victim = 0;
	if (!id || !id[0]) return NULL;
	for (int i = 0; i < TEXN; i++) {
		if (texc[i].t && !strcmp(texc[i].id, id)) {
			texc[i].used = ++texclock;
			*w = texc[i].w; *h = texc[i].h;
			return texc[i].t;
		}
		if (!texc[i].t) victim = i;
		else if (texc[victim].t && texc[i].used < texc[victim].used) victim = i;
	}
	cover_path(id, path, sizeof(path));
	if (!file_exists(path)) { covers_request(id); return NULL; }
	s = IMG_Load(path);
	if (!s) return NULL;
	if (texc[victim].t) SDL_DestroyTexture(texc[victim].t);
	texc[victim].t = SDL_CreateTextureFromSurface((SDL_Renderer *)renderer, s);
	texc[victim].w = s->w;
	texc[victim].h = s->h;
	SDL_FreeSurface(s);
	strcpy(texc[victim].id, id);
	texc[victim].used = ++texclock;
	*w = texc[victim].w; *h = texc[victim].h;
	return texc[victim].t;
}
