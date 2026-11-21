/*
 * WiiUStation Wii U - menu.
 *
 * Drawn with SDL2 + the Wii U system font on the TV and on the GamePad at the
 * same time.  Navigation works with any controller (GamePad, Pro, Classic) and
 * with the GamePad touch screen (tap = select, tap again = confirm).
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include "platform.h"

/* ------------------------------------------------------------------------ */
/* text rendering with a small texture cache                                */
/* ------------------------------------------------------------------------ */
static SDL_Renderer *R;
static TTF_Font *fontBig, *fontMed, *fontSmall;
#define wiiu_open_font(n) ((TTF_Font *)video_open_font(n))

#define TCACHE 160
typedef struct {
	char text[128];
	TTF_Font *font;
	uint32_t rgba;
	SDL_Texture *tex;
	int w, h;
	uint32_t used;
} tcache_t;
static tcache_t tcache[TCACHE];
static uint32_t tclock;

static tcache_t *get_text(TTF_Font *font, const char *s, SDL_Color c)
{
	uint32_t rgba = (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
	tcache_t *victim = &tcache[0];
	for (int i = 0; i < TCACHE; i++) {
		tcache_t *e = &tcache[i];
		if (e->tex && e->font == font && e->rgba == rgba && !strncmp(e->text, s, sizeof(e->text) - 1)) {
			e->used = ++tclock;
			return e;
		}
		if (!e->tex) { victim = e; break; }
		if (e->used < victim->used) victim = e;
	}
	if (victim->tex) SDL_DestroyTexture(victim->tex);
	victim->tex = NULL;
	SDL_Surface *surf = TTF_RenderUTF8_Blended(font, s, c);
	if (!surf) return NULL;
	victim->tex = SDL_CreateTextureFromSurface(R, surf);
	victim->w = surf->w;
	victim->h = surf->h;
	SDL_FreeSurface(surf);
	strncpy(victim->text, s, sizeof(victim->text) - 1);
	victim->text[sizeof(victim->text) - 1] = 0;
	victim->font = font;
	victim->rgba = rgba;
	victim->used = ++tclock;
	return victim;
}

static int text(TTF_Font *f, int x, int y, const char *s, SDL_Color c, int align)
{
	if (!f || !s || !*s) return 0;
	tcache_t *e = get_text(f, s, c);
	if (!e || !e->tex) return 0;
	SDL_Rect d = { x, y, e->w, e->h };
	if (align == 1) d.x = x - e->w / 2;
	if (align == 2) d.x = x - e->w;
	SDL_RenderCopy(R, e->tex, NULL, &d);
	return e->w;
}

static void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	SDL_Rect rc = { x, y, w, h };
	SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(R, r, g, b, a);
	SDL_RenderFillRect(R, &rc);
}

static const SDL_Color WHITE  = { 255, 255, 255, 255 };
static const SDL_Color GREY   = { 170, 176, 190, 255 };
static const SDL_Color ACCENT = { 120, 200, 255, 255 };
static const SDL_Color DIM    = { 120, 126, 140, 255 };

/* ------------------------------------------------------------------------ */
/* input helpers (auto-repeat)                                              */
/* ------------------------------------------------------------------------ */
static uint32_t repeatMask;
static uint64_t repeatAt;

static uint32_t poll_buttons(void)
{
	uint32_t held, pressed, now = SDL_GetTicks();
	static uint32_t prevHeld;
	wiiu_input_update();
	held = wiiu_input_menu_held();
	pressed = held & ~prevHeld;
	prevHeld = held;

	const uint32_t rep = WIIU_BTN_UP | WIIU_BTN_DOWN | WIIU_BTN_LEFT | WIIU_BTN_RIGHT |
	                     WIIU_BTN_L | WIIU_BTN_R;
	if (pressed & rep) {
		repeatMask = pressed & rep;
		repeatAt = now + 350;
	} else if ((held & repeatMask) && now >= repeatAt) {
		pressed |= held & repeatMask;
		repeatAt = now + 70;
	} else if (!(held & repeatMask)) {
		repeatMask = 0;
	}
	return pressed;
}

/* ------------------------------------------------------------------------ */
/* generic list screen                                                      */
/* ------------------------------------------------------------------------ */
#define LIST_X      150
#define LIST_Y      150
#define LIST_W      980
#define ROW_H       46
#define VISIBLE_ROWS 11

static char statusMsg[160];
static uint32_t statusUntil;

static void status(const char *m)
{
	strncpy(statusMsg, m, sizeof(statusMsg) - 1);
	statusMsg[sizeof(statusMsg) - 1] = 0;
	statusUntil = SDL_GetTicks() + 3000;
}

