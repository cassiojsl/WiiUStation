/*
 * WiiUStation - menu.
 *
 * Styled after the PlayStation's own system menu: dark blue/black
 * background with slowly drifting translucent squares, the four PlayStation
 * button colours and symbols, light text on dark panels.
 *
 * Built for fingers on the GamePad: 64 px rows, a single tap opens an item,
 * dragging scrolls lists, settings have big ◀ ▶ targets, and there is an
 * on-screen Back button. Buttons on any controller keep working too.
 * Drawn on the TV and the GamePad at the same time.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

#include "platform.h"

/* ------------------------------------------------------------------------ */
/* drawing helpers                                                          */
/* ------------------------------------------------------------------------ */
static SDL_Renderer *R;
static TTF_Font *fontTitle, *fontItem, *fontSmall;

static const SDL_Color WHITE  = { 240, 242, 248, 255 };
static const SDL_Color GREY   = { 150, 158, 178, 255 };
static const SDL_Color DIM    = { 100, 108, 128, 255 };
static const SDL_Color PS_GREEN  = {  64, 226, 160, 255 };  /* triangle */
static const SDL_Color PS_RED    = { 255,  92,  92, 255 };  /* circle   */
static const SDL_Color PS_BLUE   = { 124, 178, 232, 255 };  /* cross    */
static const SDL_Color PS_PINK   = { 255, 105, 248, 255 };  /* square   */
static const SDL_Color PS_YELLOW = { 255, 204,  32, 255 };

#define TCACHE 200
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

/* align: 0 left, 1 centre, 2 right; y is the vertical centre */
static int text(TTF_Font *f, int x, int yc, const char *s, SDL_Color c, int align)
{
	if (!f || !s || !*s) return 0;
	tcache_t *e = get_text(f, s, c);
	if (!e || !e->tex) return 0;
	SDL_Rect d = { x, yc - e->h / 2, e->w, e->h };
	if (align == 1) d.x = x - e->w / 2;
	if (align == 2) d.x = x - e->w;
	SDL_RenderCopy(R, e->tex, NULL, &d);
	return e->w;
}

static void fill(int x, int y, int w, int h, SDL_Color c, uint8_t a)
{
	SDL_Rect rc = { x, y, w, h };
	SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(R, c.r, c.g, c.b, a);
	SDL_RenderFillRect(R, &rc);
}

static void outline(int x, int y, int w, int h, SDL_Color c, uint8_t a, int t)
{
	fill(x, y, w, t, c, a);
	fill(x, y + h - t, w, t, c, a);
	fill(x, y, t, h, c, a);
	fill(x + w - t, y, t, h, c, a);
}

static void poly(const float *xy, int n, SDL_Color c, uint8_t a)
{
	/* convex polygon as a triangle fan */
	SDL_Vertex v[16];
	int idx[48], k = 0;
	if (n > 16) n = 16;
	for (int i = 0; i < n; i++) {
		v[i].position.x = xy[i * 2];
		v[i].position.y = xy[i * 2 + 1];
		v[i].color = (SDL_Color){ c.r, c.g, c.b, a };
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	for (int i = 1; i + 1 < n; i++) { idx[k++] = 0; idx[k++] = i; idx[k++] = i + 1; }
	SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(R, NULL, v, n, idx, k);
}

static void ring(float cx, float cy, float r, float t, SDL_Color c)
{
	const int N = 24;
	for (int i = 0; i < N; i++) {
		float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
		float q[8] = {
			cx + cosf(a0) * r, cy + sinf(a0) * r,
			cx + cosf(a1) * r, cy + sinf(a1) * r,
			cx + cosf(a1) * (r - t), cy + sinf(a1) * (r - t),
			cx + cosf(a0) * (r - t), cy + sinf(a0) * (r - t),
		};
		poly(q, 4, c, 255);
	}
}

static void bar(float x0, float y0, float x1, float y1, float t, SDL_Color c)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
	float nx = -dy / l * t / 2, ny = dx / l * t / 2;
	float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
	poly(q, 4, c, 255);
}

