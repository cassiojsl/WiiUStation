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
#define PC_FLAG    WIIU_APP_DIR "/lastpc.txt"
#define LOG_MAX    (256 * 1024)
#define LINES_MAX  500                     /* per run: routine lines (sound, slow stretches, alive) */
#define IMPORTANT_MAX 1500                 /* per run: loads, errors, sessions, exits are always kept */

static FILE *lf;
static SDL_mutex *lock;
static int lines, important;
static char last[400];

void wlog(const char *fmt, ...)
{
	char b[400];
	if (!lf) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	b[strcspn(b, "\r\n")] = 0;
	if (!b[0]) return;
	if (lock) SDL_LockMutex(lock);
	/* what matters when something went wrong is never dropped: only the
	 * routine lines (sound state, slow stretches, alive) have a limit */
	int routine = !strncmp(b, "spu ", 4) || !strncmp(b, "slow:", 5) || !strncmp(b, "alive", 5) || !strncmp(b, "cd:", 3);
	int room = routine ? lines < LINES_MAX : important < IMPORTANT_MAX;
	if (strcmp(b, last) && room) {                    /* no repeats, no floods */
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char ts[16] = "--:--:--";
		if (tm) strftime(ts, sizeof(ts), "%H:%M:%S", tm);
		fprintf(lf, "[%s] %s\n", ts, b);
		if (routine) {
			if (++lines == LINES_MAX) fprintf(lf, "(more routine lines in this run are not logged)\n");
		} else {
			important++;
		}
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
	char was[420] = "";
	if (crashed) {                       /* what the previous run was doing */
		FILE *rf = fopen(RUN_FLAG, "r");
		if (rf) {
			if (fgets(was, sizeof(was), rf)) was[strcspn(was, "\r\n")] = 0;
			fclose(rf);
		}
	}
	char lastpc[12][200] = {{0}};
	int nlast = 0;
	if (crashed) {
		FILE *pf = fopen(PC_FLAG, "r");
		if (pf) {
			while (nlast < 12 && fgets(lastpc[nlast], sizeof(lastpc[0]), pf)) {
				lastpc[nlast][strcspn(lastpc[nlast], "\r\n")] = 0;
				nlast++;
			}
			fclose(pf);
		}
	}
	lf = fopen(LOG_FILE, "a");
	if (!lf) return;
	lock = SDL_CreateMutex();
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char date[32] = "";
	if (tm) strftime(date, sizeof(date), "%Y-%m-%d", tm);
	fprintf(lf, "\n===== WiiUStation %s - %s =====\n", app_version(), date);
	fflush(lf);
	if (crashed) {
		if (strncmp(was, "playing ", 8) == 0) {
			wlog("the previous run did not end normally (crash or power off) while %s", was);
			wlog("(that was the last state it saved: the freeze or crash came right after it)");
		}
		else wlog("the previous run did not end normally (crash or power off)");
		for (int i = 0; i < nlast; i++) wlog("watchdog sample %d/%d before the end: %s", i + 1, nlast, lastpc[i]);
	}
	FILE *f = fopen(RUN_FLAG, "w");
	if (f) { fputs("running\n", f); fclose(f); }
}

/* what the app is doing now, kept in the run flag: if the app dies, the next
 * start says what it was doing (the game name, or "" for the menu) */
void wlog_activity(const char *what)
{
	FILE *f = fopen(RUN_FLAG, "w");
	if (!f) return;
	fprintf(f, "%s\n", what && what[0] ? what : "running");
	fclose(f);
}

void wlog_lastpc(const char *what)
{
	/* the last 12 samples, oldest first: the trend before a freeze (a stuck loop, memory growing...) */
	static char ring[12][200];
	static int n;
	if (n == 12) { memmove(ring[0], ring[1], sizeof(ring[0]) * 11); n = 11; }
	snprintf(ring[n++], sizeof(ring[0]), "%s", what);
	FILE *f = fopen(PC_FLAG, "w");
	if (!f) return;
	for (int i = 0; i < n; i++) fprintf(f, "%s\n", ring[i]);
	fclose(f);
}

void wlog_exit(void)
{
	wlog("exit");
	remove(RUN_FLAG);
	remove(PC_FLAG);
	if (lf) { fclose(lf); lf = NULL; }
}