static void draw_frame(const char *title, const char *subtitle, const char *hint)
{
	SDL_SetRenderDrawColor(R, 12, 14, 24, 255);
	SDL_RenderClear(R);
	if (emu_game_loaded())
		video_draw_game(1);
	else {
		/* subtle vertical gradient */
		for (int y = 0; y < 720; y += 8)
			fill(0, y, 1280, 8, 18, 24 + y / 40, 44 + y / 24, 255);
	}
	fill(0, 0, 1280, 104, 0, 0, 0, 140);
	text(fontBig, 60, 24, "WiiUStation", ACCENT, 0);
	if (title) text(fontMed, 1220, 36, title, WHITE, 2);
	if (subtitle) text(fontSmall, LIST_X, 112, subtitle, GREY, 0);
	fill(0, 668, 1280, 52, 0, 0, 0, 150);
	if (statusMsg[0] && SDL_GetTicks() < statusUntil)
		text(fontSmall, 40, 680, statusMsg, ACCENT, 0);
	else if (hint)
		text(fontSmall, 40, 680, hint, GREY, 0);
}

/* returns index tapped on the touch screen, or -1 */
static int touched_row(int first, int count)
{
	const wiiu_pad_state *gp = wiiu_input_get(0);
	if (!(gp->pressed & WIIU_BTN_TOUCH)) return -1;
	/* GamePad touch is reported in the 1280x720 space of the DRC screen */
	int y = gp->touch_y, x = gp->touch_x;
	if (x < LIST_X - 40 || x > LIST_X + LIST_W + 40) return -1;
	int row = (y - LIST_Y) / ROW_H;
	if (y < LIST_Y || row >= VISIBLE_ROWS) return -1;
	if (first + row >= count) return -1;
	return first + row;
}

static void draw_list(const char **labels, const char **values, int count, int sel, int *first)
{
	if (sel < *first) *first = sel;
	if (sel >= *first + VISIBLE_ROWS) *first = sel - VISIBLE_ROWS + 1;
	if (*first < 0) *first = 0;

	fill(LIST_X - 30, LIST_Y - 10, LIST_W + 60, VISIBLE_ROWS * ROW_H + 20, 0, 0, 0, 120);
	for (int i = 0; i < VISIBLE_ROWS && *first + i < count; i++) {
		int idx = *first + i;
		int y = LIST_Y + i * ROW_H;
		if (idx == sel) fill(LIST_X - 20, y, LIST_W + 40, ROW_H - 4, 60, 120, 200, 200);
		text(fontMed, LIST_X, y + 4, labels[idx], idx == sel ? WHITE : GREY, 0);
		if (values && values[idx])
			text(fontMed, LIST_X + LIST_W, y + 4, values[idx], idx == sel ? WHITE : ACCENT, 2);
	}
	if (count > VISIBLE_ROWS) {
		int barH = VISIBLE_ROWS * ROW_H;
		int h = barH * VISIBLE_ROWS / count;
		int y = LIST_Y + (barH - h) * *first / (count - VISIBLE_ROWS);
		fill(LIST_X + LIST_W + 34, y, 6, h, 120, 200, 255, 200);
	}
}

static void present(void)
{
	SDL_RenderPresent(R);
	pump_events();
	SDL_Delay(1);
}

/* ------------------------------------------------------------------------ */
/* file browser                                                             */
/* ------------------------------------------------------------------------ */
typedef struct { char name[256]; int dir; } entry_t;
static char browsePath[512] = WIIU_ISO_DIR;
static char lastGame[512];

static int is_game_file(const char *n)
{
	static const char *exts[] = { ".bin", ".cue", ".iso", ".img", ".mdf", ".mds",
	                              ".ccd", ".pbp", ".cbn", ".exe", ".psx", ".toc",
	                              ".chd", ".m3u", ".ecm", ".z", ".znx", NULL };
	const char *e = strrchr(n, '.');
	if (!e) return 0;
	for (int i = 0; exts[i]; i++)
		if (!strcasecmp(e, exts[i])) return 1;
	return 0;
}

static int cmp_entry(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return strcasecmp(x->name, y->name);
}

