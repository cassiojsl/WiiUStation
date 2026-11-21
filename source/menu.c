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
#include <SDL2/SDL_image.h>
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
	SDL_Vertex v[32];
	int idx[96], k = 0;
	if (n > 32) n = 32;
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
static SDL_Texture *logoTex;
static int logoW, logoH;

static int draw_header(const char *title, int showBack)
{
	SDL_Color hdr = { 0, 0, 0 };
	fill(0, 0, 1280, 104, hdr, 150);

	if (showBack) {
		SDL_Color panel = { 40, 48, 80 };
		fill(BACK_X, BACK_Y, BACK_W, BACK_H, panel, 230);
		outline(BACK_X, BACK_Y, BACK_W, BACK_H, GREY, 120, 2);
		float ax = BACK_X + 30, ay = BACK_Y + BACK_H / 2.0f;
		float tri[6] = { ax - 12, ay, ax + 8, ay - 14, ax + 8, ay + 14 };
		poly(tri, 3, WHITE, 255);
		text(fontItem, BACK_X + 52, BACK_Y + BACK_H / 2, "Back", WHITE, 0);
	} else {
		if (!logoTex) {
			SDL_Surface *s = IMG_Load("fs:/vol/content/logo.png");
			if (s) {
				logoTex = SDL_CreateTextureFromSurface(R, s);
				logoW = s->w; logoH = s->h;
				SDL_FreeSurface(s);
			}
		}
		if (logoTex) {
			int h = 80, w = logoW * h / logoH;
			SDL_Rect d = { 28, 12, w, h };
			SDL_RenderCopy(R, logoTex, NULL, &d);
		} else {
			text(fontTitle, 40, 52, "WiiUStation", WHITE, 0);
		}
	}
	if (title) text(fontTitle, 1240, 52, title, WHITE, 2);
	return showBack && tap_in(BACK_X, BACK_Y, BACK_W, BACK_H);
}

/* Wii U style button glyph: light disc with a dark letter */
static void wiiu_button(int x, int y, const char *label)
{
	const int N = 24;
	float r = 15, xy[2 * 24];
	for (int i = 0; i < N; i++) {
		xy[i * 2] = x + cosf(i * 6.2831853f / N) * r;
		xy[i * 2 + 1] = y + sinf(i * 6.2831853f / N) * r;
	}
	poly(xy, N, WHITE, 230);
	text(fontSmall, x, y, label, (SDL_Color){ 30, 34, 50, 255 }, 1);
}

static void draw_footer(const char *hint)
{
	SDL_Color bg = { 0, 0, 0 };
	fill(0, 664, 1280, 56, bg, 160);
	if (statusMsg[0] && SDL_GetTicks() < statusUntil) {
		text(fontSmall, 40, 692, statusMsg, PS_YELLOW, 0);
		return;
	}
	int x = 52;
	wiiu_button(x, 692, "A"); x += 24;
	x += text(fontSmall, x, 692, "OK", GREY, 0) + 34;
	wiiu_button(x, 692, "B"); x += 24;
	x += text(fontSmall, x, 692, "Back", GREY, 0) + 34;
	if (hint && hint[0] == 'X' && hint[1] == ' ') {     /* "X  ..." -> X glyph */
		wiiu_button(x, 692, "X"); x += 24;
		hint += 3;
	}
	if (hint) text(fontSmall, x, 692, hint, DIM, 0);
}

/* Screen transitions: every new screen fades in from black. */
static uint32_t fadeStart;
static void transition(void) { fadeStart = SDL_GetTicks(); }

static void present(void)
{
	uint32_t dt = SDL_GetTicks() - fadeStart;
	if (dt < 220) {
		SDL_Color k = { 0, 0, 0 };
		fill(0, 0, 1280, 720, k, (uint8_t)(255 - dt * 255 / 220));
	}
	SDL_RenderPresent(R);
	pump_events();
	SDL_Delay(1);
}

