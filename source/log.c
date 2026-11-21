/*
 * WiiUStation - log file: sd:/wiiu/apps/WiiUStation/log.txt
 *
 * What happened, to understand a problem afterwards: start and exit, games
 * loaded or not (and why), write errors, update and download failures,
 * warnings from the core. Every line is written at once, so it survives a
 * crash; a run that ended without a clean exit is reported by the next one.
 * The file is kept small: past 256 KB it becomes log.old.txt.
 */
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "platform.h"

#define LOG_FILE   WIIU_APP_DIR "/log.txt"
#define LOG_OLD    WIIU_APP_DIR "/log.old.txt"
#define RUN_FLAG   WIIU_APP_DIR "/running.flag"
#define LOG_MAX    (256 * 1024)
#define LINES_MAX  400                     /* per run */

static FILE *lf;
static SDL_mutex *lock;
static int lines;
static char last[256];

void wlog(const char *fmt, ...)
{
	char b[256];
	if (!lf) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	b[strcspn(b, "\r\n")] = 0;
	if (!b[0]) return;
	if (lock) SDL_LockMutex(lock);
	if (strcmp(b, last) && lines < LINES_MAX) {       /* no repeats, no floods */
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char ts[16] = "--:--:--";
		if (tm) strftime(ts, sizeof(ts), "%H:%M:%S", tm);
		fprintf(lf, "[%s] %s\n", ts, b);
		if (++lines == LINES_MAX) fprintf(lf, "(more messages in this run are not logged)\n");
		fflush(lf);
		snprintf(last, sizeof(last), "%s", b);
	}
	if (lock) SDL_UnlockMutex(lock);
}

void wlog_init(void)
{
	struct stat st;
	wiiu_mkdirs(WIIU_APP_DIR);
	if (stat(LOG_FILE, &st) == 0 && st.st_size > LOG_MAX) {
		remove(LOG_OLD);
		rename(LOG_FILE, LOG_OLD);
	}
	int crashed = stat(RUN_FLAG, &st) == 0;
	lf = fopen(LOG_FILE, "a");
	if (!lf) return;
	lock = SDL_CreateMutex();
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char date[32] = "";
	if (tm) strftime(date, sizeof(date), "%Y-%m-%d", tm);
	fprintf(lf, "\n===== WiiUStation %s - %s =====\n", app_version(), date);
	fflush(lf);
	if (crashed) wlog("the previous run did not end normally (crash or power off)");
	FILE *f = fopen(RUN_FLAG, "w");
	if (f) { fputs("running\n", f); fclose(f); }
}

void wlog_exit(void)
{
	wlog("exit");
	remove(RUN_FLAG);
	if (lf) { fclose(lf); lf = NULL; }
}