static int scan_dir(const char *path, entry_t **out)
{
	DIR *d = opendir(path);
	int n = 0, cap = 64;
	entry_t *list = malloc(cap * sizeof(entry_t));
	struct dirent *ent;
	/* ".." unless at the SD root */
	if (strcmp(path, WIIU_SD_ROOT) != 0) {
		strcpy(list[n].name, "..");
		list[n++].dir = 1;
	}
	if (d) {
		while ((ent = readdir(d)) != NULL) {
			if (ent->d_name[0] == '.') continue;
			int isdir = (ent->d_type & DT_DIR) != 0;
			if (!isdir && !is_game_file(ent->d_name)) continue;
			if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(entry_t)); }
			strncpy(list[n].name, ent->d_name, sizeof(list[n].name) - 1);
			list[n].name[sizeof(list[n].name) - 1] = 0;
			list[n].dir = isdir;
			n++;
		}
		closedir(d);
	}
	int start = (n && !strcmp(list[0].name, "..")) ? 1 : 0;
	qsort(list + start, n - start, sizeof(entry_t), cmp_entry);
	*out = list;
	return n;
}

/* returns 1 when a file was chosen (path in out), 0 on cancel */
static int browse(const char *title, char *out, size_t outsz)
{
	entry_t *list = NULL;
	int count = scan_dir(browsePath, &list);
	int sel = 0, first = 0;
	const char **labels = NULL;

	for (;;) {
		labels = realloc(labels, (count + 1) * sizeof(char *));
		for (int i = 0; i < count; i++) labels[i] = list[i].name;
		if (wiiu_quit) { free(list); free(labels); return 0; }

		uint32_t b = poll_buttons();
		int t = touched_row(first, count);
		int activate = 0;
		if (t >= 0) { if (t == sel) activate = 1; sel = t; }
		if (b & WIIU_BTN_UP)    sel = sel > 0 ? sel - 1 : count - 1;
		if (b & WIIU_BTN_DOWN)  sel = sel < count - 1 ? sel + 1 : 0;
		if (b & (WIIU_BTN_L | WIIU_BTN_LEFT))  { sel -= VISIBLE_ROWS; if (sel < 0) sel = 0; }
		if (b & (WIIU_BTN_R | WIIU_BTN_RIGHT)) { sel += VISIBLE_ROWS; if (sel >= count) sel = count - 1; }
		if (b & WIIU_BTN_A) activate = 1;
		if (b & WIIU_BTN_B) {
			/* go up one folder, or leave the browser */
			if (count && !strcmp(list[0].name, "..")) { sel = 0; activate = 1; }
			else { free(list); free(labels); return 0; }
		}
		if (b & WIIU_BTN_PLUS) { free(list); free(labels); return 0; }

		if (activate && count > 0) {
			entry_t *e = &list[sel];
			if (!strcmp(e->name, "..")) {
				char *slash = strrchr(browsePath, '/');
				if (slash && slash > browsePath + strlen(WIIU_SD_ROOT) - 1) *slash = 0;
				else strcpy(browsePath, WIIU_SD_ROOT);
				if (strlen(browsePath) < strlen(WIIU_SD_ROOT)) strcpy(browsePath, WIIU_SD_ROOT);
			} else if (e->dir) {
				size_t l = strlen(browsePath);
				snprintf(browsePath + l, sizeof(browsePath) - l, "/%s", e->name);
			} else {
				snprintf(out, outsz, "%s/%s", browsePath, e->name);
				free(list); free(labels);
				return 1;
			}
			free(list);
			count = scan_dir(browsePath, &list);
			sel = 0; first = 0;
			continue;
		}

		draw_frame(title, browsePath + strlen(WIIU_SD_ROOT), "A: open   B: back   L/R: page   +: cancel   Touch: select");
		if (count == 0)
			text(fontMed, 640, 360, "No games here. Copy them to sd:/wiisxrx/isos", GREY, 1);
		else
			draw_list(labels, NULL, count, sel, &first);
		present();
	}
}