/* "Crash Bandicoot 3 (USA) (SCUS-94244).chd" -> "Crash Bandicoot 3" */
static void pretty_name(const char *file, char *out, size_t n)
{
	const char *b = strrchr(file, '/');
	b = b ? b + 1 : file;
	snprintf(out, n, "%s", b);
	char *dot = strrchr(out, '.');
	if (dot && strlen(dot) <= 5) *dot = 0;
	for (char *p = out; *p; p++)
		if ((*p == '(' || *p == '[') && p > out) {
			char *e = p;
			while (e > out && e[-1] == ' ') e--;
			*e = 0;
			break;
		}
	if (!out[0]) snprintf(out, n, "%s", b);
}

/* Smooth loading indicator: a rotating arc with a fading tail. */
static void spinner(float cx, float cy, float r, SDL_Color c)
{
	float base = SDL_GetTicks() * 0.006f;
	for (int i = 0; i < 12; i++) {
		float a0 = base + i * 0.22f, a1 = a0 + 0.2f, t = r * 0.3f;
		float q[8] = {
			cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r,
			cx + cosf(a1) * (r - t), cy + sinf(a1) * (r - t), cx + cosf(a0) * (r - t), cy + sinf(a0) * (r - t)
		};
		poly(q, 4, c, (uint8_t)(40 + i * 17));
	}
}

/* Cover art panel on the right side (PS1 jewel-case look). */
#define COVER_X 860
#define COVER_W 360
static void draw_cover(const char *id, const char *caption)
{
	int w = 0, h = 0;
	SDL_Texture *t = id && id[0] ? (SDL_Texture *)covers_texture(R, id, &w, &h) : NULL;
	SDL_Color panel = { 10, 14, 34 };
	int y = 128;
	fill(COVER_X - 12, y - 8, COVER_W + 24, COVER_W + 80, panel, 170);
	if (t && w > 0 && h > 0) {
		int dw = COVER_W, dh = COVER_W * h / w;
		if (dh > COVER_W) { dh = COVER_W; dw = COVER_W * w / h; }
		SDL_Rect d = { COVER_X + (COVER_W - dw) / 2, y + (COVER_W - dh) / 2, dw, dh };
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(R, t, NULL, &d);
		outline(d.x - 2, d.y - 2, d.w + 4, d.h + 4, GREY, 160, 2);
	} else {
		float cx = COVER_X + COVER_W / 2.0f, cy = y + COVER_W / 2.0f;
		if (id && id[0]) {
			spinner(cx, cy, 46, PS_BLUE);
			text(fontSmall, (int)cx, (int)cy + 90, "Loading cover", DIM, 1);
		} else {
			/* no ID: a plain disc */
			ring(cx, cy, 130, 100, (SDL_Color){ 60, 66, 92, 255 });
			ring(cx, cy, 30, 6, GREY);
			text(fontSmall, (int)cx, (int)cy + 160, "No cover", DIM, 1);
		}
	}
	if (caption && caption[0]) text(fontSmall, COVER_X + COVER_W / 2, y + COVER_W + 40, caption, GREY, 1);
}

/* ------------------------------------------------------------------------ */
/* list widget (touch-friendly)                                             */
/* ------------------------------------------------------------------------ */
#define LIST_X   110
#define LIST_Y   128
static int listW = 1060;
#define LIST_W   listW
#define ROW_H    64
#define ROWS     8