/* PlayStation face-button symbols, size s, centred on (x, y) */
enum { SYM_CIRCLE, SYM_CROSS, SYM_TRIANGLE, SYM_SQUARE };
static void symbol(int which, float x, float y, float s)
{
	float t = s * 0.16f, h = s / 2;
	switch (which) {
	case SYM_CIRCLE:
		ring(x, y, h, t, PS_RED);
		break;
	case SYM_CROSS:
		bar(x - h * 0.8f, y - h * 0.8f, x + h * 0.8f, y + h * 0.8f, t, PS_BLUE);
		bar(x - h * 0.8f, y + h * 0.8f, x + h * 0.8f, y - h * 0.8f, t, PS_BLUE);
		break;
	case SYM_TRIANGLE:
		bar(x, y - h, x - h, y + h * 0.75f, t, PS_GREEN);
		bar(x - h, y + h * 0.75f, x + h, y + h * 0.75f, t, PS_GREEN);
		bar(x + h, y + h * 0.75f, x, y - h, t, PS_GREEN);
		break;
	default:
		outline((int)(x - h), (int)(y - h), (int)s, (int)s, PS_PINK, 255, (int)t);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* background: PlayStation-menu style                                       */
/* ------------------------------------------------------------------------ */
static void draw_background(void)
{
	uint32_t t = SDL_GetTicks();
	if (emu_game_loaded()) {
		video_draw_game(1);
	} else {
		for (int y = 0; y < 720; y += 6) {
			SDL_Color c = { (uint8_t)(4 + y / 90), (uint8_t)(8 + y / 40), (uint8_t)(30 + y / 14) };
			fill(0, y, 1280, 6, c, 255);
		}
	}
	/* drifting translucent squares */
	for (int i = 0; i < 14; i++) {
		float sz = 60 + (i * 37) % 140;
		float x = fmodf((i * 211.0f) + t * (0.004f + i * 0.0007f), 1280 + sz * 2) - sz;
		float y = 60 + (i * 97) % 600 + sinf(t * 0.0006f + i) * 20;
		float a = t * 0.00015f * (i & 1 ? 1 : -1) + i;
		float c = cosf(a) * sz / 2, s = sinf(a) * sz / 2;
		float q[8] = { x + c - s, y + s + c, x - c - s, y - s + c,
		               x - c + s, y - s - c, x + c + s, y + s - c };
		SDL_Color col = { 120, 150, 255, 255 };
		poly(q, 4, col, 14);
	}
}

/* ------------------------------------------------------------------------ */
/* input: buttons with auto-repeat + touch (tap / drag)                     */
/* ------------------------------------------------------------------------ */
static uint32_t repeatMask;
static uint32_t repeatAt;

static uint32_t poll_buttons(void)
{
	uint32_t held, pressed, now = SDL_GetTicks();
	static uint32_t prevHeld;
	wiiu_input_update();
	held = wiiu_input_menu_held() & ~WIIU_BTN_TOUCH;
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

typedef struct {
	int down;          /* finger currently on the screen */
	int sx, sy;        /* where it went down */
	int x, y;          /* current position */
	int dragging;      /* moved far enough to be a scroll, not a tap */
	int tapped;        /* released this frame without dragging */
	int tx, ty;        /* tap position */
	int dy;            /* vertical movement since last frame */
} touch_t;
static touch_t T;

static void poll_touch(void)
{
	const wiiu_pad_state *gp = wiiu_input_get(0);
	int on = gp->connected && (gp->held & WIIU_BTN_TOUCH);
	T.tapped = 0;
	T.dy = 0;
	if (on) {
		if (!T.down) {
			T.down = 1;
			T.sx = T.x = gp->touch_x;
			T.sy = T.y = gp->touch_y;
			T.dragging = 0;
		} else {
			T.dy = gp->touch_y - T.y;
			T.x = gp->touch_x;
			T.y = gp->touch_y;
			if (abs(T.y - T.sy) > 18 || abs(T.x - T.sx) > 18) T.dragging = 1;
		}
	} else if (T.down) {
		T.down = 0;
		if (!T.dragging) { T.tapped = 1; T.tx = T.sx; T.ty = T.sy; }
	}
}

static int tap_in(int x, int y, int w, int h)
{
	return T.tapped && T.tx >= x && T.tx < x + w && T.ty >= y && T.ty < y + h;
}

/* ------------------------------------------------------------------------ */
/* frame chrome                                                             */
/* ------------------------------------------------------------------------ */
static char statusMsg[160];
static uint32_t statusUntil;

static void status(const char *m)
{
	strncpy(statusMsg, m, sizeof(statusMsg) - 1);
	statusMsg[sizeof(statusMsg) - 1] = 0;
	statusUntil = SDL_GetTicks() + 3000;
}

#define BACK_X 28
#define BACK_Y 20
#define BACK_W 150
#define BACK_H 60

/* Returns 1 when the on-screen Back button was tapped. */
static int draw_header(const char *title, int showBack)
{
	SDL_Color hdr = { 0, 0, 0 };
	fill(0, 0, 1280, 100, hdr, 150);
	/* four-colour stripe under the header */
	fill(0, 100, 320, 4, PS_GREEN, 255);
	fill(320, 100, 320, 4, PS_RED, 255);
	fill(640, 100, 320, 4, PS_BLUE, 255);
	fill(960, 100, 320, 4, PS_PINK, 255);

	if (showBack) {
		SDL_Color panel = { 40, 48, 80 };
		fill(BACK_X, BACK_Y, BACK_W, BACK_H, panel, 230);
		outline(BACK_X, BACK_Y, BACK_W, BACK_H, GREY, 120, 2);
		symbol(SYM_CROSS, BACK_X + 32, BACK_Y + BACK_H / 2, 26);
		text(fontItem, BACK_X + 56, BACK_Y + BACK_H / 2, "Back", WHITE, 0);
	} else {
		/* PlayStation-coloured diamond logo */
		float cx = 70, cy = 50, r = 26;
		float q1[6] = { cx, cy - r, cx + r, cy, cx, cy };
		float q2[6] = { cx + r, cy, cx, cy + r, cx, cy };
		float q3[6] = { cx, cy + r, cx - r, cy, cx, cy };
		float q4[6] = { cx - r, cy, cx, cy - r, cx, cy };
		poly(q1, 3, PS_RED, 255);
		poly(q2, 3, PS_YELLOW, 255);
		poly(q3, 3, PS_BLUE, 255);
		poly(q4, 3, PS_GREEN, 255);
		text(fontTitle, 112, 50, "WiiUStation", WHITE, 0);
	}
	if (title) text(fontTitle, 1240, 50, title, WHITE, 2);
	return showBack && tap_in(BACK_X, BACK_Y, BACK_W, BACK_H);
}

static void draw_footer(const char *hint)
{
	SDL_Color bg = { 0, 0, 0 };
	fill(0, 664, 1280, 56, bg, 160);
	if (statusMsg[0] && SDL_GetTicks() < statusUntil) {
		text(fontSmall, 40, 692, statusMsg, PS_YELLOW, 0);
		return;
	}
	/* hint layout: "A ○ Select   B ✕ Back ..." drawn with real symbols */
	int x = 40;
	symbol(SYM_CIRCLE, x + 12, 692, 22); x += 32;
	x += text(fontSmall, x, 692, "A  OK", GREY, 0) + 30;
	symbol(SYM_CROSS, x + 12, 692, 22); x += 32;
	x += text(fontSmall, x, 692, "B  Back", GREY, 0) + 30;
	if (hint) text(fontSmall, x, 692, hint, DIM, 0);
}

static void present(void)
{
	SDL_RenderPresent(R);
	pump_events();
	SDL_Delay(1);
}

/* ------------------------------------------------------------------------ */
/* list widget (touch-friendly)                                             */
/* ------------------------------------------------------------------------ */
#define LIST_X   110
#define LIST_Y   128
#define LIST_W   1060
#define ROW_H    64
#define ROWS     8

typedef struct {
	int sel;
	float scroll;      /* first visible row, fractional while dragging */
} list_t;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Draws the list and handles touch. Returns the row tapped, or -1.
 * values (optional) are drawn right-aligned with ◀ ▶ arrows when arrows=1;
 * *arrow is set to -1/+1 when an arrow was tapped. */
static int list_run(list_t *L, const char **labels, const char **values, int count,
                    int arrows, int *arrow, const SDL_Color *marks)
{
	int maxFirst = count > ROWS ? count - ROWS : 0;
	int tapped = -1;
	if (arrow) *arrow = 0;

	/* drag to scroll */
	if (T.down && T.dragging && T.sy >= LIST_Y && T.sy < LIST_Y + ROWS * ROW_H)
		L->scroll -= (float)T.dy / ROW_H;
	if (!T.down) {
		/* keep the selection visible when using buttons */
		if (L->sel < (int)(L->scroll + 0.5f)) L->scroll = L->sel;
		if (L->sel >= (int)(L->scroll + 0.5f) + ROWS) L->scroll = L->sel - ROWS + 1;
		L->scroll = (float)(int)(L->scroll + 0.5f);
	}
	if (L->scroll < 0) L->scroll = 0;
	if (L->scroll > maxFirst) L->scroll = maxFirst;

	int first = (int)L->scroll;
	float frac = L->scroll - first;

	SDL_Color panel = { 10, 14, 34 };
	fill(LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16, panel, 170);
	SDL_RenderSetClipRect(R, &(SDL_Rect){ LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16 });

	for (int i = 0; i <= ROWS && first + i < count; i++) {
		int idx = first + i;
		int y = LIST_Y + (int)((i - frac) * ROW_H);
		int selected = idx == L->sel;
		if (selected) {
			SDL_Color hl = { 52, 84, 168 };
			fill(LIST_X - 12, y + 3, LIST_W + 24, ROW_H - 6, hl, 220);
			outline(LIST_X - 12, y + 3, LIST_W + 24, ROW_H - 6, PS_BLUE, 200, 2);
		} else {
			SDL_Color sep = { 60, 70, 110 };
			fill(LIST_X, y + ROW_H - 1, LIST_W, 1, sep, 120);
		}
		int tx = LIST_X;
		if (marks) {
			/* small coloured square marker, like the PS1 memory card screen */
			fill(LIST_X, y + ROW_H / 2 - 9, 18, 18, marks[idx], 255);
			tx += 34;
		}
		text(fontItem, tx, y + ROW_H / 2, labels[idx], selected ? WHITE : GREY, 0);
		if (values && values[idx] && values[idx][0]) {
			int right = LIST_X + LIST_W;
			if (arrows) {
				float ay = y + ROW_H / 2.0f;
				float rq[6] = { right - 6, ay, right - 26, ay - 14, right - 26, ay + 14 };
				poly(rq, 3, selected ? WHITE : DIM, 255);
				right -= 46;
				int w = text(fontItem, right, y + ROW_H / 2, values[idx], selected ? WHITE : PS_BLUE, 2);
				float lx = right - w - 20;
				float lq[6] = { lx - 20, ay, lx, ay - 14, lx, ay + 14 };
				poly(lq, 3, selected ? WHITE : DIM, 255);
			} else {
				text(fontItem, right, y + ROW_H / 2, values[idx], selected ? WHITE : PS_BLUE, 2);
			}
		}
	}
	SDL_RenderSetClipRect(R, NULL);

	if (count > ROWS) {
		int barH = ROWS * ROW_H;
		int h = barH * ROWS / count;
		int y = LIST_Y + (int)((barH - h) * L->scroll / maxFirst);
		fill(LIST_X + LIST_W + 30, y, 6, h, PS_BLUE, 200);
	}

	/* taps */
	if (T.tapped && T.ty >= LIST_Y && T.ty < LIST_Y + ROWS * ROW_H &&
	    T.tx >= LIST_X - 20 && T.tx < LIST_X + LIST_W + 40) {
		int idx = first + (int)((T.ty - LIST_Y) / ROW_H + frac);
		if (idx >= 0 && idx < count) {
			L->sel = idx;
			if (arrows && arrow && values && values[idx] && values[idx][0]) {
				if (T.tx > LIST_X + LIST_W - 70) *arrow = 1;
				else if (T.tx > LIST_X + LIST_W / 2) *arrow = -1;
				else tapped = idx;
			} else {
				tapped = idx;
			}
		}
	}
	return tapped;
}

/* ------------------------------------------------------------------------ */
/* file browser                                                             */
/* ------------------------------------------------------------------------ */
typedef struct { char name[256]; int dir; } entry_t;
static char browsePath[512] = WIIU_ISO_DIR;

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
	if (strcmp(path, WIIU_SD_ROOT) != 0) {
		strcpy(list[n].name, "..");
		list[n++].dir = 1;
	}
	if (d) {
		while ((ent = readdir(d)) != NULL) {
			if (ent->d_name[0] == '.') continue;
			int isdir = (ent->d_type & DT_DIR) != 0;
			if (!isdir && !is_game_file(ent->d_name)) continue;
			/* hide .bin tracks that belong to a .cue/.m3u in the same folder? keep simple */
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

static void go_up(void)
{
	char *slash = strrchr(browsePath, '/');
	if (slash && (size_t)(slash - browsePath) >= strlen(WIIU_SD_ROOT)) *slash = 0;
	if (strlen(browsePath) < strlen(WIIU_SD_ROOT)) strcpy(browsePath, WIIU_SD_ROOT);
}

static int browse(const char *title, char *out, size_t outsz)
{
	entry_t *list = NULL;
	int count = scan_dir(browsePath, &list);
	list_t L = { 0, 0 };
	const char **labels = NULL;
	SDL_Color *marks = NULL;

	for (;;) {
		labels = realloc(labels, (count + 1) * sizeof(char *));
		marks = realloc(marks, (count + 1) * sizeof(SDL_Color));
		for (int i = 0; i < count; i++) {
			labels[i] = list[i].name;
			marks[i] = list[i].dir ? PS_YELLOW : PS_BLUE;
		}
		if (wiiu_quit) break;

		poll_touch();
		uint32_t b = poll_buttons();
		int activate = -1;
		if (count) {
			if (b & WIIU_BTN_UP)    L.sel = L.sel > 0 ? L.sel - 1 : count - 1;
			if (b & WIIU_BTN_DOWN)  L.sel = L.sel < count - 1 ? L.sel + 1 : 0;
			if (b & (WIIU_BTN_L | WIIU_BTN_LEFT))  L.sel = clampi(L.sel - ROWS, 0, count - 1);
			if (b & (WIIU_BTN_R | WIIU_BTN_RIGHT)) L.sel = clampi(L.sel + ROWS, 0, count - 1);
			if (b & WIIU_BTN_A) activate = L.sel;
		}

		draw_background();
		int back = draw_header(title, 1);
		{
			const char *p = browsePath + strlen(WIIU_SD_ROOT);
			text(fontSmall, LIST_X, 116, *p ? p : "/", DIM, 0);
		}
		int t = -1;
		if (count == 0)
			text(fontItem, 640, 360, "No games here. Copy them to sd:/wiisxrx/isos", GREY, 1);
		else
			t = list_run(&L, labels, NULL, count, 0, NULL, marks);
		if (t >= 0) activate = t;
		draw_footer("L/R  Page    Drag to scroll");
		present();

		if (back || (b & WIIU_BTN_B)) {
			if (count && !strcmp(list[0].name, "..")) activate = 0;
			else break;
		}
		if (b & WIIU_BTN_PLUS) break;

		if (activate >= 0 && activate < count) {
			entry_t *e = &list[activate];
			if (!strcmp(e->name, "..")) {
				go_up();
			} else if (e->dir) {
				size_t l = strlen(browsePath);
				snprintf(browsePath + l, sizeof(browsePath) - l, "/%s", e->name);
			} else {
				snprintf(out, outsz, "%s/%s", browsePath, e->name);
				free(list); free(labels); free(marks);
				return 1;
			}
			free(list);
			count = scan_dir(browsePath, &list);
			L.sel = 0;
			L.scroll = 0;
		}
	}
	free(list); free(labels); free(marks);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* settings                                                                 */
/* ------------------------------------------------------------------------ */
static void advanced(void);

static void settings(void)
{
	enum { S_CORE, S_SKIP, S_FPS, S_AUDIO, S_ASPECT, S_FILTER, S_SCAN, S_CRT,
	       S_DITHER, S_PAD, S_RUMBLE, S_BIOS, S_LOGO, S_ADV, S_COUNT };
	const char *labels[S_COUNT] = {
		"CPU core", "Frame skip", "Show FPS", "Sound", "Screen", "Picture",
		"Scanlines", "CRT mask", "Dithering", "Controller", "Vibration",
		"BIOS", "PlayStation boot logo", "Advanced..."
	};
	const char *values[S_COUNT];
	list_t L = { 0, 0 };

	for (;;) {
		values[S_CORE]   = cfg.dynarec ? "Lightrec" : "Interpreter";
		values[S_SKIP]   = cfg.frameskip ? "Auto" : "Off";
		values[S_FPS]    = cfg.show_fps ? "On" : "Off";
		values[S_AUDIO]  = cfg.audio ? "On" : "Off";
		values[S_ASPECT] = cfg.aspect == WIIU_ASPECT_4_3 ? "4:3" :
		                   cfg.aspect == WIIU_ASPECT_STRETCH ? "Stretch" : "Integer";
		values[S_FILTER] = cfg.filter == WIIU_FILTER_LINEAR ? "Smooth" :
		                   cfg.filter == WIIU_FILTER_NEAREST ? "Pixel" : "Sharp smooth";
		values[S_SCAN]   = cfg.scanlines ? "On" : "Off";
		values[S_CRT]    = cfg.crt ? "On" : "Off";
		values[S_DITHER] = cfg.dithering == 0 ? "Off" : cfg.dithering == 1 ? "On" : "Force";
		values[S_PAD]    = cfg.analog ? "DualShock" : "Digital";
		values[S_RUMBLE] = cfg.rumble ? "On" : "Off";
		values[S_BIOS]   = cfg.bios_hle ? "Built-in" : "Auto";
		values[S_LOGO]   = cfg.bootlogo ? "On" : "Off";
		values[S_ADV]    = NULL;

		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : S_COUNT - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < S_COUNT - 1 ? L.sel + 1 : 0;
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;

		draw_background();
		int back = draw_header("Settings", 1);
		int arrow = 0;
		int t = list_run(&L, labels, values, S_COUNT, 1, &arrow, NULL);
		if (t >= 0) dir = 1;
		if (arrow) dir = arrow;
		draw_footer("Tap the arrows to change");
		present();

		if (back || (b & WIIU_BTN_B) || wiiu_quit) {
			settings_save();          /* settings are saved automatically */
			emu_apply_settings();
			return;
		}
		if (!dir) continue;
		switch (L.sel) {
		case S_CORE:
			if (!jit_available()) {
				cfg.dynarec = 0;
				status("Lightrec unavailable: no JIT (codegen) area for this title.");
			} else {
				cfg.dynarec = !cfg.dynarec;
				status("Applies the next time a game is loaded.");
			}
			break;
		case S_SKIP:   cfg.frameskip = !cfg.frameskip; break;
		case S_FPS:    cfg.show_fps = !cfg.show_fps; break;
		case S_AUDIO:  cfg.audio = !cfg.audio; break;
		case S_ASPECT: cfg.aspect = (cfg.aspect + 3 + dir) % 3; break;
		case S_FILTER: cfg.filter = (cfg.filter + 3 + dir) % 3; break;
		case S_SCAN:   cfg.scanlines = !cfg.scanlines; break;
		case S_CRT:    cfg.crt = !cfg.crt; break;
		case S_DITHER: cfg.dithering = (cfg.dithering + 3 + dir) % 3; break;
		case S_PAD:    cfg.analog = !cfg.analog;
		               if (cfg.analog) status("DualShock: some older games only work with Digital.");
		               break;
		case S_RUMBLE: cfg.rumble = !cfg.rumble; break;
		case S_BIOS:   cfg.bios_hle = !cfg.bios_hle; status("Applies the next time a game is loaded."); break;
		case S_LOGO:   cfg.bootlogo = !cfg.bootlogo; break;
		case S_ADV:    advanced(); break;
		}
	}
}

static void advanced(void)
{
	enum { A_GPU, A_SPU, A_REVERB, A_COUNT };
	const char *labels[A_COUNT] = {
		"GPU rendering on core 2", "Sound mixing on core 0", "Sound reverb"
	};
	const char *values[A_COUNT];
	list_t L = { 0, 0 };

	for (;;) {
		values[A_GPU]    = cfg.gpu_thread ? "On" : "Off";
		values[A_SPU]    = cfg.spu_thread ? "On" : "Off";
		values[A_REVERB] = cfg.reverb ? "On" : "Off";

		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : A_COUNT - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < A_COUNT - 1 ? L.sel + 1 : 0;
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A | WIIU_BTN_LEFT)) dir = 1;

		draw_background();
		int back = draw_header("Advanced", 1);
		int arrow = 0;
		int t = list_run(&L, labels, values, A_COUNT, 1, &arrow, NULL);
		if (t >= 0 || arrow) dir = 1;
		text(fontSmall, LIST_X, LIST_Y + A_COUNT * ROW_H + 40,
		     "Turn these off only if a game shows glitches. Reverb off = a little faster.", DIM, 0);
		draw_footer(NULL);
		present();

		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (!dir) continue;
		switch (L.sel) {
		case A_GPU:    cfg.gpu_thread = !cfg.gpu_thread; status("Applies the next time a game is loaded."); break;
		case A_SPU:    cfg.spu_thread = !cfg.spu_thread; break;
		case A_REVERB: cfg.reverb = !cfg.reverb; break;
		}
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
	static const SDL_Color allMarks[M_COUNT] = {
		{ 64, 226, 160, 255 }, { 124, 178, 232, 255 }, { 255, 92, 92, 255 },
		{ 255, 204, 32, 255 }, { 255, 105, 248, 255 }, { 255, 105, 248, 255 },
		{ 255, 105, 248, 255 }, { 150, 158, 178, 255 }, { 100, 108, 128, 255 }
	};
	int map[M_COUNT];
	const char *labels[M_COUNT];
	const char *values[M_COUNT];
	SDL_Color marks[M_COUNT];
	char slotStr[8], discStr[16];
	list_t L = { 0, 0 };

	if (!R) {
		R = (SDL_Renderer *)video_renderer();
		fontTitle = (TTF_Font *)video_open_font(38);
		fontItem  = (TTF_Font *)video_open_font(31);
		fontSmall = (TTF_Font *)video_open_font(23);
	}
	poll_buttons();
	T.down = T.tapped = 0;

	for (;;) {
		int n = 0, loaded = emu_game_loaded();
		for (int i = 0; i < M_COUNT; i++) {
			if (!loaded && (i == M_RESUME || i == M_RESET || i == M_DISC ||
			                i == M_SAVE || i == M_LOADST || i == M_SLOT))
				continue;
			map[n] = i;
			labels[n] = allLabels[i];
			values[n] = NULL;
			marks[n] = allMarks[i];
			if (i == M_SLOT) { snprintf(slotStr, sizeof(slotStr), "%d", slot); values[n] = slotStr; }
			if (i == M_DISC && emu_disk_count() > 1) {
				snprintf(discStr, sizeof(discStr), "%d / %d", emu_disk_index() + 1, emu_disk_count());
				values[n] = discStr;
			}
			n++;
		}
		if (L.sel >= n) L.sel = n - 1;
		if (wiiu_quit) return 0;

		poll_touch();
		uint32_t b = poll_buttons();
		int activate = 0, dir = 0;
		if (b & WIIU_BTN_UP)    L.sel = L.sel > 0 ? L.sel - 1 : n - 1;
		if (b & WIIU_BTN_DOWN)  L.sel = L.sel < n - 1 ? L.sel + 1 : 0;
		if (b & WIIU_BTN_A)     activate = 1;
		if (b & WIIU_BTN_LEFT)  dir = -1;
		if (b & WIIU_BTN_RIGHT) dir = 1;

		draw_background();
		int back = draw_header(NULL, 0);
		if (loaded) {
			char sub[300];
			snprintf(sub, sizeof(sub), "Now playing: %s", emu_game_name());
			text(fontSmall, LIST_X, 116, sub, GREY, 0);
		}
		int arrow = 0;
		int t = list_run(&L, labels, values, n, 0, &arrow, marks);
		if (t >= 0) activate = 1;
		/* touch on the slot / disc value cycles it */
		draw_footer(loaded ? "HOME  Wii U HOME Menu    In game: HOME / touch / L3+R3 = menu" :
		                     "Games go in sd:/wiisxrx/isos");
		{
			char info[96];
			snprintf(info, sizeof(info), "PCSX-ReARMed  |  %s", cfg.dynarec ? "Lightrec" : "Interpreter");
			text(fontSmall, 1240, 642, info, DIM, 2);
		}
		present();
		(void)back;

		if ((b & WIIU_BTN_B) && loaded) return 1;

		int item = map[L.sel];
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
					draw_background();
					draw_header("Loading", 0);
					text(fontItem, 640, 360, strrchr(path, '/') ? strrchr(path, '/') + 1 : path, WHITE, 1);
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
				                                            : "Could not save the state (slot %d).", slot);
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
	}
}