/* ------------------------------------------------------------------------ */
/* settings                                                                 */
/* ------------------------------------------------------------------------ */
static void settings(void)
{
	enum { S_CORE, S_GPUTHREAD, S_SPUTHREAD, S_SKIP, S_FPS, S_AUDIO, S_ASPECT,
	       S_FILTER, S_SCAN, S_DITHER, S_PAD, S_RUMBLE, S_BIOS, S_LOGO, S_SAVE, S_COUNT };
	const char *labels[S_COUNT] = {
		"CPU core", "GPU rendering on core 2", "Sound mixing on core 0",
		"Frame skip", "Show FPS", "Sound", "Screen aspect", "Filtering",
		"Scanlines", "Dithering", "PSX controller", "Rumble", "BIOS",
		"PlayStation boot logo", "Save settings"
	};
	const char *values[S_COUNT];
	int sel = 0, first = 0;

	for (;;) {
		values[S_CORE]      = cfg.dynarec ? "Lightrec (dynarec)" : "Interpreter";
		values[S_GPUTHREAD] = cfg.gpu_thread ? "On" : "Off";
		values[S_SPUTHREAD] = cfg.spu_thread ? "On" : "Off";
		values[S_SKIP]      = cfg.frameskip ? "Auto" : "Off";
		values[S_FPS]       = cfg.show_fps ? "On" : "Off";
		values[S_AUDIO]     = cfg.audio ? "On" : "Off";
		values[S_ASPECT]    = cfg.aspect == WIIU_ASPECT_4_3 ? "4:3" :
		                      cfg.aspect == WIIU_ASPECT_STRETCH ? "Stretch 16:9" : "Integer scale";
		values[S_FILTER]    = cfg.filter == WIIU_FILTER_LINEAR ? "Bilinear" : "Sharp (nearest)";
		values[S_SCAN]      = cfg.scanlines ? "On" : "Off";
		values[S_DITHER]    = cfg.dithering == 0 ? "Off" : cfg.dithering == 1 ? "On" : "Force";
		values[S_PAD]       = cfg.analog ? "DualShock (analog)" : "Digital";
		values[S_RUMBLE]    = cfg.rumble ? "On" : "Off";
		values[S_BIOS]      = cfg.bios_hle ? "HLE (built-in)" : "Auto (SD, else HLE)";
		values[S_LOGO]      = cfg.bootlogo ? "On" : "Off";
		values[S_SAVE]      = "";

		uint32_t b = poll_buttons();
		int t = touched_row(first, S_COUNT);
		int dir = 0;
		if (t >= 0) { if (t == sel) dir = 1; sel = t; }
		if (b & WIIU_BTN_UP)   sel = sel > 0 ? sel - 1 : S_COUNT - 1;
		if (b & WIIU_BTN_DOWN) sel = sel < S_COUNT - 1 ? sel + 1 : 0;
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;
		if ((b & WIIU_BTN_B) || wiiu_quit) { emu_apply_settings(); return; }

		if (dir) {
			switch (sel) {
			case S_CORE:
				if (!jit_available()) {
					cfg.dynarec = 0;
					status("Lightrec unavailable: no codegen (JIT) area for this title.");
				} else {
					cfg.dynarec = !cfg.dynarec;
					status("Applies when a game is (re)loaded.");
				}
				break;
			case S_GPUTHREAD: cfg.gpu_thread = !cfg.gpu_thread; status("Applies when a game is (re)loaded."); break;
			case S_SPUTHREAD: cfg.spu_thread = !cfg.spu_thread; break;
			case S_SKIP:      cfg.frameskip = !cfg.frameskip; break;
			case S_FPS:       cfg.show_fps = !cfg.show_fps; break;
			case S_AUDIO:     cfg.audio = !cfg.audio; break;
			case S_ASPECT:    cfg.aspect = (cfg.aspect + 3 + dir) % 3; break;
			case S_FILTER:    cfg.filter = !cfg.filter; break;
			case S_SCAN:      cfg.scanlines = !cfg.scanlines; break;
			case S_DITHER:    cfg.dithering = (cfg.dithering + 3 + dir) % 3; break;
			case S_PAD:       cfg.analog = !cfg.analog; break;
			case S_RUMBLE:    cfg.rumble = !cfg.rumble; break;
			case S_BIOS:      cfg.bios_hle = !cfg.bios_hle; status("Applies when a game is (re)loaded."); break;
			case S_LOGO:      cfg.bootlogo = !cfg.bootlogo; break;
			case S_SAVE:
				status(settings_save() ? "Settings saved to sd:/wiisxrx/wiiustation.cfg"
				                       : "Could not write settings to the SD card!");
				break;
			}
		}

		draw_frame("Settings", NULL, "A/Right: change   Left: previous   B: back   Touch: select");
		draw_list(labels, values, S_COUNT, sel, &first);
		present();
	}
}

/* ------------------------------------------------------------------------ */
/* main menu                                                                */
/* ------------------------------------------------------------------------ */
static int slot = 0;