typedef struct {
	int sel;
	float scroll;      /* first visible row, fractional while dragging */
	float hy;          /* animated highlight position (screen y) */
	int hyInit;
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
		/* keep the selection visible when using buttons; glide there */
		float target = (float)(int)(L->scroll + 0.5f);
		if (L->sel < target) target = L->sel;
		if (L->sel >= target + ROWS) target = L->sel - ROWS + 1;
		L->scroll += (target - L->scroll) * 0.35f;
		if (fabsf(target - L->scroll) < 0.01f) L->scroll = target;
	}
	if (L->scroll < 0) L->scroll = 0;
	if (L->scroll > maxFirst) L->scroll = maxFirst;

	int first = (int)L->scroll;
	float frac = L->scroll - first;

	SDL_Color panel = { 10, 14, 34 };
	fill(LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16, panel, 170);
	SDL_RenderSetClipRect(R, &(SDL_Rect){ LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16 });
	{
		/* highlight glides to the selected row */
		float ty = LIST_Y + (L->sel - L->scroll) * ROW_H;
		if (!L->hyInit) { L->hy = ty; L->hyInit = 1; }
		L->hy += (ty - L->hy) * 0.4f;
		SDL_Color hl = { 52, 84, 168 };
		fill(LIST_X - 12, (int)L->hy + 3, LIST_W + 24, ROW_H - 6, hl, 220);
		outline(LIST_X - 12, (int)L->hy + 3, LIST_W + 24, ROW_H - 6, PS_BLUE, 200, 2);
	}

	for (int i = 0; i <= ROWS && first + i < count; i++) {
		int idx = first + i;
		int y = LIST_Y + (int)((i - frac) * ROW_H);
		int selected = idx == L->sel;
		if (!selected) {
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
typedef struct { char name[256]; int dir; int idTried; char id[16]; } entry_t;
static char browsePath[512];

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
		list[n].idTried = 1;
		list[n].id[0] = 0;
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
			list[n].idTried = 0;
			list[n].id[0] = 0;
			n++;
		}
		closedir(d);
	}
	int start = (n && !strcmp(list[0].name, "..")) ? 1 : 0;
	qsort(list + start, n - start, sizeof(entry_t), cmp_entry);
	*out = list;
	return n;
}


/* ------------------------------------------------------------------------ */
/* game grid (covers)                                                       */
/* ------------------------------------------------------------------------ */
#define GRID_X     64
#define GRID_Y     124
#define GRID_COLS  5
#define CELL_W     230
#define CELL_H     268
#define COVER_SZ   190
#define GRID_ROWS_VIS 2

typedef struct {
	int sel;
	float scroll;      /* first visible row */
	float hx, hy;      /* animated highlight */
	int hInit;
} grid_t;

static void cover_or_disc(const char *id, int x, int y, int sz)
{
	int w = 0, h = 0;
	SDL_Texture *t = id && id[0] ? (SDL_Texture *)covers_texture(R, id, &w, &h) : NULL;
	if (t && w > 0 && h > 0) {
		int dw = sz, dh = sz * h / w;
		if (dh > sz) { dh = sz; dw = sz * w / h; }
		SDL_Rect d = { x + (sz - dw) / 2, y + (sz - dh) / 2, dw, dh };
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(R, t, NULL, &d);
	} else {
		SDL_Color slot = { 26, 32, 60 };
		fill(x, y, sz, sz, slot, 220);
		if (id && id[0]) spinner(x + sz / 2.0f, y + sz / 2.0f, 26, PS_BLUE);
		else {
			ring(x + sz / 2.0f, y + sz / 2.0f, sz * 0.36f, sz * 0.26f, (SDL_Color){ 60, 66, 92, 255 });
			ring(x + sz / 2.0f, y + sz / 2.0f, sz * 0.09f, 3, GREY);
		}
	}
}

/* Draws the grid of entries; returns the tapped entry or -1.
 * Detects the game ID of at most one visible entry per frame. */
static int grid_run(grid_t *G, entry_t *list, const char **labels, int count, const char *dir)
{
	int rows = (count + GRID_COLS - 1) / GRID_COLS;
	int maxFirst = rows > GRID_ROWS_VIS ? rows - GRID_ROWS_VIS : 0;
	int tapped = -1, detected = 0;

	if (T.down && T.dragging && T.sy >= GRID_Y)
		G->scroll -= (float)T.dy / CELL_H;
	if (!T.down) {
		int selRow = G->sel / GRID_COLS;
		float target = (float)(int)(G->scroll + 0.5f);
		if (selRow < target) target = selRow;
		if (selRow >= target + GRID_ROWS_VIS) target = selRow - GRID_ROWS_VIS + 1;
		G->scroll += (target - G->scroll) * 0.3f;
		if (fabsf(target - G->scroll) < 0.01f) G->scroll = target;
	}
	if (G->scroll < 0) G->scroll = 0;
	if (G->scroll > maxFirst) G->scroll = maxFirst;

	SDL_RenderSetClipRect(R, &(SDL_Rect){ 0, GRID_Y - 6, 1280, 664 - GRID_Y + 6 });
	{
		float tx = GRID_X + (G->sel % GRID_COLS) * CELL_W;
		float ty = GRID_Y + (G->sel / GRID_COLS - G->scroll) * CELL_H;
		if (!G->hInit) { G->hx = tx; G->hy = ty; G->hInit = 1; }
		G->hx += (tx - G->hx) * 0.4f;
		G->hy += (ty - G->hy) * 0.4f;
		SDL_Color hl = { 52, 84, 168 };
		fill((int)G->hx, (int)G->hy, CELL_W - 12, CELL_H - 10, hl, 200);
		outline((int)G->hx, (int)G->hy, CELL_W - 12, CELL_H - 10, PS_BLUE, 220, 3);
	}
	int firstRow = (int)G->scroll;
	for (int r = firstRow; r <= firstRow + GRID_ROWS_VIS && r < rows; r++) {
		for (int c = 0; c < GRID_COLS; c++) {
			int i = r * GRID_COLS + c;
			if (i >= count) break;
			entry_t *e = &list[i];
			int x = GRID_X + c * CELL_W;
			int y = GRID_Y + (int)((r - G->scroll) * CELL_H);
			int cx = x + (CELL_W - 12 - COVER_SZ) / 2;
			if (!e->dir && !e->idTried && !detected && !T.down) {
				char full[768];
				snprintf(full, sizeof(full), "%s/%s", dir, e->name);
				e->idTried = 1;
				if (!covers_detect_id(full, e->id)) e->id[0] = 0;
				detected = 1;
			}
			if (e->dir) {
				SDL_Color folder = { 40, 48, 84 };
				fill(cx, y + 10, COVER_SZ, COVER_SZ, folder, 220);
				fill(cx + 40, y + 64, COVER_SZ - 80, COVER_SZ - 110, PS_YELLOW, 200);
				fill(cx + 40, y + 52, 60, 16, PS_YELLOW, 200);
			} else {
				cover_or_disc(e->id, cx, y + 10, COVER_SZ);
			}
			/* name, cleaned and shortened to fit */
			char full[272], nm[40];
			if (e->dir) snprintf(full, sizeof(full), "%s", labels[i]);
			else pretty_name(e->name, full, sizeof(full));
			if (strlen(full) > 21) snprintf(nm, sizeof(nm), "%.19s...", full);
			else snprintf(nm, sizeof(nm), "%s", full);
			text(fontSmall, x + (CELL_W - 12) / 2, y + COVER_SZ + 36, nm, i == G->sel ? WHITE : GREY, 1);
			if (T.tapped && T.tx >= x && T.tx < x + CELL_W - 12 && T.ty >= y && T.ty < y + CELL_H - 10 &&
			    T.ty >= GRID_Y)
				tapped = i;
		}
	}
	SDL_RenderSetClipRect(R, NULL);
	if (rows > GRID_ROWS_VIS) {
		int barH = 664 - GRID_Y - 20;
		int h = barH * GRID_ROWS_VIS / rows;
		int y = GRID_Y + (int)((barH - h) * G->scroll / maxFirst);
		fill(1260, y, 6, h, PS_BLUE, 200);
	}
	if (tapped >= 0) G->sel = tapped;
	return tapped;
}

static int at_or_above(const char *path, const char *root)
{
	/* 1 when path is root itself (or shorter, i.e. outside it) */
	return strlen(path) <= strlen(root);
}