int menu_run(void)
{
	enum { M_RESUME, M_LOAD, M_RESET, M_DISC, M_SAVE, M_LOADST, M_SLOT, M_SETTINGS, M_EXIT, M_COUNT };
	static const char *allLabels[M_COUNT] = {
		"Resume game", "Load game", "Reset game", "Change disc", "Save state",
		"Load state", "State slot", "Settings", "Exit to Wii U Menu"
	};
	int map[M_COUNT];
	const char *labels[M_COUNT];
	const char *values[M_COUNT];
	char slotStr[8], discStr[16];
	int sel = 0, first = 0;

	if (!R) {
		R = (SDL_Renderer *)video_renderer();
		fontBig = wiiu_open_font(40);
		fontMed = wiiu_open_font(28);
		fontSmall = wiiu_open_font(22);
	}
	poll_buttons();

	for (;;) {
		int n = 0, loaded = emu_game_loaded();
		for (int i = 0; i < M_COUNT; i++) {
			if (!loaded && (i == M_RESUME || i == M_RESET || i == M_DISC ||
			                i == M_SAVE || i == M_LOADST || i == M_SLOT))
				continue;
			map[n] = i;
			labels[n] = allLabels[i];
			values[n] = NULL;
			if (i == M_SLOT) { snprintf(slotStr, sizeof(slotStr), "%d", slot); values[n] = slotStr; }
			if (i == M_DISC && emu_disk_count() > 1) {
				snprintf(discStr, sizeof(discStr), "%d / %d", emu_disk_index() + 1, emu_disk_count());
				values[n] = discStr;
			}
			n++;
		}
		if (sel >= n) sel = n - 1;
		if (wiiu_quit) return 0;

		uint32_t b = poll_buttons();
		int t = touched_row(first, n);
		int activate = 0, dir = 0;
		if (t >= 0) { if (t == sel) activate = 1; sel = t; }
		if (b & WIIU_BTN_UP)    sel = sel > 0 ? sel - 1 : n - 1;
		if (b & WIIU_BTN_DOWN)  sel = sel < n - 1 ? sel + 1 : 0;
		if (b & WIIU_BTN_A)     activate = 1;
		if (b & WIIU_BTN_LEFT)  dir = -1;
		if (b & WIIU_BTN_RIGHT) dir = 1;
		if ((b & WIIU_BTN_B) && loaded) return 1;

		int item = map[sel];
		if (item == M_SLOT && (dir || activate)) {
			slot = (slot + 10 + (dir ? dir : 1)) % 10;
		} else if (item == M_DISC && dir && emu_disk_count() > 1) {
			int c = emu_disk_count();
			emu_disk_set((emu_disk_index() + c + dir) % c);
		} else if (activate) {
			char path[512], m[96];
			switch (item) {
			case M_RESUME:
				return 1;
			case M_LOAD:
				if (browse("Load game", path, sizeof(path))) {
					draw_frame("Loading...", NULL, NULL);
					text(fontMed, 640, 340, strrchr(path, '/') ? strrchr(path, '/') + 1 : path, WHITE, 1);
					present();
					if (emu_load_game(path) == 0) return 1;
					status("Could not load the game (bad image or missing BIOS?).");
				}
				break;
			case M_RESET:
				emu_reset();
				return 1;
			case M_DISC:
				if (emu_disk_count() > 1) {
					int c = emu_disk_count();
					emu_disk_set((emu_disk_index() + 1) % c);
					snprintf(m, sizeof(m), "Disc %d of %d inserted.", emu_disk_index() + 1, c);
					status(m);
				} else if (browse("Change disc", path, sizeof(path))) {
					if (emu_disk_append(path)) return 1;
					status("Disc change failed.");
				}
				break;
			case M_SAVE:
				snprintf(m, sizeof(m), emu_state_save(slot) ? "State saved to slot %d."
				                                            : "Failed to save state (slot %d).", slot);
				status(m);
				break;
			case M_LOADST:
				if (emu_state_load(slot)) return 1;
				snprintf(m, sizeof(m), "No state in slot %d.", slot);
				status(m);
				break;
			case M_SETTINGS:
				settings();
				break;
			case M_EXIT:
				return 0;
			}
		}

		static char sub[300];
		const char *subp = NULL;
		if (loaded) { snprintf(sub, sizeof(sub), "Now playing: %s", emu_game_name()); subp = sub; }
		draw_frame(NULL, subp, loaded ?
		           "A: select   B: back to game   HOME: Wii U HOME Menu   In game: HOME/touch/L3+R3 = menu" :
		           "A: select   Touch: select   Games go in sd:/wiisxrx/isos");
		draw_list(labels, values, n, sel, &first);
		{
			char info[96];
			snprintf(info, sizeof(info), "PCSX-ReARMed  |  %s  |  3 cores",
			         cfg.dynarec ? "Lightrec" : "Interpreter");
			text(fontSmall, 1240, 640, info, DIM, 2);
		}
		present();
	}
}