static void go_up(void)
{
	char *slash = strrchr(browsePath, '/');
	if (slash && (size_t)(slash - browsePath) >= strlen(WIIU_SD_ROOT)) *slash = 0;
	if (strlen(browsePath) < strlen(WIIU_SD_ROOT)) strcpy(browsePath, WIIU_SD_ROOT);
}

/* Game browser (pickDir = 0) or folder picker (pickDir = 1).
 * Opens in the games folder. B goes up one level and leaves the browser
 * once it is back in the games folder; the on-screen Back button always
 * leaves straight away. Returns 1 with the chosen file / folder in out. */
static int browse(const char *title, char *out, size_t outsz, int pickDir)
{
	entry_t *list = NULL;
	int count;
	list_t L = { 0, 0 };
	grid_t G = { 0 };
	const char **labels = NULL;
	char (*names)[272] = NULL;
	const char *root = pickDir ? WIIU_SD_ROOT : cfg.games_dir;
	int ret = 0;

	if (pickDir || !browsePath[0] || strncmp(browsePath, cfg.games_dir, strlen(cfg.games_dir)))
		strcpy(browsePath, pickDir ? cfg.games_dir : cfg.games_dir);
	{
		struct stat st;
		if (stat(browsePath, &st) != 0) strcpy(browsePath, WIIU_SD_ROOT);
	}

rescan:
	free(list);
	count = scan_dir(browsePath, &list);
	if (!pickDir && count && !strcmp(list[0].name, "..") && at_or_above(browsePath, cfg.games_dir)) {
		memmove(list, list + 1, (count - 1) * sizeof(entry_t));
		count--;
	}
	if (pickDir) {
		/* folders only, plus "use this folder" on top */
		int m = 0;
		for (int i = 0; i < count; i++) if (list[i].dir) list[m++] = list[i];
		count = m;
		list = realloc(list, (count + 1) * sizeof(entry_t));
		memmove(list + 1, list, count * sizeof(entry_t));
		strcpy(list[0].name, "\x01");    /* marker: use this folder */
		list[0].dir = 2;
		list[0].idTried = 1;
		list[0].id[0] = 0;
		count++;
	}
	labels = realloc(labels, (count + 1) * sizeof(char *));
	names = realloc(names, (count + 1) * sizeof(*names));
	for (int i = 0; i < count; i++) {
		if (list[i].dir == 2) snprintf(names[i], sizeof(names[i]), "[ Use this folder ]");
		else if (!strcmp(list[i].name, "..")) snprintf(names[i], sizeof(names[i]), ".. (up)");
		else if (list[i].dir) snprintf(names[i], sizeof(names[i]), "%s/", list[i].name);
		else snprintf(names[i], sizeof(names[i]), "%s", list[i].name);
		labels[i] = names[i];
	}
	L.sel = 0;
	L.scroll = 0;
	L.hyInit = 0;
	memset(&G, 0, sizeof(G));
	transition();

	for (;;) {
		int useGrid = !pickDir && cfg.view == 0;
		if (wiiu_quit) break;
		poll_touch();
		uint32_t b = poll_buttons();
		int activate = -1;
		if (count) {
			if (useGrid) {
				if (b & WIIU_BTN_LEFT)  G.sel = clampi(G.sel - 1, 0, count - 1);
				if (b & WIIU_BTN_RIGHT) G.sel = clampi(G.sel + 1, 0, count - 1);
				if (b & WIIU_BTN_UP)    G.sel = clampi(G.sel - GRID_COLS, 0, count - 1);
				if (b & WIIU_BTN_DOWN)  G.sel = clampi(G.sel + GRID_COLS, 0, count - 1);
				if (b & WIIU_BTN_L)     G.sel = clampi(G.sel - GRID_COLS * 2, 0, count - 1);
				if (b & WIIU_BTN_R)     G.sel = clampi(G.sel + GRID_COLS * 2, 0, count - 1);
				if (b & WIIU_BTN_A) activate = G.sel;
			} else {
				if (b & WIIU_BTN_UP)    L.sel = L.sel > 0 ? L.sel - 1 : count - 1;
				if (b & WIIU_BTN_DOWN)  L.sel = L.sel < count - 1 ? L.sel + 1 : 0;
				if (b & (WIIU_BTN_L | WIIU_BTN_LEFT))  L.sel = clampi(L.sel - ROWS, 0, count - 1);
				if (b & (WIIU_BTN_R | WIIU_BTN_RIGHT)) L.sel = clampi(L.sel + ROWS, 0, count - 1);
				if (b & WIIU_BTN_A) activate = L.sel;
			}
		}
		if (!pickDir && (b & WIIU_BTN_X)) {
			cfg.view = !cfg.view;          /* X: switch grid / list */
			settings_save();
			if (cfg.view) L.sel = G.sel; else G.sel = L.sel;
			L.hyInit = G.hInit = 0;
			transition();
		}

		draw_background();
		int back = draw_header(title, 1);
		{
			const char *p = browsePath + strlen(WIIU_SD_ROOT);
			text(fontSmall, LIST_X, 116, *p ? p : "/", DIM, 0);
		}
		int t = -1;
		if (count == 0) {
			text(fontItem, 640, 360, "No games in this folder.", GREY, 1);
			text(fontSmall, 640, 410, "Choose your games folder in Settings.", DIM, 1);
		} else if (pickDir) {
			t = list_run(&L, labels, NULL, count, 0, NULL, NULL);
		} else if (useGrid) {
			t = grid_run(&G, list, labels, count, browsePath);
		} else {
			entry_t *e = &list[L.sel];
			listW = 700;
			t = list_run(&L, labels, NULL, count, 0, NULL, NULL);
			listW = 1060;
			if (!e->dir) {
				if (!e->idTried && !T.down) {
					char full[768];
					snprintf(full, sizeof(full), "%s/%s", browsePath, e->name);
					e->idTried = 1;
					if (!covers_detect_id(full, e->id)) e->id[0] = 0;
				}
				draw_cover(e->id, e->id[0] ? e->id : NULL);
			}
		}
		if (t >= 0) activate = t;
		draw_footer(pickDir ? "Pick the folder with your games" :
		            useGrid ? "X  List view    Drag to scroll" : "X  Grid view    L/R  Page");
		present();

		if (back || (b & WIIU_BTN_PLUS)) break;               /* leave now */
		if (b & WIIU_BTN_B) {
			if (at_or_above(browsePath, root)) break;          /* back to menu */
			go_up();
			goto rescan;
		}
		if (activate >= 0 && activate < count) {
			entry_t *e = &list[activate];
			if (e->dir == 2) {
				snprintf(out, outsz, "%s", browsePath);
				ret = 1;
				break;
			} else if (!strcmp(e->name, "..")) {
				go_up();
			} else if (e->dir) {
				size_t l = strlen(browsePath);
				snprintf(browsePath + l, sizeof(browsePath) - l, "/%s", e->name);
			} else {
				snprintf(out, outsz, "%s/%s", browsePath, e->name);
				ret = 1;
				break;
			}
			goto rescan;
		}
	}
	free(list); free(labels); free(names);
	return ret;
}

/* ------------------------------------------------------------------------ */
/* settings                                                                 */
/* ------------------------------------------------------------------------ */
static void advanced(void);

static void settings(void)
{
	enum { S_GAMES, S_VIEW, S_CORE, S_SKIP, S_FPS, S_AUDIO, S_ASPECT, S_FILTER, S_BEZEL, S_SCAN, S_CRT,
	       S_DITHER, S_PAD, S_RUMBLE, S_BIOS, S_LOGO, S_ADV, S_COUNT };
	const char *labels[S_COUNT] = {
		"Games folder", "Game list", "CPU core", "Frame skip", "Show FPS", "Sound", "Screen", "Picture", "Border",
		"Scanlines", "CRT mask", "Dithering", "Controller", "Vibration",
		"BIOS", "PlayStation boot logo", "Advanced..."
	};
	const char *values[S_COUNT];
	list_t L = { 0, 0 };
	transition();

	for (;;) {
		{
			/* show the last two folder names: ".../WiiUStation/games" */
			static char gd[80];
			const char *p = cfg.games_dir + strlen(WIIU_SD_ROOT);
			const char *last = strrchr(p, '/'), *prev = last;
			if (last && last > p) { prev = last - 1; while (prev > p && *prev != '/') prev--; }
			snprintf(gd, sizeof(gd), "%s%.40s", prev && prev > p ? "..." : "", prev && *p ? prev : "/");
			values[S_GAMES] = gd;
		}
		values[S_VIEW]   = cfg.view ? "List" : "Grid with covers";
		values[S_BEZEL]  = cfg.bezel == BEZEL_OFF ? "Off" : cfg.bezel == BEZEL_AMBIENT ? "Ambient" : "Custom image";
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
		case S_GAMES: {
			char dir[512];
			if (browse("Games folder", dir, sizeof(dir), 1)) {
				strncpy(cfg.games_dir, dir, sizeof(cfg.games_dir) - 1);
				cfg.games_dir[sizeof(cfg.games_dir) - 1] = 0;
				browsePath[0] = 0;
				settings_save();
				status("Games folder saved.");
			}
			break;
		}
		case S_VIEW:  cfg.view = !cfg.view; break;
		case S_BEZEL:
			cfg.bezel = (cfg.bezel + 3 + dir) % 3;
			if (cfg.bezel == BEZEL_CUSTOM)
				status("Put 1280x720 PNGs in WiiUStation/bezels: default.png or <GAME-ID>.png");
			break;
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
	transition();

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
/* credits                                                                  */
/* ------------------------------------------------------------------------ */
static void credits(void)
{
	static const char *lines[] = {
		"#WiiUStation",
		"PlayStation emulator for the Wii U",
		"",
		"#Emulator core",
		"PCSX-ReARMed  -  notaz, Exophase, senquack, pcercuei",
		"and the libretro / PCSX-ReARMed contributors",
		"PCSX / PCSX-df / PCSX-Reloaded teams",
		"Lightrec MIPS-to-PowerPC recompiler  -  Paul Cercueil",
		"GNU lightning  -  Free Software Foundation",
		"P.E.Op.S. soft GPU  -  Pete Bernert and the P.E.Op.S. team",
		"dfsound SPU  -  PCSX-df team",
		"libchdr  -  Romain Tisserand and contributors",
		"",
		"#Wii U",
		"wut  -  devkitPro and the Wii U homebrew community",
		"devkitPPC / devkitPro toolchain",
		"Aroma environment  -  Maschell and contributors",
		"SDL2 for Wii U  -  GaryOderNichts, Maschell and contributors",
		"",
		"#Libraries",
		"SDL2, SDL2_ttf, SDL2_image  -  Sam Lantinga and the SDL team",
		"FreeType, HarfBuzz, libpng, libjpeg-turbo, libwebp, zlib",
		"libcurl  -  Daniel Stenberg and contributors",
		"Mbed TLS  -  Trusted Firmware / Arm",
		"Mozilla CA certificate store (certifi)",
		"",
		"#Content",
		"Game covers  -  xlenore/psx-covers",
		"",
		"#Licence",
		"GPL-2.0-or-later. Source code is available on GitHub.",
		"PlayStation is a trademark of Sony Interactive Entertainment.",
		"Wii U is a trademark of Nintendo. This project is not affiliated",
		"with or endorsed by Sony or Nintendo.",
	};
	const int n = sizeof(lines) / sizeof(lines[0]);
	const int LH = 40;
	float scroll = 0, autoV = 0.35f;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (T.down && T.dragging) { scroll -= T.dy; autoV = 0; }
		if (b & WIIU_BTN_DOWN) { scroll += LH; autoV = 0; }
		if (b & WIIU_BTN_UP)   { scroll -= LH; autoV = 0; }
		scroll += autoV;
		float maxS = n * LH - 480;
		if (scroll < 0) scroll = 0;
		if (scroll > maxS) scroll = maxS;

		draw_background();
		int back = draw_header("Credits", 1);
		SDL_Color panel = { 10, 14, 34 };
		fill(90, 120, 1100, 536, panel, 170);
		SDL_RenderSetClipRect(R, &(SDL_Rect){ 90, 124, 1100, 528 });
		for (int i = 0; i < n; i++) {
			int y = 150 + i * LH - (int)scroll;
			if (y < 100 || y > 700) continue;
			if (lines[i][0] == '#')
				text(fontItem, 640, y, lines[i] + 1, PS_BLUE, 1);
			else
				text(fontSmall, 640, y, lines[i], i < 2 ? WHITE : GREY, 1);
		}
		SDL_RenderSetClipRect(R, NULL);
		draw_footer("Drag to scroll");
		present();
		if (back || (b & (WIIU_BTN_B | WIIU_BTN_A)) || wiiu_quit) return;
	}
}

/* Loading screen shown while the game starts (the load itself blocks). */
static void loading_screen(const char *path)
{
	char id[16] = "";
	const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	covers_detect_id(path, id);
	for (int frame = 0; frame < 2; frame++) {     /* two frames: TV + GamePad */
		SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
		SDL_RenderClear(R);
		for (int y = 0; y < 720; y += 6) {
			SDL_Color c = { (uint8_t)(4 + y / 90), (uint8_t)(8 + y / 40), (uint8_t)(30 + y / 14) };
			fill(0, y, 1280, 6, c, 255);
		}
		cover_or_disc(id, 640 - 150, 150, 300);
		char nm[80];
		pretty_name(name, nm, sizeof(nm));
		text(fontItem, 640, 500, nm, WHITE, 1);
		spinner(640, 580, 22, PS_BLUE);
		text(fontSmall, 640, 630, "Loading...", GREY, 1);
		SDL_RenderPresent(R);
	}
}

static void menu_fonts(void)
{
	if (!R) R = (SDL_Renderer *)video_renderer();
	if (!fontTitle) {
		fontTitle = (TTF_Font *)video_open_font(38);
		fontItem  = (TTF_Font *)video_open_font(31);
		fontSmall = (TTF_Font *)video_open_font(23);
	}
}

/* ------------------------------------------------------------------------ */
/* main menu                                                                */
/* ------------------------------------------------------------------------ */
static int slot = 0;

int menu_run(void)
{
	enum { M_RESUME, M_LOAD, M_RESET, M_DISC, M_SAVE, M_LOADST, M_SLOT, M_SETTINGS, M_CREDITS, M_EXIT, M_COUNT };
	static const char *allLabels[M_COUNT] = {
		"Resume game", "Load game", "Reset game", "Change disc", "Save state",
		"Load state", "State slot", "Settings", "Credits", "Exit to Wii U Menu"
	};
	int map[M_COUNT];
	const char *labels[M_COUNT];
	const char *values[M_COUNT];
	char slotStr[8], discStr[16];
	list_t L = { 0, 0 };

	menu_fonts();
	poll_buttons();
	T.down = T.tapped = 0;
	transition();

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
		if (loaded) listW = 700;
		int t = list_run(&L, labels, values, n, 0, &arrow, NULL);
		listW = 1060;
		if (loaded) draw_cover(emu_game_id(), emu_game_id());
		if (t >= 0) activate = 1;
		/* touch on the slot / disc value cycles it */
		draw_footer(loaded ? "HOME  Wii U HOME Menu    In game: HOME / touch / L3+R3 = menu" :
		                     "Games go in sd:/wiiu/apps/WiiUStation/games");
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
				if (browse("Load game", path, sizeof(path), 0)) {
					loading_screen(path);
					if (emu_load_game(path) == 0) { video_fade_in(); return 1; }
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
				} else if (browse("Change disc", path, sizeof(path), 0)) {
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
				transition();
				break;
			case M_CREDITS:
				credits();
				transition();
				break;
			case M_EXIT:
				return 0;
			}
		}
	}
}
