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
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "platform.h"

/* ------------------------------------------------------------------------ */
/* drawing helpers                                                          */
/* ------------------------------------------------------------------------ */
static SDL_Renderer *R;
static TTF_Font *fontTitle, *fontItem, *fontSmall;
static int introShown;   /* the opening plays once */
static int introCancel;  /* B in the opening: don't open the last game */
static int autoResume;   /* continue without asking (opening the last game) */

static const SDL_Color WHITE  = { 240, 242, 248, 255 };
static const SDL_Color GREY   = { 150, 158, 178, 255 };
static const SDL_Color DIM    = { 100, 108, 128, 255 };
static const SDL_Color PS_GREEN  = {  64, 226, 160, 255 };  /* triangle */
static const SDL_Color PS_RED    = { 255,  92,  92, 255 };  /* circle   */
static const SDL_Color PS_CROSS  = { 124, 178, 232, 255 };  /* cross    */
static SDL_Color th(int r, int g, int b);
#define PS_BLUE th(124, 178, 232)                            /* the theme's light colour */
static const SDL_Color PS_PINK   = { 255, 105, 248, 255 };  /* square   */
static const SDL_Color PS_YELLOW = { 255, 204,  32, 255 };

/* ---- colour themes: the menu's blues turned to the chosen hue ---- */
static SDL_Color th(int r, int g, int b)
{
	/* the theme's hue (degrees), how much colour and brightness are kept */
	static const float hue[THEME_COUNT]  = { 210, 272, 322, 356, 24, 44, 142, 178, 215 };
	static const float satk[THEME_COUNT] = { 1, 1, 1, 1, 1, 0.95f, 0.85f, 1, 0.10f };
	static const float valk[THEME_COUNT] = { 1, 1, 1, 1, 1, 0.92f, 0.80f, 0.92f, 1 };
	SDL_Color c = { (Uint8)r, (Uint8)g, (Uint8)b, 255 };
	int t = cfg.theme;
	if (t <= 0 || t >= THEME_COUNT) return c;
	float fr = r / 255.0f, fg = g / 255.0f, fb = b / 255.0f;
	float mx = fmaxf(fr, fmaxf(fg, fb)), mn = fminf(fr, fminf(fg, fb)), d = mx - mn;
	float h = 0, sat = mx > 0 ? d / mx : 0, v = mx;
	if (d > 0) {
		if (mx == fr) h = 60 * fmodf((fg - fb) / d, 6);
		else if (mx == fg) h = 60 * ((fb - fr) / d + 2);
		else h = 60 * ((fr - fg) / d + 4);
	}
	/* the blues lie around 210 degrees: keep a little of their spread
	 * around the new hue, so the dark background and the bright accents
	 * stay one family */
	h = fmodf(hue[t] + (h - 210) * 0.35f + 720, 360);
	if (t == THEME_GRAPHITE) v *= 1 - 0.55f * sat;   /* a colourful accent becomes a mid grey */
	sat *= satk[t];
	v *= valk[t];
	float C = v * sat, X = C * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - C, o[3];
	int seg = (int)(h / 60) % 6;
	switch (seg) {
	case 0: o[0] = C; o[1] = X; o[2] = 0; break;
	case 1: o[0] = X; o[1] = C; o[2] = 0; break;
	case 2: o[0] = 0; o[1] = C; o[2] = X; break;
	case 3: o[0] = 0; o[1] = X; o[2] = C; break;
	case 4: o[0] = X; o[1] = 0; o[2] = C; break;
	default: o[0] = C; o[1] = 0; o[2] = X; break;
	}
	c.r = (Uint8)((o[0] + m) * 255 + 0.5f);
	c.g = (Uint8)((o[1] + m) * 255 + 0.5f);
	c.b = (Uint8)((o[2] + m) * 255 + 0.5f);
	return c;
}

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

/* opacity of the text drawn (for fades); 255 normally */
static uint8_t textAlpha = 255;

/* align: 0 left, 1 centre, 2 right; y is the vertical centre */
static int text(TTF_Font *f, int x, int yc, const char *s, SDL_Color c, int align)
{
	if (!f || !s || !*s) return 0;
	s = tr(s);                         /* interface language */
	tcache_t *e = get_text(f, s, c);
	if (!e || !e->tex) return 0;
	SDL_Rect d = { x, yc - e->h / 2, e->w, e->h };
	if (align == 1) d.x = x - e->w / 2;
	if (align == 2) d.x = x - e->w;
	if (textAlpha != 255) SDL_SetTextureAlphaMod(e->tex, textAlpha);
	SDL_RenderCopy(R, e->tex, NULL, &d);
	if (textAlpha != 255) SDL_SetTextureAlphaMod(e->tex, 255);
	return e->w;
}

/* copy s into out, cut with "..." so it is at most maxw pixels wide (UTF-8 safe) */
static void fit_text(TTF_Font *f, const char *s, int maxw, char *out, size_t n)
{
	int w = 0;
	snprintf(out, n, "%s", s);
	if (!f || (TTF_SizeUTF8(f, out, &w, NULL) == 0 && w <= maxw)) return;
	size_t len = strlen(out);
	while (len > 0) {
		do len--; while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
		while (len > 0 && out[len - 1] == ' ') len--;
		if (len + 4 > n) continue;
		memcpy(out + len, "...", 4);
		if (TTF_SizeUTF8(f, out, &w, NULL) == 0 && w <= maxw) return;
	}
	snprintf(out, n, "...");
}

/* Text in a box maxw pixels wide. When it does not fit: cut with "..."
 * normally, and when active (the focused item) it scrolls slowly from side
 * to side (marquee) so all of it can be read. align as in text(). */
static int marquee(TTF_Font *f, int x, int yc, int maxw, const char *s, SDL_Color c, int align, int active)
{
	int w = 0, h = 0;
	if (!f || !s || !*s) return 0;
	s = tr(s);
	if (TTF_SizeUTF8(f, s, &w, &h) != 0 || w <= maxw) return text(f, x, yc, s, c, align);
	if (!active) {
		char cut[160];
		fit_text(f, s, maxw, cut, sizeof(cut));
		return text(f, x, yc, cut, c, align);
	}
	/* each text keeps its own clock, restarted when it becomes active */
	static struct { unsigned key; uint32_t start, seen; } mq[4];
	unsigned key = 2166136261u;
	for (const char *p = s; *p; p++) key = (key ^ (unsigned char)*p) * 16777619u;
	key ^= (unsigned)x * 31u + (unsigned)yc;
	uint32_t now = SDL_GetTicks();
	int slot = -1, old = 0;
	for (int i = 0; i < 4; i++) {
		if (mq[i].key == key && now - mq[i].seen < 200) { slot = i; break; }
		if (mq[i].seen < mq[old].seen) old = i;
	}
	if (slot < 0) { slot = old; mq[slot].key = key; mq[slot].start = now; }
	mq[slot].seen = now;
	/* pause, scroll to the end at 70 px/s, pause, start again */
	int over = w - maxw;
	uint32_t hold = 1200, run = (uint32_t)over * 1000 / 70, cyc = hold + run + hold;
	uint32_t t = (now - mq[slot].start) % cyc;
	int off = t < hold ? 0 : t < hold + run ? (int)((t - hold) * 70 / 1000) : over;
	int x0 = align == 1 ? x - maxw / 2 : align == 2 ? x - maxw : x;
	SDL_Rect prev, clip = { x0, yc - h, maxw, h * 2 };
	int had = SDL_RenderIsClipEnabled(R);
	if (had) {
		SDL_RenderGetClipRect(R, &prev);
		SDL_Rect both;
		if (!SDL_IntersectRect(&prev, &clip, &both)) return maxw;
		clip = both;
	}
	SDL_RenderSetClipRect(R, &clip);
	text(f, x0 - off, yc, s, c, 0);
	SDL_RenderSetClipRect(R, had ? &prev : NULL);
	return maxw;
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

/* Filled rounded rectangle (triangle fan per corner + 3 rects). */
static void rrect(int x, int y, int w, int h, int r, SDL_Color c, uint8_t a)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	/* opaque shapes overlap their parts by a pixel to hide raster seams */
	int o = a == 255 ? 1 : 0;
	fill(x + r - o, y, w - 2 * r + 2 * o, h, c, a);
	fill(x, y + r - o, r, h - 2 * r + 2 * o, c, a);
	fill(x + w - r, y + r - o, r, h - 2 * r + 2 * o, c, a);
	const int N = 8;
	float cxs[4] = { x + r, x + w - r, x + w - r, x + r };
	float cys[4] = { y + r, y + r, y + h - r, y + h - r };
	for (int k = 0; k < 4; k++) {
		float xy[2 * (N + 2)];
		xy[0] = cxs[k]; xy[1] = cys[k];
		for (int i = 0; i <= N; i++) {
			float ang = 3.14159265f * (1.0f + k * 0.5f) + i * (1.5707963f / N);
			xy[2 + i * 2] = cxs[k] + cosf(ang) * (r + o * 0.5f);
			xy[3 + i * 2] = cys[k] + sinf(ang) * (r + o * 0.5f);
		}
		poly(xy, N + 2, c, a);
	}
}

/* Rounded outline: a ring of thickness t drawn as two rrects would blend
 * twice, so draw thin rounded strips instead. */
static void rframe(int x, int y, int w, int h, int r, int t, SDL_Color c, uint8_t a)
{
	fill(x + r, y, w - 2 * r, t, c, a);
	fill(x + r, y + h - t, w - 2 * r, t, c, a);
	fill(x, y + r, t, h - 2 * r, c, a);
	fill(x + w - t, y + r, t, h - 2 * r, c, a);
	const int N = 8;
	float cxs[4] = { x + r, x + w - r, x + w - r, x + r };
	float cys[4] = { y + r, y + r, y + h - r, y + h - r };
	for (int k = 0; k < 4; k++)
		for (int i = 0; i < N; i++) {
			float a0 = 3.14159265f * (1.0f + k * 0.5f) + i * (1.5707963f / N);
			float a1 = a0 + 1.5707963f / N;
			float q[8] = { cxs[k] + cosf(a0) * r, cys[k] + sinf(a0) * r,
			               cxs[k] + cosf(a1) * r, cys[k] + sinf(a1) * r,
			               cxs[k] + cosf(a1) * (r - t), cys[k] + sinf(a1) * (r - t),
			               cxs[k] + cosf(a0) * (r - t), cys[k] + sinf(a0) * (r - t) };
			poly(q, 4, c, a);
		}
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
		bar(x - h * 0.8f, y - h * 0.8f, x + h * 0.8f, y + h * 0.8f, t, PS_CROSS);
		bar(x - h * 0.8f, y + h * 0.8f, x + h * 0.8f, y - h * 0.8f, t, PS_CROSS);
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
/* ---- background: a soft gradient with PlayStation shapes floating by ----
 * All shapes go into one vertex batch, drawn with a single call. */
enum { BG_MAXV = 6000 };
static SDL_Vertex bgV[BG_MAXV];
static int bgIdx[BG_MAXV * 2], bgNV, bgNI;

static void bg_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                    SDL_Color c)
{
	if (bgNV + 4 > BG_MAXV) return;
	SDL_Vertex *v = &bgV[bgNV];
	float p[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
	for (int k = 0; k < 4; k++) {
		v[k].position.x = p[k * 2]; v[k].position.y = p[k * 2 + 1];
		v[k].color = c; v[k].tex_coord.x = v[k].tex_coord.y = 0;
	}
	int *ix = &bgIdx[bgNI];
	ix[0] = bgNV; ix[1] = bgNV + 1; ix[2] = bgNV + 2;
	ix[3] = bgNV; ix[4] = bgNV + 2; ix[5] = bgNV + 3;
	bgNV += 4; bgNI += 6;
}

/* a soft round light: alpha is aMax in the middle and fades smoothly to 0 at
 * the radius (per-vertex colours, so the GPU interpolates: no visible rings) */
static void bg_glow(float cx, float cy, float r, SDL_Color c, float aMax)
{
	enum { RINGS = 12, SEG = 48 };
	if (r < 1 || bgNV + (RINGS + 1) * SEG > BG_MAXV - 8) return;
	int base = bgNV;
	for (int i = 0; i <= RINGS; i++) {
		float t = (float)i / RINGS, sm = 1 - t;
		float a = aMax * sm * sm * (3 - 2 * sm);
		for (int k = 0; k < SEG; k++) {
			float an = k * 6.2831853f / SEG;
			SDL_Vertex *v = &bgV[bgNV++];
			v->position.x = cx + cosf(an) * r * t;
			v->position.y = cy + sinf(an) * r * t;
			v->color = (SDL_Color){ c.r, c.g, c.b, (Uint8)(a > 255 ? 255 : a) };
			v->tex_coord.x = v->tex_coord.y = 0;
		}
	}
	for (int i = 0; i < RINGS; i++)
		for (int k = 0; k < SEG; k++) {
			int k1 = (k + 1) % SEG;
			int a0 = base + i * SEG + k, a1 = base + i * SEG + k1;
			int b0 = base + (i + 1) * SEG + k, b1 = base + (i + 1) * SEG + k1;
			bgIdx[bgNI++] = a0; bgIdx[bgNI++] = b0; bgIdx[bgNI++] = b1;
			bgIdx[bgNI++] = a0; bgIdx[bgNI++] = b1; bgIdx[bgNI++] = a1;
		}
}

/* outline of a regular polygon (n corners), stroke width w, rotated by a */
static void bg_ngon(float cx, float cy, float r, int n, float w, float a, SDL_Color c)
{
	float ri = r - w;
	for (int k = 0; k < n; k++) {
		float a0 = a + k * 6.2831853f / n, a1 = a + (k + 1) * 6.2831853f / n;
		float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		bg_quad(cx + c0 * r, cy + s0 * r, cx + c1 * r, cy + s1 * r,
		        cx + c1 * ri, cy + s1 * ri, cx + c0 * ri, cy + s0 * ri, c);
	}
}

static void bg_bar(float cx, float cy, float len, float w, float a, SDL_Color c)
{
	float dx = cosf(a) * len / 2, dy = sinf(a) * len / 2;
	float nx = -sinf(a) * w / 2, ny = cosf(a) * w / 2;
	bg_quad(cx - dx + nx, cy - dy + ny, cx + dx + nx, cy + dy + ny,
	        cx + dx - nx, cy + dy - ny, cx - dx - nx, cy - dy - ny, c);
}

static void draw_background(void)
{
	uint32_t t = SDL_GetTicks();
	const float tf = t * 0.001f;
	int ingame = emu_game_loaded();
	bgNV = bgNI = 0;
	if (ingame) {
		video_draw_game(1);
	} else {
		/* vertical gradient, slowly breathing between two blues */
		float p = 0.5f + 0.5f * sinf(tf * 0.25f);
		SDL_Color top = th((int)(6 + 6 * p), (int)(10 + 8 * p), (int)(34 + 14 * p));
		SDL_Color bot = th((int)(10 + 8 * (1 - p)), (int)(30 + 14 * (1 - p)), (int)(86 + 20 * (1 - p)));
		SDL_Vertex g[4] = {
			{ { 0, 0 }, top, { 0, 0 } }, { { 1280, 0 }, top, { 0, 0 } },
			{ { 1280, 720 }, bot, { 0, 0 } }, { { 0, 720 }, bot, { 0, 0 } },
		};
		int gi[6] = { 0, 1, 2, 0, 2, 3 };
		SDL_RenderGeometry(R, NULL, g, 4, gi, 6);
	}

	/* floating triangle / circle / cross / square outlines, drifting up and
	 * to the left, each turning slowly and fading in and out */
	static const SDL_Color tint[4] = {
		{ 150, 240, 205, 0 }, { 255, 170, 170, 0 }, { 175, 205, 255, 0 }, { 255, 175, 240, 0 }
	};
	const int N = 26;
	for (int i = 0; i < N; i++) {
		unsigned h = (unsigned)i * 2654435761u;
		float sz = 26 + (h >> 8) % 56;                  /* 26..81 px */
		float speed = 8 + (h >> 16) % 14;               /* px per second */
		float span = 1280 + 720 + 200;
		float d = fmodf((h % 2200) + tf * speed, span);
		/* move along a diagonal, wrapping around */
		float x = fmodf(1380 - d + ((h >> 4) % 1280), 1480) - 100;
		float y = fmodf(820 - d * 0.55f + ((h >> 12) % 720), 920) - 100;
		x += sinf(tf * 0.5f + i) * 12;
		float rot = tf * (0.15f + ((h >> 20) % 10) * 0.03f) * ((h & 1) ? 1 : -1) + i;
		float pulse = 0.55f + 0.45f * sinf(tf * 0.7f + i * 1.7f);
		int kind = i % 4;
		SDL_Color c = tint[kind];
		c.a = (uint8_t)((ingame ? 26 : 60) * pulse + (ingame ? 8 : 16));
		float w = sz * 0.09f + 1.5f;
		switch (kind) {
		case 0: bg_ngon(x, y, sz * 0.62f, 3, w, rot - 1.5707963f, c); break;        /* triangle */
		case 1: bg_ngon(x, y, sz * 0.5f, 28, w, rot, c); break;                     /* circle */
		case 2: bg_bar(x, y, sz * 1.0f, w, rot + 0.785398f, c);                     /* cross */
		        bg_bar(x, y, sz * 1.0f, w, rot - 0.785398f, c); break;
		default: bg_ngon(x, y, sz * 0.6f, 4, w, rot + 0.785398f, c); break;         /* square */
		}
	}
	SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
	if (bgNI) SDL_RenderGeometry(R, NULL, bgV, bgNV, bgIdx, bgNI);
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
	if (pressed & (WIIU_BTN_UP | WIIU_BTN_DOWN | WIIU_BTN_LEFT | WIIU_BTN_RIGHT | WIIU_BTN_L | WIIU_BTN_R))
		sfx_play(SFX_MOVE);
	if (pressed & WIIU_BTN_A) sfx_play(SFX_OK);
	else if (pressed & WIIU_BTN_B) sfx_play(SFX_BACK);
	return pressed;
}

typedef struct {
	int down;          /* finger currently on the screen */
	int sx, sy;        /* where it went down */
	int x, y;          /* current position */
	int dragging;      /* moved far enough to be a scroll, not a tap */
	int tapped;        /* released this frame without dragging */
	int tx, ty;        /* tap position */
	int dx, dy;        /* movement since last frame */
} touch_t;
static touch_t T;

/* set when the menu opens: the touch that opened it (still on the screen)
 * must not become a tap in the menu, so it is ignored until lifted */
static int touchBlock;
static int arrowGrab;        /* a carousel arrow is being held: its own sounds, no "tap" chime */
/* the Wii Remote pointer moved this frame (position in menu pixels) */
static struct { int valid, moved, x, y; } P;
static int ptrOnItem;                /* the pointer is over something that can be chosen (the cursor points) */

static void poll_touch(void)
{
	const wiiu_pad_state *gp = wiiu_input_get(0);
	int on = gp->connected && (gp->held & WIIU_BTN_TOUCH);
	wiiu_input_pointer_menu(1);
	P.moved = 0;
	P.valid = 0;
	for (int i = 1; i < WIIU_MAX_PADS; i++) {
		static float lx[WIIU_MAX_PADS], ly[WIIU_MAX_PADS];
		const wiiu_pad_state *w = wiiu_input_get(i);
		if (!w->connected || !w->ptr_valid) continue;
		P.valid = 1;
		P.x = (int)w->ptr_x;
		P.y = (int)w->ptr_y;
		if (fabsf(w->ptr_x - lx[i]) > 3 || fabsf(w->ptr_y - ly[i]) > 3) {
			lx[i] = w->ptr_x;
			ly[i] = w->ptr_y;
			P.moved = 1;
		}
	}
	if (!on)                                       /* a Wii Remote's pointer clicks like a finger */
		for (int i = 1; i < WIIU_MAX_PADS; i++) {
			const wiiu_pad_state *w = wiiu_input_get(i);
			if (w->connected && w->ptr_valid && (w->held & WIIU_BTN_TOUCH)) { gp = w; on = 1; break; }
		}
	T.tapped = 0;
	T.dx = T.dy = 0;
	if (touchBlock) {
		if (!on) touchBlock = 0;
		T.down = 0;
		return;
	}
	if (on) {
		if (!T.down) {
			T.down = 1;
			T.sx = T.x = gp->touch_x;
			T.sy = T.y = gp->touch_y;
			T.dragging = 0;
		} else {
			T.dx = gp->touch_x - T.x;
			T.dy = gp->touch_y - T.y;
			T.x = gp->touch_x;
			T.y = gp->touch_y;
			if (abs(T.y - T.sy) > 18 || abs(T.x - T.sx) > 18) T.dragging = 1;
		}
	} else if (T.down) {
		T.down = 0;
		if (!T.dragging) { T.tapped = 1; T.tx = T.sx; T.ty = T.sy; if (!arrowGrab) sfx_play(SFX_OK); }
		arrowGrab = 0;
	}
}

static int tap_in(int x, int y, int w, int h)
{
	return T.tapped && T.tx >= x && T.tx < x + w && T.ty >= y && T.ty < y + h;
}

/* the Wii Remote pointer moved this frame and is over the rectangle: the
 * highlight follows it, without a click */
static int hover_in(int x, int y, int w, int h)
{
	int in = P.valid && P.x >= x && P.x < x + w && P.y >= y && P.y < y + h;
	if (in) ptrOnItem = 1;
	return in && P.moved;
}

/* ------------------------------------------------------------------------ */
/* frame chrome                                                             */
/* ------------------------------------------------------------------------ */
static char statusMsg[160];
static uint32_t statusUntil;

static void status(const char *m)
{
	m = tr(m);
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
	fill(0, 0, 1280, 104, hdr, 90);

	if (showBack) {
		SDL_Color panel = th(44, 50, 78);
		rrect(BACK_X, BACK_Y, BACK_W, BACK_H, BACK_H / 2, panel, 255);
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
	else {
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char clk[16];
		int cw = 0;
		if (tm) {
			snprintf(clk, sizeof(clk), "%02d:%02d", tm->tm_hour, tm->tm_min);
			cw = text(fontTitle, 1240, 52, clk, GREY, 2);
		}
		/* Wi-Fi: three arcs and a dot, only while connected */
		if (net_status() == 1) {
			float wx = 1240 - cw - (wiiu_battery() >= 0 ? 130 : 44), wy = 66;
			SDL_Color wc = { 200, 210, 230 };
			for (int k = 0; k < 3; k++) {
				float r = 8 + k * 8;
				const int N = 10;
				for (int i = 0; i < N; i++) {      /* a 90-degree arc, opening upwards */
					float a0 = -2.356f + i * 1.5708f / N, a1 = a0 + 1.5708f / N;
					float q[8] = { wx + cosf(a0) * r, wy + sinf(a0) * r, wx + cosf(a1) * r, wy + sinf(a1) * r,
					               wx + cosf(a1) * (r - 3.5f), wy + sinf(a1) * (r - 3.5f),
					               wx + cosf(a0) * (r - 3.5f), wy + sinf(a0) * (r - 3.5f) };
					poly(q, 4, wc, 255);
				}
			}
			ring(wx, wy - 1, 3, 3, wc);
		}
		/* GamePad battery: 1-6 = level (1 critical), 0 = charging */
		int bat = wiiu_battery();
		if (bat >= 0) {
			int bx = 1240 - cw - 86, by = 38, bw = 54, bh = 28;
			SDL_Color frame = GREY, lvl = bat == 1 ? (SDL_Color){ 255, 80, 80 } : bat == 2 ? PS_YELLOW : PS_GREEN;
			rframe(bx, by, bw, bh, 6, 3, frame, 255);
			rrect(bx + bw, by + 8, 5, bh - 16, 2, frame, 255);
			if (bat == 0) {                       /* charging: a bolt */
				float q[12] = { bx + 30, by + 4, bx + 18, by + 16, bx + 27, by + 16,
				                bx + 22, by + 25, bx + 37, by + 11, bx + 28, by + 11 };
				poly(q, 6, PS_YELLOW, 255);
			} else {
				int bars = bat > 6 ? 5 : bat - 1;     /* 1..6 -> 0..5 bars */
				if (bars < 1) bars = 1;
				for (int i = 0; i < bars; i++)
					rrect(bx + 6 + i * 9, by + 6, 7, bh - 12, 2, lvl, 255);
			}
		}
	}
	if (showBack && P.valid && P.x >= BACK_X && P.x < BACK_X + BACK_W && P.y >= BACK_Y && P.y < BACK_Y + BACK_H) ptrOnItem = 1;
	return showBack && tap_in(BACK_X, BACK_Y, BACK_W, BACK_H);
}

/* centred text broken into lines of at most maxw pixels (at spaces) */
static void text_wrap(TTF_Font *f, int cx, int y, int maxw, const char *str, SDL_Color c)
{
	char buf[512], line[512];
	snprintf(buf, sizeof(buf), "%s", tr(str));
	const char *p = buf;
	int lh = TTF_FontLineSkip(f);
	while (*p) {
		int best = 0, len = (int)strlen(p);
		for (int i = 1; i <= len; i++) {
			if (p[i] != ' ' && p[i] != 0) continue;
			int w = 0;
			snprintf(line, sizeof(line), "%.*s", i, p);
			TTF_SizeUTF8(f, line, &w, NULL);
			if (w > maxw && best) break;
			best = i;
			if (w > maxw) break;
		}
		if (!best) best = len;
		snprintf(line, sizeof(line), "%.*s", best, p);
		text(f, cx, y, line, c, 1);
		y += lh;
		p += best;
		while (*p == ' ') p++;
	}
}

/* a second line of information in the header, between the Back button and
 * the title (cut with "..." to fit), so it never sits under the list */
static void header_sub(const char *title, const char *str, SDL_Color c)
{
	int tw = 0;
	char fitted[256];
	if (!str || !*str) return;
	if (title && fontTitle) TTF_SizeUTF8(fontTitle, tr(title), &tw, NULL);
	int x0 = BACK_X + BACK_W + 28, x1 = 1240 - tw - 40;
	if (x1 - x0 < 120) return;
	const char *t = tr(str);
	int w = 0;
	if (TTF_SizeUTF8(fontSmall, t, &w, NULL) == 0 && w > x1 - x0) {
		/* two lines: break at the last space that fits on the first */
		char a[256], b[256];
		snprintf(a, sizeof(a), "%s", t);
		int cut = -1;
		for (int i = 0; a[i]; i++)
			if (a[i] == ' ') {
				char save = a[i];
				a[i] = 0;
				int wa = 0;
				TTF_SizeUTF8(fontSmall, a, &wa, NULL);
				a[i] = save;
				if (wa <= x1 - x0) cut = i; else break;
			}
		if (cut > 0) {
			a[cut] = 0;
			fit_text(fontSmall, a + cut + 1, x1 - x0, b, sizeof(b));
			text(fontSmall, x0, 38, a, c, 0);
			text(fontSmall, x0, 68, b, c, 0);
			return;
		}
	}
	fit_text(fontSmall, t, x1 - x0, fitted, sizeof(fitted));
	text(fontSmall, x0, 52, fitted, c, 0);
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

/* a button name in a hint: "A", "X", "ZL", "+", "-"... -> its length, else 0.
 * It must start the hint or follow a space, and be followed by ':' or a space
 * (single letters A/B/L/R only when followed by ':' or two spaces, so a word
 * such as the Italian "A" is left alone). */
static int hint_button(const char *h, int i)
{
	static const char *const names[] = { "ZL", "ZR", "A", "B", "X", "Y", "L", "R", "+", "-" };
	if (i > 0 && h[i - 1] != ' ') return 0;
	for (int k = 0; k < 10; k++) {
		int n = (int)strlen(names[k]);
		if (strncmp(h + i, names[k], n)) continue;
		char c = h[i + n];
		if (c == ':') return n;
		if (c != ' ') return 0;
		if (n == 1 && names[k][0] >= 'A' && names[k][0] <= 'Z' && !strchr("XY", names[k][0]) && h[i + n + 1] != ' ') return 0;
		return n;
	}
	return 0;
}

/* the hint with its button names drawn as Wii U glyphs; returns its width
 * (draw = 0 only measures) */
static int draw_hint(int x, const char *hint, int draw)
{
	char seg[256];
	int n = 0, x0 = x;
	for (int i = 0; hint[i];) {
		int b = hint_button(hint, i);
		if (b) {
			char lab[4];
			snprintf(lab, sizeof(lab), "%.*s", b, hint + i);
			if (n) {
				seg[n] = 0; n = 0;
				if (draw) x += text(fontSmall, x, 692, seg, DIM, 0);
				else { int w = 0; TTF_SizeUTF8(fontSmall, seg, &w, NULL); x += w; }
			}
			if (b == 1) { if (draw) wiiu_button(x + 15, 692, lab); x += 34; }
			else {
				if (draw) {
					rrect(x, 676, 44, 32, 14, WHITE, 230);
					text(fontSmall, x + 22, 692, lab, (SDL_Color){ 30, 34, 50, 255 }, 1);
				}
				x += 50;
			}
			i += b;
			if (hint[i] == ':') i++;
			while (hint[i] == ' ') i++;
			continue;
		}
		if (n < (int)sizeof(seg) - 2) seg[n++] = hint[i];
		i++;
	}
	if (n) {
		seg[n] = 0;
		if (draw) x += text(fontSmall, x, 692, seg, DIM, 0);
		else { int w = 0; TTF_SizeUTF8(fontSmall, seg, &w, NULL); x += w; }
	}
	return x - x0;
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
	if (!hint) return;
	hint = tr(hint);
	int room = 1240 - x, w = draw_hint(x, hint, 0);
	if (w <= room) { draw_hint(x, hint, 1); return; }
	/* too long: it scrolls (pause, scroll to the end, pause, again) */
	static char shownHint[256];
	static uint32_t start;
	uint32_t now = SDL_GetTicks();
	if (strncmp(shownHint, hint, sizeof(shownHint) - 1)) {
		snprintf(shownHint, sizeof(shownHint), "%s", hint);
		start = now;
	}
	int over = w - room;
	uint32_t hold = 1500, run = (uint32_t)over * 1000 / 70, cyc = hold + run + hold;
	uint32_t t = (now - start) % cyc;
	int off = t < hold ? 0 : t < hold + run ? (int)((t - hold) * 70 / 1000) : over;
	int had = SDL_RenderIsClipEnabled(R);
	SDL_Rect prev, clip = { x, 664, room, 56 };
	if (had) SDL_RenderGetClipRect(R, &prev);
	SDL_RenderSetClipRect(R, &clip);
	draw_hint(x - off, hint, 1);
	SDL_RenderSetClipRect(R, had ? &prev : NULL);
}

/* Screen transitions: every new screen fades in from black. */
static uint32_t fadeStart;
static void transition(void) { fadeStart = SDL_GetTicks(); }

/* The Wii Remote pointer: a hand in three poses, from content/cursors:
 * pointing (over something to choose), open (idle) and a fist (A held).
 * The fingertip of the pointing hand is the point; the open hand and the
 * fist are centred on it. */
enum { HAND_POINT, HAND_OPEN, HAND_FIST };
static SDL_Texture *handTex[3];
static int handW[3], handH[3], handBuilt;

static void hands_load(void)
{
	static const char *const names[3] = { "hand_point", "hand_open", "hand_fist" };
	handBuilt = 1;
	for (int i = 0; i < 3; i++) {
		char path[160];
		snprintf(path, sizeof(path), "fs:/vol/content/cursors/%s.png", names[i]);
		SDL_Surface *sf = IMG_Load(path);
		if (!sf) { wlog("cursor image missing: %s", path); continue; }
		handTex[i] = SDL_CreateTextureFromSurface(R, sf);
		handW[i] = sf->w;
		handH[i] = sf->h;
		SDL_FreeSurface(sf);
		if (handTex[i]) {
			SDL_SetTextureBlendMode(handTex[i], SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(handTex[i], SDL_ScaleModeLinear);
#endif
		}
	}
}

static void draw_hand(float hx, float hy, int pose)
{
	if (!handBuilt) hands_load();
	if (!handTex[pose]) return;
	SDL_Rect dst = { 0, 0, handW[pose], handH[pose] };
	if (pose == HAND_POINT) { dst.x = (int)hx - 24; dst.y = (int)hy; }              /* the index fingertip */
	else { dst.x = (int)hx - dst.w / 2; dst.y = (int)hy - dst.h * 45 / 100; }       /* centred */
	SDL_RenderCopy(R, handTex[pose], NULL, &dst);
}

static void draw_pointers(void)
{
	static float lx[WIIU_MAX_PADS], ly[WIIU_MAX_PADS];
	static uint32_t moved[WIIU_MAX_PADS];
	uint32_t now = SDL_GetTicks();
	for (int i = 1; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *w = wiiu_input_get(i);
		if (!w->connected || !w->ptr_valid) continue;
		if (fabsf(w->ptr_x - lx[i]) > 3 || fabsf(w->ptr_y - ly[i]) > 3 || (w->held & WIIU_BTN_TOUCH)) {
			moved[i] = now;
			lx[i] = w->ptr_x;
			ly[i] = w->ptr_y;
		}
		if (now - moved[i] > 5000) continue;
		/* pointing by default; a fist only while dragging (the covers, a list) */
		draw_hand(w->ptr_x, w->ptr_y, (w->held & WIIU_BTN_TOUCH) && T.down && T.dragging ? HAND_FIST : HAND_POINT);
	}
	ptrOnItem = 0;                       /* set again by the items drawn next frame */
}

static void present(void)
{
	draw_pointers();
	uint32_t dt = SDL_GetTicks() - fadeStart;
	if (dt < 220) {
		SDL_Color k = { 0, 0, 0 };
		fill(0, 0, 1280, 720, k, (uint8_t)(255 - dt * 255 / 220));
	}
	SDL_RenderPresent(R);
	pump_events();
	SDL_Delay(1);
}

/* case-insensitive substring search (newlib has no strcasestr) */
static int contains_ci(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	if (!n) return 1;
	for (; *hay; hay++)
		if (!strncasecmp(hay, needle, n)) return 1;
	return 0;
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

static void star(float cx, float cy, float r, SDL_Color c)
{
	float xy[20];
	for (int i = 0; i < 10; i++) {
		float a = -1.5707963f + i * 0.6283185f, rr = (i & 1) ? r * 0.45f : r;
		xy[i * 2] = cx + cosf(a) * rr;
		xy[i * 2 + 1] = cy + sinf(a) * rr;
	}
	/* fan from the centre */
	for (int i = 0; i < 10; i++) {
		float tri[6] = { cx, cy, xy[i * 2], xy[i * 2 + 1], xy[((i + 1) % 10) * 2], xy[((i + 1) % 10) * 2 + 1] };
		poly(tri, 3, c, 255);
	}
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
	SDL_Color panel = th(14, 18, 40);
	int y = 128;
	rrect(COVER_X - 12, y - 8, COVER_W + 24, COVER_W + 80, 18, panel, 190);
	if (t && w > 0 && h > 0) {
		int dw = COVER_W, dh = COVER_W * h / w;
		if (dh > COVER_W) { dh = COVER_W; dw = COVER_W * w / h; }
		SDL_Rect d = { COVER_X + (COVER_W - dw) / 2, y + (COVER_W - dh) / 2, dw, dh };
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);   /* 3D boxes have transparency */
		SDL_RenderCopy(R, t, NULL, &d);
		if (!cfg.cover3d) outline(d.x - 2, d.y - 2, d.w + 4, d.h + 4, GREY, 160, 2);
	} else {
		float cx = COVER_X + COVER_W / 2.0f, cy = y + COVER_W / 2.0f;
		if (covers_status(id) == 1) {
			spinner(cx, cy, 46, PS_BLUE);
			text(fontSmall, (int)cx, (int)cy + 90, "Loading cover", DIM, 1);
		} else {
			/* no ID: a plain disc */
			ring(cx, cy, 130, 100, th(60, 66, 92));
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
	int lastSel;       /* the selection of the previous frame (the list follows it only when it changes) */
	float target;      /* where the list is gliding to (kept between frames) */
	int tgtInit;
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
		if (!L->tgtInit) { L->target = L->scroll; L->tgtInit = 1; }
		float target = L->target;
		/* bring the selection into view only when it changed (the buttons, a tap):
		 * after a drag the list stays where the finger left it */
		if (L->sel != L->lastSel) {
			if (L->sel < target) target = (float)L->sel;
			if (L->sel >= target + ROWS) target = (float)(L->sel - ROWS + 1);
		}
		if (target > maxFirst) target = (float)maxFirst;
		if (target < 0) target = 0;
		L->target = target;
		L->scroll += (target - L->scroll) * 0.35f;
		if (fabsf(target - L->scroll) < 0.01f) L->scroll = target;
	}
	else { L->target = (float)(int)(L->scroll + 0.5f); L->tgtInit = 1; }
	L->lastSel = L->sel;
	if (L->scroll < 0) L->scroll = 0;
	if (L->scroll > maxFirst) L->scroll = maxFirst;

	int first = (int)L->scroll;
	float frac = L->scroll - first;

	if (P.valid && P.x >= LIST_X - 20 && P.x < LIST_X + LIST_W + 40 && P.y >= LIST_Y && P.y < LIST_Y + ROWS * ROW_H) {
		int idx = first + (int)((P.y - LIST_Y) / ROW_H + frac);
		float top = LIST_Y + (idx - L->scroll) * ROW_H;
		if (idx >= 0 && idx < count && labels[idx][0] != '#') ptrOnItem = 1;
		if (P.moved && idx >= 0 && idx < count && labels[idx][0] != '#' && top >= LIST_Y - 1 && top + ROW_H <= LIST_Y + ROWS * ROW_H + 1)
			L->sel = idx;                  /* the blue bar goes where the pointer is */
	}

	SDL_Color panel = th(14, 18, 40);
	rrect(LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16, 18, panel, 190);
	SDL_RenderSetClipRect(R, &(SDL_Rect){ LIST_X - 20, LIST_Y - 8, LIST_W + 40, ROWS * ROW_H + 16 });
	{
		/* highlight glides to the selected row */
		float ty = LIST_Y + (L->sel - L->scroll) * ROW_H;
		if (!L->hyInit) { L->hy = ty; L->hyInit = 1; }
		L->hy += (ty - L->hy) * 0.4f;
		SDL_Color hl = th(0, 110, 190);
		rrect(LIST_X - 12, (int)L->hy + 4, LIST_W + 24, ROW_H - 8, 14, hl, 255);
	}

	for (int i = 0; i <= ROWS && first + i < count; i++) {
		int idx = first + i;
		int y = LIST_Y + (int)((i - frac) * ROW_H);
		int selected = idx == L->sel;
		if (!selected) {
			SDL_Color sep = th(60, 70, 110);
			fill(LIST_X, y + ROW_H - 1, LIST_W, 1, sep, 120);
		}
		int tx = LIST_X;
		if (marks) {
			/* small coloured square marker, like the PS1 memory card screen */
			fill(LIST_X, y + ROW_H / 2 - 9, 18, 18, marks[idx], 255);
			tx += 34;
		}
		if (labels[idx][0] == '#') {      /* section title, translated with its '#' */
			text(fontSmall, tx, y + ROW_H / 2 + 6, tr(labels[idx]) + 1, PS_BLUE, 0);
			continue;
		}
		{   /* the label gets the room the value leaves */
			int room = LIST_X + LIST_W - tx - 12, vw = 0;
			if (values && values[idx] && values[idx][0] && TTF_SizeUTF8(fontItem, tr(values[idx]), &vw, NULL) == 0)
				room -= vw + (arrows ? 86 : 24);
			if (room < 120) room = 120;
			marquee(fontItem, tx, y + ROW_H / 2, room, labels[idx], selected ? WHITE : GREY, 0, selected);
		}
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

/* On-screen keyboard (Wii U system keyboard through SDL text input).
 * Returns 1 with the text in out; an empty result clears the filter. */
static int text_prompt(const char *title, const char *initial, char *out, size_t n)
{
	char buf[128];
	int frames = 0;
	snprintf(buf, sizeof(buf), "%s", initial);
	video_text_begin();
	for (;;) {
		int st = video_text_poll(buf, sizeof(buf));
		poll_touch();
		uint32_t b = poll_buttons();
		draw_background();
		draw_header(title, 0);
		SDL_Color card = th(14, 18, 40);
		rrect(240, 280, 800, 120, 22, card, 240);
		text(fontItem, 640, 340, buf[0] ? buf : "Type a name...", buf[0] ? WHITE : DIM, 1);
		draw_footer("Use the keyboard on the GamePad");
		present();
		frames++;
		if (st > 0) { snprintf(out, n, "%s", buf); video_text_end(); return 1; }
		if (st < 0 || (b & WIIU_BTN_B) || wiiu_quit || (frames > 20 && !video_text_active())) {
			video_text_end();
			snprintf(out, n, "%s", buf);
			return buf[0] != 0 || initial[0] != 0;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* file browser                                                             */
/* ------------------------------------------------------------------------ */
typedef struct { char name[256]; int dir; int idTried; char id[16]; int fav; int discs; long secs; long long last;
                 char cover[16]; int coverChecked, hasCustom; const char *base; } entry_t;
static int disc_tag(const char *name, char *base, size_t n);
static char browsePath[512];
static int favOnly;            /* show favourites only */
static void game_options(entry_t *e, const char *dir);
static void memcards(void);

/* play-time key of a list entry: file name without extension; for a
 * multi-disc game, the shared name (the .m3u written for it has it too) */
static void entry_key(const char *name, int discs, char *out, size_t n)
{
	char b[256];
	if (discs > 1 && disc_tag(name, b, sizeof(b))) snprintf(out, n, "%s", b);
	else {
		snprintf(out, n, "%s", name);
		char *dot = strrchr(out, '.');
		if (dot) *dot = 0;
	}
}

/* 1 USA, 2 Europe, 3 Japan, 0 unknown: from the game ID, else the name */
static int entry_region(const char *name, const char *id)
{
	if (id && id[0]) {
		if (!strncmp(id, "SCUS", 4) || !strncmp(id, "SLUS", 4) || !strncmp(id, "SCUD", 4) || !strncmp(id, "SLUD", 4)) return 1;
		if (!strncmp(id, "SCES", 4) || !strncmp(id, "SLES", 4) || !strncmp(id, "SCED", 4) || !strncmp(id, "SLED", 4)) return 2;
		if (!strncmp(id, "SCPS", 4) || !strncmp(id, "SLPS", 4) || !strncmp(id, "SLPM", 4) || !strncmp(id, "SCPM", 4) ||
		    !strncmp(id, "SIPS", 4) || !strncmp(id, "PAPX", 4) || !strncmp(id, "ESPM", 4) || !strncmp(id, "SCZS", 4)) return 3;
	}
	if (contains_ci(name, "(USA") || contains_ci(name, "(US)") || contains_ci(name, "(U)")) return 1;
	if (contains_ci(name, "(Europe") || contains_ci(name, "(E)") || contains_ci(name, "(PAL")) return 2;
	if (contains_ci(name, "(Japan") || contains_ci(name, "(J)")) return 3;
	return 0;
}

static const char *sort_name(int v) { return v == 1 ? "Last played" : v == 2 ? "Most played" : "Name"; }
static const char *regionf_name(int v)
{
	if (v >= 5) return emu_coll_name(v - 5);
	return v == 1 ? "USA" : v == 2 ? "Europe" : v == 3 ? "Japan" : v == 4 ? "Favourites" : "All games";
}

static int cmp_last(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return x->last < y->last ? 1 : x->last > y->last ? -1 : strcasecmp(x->name, y->name);
}

static int cmp_most(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir) return y->dir - x->dir;
	return x->secs < y->secs ? 1 : x->secs > y->secs ? -1 : strcasecmp(x->name, y->name);
}

/* the cover to draw for an entry: a custom one chosen in its options, or
 * the one of its game ID */
static const char *ecover(entry_t *e)
{
	if (!e->coverChecked) {
		char key[256];
		entry_key(e->name, e->discs, key, sizeof(key));
		e->hasCustom = covers_custom_id(key, e->cover);
		e->coverChecked = 1;
	}
	return e->hasCustom ? e->cover : e->id;
}

/* "12 h 30 min" */
static void fmt_playtime(long secs, char *out, size_t n)
{
	if (secs < 60) snprintf(out, n, "%s", tr("less than 1 min"));
	else if (secs < 3600) snprintf(out, n, tr("%ld min"), secs / 60);
	else snprintf(out, n, tr("%ld h %ld min"), secs / 3600, (secs / 60) % 60);
}
static char searchText[64];    /* filter by name */

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

#define MERGED "merged:"     /* the top level of the SD + USB view: the games folders of both drives together */
/* a USB drive (FAT32) is the device "usb:"; its root is written "usb:" and opened as "usb:/" */
static int is_usb_path(const char *p) { return !strncmp(p, "usb:", 4); }
static const char *fs_path(const char *p) { return !strcmp(p, "usb:") ? "usb:/" : p; }
/* the folder as shown to the user: relative to the SD card, or "USB/..." */
static const char *show_path(const char *p, char *buf, size_t n)
{
	if (!strcmp(p, MERGED)) { snprintf(buf, n, "%s", tr("SD + USB")); return buf; }
	if (is_usb_path(p)) { snprintf(buf, n, "USB%s", p + 4); return buf; }
	return p + strlen(WIIU_SD_ROOT);
}

/* where the games are: 0 SD, 1 USB, 2 both; USB only counts while the drive is there */
static int src_effective(void) { return usbfs_mounted() ? cfg.games_src : 0; }
static const char *edir(const entry_t *e, const char *def) { return e->base ? e->base : def; }

static int scan_dir(const char *path, entry_t **out)
{
	DIR *d = opendir(fs_path(path));
	int n = 0, cap = 64;
	entry_t *list = malloc(cap * sizeof(entry_t));
	struct dirent *ent;
	if (strcmp(path, WIIU_SD_ROOT) != 0 && strcmp(path, "usb:") != 0) {
		memset(&list[n], 0, sizeof(entry_t));
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
			/* every field starts empty: a reused block of memory once left
			 * "has a custom cover" set and the entry showed no cover */
			memset(&list[n], 0, sizeof(entry_t));
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
/* game browser views                                                       */
/* ------------------------------------------------------------------------ */
/* cfg.view: the first two keep their old numbers in settings.cfg */
enum { VIEW_GRID = 0, VIEW_LIST, VIEW_COMPACT, VIEW_SHELF, VIEW_CAROUSEL, VIEW_COUNT };
/* VIEW_SHELF was removed; its number is kept so old settings files still load */
static const int viewOrder[] = { VIEW_CAROUSEL, VIEW_GRID, VIEW_COMPACT, VIEW_LIST };
#define NVIEWS (int)(sizeof(viewOrder) / sizeof(viewOrder[0]))
static const char *view_name(int v)
{
	switch (v) {
	case VIEW_LIST:     return "List";
	case VIEW_COMPACT:  return "Compact grid";
	case VIEW_CAROUSEL: return "Carousel";
	default:            return "Grid";
	}
}
static int view_next(int v, int dir)
{
	int i = 0;
	while (i < NVIEWS && viewOrder[i] != v) i++;
	if (i == NVIEWS) i = 0;
	return viewOrder[(i + NVIEWS + dir) % NVIEWS];
}

#define GRID_X     64
#define GRID_Y     124
#define GRID_BOTTOM 664

typedef struct {
	int cols, cellW, cellH, cover, rowsVis, names;
} gstyle_t;

static const gstyle_t STYLE_GRID    = { 5, 230, 268, 190, 2, 1 };
static const gstyle_t STYLE_COMPACT = { 8, 144, 172, 128, 3, 0 };

static const gstyle_t *grid_style(void)
{
	return cfg.view == VIEW_COMPACT ? &STYLE_COMPACT : &STYLE_GRID;
}

typedef struct {
	int sel;
	float scroll;      /* first visible row (grid) */
	float hx, hy;      /* animated highlight */
	int hInit;
	float pos;         /* carousel position, eases towards sel */
	int lastSel;       /* the selection of the previous frame (the grid follows it only when it changes) */
} grid_t;

/* draws the cover into the sz x sz square; returns the drawn rectangle */
static SDL_Rect cover_or_disc_r(const char *id, int x, int y, int sz, int bottom)
{
	int w = 0, h = 0;
	SDL_Texture *t = id && id[0] ? (SDL_Texture *)covers_texture(R, id, &w, &h) : NULL;
	SDL_Rect d = { x, y, sz, sz };
	if (t && w > 0 && h > 0) {
		int dw = sz, dh = sz * h / w;
		if (dh > sz) { dh = sz; dw = sz * w / h; }
		d.x = x + (sz - dw) / 2;
		d.y = bottom ? y + sz - dh : y + (sz - dh) / 2;   /* carousel: stand on the floor */
		d.w = dw; d.h = dh;
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);   /* 3D boxes have transparency */
		SDL_RenderCopy(R, t, NULL, &d);
	} else {
		SDL_Color slot = th(26, 32, 60);
		fill(x, y, sz, sz, slot, 220);
		if (covers_status(id) == 1) spinner(x + sz / 2.0f, y + sz / 2.0f, sz * 0.14f, PS_BLUE);
		else {
			ring(x + sz / 2.0f, y + sz / 2.0f, sz * 0.36f, sz * 0.26f, th(60, 66, 92));
			ring(x + sz / 2.0f, y + sz / 2.0f, sz * 0.09f, 3, GREY);
		}
	}
	return d;
}

static void cover_or_disc(const char *id, int x, int y, int sz)
{
	cover_or_disc_r(id, x, y, sz, 0);
}

/* mirrored, faded copy of the cover under it (carousel) */
static void cover_reflection(const char *id, SDL_Rect d, int depth, uint8_t alpha)
{
	int w = 0, h = 0;
	SDL_Texture *t = id && id[0] ? (SDL_Texture *)covers_texture(R, id, &w, &h) : NULL;
	if (!t || h <= 0 || depth <= 0) return;
	/* one quad, texture flipped, alpha fading from top to bottom */
	float v0 = 1.0f, v1 = 1.0f - (float)depth / d.h;
	if (v1 < 0) v1 = 0;
	float y0 = d.y + d.h + 2, y1 = y0 + depth;
	SDL_Color top = { 255, 255, 255, alpha }, bot = { 255, 255, 255, 0 };
	SDL_Vertex v[4] = {
		{ { d.x, y0 },       top, { 0, v0 } },
		{ { d.x + d.w, y0 }, top, { 1, v0 } },
		{ { d.x + d.w, y1 }, bot, { 1, v1 } },
		{ { d.x, y1 },       bot, { 0, v1 } },
	};
	int idx[6] = { 0, 1, 2, 0, 2, 3 };
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(R, t, v, 4, idx, 6);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);   /* 3D boxes have transparency */
}

static void entry_name(entry_t *e, const char *label, char *out, size_t n)
{
	if (e->dir) { snprintf(out, n, "%s", label); return; }
	char b[256];
	if (e->discs > 1 && disc_tag(e->name, b, sizeof(b))) pretty_name(b, out, n);
	else pretty_name(e->name, out, n);
}

static void detect_entry(entry_t *e, const char *dir)
{
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", dir, e->name);
	e->idTried = 1;
	if (!covers_detect_id(full, e->id)) e->id[0] = 0;
}

static void folder_icon(int x, int y, int sz)
{
	SDL_Color folder = th(40, 48, 84);
	fill(x, y, sz, sz, folder, 220);
	fill(x + sz * 21 / 100, y + sz * 28 / 100, sz * 58 / 100, sz * 42 / 100, PS_YELLOW, 200);
	fill(x + sz * 21 / 100, y + sz * 22 / 100, sz * 32 / 100, sz * 8 / 100, PS_YELLOW, 200);
}

static void disc_badge(entry_t *e, int x, int y, int sz)
{
	if (e->fav) star(x + sz - sz * 12 / 100, y + sz * 17 / 100, sz * 9.5f / 100, PS_YELLOW);
	if (e->discs > 1) {
		char dn[16];
		snprintf(dn, sizeof(dn), tr(sz < 150 ? "%d CD" : "%d discs"), e->discs);
		int bw = sz < 150 ? 62 : 92;
		SDL_Color badge = th(0, 110, 190);
		rrect(x + 6, y + sz - 34, bw, 28, 14, badge, 255);
		text(fontSmall, x + 6 + bw / 2, y + sz - 20, dn, WHITE, 1);
	}
}

/* Grid and compact grid. Returns the tapped entry or -1.
 * Detects the game ID of at most one visible entry per frame. */
static int grid_run(grid_t *G, entry_t *list, const char **labels, int count, const char *dir)
{
	const gstyle_t *S = grid_style();
	int rows = (count + S->cols - 1) / S->cols;
	int maxFirst = rows > S->rowsVis ? rows - S->rowsVis : 0;
	int tapped = -1, detected = 0;
	int gx = (1280 - S->cols * S->cellW) / 2 + 6;

	if (T.down && T.dragging && T.sy >= GRID_Y)
		G->scroll -= (float)T.dy / S->cellH;
	if (!T.down) {
		int selRow = G->sel / S->cols;
		float target = (float)(int)(G->scroll + 0.5f);
		if (G->sel != G->lastSel) {                 /* only when the selection changed, not after a drag */
			if (selRow < target) target = selRow;
			if (selRow >= target + S->rowsVis) target = selRow - S->rowsVis + 1;
		}
		G->scroll += (target - G->scroll) * 0.3f;
		if (fabsf(target - G->scroll) < 0.01f) G->scroll = target;
	}
	G->lastSel = G->sel;
	if (G->scroll < 0) G->scroll = 0;
	if (G->scroll > maxFirst) G->scroll = maxFirst;

	SDL_RenderSetClipRect(R, &(SDL_Rect){ 0, GRID_Y - 6, 1280, GRID_BOTTOM - GRID_Y + 6 });
	{
		float tx = gx + (G->sel % S->cols) * S->cellW;
		float ty = GRID_Y + (G->sel / S->cols - G->scroll) * S->cellH;
		if (!G->hInit) { G->hx = tx; G->hy = ty; G->hInit = 1; }
		G->hx += (tx - G->hx) * 0.4f;
		G->hy += (ty - G->hy) * 0.4f;
		SDL_Color hl = th(0, 110, 190);
		if (S->names)
			rrect((int)G->hx, (int)G->hy, S->cellW - 12, S->cellH - 10, 18, hl, 255);
		else {   /* a frame hugging the cover */
			int cx = (int)G->hx + (S->cellW - 12 - S->cover) / 2;
			rrect(cx - 7, (int)G->hy + 3, S->cover + 14, S->cover + 14, 12, hl, 255);
		}
	}
	int firstRow = (int)G->scroll;
	for (int r = firstRow; r <= firstRow + S->rowsVis && r < rows; r++) {
		int y = GRID_Y + (int)((r - G->scroll) * S->cellH);
		for (int c = 0; c < S->cols; c++) {
			int i = r * S->cols + c;
			if (i >= count) break;
			entry_t *e = &list[i];
			int x = gx + c * S->cellW;
			int cx = x + (S->cellW - 12 - S->cover) / 2;
			if (!e->dir && !e->idTried && !detected && !T.down) { detect_entry(e, edir(e, dir)); detected = 1; }
			if (e->dir) folder_icon(cx, y + 10, S->cover);
			else {
				cover_or_disc_r(ecover(e), cx, y + 10, S->cover, 0);
				disc_badge(e, cx, y + 10, S->cover);
			}
			if (S->names) {
				char full[272], nm[96];
				entry_name(e, labels[i], full, sizeof(full));
				(void)nm;
				marquee(fontSmall, x + (S->cellW - 12) / 2, y + S->cover + 36, S->cellW - 24, full,
				        i == G->sel ? WHITE : GREY, 1, i == G->sel);
			}
			if (T.tapped && T.tx >= x && T.tx < x + S->cellW - 12 && T.ty >= y && T.ty < y + S->cellH - 10 &&
			    T.ty >= GRID_Y)
				tapped = i;
			else if (y >= GRID_Y && y + S->cellH - 10 <= GRID_BOTTOM && hover_in(x, y, S->cellW - 12, S->cellH - 10))
				G->sel = i;                /* the selection follows the pointer */
		}
	}
	SDL_RenderSetClipRect(R, NULL);
	if (rows > S->rowsVis) {
		int barH = GRID_BOTTOM - GRID_Y - 20;
		int h = barH * S->rowsVis / rows;
		int y = GRID_Y + (int)((barH - h) * G->scroll / maxFirst);
		fill(1260, y, 6, h, PS_BLUE, 200);
	}
	/* views without names: the selected game's name in the top right */
	if (!S->names && G->sel < count) {
		char full[272], nm[64];
		entry_name(&list[G->sel], labels[G->sel], full, sizeof(full));
		(void)nm;
		marquee(fontSmall, 1240, 116, 640, full, WHITE, 2, 1);
	}
	if (tapped >= 0) G->sel = tapped;
	return tapped;
}

/* Carousel: one big cover in the middle, the neighbours smaller and dimmer
 * at the sides. Swipe or press left/right. Tap the middle cover to start,
 * a side cover to move to it. Returns the activated entry or -1. */
static int wii_remote_connected(void)
{
	for (int i = 1; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *w = wiiu_input_get(i);
		if (w->connected && w->kind >= PADKIND_REMOTE) return 1;
	}
	return 0;
}

static int in_rect(SDL_Rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int carousel_run(grid_t *G, entry_t *list, const char **labels, int count, const char *dir)
{
	const int CX = 640, CY = 318, BIG = 340;
	int tapped = -1;
	/* arrows on both sides while a Wii Remote is there (like USB Loader GX):
	 * press to go one game, hold to go on, faster after a while */
	const SDL_Rect aL = { 6, 230, 76, 176 }, aR = { 1198, 230, 76, 176 };
	const int wiiOn = cfg.pointer && wii_remote_connected();
	int arrowDir = 0;
	if (wiiOn && T.down) arrowDir = in_rect(aL, T.sx, T.sy) ? -1 : in_rect(aR, T.sx, T.sy) ? 1 : 0;
	if (!G->hInit) { G->pos = G->sel; G->hInit = 1; }
	static int heldDir;
	static uint32_t pressAt, lastStep;
	if (arrowDir) {
		uint32_t now = SDL_GetTicks();
		int step = 0;
		arrowGrab = 1;
		if (heldDir != arrowDir) { heldDir = arrowDir; pressAt = lastStep = now; step = 1; }
		else {
			uint32_t held = now - pressAt;
			uint32_t every = held < 1500 ? 150 : 70;
			if (held >= 450 && now - lastStep >= every) { step = 1; lastStep = now; }
		}
		if (step) {
			int ns = G->sel + arrowDir;
			if (ns >= 0 && ns < count) { G->sel = ns; sfx_play(SFX_MOVE); }
		}
	} else {
		heldDir = 0;
	}
	if (wiiOn && T.tapped && (in_rect(aL, T.tx, T.ty) || in_rect(aR, T.tx, T.ty))) T.tapped = 0;
	if (T.down && T.dragging && !arrowDir && T.sy >= GRID_Y && T.sy < 560) {
		G->pos -= T.dx / 230.0f;
		if (G->pos < 0) G->pos = 0;
		if (G->pos > count - 1) G->pos = count - 1;
		G->sel = (int)(G->pos + 0.5f);
	} else {
		G->pos += (G->sel - G->pos) * 0.22f;
		if (fabsf(G->sel - G->pos) < 0.002f) G->pos = G->sel;
	}
	/* detect IDs around the middle, nearest first */
	for (int k = 0; k <= 4; k++) {
		int done = 0;
		for (int sgn = 1; sgn >= -1; sgn -= 2) {
			int i = G->sel + k * sgn;
			if (i >= 0 && i < count && !list[i].dir && !list[i].idTried && !T.down) {
				detect_entry(&list[i], edir(&list[i], dir));
				done = 1;
				break;
			}
		}
		if (done) break;
	}

	/* draw far covers first so the middle one is on top */
	int lo = (int)G->pos - 5, hi = (int)G->pos + 6;
	for (int ring_ = 6; ring_ >= 0; ring_--) {
		for (int i = lo; i <= hi; i++) {
			if (i < 0 || i >= count) continue;
			float d = i - G->pos, ad = fabsf(d);
			if ((int)(ad + 0.5f) != ring_) continue;
			float sc = 1.0f - 0.24f * (ad > 2.5f ? 2.5f : ad);
			int sz = (int)(BIG * sc);
			float off = d * 150.0f + (d > 1 ? 1 : d < -1 ? -1 : d) * 175.0f;
			int x = (int)(CX + off) - sz / 2, y = CY - sz / 2 + (int)(ad * 10);
			if (x > 1280 || x + sz < 0) continue;
			entry_t *e = &list[i];
			SDL_Rect d2;
			if (e->dir) { folder_icon(x, y, sz); d2 = (SDL_Rect){ x, y, sz, sz }; }
			else {
				d2 = cover_or_disc_r(ecover(e), x, y, sz, 1);
				cover_reflection(ecover(e), d2, sz / 4, (uint8_t)(ad < 1 ? 110 - ad * 60 : 50));
				disc_badge(e, x, y, sz);
			}
			if (ad > 0.05f) {   /* dim the side covers */
				int a = (int)(ad * 110); if (a > 170) a = 170;
				fill(d2.x, d2.y, d2.w, d2.h, th(4, 6, 16), (uint8_t)a);
			} else {
				if (!cfg.cover3d) outline(d2.x - 3, d2.y - 3, d2.w + 6, d2.h + 6, th(0, 140, 220), 255, 3);
			}
			if (P.valid && P.y >= GRID_Y && P.y < 560 && in_rect(d2, P.x, P.y)) ptrOnItem = 1;
			if (T.tapped && T.ty >= GRID_Y && T.ty < 560 &&
			    T.tx >= d2.x && T.tx < d2.x + d2.w && T.ty >= d2.y && T.ty < d2.y + d2.h) {
				if (i == G->sel) tapped = i;
				else G->sel = i;
			}
		}
	}

	/* name and details of the middle game */
	if (G->sel < count) {
		char full[272], nm[64], info[128];
		entry_t *e = &list[G->sel];
		entry_name(e, labels[G->sel], full, sizeof(full));
		fit_text(fontTitle, full, 1160, nm, sizeof(nm));
		text(fontTitle, CX, 590, nm, WHITE, 1);
		if (e->dir) snprintf(info, sizeof(info), "%s", tr("Folder"));
		else if (e->discs > 1) snprintf(info, sizeof(info), tr("%s%s%d discs"), e->id, e->id[0] ? "   " : "", e->discs);
		else snprintf(info, sizeof(info), "%s", e->id);
		if (!e->dir && e->secs > 0) {
			char pt[48], tmp[128];
			fmt_playtime(e->secs, pt, sizeof(pt));
			snprintf(tmp, sizeof(tmp), tr("%s%sPlayed %s"), info, info[0] ? "   \xc2\xb7   " : "", pt);
			snprintf(info, sizeof(info), "%s", tmp);
		}
		{   /* position in the list, top right (next to the folder path) */
			char pos[24];
			snprintf(pos, sizeof(pos), "%d / %d", G->sel + 1, count);
			text(fontSmall, 1240, 116, pos, GREY, 2);
		}
		text(fontSmall, CX, 634, info, GREY, 1);
	}
	if (wiiOn) {
		for (int side = 0; side < 2; side++) {
			SDL_Rect r = side ? aR : aL;
			int hot = (arrowDir == (side ? 1 : -1)) || (P.moved && in_rect(r, P.x, P.y));
			if (P.valid && in_rect(r, P.x, P.y)) ptrOnItem = 1;
			int possible = side ? G->sel < count - 1 : G->sel > 0;
			rrect(r.x, r.y, r.w, r.h, 20, th(14, 18, 40), hot ? 200 : 110);
			if (hot) outline(r.x, r.y, r.w, r.h, th(0, 140, 220), 255, 3);
			float cy = r.y + r.h / 2.0f, x0 = r.x + (side ? 20 : 56), x1 = r.x + (side ? 56 : 20);
			float tri[6] = { x1, cy, x0, cy - 36, x0, cy + 36 };
			poly(tri, 3, WHITE, possible ? (hot ? 255 : 190) : 70);
		}
	}
	return tapped;
}


/* ---- one entry per game: hide tracks referenced by .cue/.m3u, group discs ---- */
static int ext_is(const char *n, const char *e)
{
	const char *d = strrchr(n, '.');
	return d && !strcasecmp(d, e);
}

/* "Final Fantasy VII (USA) (Disc 2).chd" -> "Final Fantasy VII (USA)" + disc 2 */
static int disc_tag(const char *name, char *base, size_t n)
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s", name);
	char *dot = strrchr(tmp, '.');
	if (dot) *dot = 0;
	for (char *p = tmp; *p; p++) {
		int skip = 0;
		if (!strncasecmp(p, "(Disc ", 6)) skip = 6;
		else if (!strncasecmp(p, "(Disk ", 6)) skip = 6;
		else if (!strncasecmp(p, "(CD", 3)) skip = 3 + (p[3] == ' ');
		else if (!strncasecmp(p, " Disc ", 6)) skip = 6;
		else if (!strncasecmp(p, " CD", 3) && p[3] >= '1' && p[3] <= '9') skip = 3;
		if (skip && p[skip] >= '1' && p[skip] <= '9') {
			int d = atoi(p + skip);
			char *e = p + skip;
			while (*e >= '0' && *e <= '9') e++;
			if (*e == ' ' && e[1] == 'o' && e[2] == 'f') { e += 3; while (*e == ' ' || (*e >= '0' && *e <= '9')) e++; }
			if (*e == ')') e++;
			char *q = p;
			while (q > tmp && q[-1] == ' ') q--;
			snprintf(base, n, "%.*s%s", (int)(q - tmp), tmp, e);
			return d;
		}
	}
	return 0;
}

static void collect_refs(const char *dir, const char *file, char (**refs)[256], int *nrefs)
{
	char path[768], line[512];
	snprintf(path, sizeof(path), "%s/%s", dir, file);
	FILE *f = fopen(path, "r");
	if (!f) return;
	int cue = ext_is(file, ".cue");
	while (fgets(line, sizeof(line), f)) {
		char name[256] = "";
		line[strcspn(line, "\r\n")] = 0;
		if (cue) {
			char *q = strchr(line, '"');
			if (strncasecmp(line, "FILE", 4) || !q) continue;
			char *e = strchr(q + 1, '"');
			if (!e) continue;
			snprintf(name, sizeof(name), "%.*s", (int)(e - q - 1), q + 1);
		} else {
			if (!line[0] || line[0] == '#') continue;
			const char *b = strrchr(line, '/');
			snprintf(name, sizeof(name), "%s", b ? b + 1 : line);
		}
		*refs = realloc(*refs, (*nrefs + 1) * sizeof(**refs));
		snprintf((*refs)[(*nrefs)++], 256, "%s", name);
	}
	fclose(f);
}

static void collapse_entries(const char *dir, entry_t *list, int *count)
{
	char (*refs)[256] = NULL;
	int nrefs = 0, n = *count, m = 0;
	for (int i = 0; i < n; i++)
		if (!list[i].dir && (ext_is(list[i].name, ".cue") || ext_is(list[i].name, ".m3u")))
			collect_refs(dir, list[i].name, &refs, &nrefs);
	for (int i = 0; i < n; i++) {
		entry_t *e = &list[i];
		int hide = 0;
		e->discs = 0;
		if (!e->dir) {
			for (int r = 0; r < nrefs && !hide; r++)
				if (!strcasecmp(refs[r], e->name)) hide = 1;
			/* track/image with a .cue of the same name next to it */
			if (!hide && (ext_is(e->name, ".bin") || ext_is(e->name, ".img") || ext_is(e->name, ".iso"))) {
				char cue[256];
				snprintf(cue, sizeof(cue), "%s", e->name);
				char *d = strrchr(cue, '.');
				if (d) strcpy(d, ".cue");
				for (int j = 0; j < n && !hide; j++) if (!strcasecmp(list[j].name, cue)) hide = 1;
			}
			if (ext_is(e->name, ".m3u")) {
				int k = 0;
				char (*r2)[256] = NULL;
				collect_refs(dir, e->name, &r2, &k);
				free(r2);
				e->discs = k;
			}
		}
		if (!hide) list[m++] = *e;
	}
	n = m;
	/* group "(Disc N)" files that no .m3u covers yet */
	for (int i = 0; i < n; i++) {
		char bi[256];
		if (list[i].dir || list[i].discs < 0 || !disc_tag(list[i].name, bi, sizeof(bi))) continue;
		int members = 1;
		for (int j = i + 1; j < n; j++) {
			char bj[256];
			if (!list[j].dir && disc_tag(list[j].name, bj, sizeof(bj)) && !strcmp(bi, bj)) {
				list[j].discs = -1;      /* merged into entry i */
				members++;
			}
		}
		if (members > 1) {
			list[i].discs = members;
			/* show the disc-1 file as the representative */
			int d1 = disc_tag(list[i].name, bi, sizeof(bi));
			(void)d1;
		}
	}
	m = 0;
	for (int i = 0; i < n; i++) if (list[i].discs >= 0) list[m++] = list[i];
	*count = m;
	free(refs);
}

/* Lists the discs of a group (by base name) or of an .m3u, sorted by disc. */
static int disc_files(const char *dir, const char *entry, char (*out)[256], int max)
{
	int n = 0;
	if (ext_is(entry, ".m3u")) {
		char (*r)[256] = NULL;
		int k = 0;
		collect_refs(dir, entry, &r, &k);
		/* keep the .m3u's own relative lines */
		char path[768], line[512];
		snprintf(path, sizeof(path), "%s/%s", dir, entry);
		FILE *f = fopen(path, "r");
		if (f) {
			while (n < max && fgets(line, sizeof(line), f)) {
				line[strcspn(line, "\r\n")] = 0;
				if (line[0] && line[0] != '#') snprintf(out[n++], 256, "%s", line);
			}
			fclose(f);
		}
		free(r);
		return n;
	}
	char base[256], b2[256];
	if (!disc_tag(entry, base, sizeof(base))) return 0;
	DIR *d = opendir(dir);
	struct dirent *ent;
	int discNo[16];
	if (!d) return 0;
	while ((ent = readdir(d)) && n < max) {
		int k = disc_tag(ent->d_name, b2, sizeof(b2));
		if (k && !strcmp(base, b2) && is_game_file(ent->d_name) && !ext_is(ent->d_name, ".bin")) {
			snprintf(out[n], 256, "%s", ent->d_name);
			discNo[n++] = k;
		}
	}
	closedir(d);
	/* sort by disc number */
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			if (discNo[j] < discNo[i]) {
				char t[256]; int tk = discNo[i];
				memcpy(t, out[i], 256); memcpy(out[i], out[j], 256); memcpy(out[j], t, 256);
				discNo[i] = discNo[j]; discNo[j] = tk;
			}
	return n;
}

static int choose_disc(const char *title, int ndisc, int current);
static char pendingDisc[768];
static int pendingIndex;

/* For a multi-disc entry: ask which disc, make sure an .m3u exists (created
 * next to the discs, so in-game disc swapping works), return it in out. */
static int prepare_multidisc(const char *dir, const char *entry, char *out, size_t outsz)
{
	char discs[16][256];
	int n = disc_files(dir, entry, discs, 16);
	if (n < 2) return 0;
	char nm[160];
	pretty_name(entry, nm, sizeof(nm));
	int pick = choose_disc(nm, n, 0);
	if (pick < 0) return -1;
	char m3u[768];
	if (ext_is(entry, ".m3u")) {
		snprintf(m3u, sizeof(m3u), "%s/%s", dir, entry);
	} else {
		char base[256];
		disc_tag(entry, base, sizeof(base));
		snprintf(m3u, sizeof(m3u), "%s/%s.m3u", dir, base);
		struct stat st;
		if (stat(m3u, &st) != 0) {
			FILE *f = fopen(m3u, "w");
			if (!f) return 0;
			for (int i = 0; i < n; i++) fprintf(f, "%s\n", discs[i]);
			fclose(f);
		}
	}
	snprintf(out, outsz, "%s", m3u);
	pendingIndex = pick;
	snprintf(pendingDisc, sizeof(pendingDisc), "%s/%s", dir, discs[pick]);
	return 1;
}

static int at_or_above(const char *path, const char *root)
{
	/* 1 when path is root itself (or shorter, i.e. outside it) */
	if (is_usb_path(path) != is_usb_path(root)) return 0;      /* the other drive is not "above" */
	return strlen(path) <= strlen(root);
}

static void go_up(void)
{
	if (is_usb_path(browsePath)) {
		char *sl = strrchr(browsePath, '/');
		if (sl) *sl = 0;
		if (strlen(browsePath) < 4) strcpy(browsePath, "usb:");
		return;
	}
	char *slash = strrchr(browsePath, '/');
	if (slash && (size_t)(slash - browsePath) >= strlen(WIIU_SD_ROOT)) *slash = 0;
	if (strlen(browsePath) < strlen(WIIU_SD_ROOT)) strcpy(browsePath, WIIU_SD_ROOT);
}

/* in the SD + USB view, the games folder of either drive goes up to the top level */
static void nav_up(int merged)
{
	if (merged && (!strcmp(browsePath, cfg.games_dir) || !strcmp(browsePath, cfg.games_dir_usb))) { strcpy(browsePath, MERGED); return; }
	go_up();
}

static int at_top(const char *root)
{
	if (!strcmp(root, MERGED)) return !strcmp(browsePath, MERGED);
	return at_or_above(browsePath, root);
}

static int path_in_games(const char *p)
{
	return !strncmp(p, cfg.games_dir, strlen(cfg.games_dir)) || !strncmp(p, cfg.games_dir_usb, strlen(cfg.games_dir_usb));
}

/* the entries of both games folders, as one list (each remembers its folder) */
static int scan_merged(entry_t **out)
{
	entry_t *all = NULL;
	int total = 0;
	const char *bases[2] = { cfg.games_dir, cfg.games_dir_usb };
	for (int b = 0; b < 2; b++) {
		entry_t *l = NULL;
		int n = scan_dir(bases[b], &l);
		if (n && !strcmp(l[0].name, "..")) { memmove(l, l + 1, (n - 1) * sizeof(entry_t)); n--; }
		collapse_entries(bases[b], l, &n);
		for (int i = 0; i < n; i++) l[i].base = bases[b];
		all = realloc(all, (total + n + 1) * sizeof(entry_t));
		if (n) memcpy(all + total, l, n * sizeof(entry_t));
		total += n;
		free(l);
	}
	if (total > 1) qsort(all, total, sizeof(entry_t), cmp_entry);
	*out = all;
	return total;
}

/* Game browser (pickDir = 0), folder picker for the SD card (1) or the USB drive (2).
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
	const int src = src_effective();
	const int merged = !pickDir && src == 2;
	const char *root = pickDir == 2 ? "usb:" : pickDir ? WIIU_SD_ROOT :
	                   merged ? MERGED : src == 1 ? cfg.games_dir_usb : cfg.games_dir;
	int ret = 0;

	if (pickDir == 2) strcpy(browsePath, "usb:");              /* the folder pickers open at the root of the drive */
	else if (pickDir) strcpy(browsePath, WIIU_SD_ROOT);
	else if (merged) { if (!path_in_games(browsePath)) strcpy(browsePath, MERGED); }
	else if (!browsePath[0] || strncmp(browsePath, root, strlen(root))) strcpy(browsePath, root);
	if (strcmp(browsePath, MERGED)) {
		struct stat st;
		if (stat(fs_path(browsePath), &st) != 0)
			strcpy(browsePath, merged ? MERGED : (pickDir == 2 || (!pickDir && src == 1)) ? "usb:" : WIIU_SD_ROOT);
	}

	int keepSel = 0;
	if (0) {
rescan_keep:
		keepSel = 1;
		free(list);
		list = NULL;
	}
	if (0) {
rescan:
		keepSel = 0;
	}
	free(list);
	if (!strcmp(browsePath, MERGED)) { count = scan_merged(&list); }
	else count = scan_dir(browsePath, &list);
	if (!pickDir && count && !strcmp(list[0].name, "..") && at_top(root)) {
		memmove(list, list + 1, (count - 1) * sizeof(entry_t));
		count--;
	}
	if (!pickDir && strcmp(browsePath, MERGED)) collapse_entries(browsePath, list, &count);
	if (!pickDir) {
		int m = 0;
		for (int i = 0; i < count; i++) {
			char full[768], nm[272];
			snprintf(full, sizeof(full), "%s/%s", edir(&list[i], browsePath), list[i].name);
			list[i].fav = !list[i].dir && emu_fav_is(full);
			if (!list[i].dir && (favOnly || cfg.regionf == 4) && !list[i].fav) continue;
			if (!list[i].dir && cfg.regionf >= 5 && !emu_coll_has(emu_coll_name(cfg.regionf - 5), full)) continue;
			if (!list[i].dir && searchText[0]) {
				pretty_name(list[i].name, nm, sizeof(nm));
				if (!contains_ci(nm, searchText)) continue;
			}
			if (!list[i].dir && cfg.regionf >= 1 && cfg.regionf <= 3) {
				/* the ID is read once and remembered (covers/ids.txt) */
				if (!list[i].idTried) {
					list[i].idTried = 1;
					if (!covers_detect_id(full, list[i].id)) list[i].id[0] = 0;
				}
				if (entry_region(list[i].name, list[i].id) != cfg.regionf) continue;
			}
			{
				char key[256];
				entry_key(list[i].name, list[i].discs, key, sizeof(key));
				list[i].secs = list[i].dir ? 0 : emu_playtime(key, &list[i].last);
			}
			list[m++] = list[i];
		}
		count = m;
		if (cfg.sort) {
			int start = (count && !strcmp(list[0].name, "..")) ? 1 : 0;
			qsort(list + start, count - start, sizeof(entry_t), cfg.sort == 1 ? cmp_last : cmp_most);
		} else {
			/* favourites first */
			int f = 0;
			for (int i = 0; i < count; i++)
				if (list[i].fav) { entry_t t = list[i]; memmove(list + f + 1, list + f, (i - f) * sizeof(entry_t)); list[f++] = t; }
		}
	}
	int pickGames = 0;                 /* games in the folder being looked at */
	if (pickDir) {
		/* folders first, then the games of this folder (shown so you can
		 * see it is the right one; they can't be chosen), and "use this
		 * folder" on top */
		collapse_entries(browsePath, list, &count);
		int m = 0;
		for (int i = 0; i < count; i++) if (list[i].dir) list[m++] = list[i];
		for (int i = 0; i < count; i++) if (!list[i].dir) { list[m] = list[i]; list[m].fav = 0; m++; pickGames++; }
		count = m;
		list = realloc(list, (count + 1) * sizeof(entry_t));
		memmove(list + 1, list, count * sizeof(entry_t));
		memset(&list[0], 0, sizeof(entry_t));
		strcpy(list[0].name, "\x01");    /* marker: use this folder */
		list[0].dir = 2;
		list[0].idTried = 1;
		list[0].id[0] = 0;
		count++;
	}
	labels = realloc(labels, (count + 1) * sizeof(char *));
	names = realloc(names, (count + 1) * sizeof(*names));
	for (int i = 0; i < count; i++) {
		if (list[i].dir == 2) snprintf(names[i], sizeof(names[i]), "%s", tr("[ Use this folder ]"));
		else if (!strcmp(list[i].name, "..")) snprintf(names[i], sizeof(names[i]), "%s", tr(".. (up)"));
		else if (list[i].dir) snprintf(names[i], sizeof(names[i]), "%s/", list[i].name);
		else if (list[i].discs > 1) {
			char b[256], nm[256];
			if (!disc_tag(list[i].name, b, sizeof(b))) snprintf(b, sizeof(b), "%s", list[i].name);
			pretty_name(b, nm, sizeof(nm));
			snprintf(names[i], sizeof(names[i]), tr("%s%s  (%d discs)"), list[i].fav ? "\xe2\x98\x85 " : "", nm, list[i].discs);
		}
		else {
			char nm[256];
			pretty_name(list[i].name, nm, sizeof(nm));
			snprintf(names[i], sizeof(names[i]), "%s%s", list[i].fav ? "\xe2\x98\x85 " : "", nm);
		}
		if (merged && !strcmp(browsePath, MERGED) && list[i].base && is_usb_path(list[i].base) && list[i].dir != 2) {
			size_t l = strlen(names[i]);
			snprintf(names[i] + l, sizeof(names[i]) - l, "  [USB]");
		}
		labels[i] = names[i];
	}
	if (!keepSel) {
		L.sel = 0;
		L.scroll = 0; L.tgtInit = 0;
		L.hyInit = 0;
		memset(&G, 0, sizeof(G));
		transition();
	} else {
		if (L.sel >= count) L.sel = count ? count - 1 : 0;
		if (G.sel >= count) G.sel = count ? count - 1 : 0;
	}

	for (;;) {
		if (cfg.view == VIEW_SHELF) cfg.view = VIEW_CAROUSEL;
		int useCar  = !pickDir && cfg.view == VIEW_CAROUSEL;
		int useGrid = !pickDir && cfg.view != VIEW_LIST && !useCar;
		int cols = grid_style()->cols;
		if (wiiu_quit) break;
		poll_touch();
		uint32_t b = poll_buttons();
		int activate = -1;
		if (count) {
			if (useCar) {
				if (b & (WIIU_BTN_LEFT | WIIU_BTN_UP))    G.sel = clampi(G.sel - 1, 0, count - 1);
				if (b & (WIIU_BTN_RIGHT | WIIU_BTN_DOWN)) G.sel = clampi(G.sel + 1, 0, count - 1);
				if (b & WIIU_BTN_L) G.sel = clampi(G.sel - 5, 0, count - 1);
				if (b & WIIU_BTN_R) G.sel = clampi(G.sel + 5, 0, count - 1);
				if (b & WIIU_BTN_A) activate = G.sel;
			} else if (useGrid) {
				if (b & WIIU_BTN_LEFT)  G.sel = clampi(G.sel - 1, 0, count - 1);
				if (b & WIIU_BTN_RIGHT) G.sel = clampi(G.sel + 1, 0, count - 1);
				if (b & WIIU_BTN_UP)    G.sel = clampi(G.sel - cols, 0, count - 1);
				if (b & WIIU_BTN_DOWN)  G.sel = clampi(G.sel + cols, 0, count - 1);
				if (b & WIIU_BTN_L)     G.sel = clampi(G.sel - cols * 2, 0, count - 1);
				if (b & WIIU_BTN_R)     G.sel = clampi(G.sel + cols * 2, 0, count - 1);
				if (b & WIIU_BTN_A) activate = G.sel;
			} else {
				if (b & WIIU_BTN_UP)    L.sel = L.sel > 0 ? L.sel - 1 : count - 1;
				if (b & WIIU_BTN_DOWN)  L.sel = L.sel < count - 1 ? L.sel + 1 : 0;
				if (b & (WIIU_BTN_L | WIIU_BTN_LEFT))  L.sel = clampi(L.sel - ROWS, 0, count - 1);
				if (b & (WIIU_BTN_R | WIIU_BTN_RIGHT)) L.sel = clampi(L.sel + ROWS, 0, count - 1);
				if (b & WIIU_BTN_A) activate = L.sel;
			}
		}
		if (!pickDir && count && (b & WIIU_BTN_Y)) {
			int i = (useGrid || useCar) ? G.sel : L.sel;
			if (i >= 0 && i < count && !list[i].dir) {
				char full[768];
				snprintf(full, sizeof(full), "%s/%s", edir(&list[i], browsePath), list[i].name);
				emu_fav_toggle(full);
				status(emu_fav_is(full) ? "Added to favourites." : "Removed from favourites.");
				int keepG = G.sel, keepL = L.sel;
				free(list); list = NULL;
				count = 0;
				/* rescan keeps the cursor */
				count = strcmp(browsePath, MERGED) ? scan_dir(browsePath, &list) : scan_merged(&list);
				(void)keepG; (void)keepL;
				goto rescan_keep;
			}
		}
		if (!pickDir && (b & WIIU_BTN_ZL)) {
			char m[96];
			cfg.sort = (cfg.sort + 1) % 3;
			settings_save();
			snprintf(m, sizeof(m), tr("Sorted by: %s"), tr(sort_name(cfg.sort)));
			status(m);
			goto rescan;
		}
		if (!pickDir && (b & WIIU_BTN_ZR)) {
			char m[96];
			cfg.regionf = (cfg.regionf + 1) % (5 + emu_coll_count());
			settings_save();
			snprintf(m, sizeof(m), tr("Showing: %s"), tr(regionf_name(cfg.regionf)));
			status(m);
			goto rescan;
		}
		if (!pickDir && count && (b & WIIU_BTN_MINUS)) {   /* - : this game's options */
			int i = (useGrid || useCar) ? G.sel : L.sel;
			if (i >= 0 && i < count && !list[i].dir) {
				game_options(&list[i], edir(&list[i], browsePath));
				transition();
				goto rescan_keep;
			}
		}
		if (!pickDir && (b & WIIU_BTN_PLUS)) {
			char q[64];
			if (text_prompt("Search games", searchText, q, sizeof(q))) {
				snprintf(searchText, sizeof(searchText), "%s", q);
				goto rescan;
			}
			transition();
		}
		if (!pickDir && (b & WIIU_BTN_X)) {
			int wasList = cfg.view == VIEW_LIST;
			cfg.view = view_next(cfg.view, 1);   /* X: next view */
			settings_save();
			if (cfg.view == VIEW_LIST) L.sel = G.sel; else if (wasList) G.sel = L.sel;
			{ char vm[64]; snprintf(vm, sizeof(vm), tr("View: %s"), tr(view_name(cfg.view))); status(vm); }
			L.hyInit = G.hInit = 0;
			transition();
		}

		draw_background();
		int back = draw_header(title, 1);
		{
			char shown[300];
			const char *p = show_path(browsePath, shown, sizeof(shown));
			char sub[200];
			snprintf(sub, sizeof(sub), "%s%s%s%s%s%s%s%s%s", *p ? p : "/", favOnly ? tr("   \xe2\x98\x85 Favourites") : "",
			         searchText[0] ? tr("   Search: \"") : "", searchText, searchText[0] ? "\"" : "",
			         cfg.sort ? "   \xc2\xb7 " : "", cfg.sort ? tr(sort_name(cfg.sort)) : "",
			         cfg.regionf ? "   \xc2\xb7 " : "", cfg.regionf ? tr(regionf_name(cfg.regionf)) : "");
			header_sub(title, sub, GREY);
		}
		int t = -1;
		if (count == 0) {
			text(fontItem, 640, 360, favOnly || searchText[0] || cfg.regionf ? "No games match." : "No games in this folder.", GREY, 1);
			text(fontSmall, 640, 410, favOnly || searchText[0] || cfg.regionf ? "Press + or ZR to change the filter."
			                                                    : "Choose your games folder in Settings.", DIM, 1);
		} else if (pickDir) {
			static SDL_Color marks[1024];
			int nm = count < 1024 ? count : 1024;
			for (int i = 0; i < nm; i++)
				marks[i] = list[i].dir == 2 ? PS_GREEN : list[i].dir ? PS_YELLOW : PS_BLUE;
			t = list_run(&L, labels, NULL, count, 0, NULL, count <= 1024 ? marks : NULL);
		} else if (useCar) {
			t = carousel_run(&G, list, labels, count, browsePath);
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
					snprintf(full, sizeof(full), "%s/%s", edir(e, browsePath), e->name);
					e->idTried = 1;
					if (!covers_detect_id(full, e->id)) e->id[0] = 0;
				}
				draw_cover(ecover(e), e->id[0] ? e->id : NULL);
			}
		}
		if (t >= 0) activate = t;
		char pickHint[160];
		if (pickDir) {
			if (pickGames) snprintf(pickHint, sizeof(pickHint), tr(pickGames == 1 ? "1 game in this folder" : "%d games in this folder"), pickGames);
			else snprintf(pickHint, sizeof(pickHint), "%s", tr("Pick the folder with your games"));
		}
		draw_footer(pickDir ? pickHint :
		            "X  View   Y Favourite   - Options   + Search   ZL Sort   ZR Filter");
		present();

		if (back) break;                                      /* leave now */
		if (b & WIIU_BTN_B) {
			if (at_top(root)) break;                           /* back to menu */
			nav_up(merged);
			goto rescan;
		}
		if (activate >= 0 && activate < count) {
			entry_t *e = &list[activate];
			if (e->dir == 2) {
				snprintf(out, outsz, "%s", browsePath);
				ret = 1;
				break;
			} else if (pickDir && !e->dir) {
				status("This is a game. Choose [ Use this folder ] to use the folder it is in.");
				continue;
			} else if (!strcmp(e->name, "..")) {
				nav_up(merged);
			} else if (e->dir) {
				if (e->base) snprintf(browsePath, sizeof(browsePath), "%s/%s", e->base, e->name);
				else {
					size_t l = strlen(browsePath);
					snprintf(browsePath + l, sizeof(browsePath) - l, "/%s", e->name);
				}
			} else {
				pendingIndex = 0;
				pendingDisc[0] = 0;
				if (e->discs > 1) {
					int r = prepare_multidisc(edir(e, browsePath), e->name, out, outsz);
					if (r < 0) { transition(); continue; }       /* cancelled */
					if (r > 0) { ret = 1; break; }
				}
				snprintf(out, outsz, "%s/%s", edir(e, browsePath), e->name);
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
static void controls(void);
static void hotkeys(void);
static void update_screen(void);
/* set once a new version was written over the app: the app closes, as it
 * can no longer read its own files */
static int quitAfterUpdate;
static void all_covers(void);
static void bios_screen(void);
static int pick_games_folder(void);
static int pick_usb_folder(void);
static int pick_games_setup(void);
static int gamesChecked;

static void settings(void)
{
	if (!usbfs_mounted()) usbfs_mount();       /* a drive connected after the start */
	enum { S_SCOPE, S_LANG, S_THEME, S_SFX, S_MUSIC, S_AUTOSTART, S_SRC, S_GAMES, S_GAMESUSB, S_VIEW, S_COVERS, S_ALLCOVERS, S_RESUME, S_CORE, S_SKIP, S_FFWD, S_FFSPEED, S_FPS, S_VSYNC, S_AUDIO,
	       S_ASPECT, S_FILTER, S_BEZEL, S_WIDE, S_SCAN, S_CRT, S_FX, S_DRC, S_DITHER, S_HIRES, S_PAD, S_GYRO, S_PAD2, S_DEADZONE, S_MTAP, S_P1FIRST, S_POINTER,
	       S_CONTROLS, S_HOTKEYS, S_RUMBLE, S_REGION, S_BIOS, S_BIOSCHECK, S_LOGO, S_AUTOFIX, S_ADV, S_UPDATE, S_COUNT };
	static const char *allLabels[S_COUNT] = {
		"Settings for", "Language", "Theme colour", "Menu sounds", "Menu music", "Open the last game at start", "Games source", "Games folder", "USB games folder", "Game list", "Covers", "Download all covers...", "Continue where you left off", "CPU core",
		"Frame skip", "Fast forward", "Fast forward speed", "Show FPS", "VSync (no tearing)", "Sound", "Screen",
		"Picture", "Border", "Widescreen (16:9)", "Scanlines", "CRT mask", "Screen effect", "GamePad screen", "Dithering", "Hi-res speed-up", "Player 1 controller", "Aim with GamePad motion",
		"Player 2+ controller", "Analog dead zone", "Multitap (up to 5 players)", "Who is player 1", "Wii Remote pointer", "Button mapping...", "Hotkeys...", "Vibration",
		"Region", "BIOS", "Check BIOS files...", "PlayStation boot logo", "Automatic game fixes", "Advanced...", "Check for updates"
	};
	/* shown in the footer for the selected option */
	static const char *allHelp[S_COUNT] = {
		"Choose whether changes apply to all games or only to this one.",
		"The language of WiiUStation. System follows the Wii U's language.",
		"The colour of the menu.",
		"Sounds when you move and choose in the menu, and their style.",
		"Soft, quiet background music in the menus. Off by default.",
		"Opens the last game right away when WiiUStation starts.",
		"Where your games are: the SD card, a USB drive or both.",
		"The folder with your games on the SD card.",
		"The folder with your games on the USB drive.",
		"How the games are shown: carousel, grid, compact grid or list.",
		"2D front covers or 3D boxes.",
		"Downloads the cover of every game in the games folder now.",
		"Saves where you stop when leaving a game and offers to continue there.",
		"Lightrec is much faster. Interpreter only for games that misbehave.",
		"Skips drawing some frames when the game can't keep full speed.",
		"How fast forward is turned on: holding R3, pressing R3, or holding ZL+ZR.",
		"How many times faster the game runs while fast forwarding.",
		"Shows the frames per second in a corner.",
		"Waits for the screen to refresh before showing a frame, so the picture never tears.",
		"Turns the game's sound on or off.",
		"The shape and size of the picture: 4:3, Auto zoom, stretch or whole multiples.",
		"Smooth softens the pixels, Pixel keeps them square, Sharp smooth is a mix.",
		"What fills the sides of the screen: a blurred copy of the game, a frame or your own image.",
		"Shows the game in 16:9 using its widescreen cheat, when it has one.",
		"Dark lines between the rows, like an old TV.",
		"The colour pattern of a CRT TV's screen.",
		"Curved CRT glass, an LCD grid or a soft glow over the picture.",
		"What the GamePad shows during a game: the same as the TV, only the game, nothing or touch buttons.",
		"The PlayStation's dithering pattern. Off looks cleaner, Force adds it everywhere.",
		"Some menus draw 640x480 and run slow. Auto shows half of the lines when the game is slow; Always does it all the time.",
		"Auto uses a DualShock for games that support it. Light guns and the mouse use the GamePad screen.",
		"With a light gun: aim by turning the GamePad.",
		"The controller of the other players.",
		"How far the sticks move before the game notices.",
		"Up to 5 players, for games that support the Multitap.",
		"With the GamePad on, it is player 1. Choose the Wii controllers to make the Pro Controller or Wii Remote player 1.",
		"Point the Wii Remote at the screen to use the menu. It needs a sensor bar: the GamePad's or the TV's.",
		"Change which Wii U button does what.",
		"Quick actions while playing: hold a button and press another.",
		"Vibration of the GamePad and Pro Controller.",
		"Auto detects the region of the game. NTSC is 60 Hz, PAL is 50 Hz.",
		"Uses a real BIOS from the bios folder when there is one, or always the built-in one.",
		"Shows which BIOS files you have and for which regions.",
		"Shows the PlayStation logo when a game starts (needs a real BIOS).",
		"Applies the fixes that some known games need.",
		"More options: threads, speed, sound, compatibility and GPU fixes.",
		"Looks for a new version of WiiUStation on GitHub.",
	};
	const char *labels[S_COUNT], *values[S_COUNT];
	int map[S_COUNT];
	list_t L = { 0, 0 };
	int picMode = 0;                  /* the picture options over the game */
	char scopeStr[96], gd[80], gdu[80], ffs[8], dzs[8];
	transition();

	for (;;) {
		int loaded = emu_game_loaded(), game = settings_game_scope(), n = 0;
		for (int i = 0; i < S_COUNT; i++) {
			if (i == S_SCOPE && !loaded) continue;
			/* the USB options only exist while a USB drive is there */
			if (i == S_SRC && !usbfs_mounted()) continue;
			if (i == S_GAMES && src_effective() == 1) continue;
			if (i == S_GAMESUSB && src_effective() == 0) continue;
			/* in a game: only what is about the game ("Settings for", then
			 * everything from the CPU core down) */
			if (loaded && i > S_SCOPE && i < S_CORE) continue;
			/* about the emulator, not the game: only in the home screen's Settings */
			if (loaded && (i == S_BIOSCHECK || i == S_AUTOFIX || i == S_UPDATE)) continue;
			/* app-wide options are hidden while editing one game's settings */
			if (game && (i == S_GAMES || i == S_SRC || i == S_GAMESUSB || i == S_VIEW || i == S_COVERS || i == S_RESUME ||
			             i == S_FFWD || i == S_LANG || i == S_HOTKEYS || i == S_SFX || i == S_MUSIC || i == S_UPDATE ||
			             i == S_AUTOSTART || i == S_ALLCOVERS || i == S_AUTOFIX || i == S_POINTER)) continue;
			map[n] = i;
			labels[n] = allLabels[i];
			/* with both drives the two folders say which is which; with one, it is just "Games folder" */
			if (i == S_GAMES && src_effective() == 2) labels[n] = "SD games folder";
			if (i == S_GAMESUSB && src_effective() == 1) labels[n] = "Games folder";
			values[n] = NULL;
			n++;
		}
		for (int k = 0; k < n; k++) {
			switch (map[k]) {
			case S_SCOPE:
				if (game) {
					char nm[64];
					pretty_name(emu_game_name(), nm, sizeof(nm));
					snprintf(scopeStr, sizeof(scopeStr), "%s", tr("This game only"));
				} else snprintf(scopeStr, sizeof(scopeStr), "%s", tr("All games"));
				values[k] = scopeStr;
				break;
			case S_GAMES: {
				char shown[300];
				const char *p = show_path(cfg.games_dir, shown, sizeof(shown));
				const char *last = strrchr(p, '/'), *prev = last;
				if (last && last > p) { prev = last - 1; while (prev > p && *prev != '/') prev--; }
				snprintf(gd, sizeof(gd), "%s%.40s", prev && prev > p ? "..." : "", prev && *p ? prev : "/");
				values[k] = gd;
				break;
			}
			case S_SRC:    values[k] = cfg.games_src == 1 ? "USB" : cfg.games_src == 2 ? "SD + USB" : "SD"; break;
			case S_GAMESUSB: {
				char shown[300];
				const char *p = show_path(cfg.games_dir_usb, shown, sizeof(shown));
				const char *last = strrchr(p, '/'), *prev = last;
				if (last && last > p) { prev = last - 1; while (prev > p && *prev != '/') prev--; }
				snprintf(gdu, sizeof(gdu), "%s%.40s", prev && prev > p ? "..." : "", prev && *p ? prev : "USB/");
				values[k] = gdu;
				break;
			}
			case S_VIEW:   values[k] = view_name(cfg.view); break;
			case S_COVERS: values[k] = cfg.cover3d ? "3D box" : "2D"; break;
			case S_LANG:   values[k] = lang_name(cfg.lang); break;
			case S_THEME: {
				static const char *tn[THEME_COUNT] = { "Blue", "Purple", "Pink", "Red", "Orange", "Gold", "Green", "Teal", "Graphite" };
				values[k] = tn[cfg.theme >= 0 && cfg.theme < THEME_COUNT ? cfg.theme : 0];
				break;
			}
			case S_SFX: {
				static const char *sn[7] = { "Off", "Classic", "Soft", "Retro 8-bit", "Crystal", "Marimba", "Click" };
				values[k] = sn[cfg.menusfx >= 0 && cfg.menusfx <= 6 ? cfg.menusfx : 1];
				break;
			}
			case S_MUSIC: values[k] = cfg.menumusic ? "On" : "Off"; break;
			case S_AUTOSTART: values[k] = cfg.autostart ? "On" : "Off"; break;
			case S_AUTOFIX: values[k] = cfg.autofix ? "On" : "Off"; break;
			case S_UPDATE: values[k] = app_version(); break;
			case S_RESUME: values[k] = cfg.resume ? "Ask" : "Off"; break;
			case S_CORE:   values[k] = cfg.dynarec ? "Lightrec" : "Interpreter"; break;
			case S_SKIP:   values[k] = cfg.frameskip ? "Auto" : "Off"; break;
			case S_FFWD:   values[k] = cfg.ffwd == 0 ? "Off" : cfg.ffwd == 1 ? "Hold R3" :
			                           cfg.ffwd == 2 ? "Press R3 (toggle)" : "Hold ZL+ZR"; break;
			case S_FFSPEED: snprintf(ffs, sizeof(ffs), "%dx", cfg.ffspeed); values[k] = ffs; break;
			case S_VSYNC:  values[k] = cfg.vsync ? "On" : "Off"; break;
			case S_FPS:    values[k] = cfg.show_fps == 2 ? "Detailed" : cfg.show_fps ? "On" : "Off"; break;
			case S_AUDIO:  values[k] = cfg.audio ? "On" : "Off"; break;
			case S_ASPECT: values[k] = cfg.aspect == WIIU_ASPECT_4_3 ? "4:3" :
			                           cfg.aspect == WIIU_ASPECT_ZOOM ? "4:3 Auto zoom" :
			                           cfg.aspect == WIIU_ASPECT_STRETCH ? "Stretch" : "Integer"; break;
			case S_FILTER: values[k] = cfg.filter == WIIU_FILTER_LINEAR ? "Smooth" :
			                           cfg.filter == WIIU_FILTER_NEAREST ? "Pixel" : "Sharp smooth"; break;
			case S_DRC: {
				static const char *dn[DRC_COUNT] = { "Same as TV", "GamePad only", "TV only", "Control panel" };
				values[k] = dn[cfg.drc >= 0 && cfg.drc < DRC_COUNT ? cfg.drc : 0];
				break;
			}
			case S_FX: {
				static const char *fxn[FX_COUNT] = { "Off", "CRT curve", "LCD grid", "Glow" };
				values[k] = fxn[cfg.fx >= 0 && cfg.fx < FX_COUNT ? cfg.fx : 0];
				break;
			}
			case S_BEZEL:  values[k] = bezel_name(cfg.bezel); break;
			case S_SCAN:   values[k] = cfg.scanlines ? "On" : "Off"; break;
			case S_CRT:    values[k] = cfg.crt ? "On" : "Off"; break;
			case S_DITHER: values[k] = cfg.dithering == 0 ? "Off" : cfg.dithering == 1 ? "On" : "Force"; break;
			case S_HIRES:  values[k] = cfg.hires_fast == 0 ? "Off" : cfg.hires_fast == 1 ? "Auto" : "Always"; break;
			case S_PAD:    values[k] = pad_type_name(cfg.analog); break;
			case S_PAD2:   values[k] = pad_type_name(cfg.analog2); break;
			case S_WIDE:   values[k] = cfg.widescreen ? "On" : "Off"; break;
			case S_GYRO:   values[k] = cfg.gyroaim ? "On" : "Off"; break;
			case S_DEADZONE: snprintf(dzs, sizeof(dzs), "%d%%", cfg.deadzone * 5); values[k] = dzs; break;
			case S_MTAP:   values[k] = cfg.multitap ? "On" : "Off"; break;
			case S_P1FIRST: values[k] = cfg.p1first ? "Wii controllers" : "GamePad"; break;
			case S_POINTER: values[k] = cfg.pointer == 0 ? "Off" : cfg.pointer == 1 ? "Automatic" : "TV sensor bar"; break;
			case S_RUMBLE: values[k] = cfg.rumble ? "On" : "Off"; break;
			case S_REGION: values[k] = cfg.region == 0 ? "Auto" : cfg.region == 1 ? "NTSC (60 Hz)" : "PAL (50 Hz)"; break;
			case S_BIOS:   values[k] = cfg.bios_hle ? "Built-in" : "Auto"; break;
			case S_LOGO:   values[k] = cfg.bootlogo ? "On" : "Off"; break;
			default: break;
			}
		}
		if (L.sel >= n) L.sel = n - 1;

		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		/* the picture options of a running game: A (or a tap) on one opens
		 * them over the game itself, full screen, so every change is seen as
		 * it happens; B goes back to the list */
#define IS_PIC(r) ((r) == S_ASPECT || (r) == S_FILTER || (r) == S_BEZEL || (r) == S_WIDE || \
                   (r) == S_SCAN || (r) == S_CRT || (r) == S_FX || (r) == S_DITHER)
		if (picMode && !(loaded && n && IS_PIC(map[L.sel]))) picMode = 0;
		if (b & (WIIU_BTN_UP | WIIU_BTN_DOWN)) {
			int step = (b & WIIU_BTN_UP) ? -1 : 1;
			if (picMode) {   /* up / down go through the picture options only */
				for (int j = L.sel + step; j >= 0 && j < n; j += step)
					if (IS_PIC(map[j])) { L.sel = j; break; }
			} else L.sel = step < 0 ? (L.sel > 0 ? L.sel - 1 : n - 1) : (L.sel < n - 1 ? L.sel + 1 : 0);
		}
		int cur = n ? map[L.sel] : -1;
		int canPic = loaded && cur >= 0 && IS_PIC(cur);
		if (!picMode && canPic && (b & WIIU_BTN_A)) { picMode = 1; b &= ~WIIU_BTN_A; }
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;

		int back = 0;
		if (picMode) {
			SDL_Texture *pv = (SDL_Texture *)video_preview();
			SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
			SDL_RenderClear(R);
			if (pv) SDL_RenderCopy(R, pv, NULL, NULL);
			SDL_Color k = { 0, 0, 0 }, hi = th(0, 140, 220);
			fill(0, 596, 1280, 124, k, 190);
			char cnt[16];
			int pos = 0, tot = 0;
			for (int j = 0; j < n; j++) if (IS_PIC(map[j])) { tot++; if (j <= L.sel) pos++; }
			snprintf(cnt, sizeof(cnt), "%d/%d", pos, tot);
			text(fontSmall, 640, 616, labels[L.sel], GREY, 1);
			text(fontSmall, 1240, 616, cnt, DIM, 2);
			rrect(440, 634, 400, 48, 24, hi, 230);
			text(fontItem, 640, 658, values[L.sel], WHITE, 1);
			float lq[6] = { 404, 658, 424, 644, 424, 672 }, rq[6] = { 876, 658, 856, 644, 856, 672 };
			poly(lq, 3, WHITE, 255);
			poly(rq, 3, WHITE, 255);
			wiiu_button(52, 700, "B");
			text(fontSmall, 76, 700, "Back", GREY, 0);
			marquee(fontSmall, 680, 700, 1000, allHelp[cur], DIM, 1, 1);
			if (statusMsg[0] && SDL_GetTicks() < statusUntil) {
				fill(0, 548, 1280, 44, k, 160);
				text(fontSmall, 640, 570, statusMsg, PS_YELLOW, 1);
			}
			/* touch: the left / right halves of the bar change the value,
			 * a tap above it goes back to the list */
			if (tap_in(0, 596, 640, 124)) dir = -1;
			else if (tap_in(640, 596, 640, 124)) dir = 1;
			else if (T.tapped && T.ty < 596) picMode = 0;
			present();
			if (b & WIIU_BTN_B) { picMode = 0; b &= ~WIIU_BTN_B; }
		} else {
			draw_background();
			back = draw_header(game ? "Game settings" : "Settings", 1);
			int arrow = 0;
			int t = list_run(&L, labels, values, n, 1, &arrow, NULL);
			if (t >= 0) { if (loaded && IS_PIC(map[t])) picMode = 1; else dir = 1; }
			if (arrow) dir = arrow;
			/* changing a picture option (left / right, the arrows) also opens
			 * it over the game, so the change is seen at once */
			if (dir && loaded && IS_PIC(map[L.sel])) picMode = 1;
			draw_footer(canPic ? "A: adjust it with the game on screen" : cur >= 0 ? allHelp[cur] : NULL);
			present();
		}

		if (back || (b & WIIU_BTN_B) || wiiu_quit) {
			settings_save();          /* settings are saved automatically */
			emu_apply_settings();
			return;
		}
		if (!dir) continue;
		switch (map[L.sel]) {
		case S_SCOPE:
			settings_save();
			settings_set_game_scope(!game);
			status(!game ? "Now editing this game's own settings." : "This game uses the global settings again.");
			break;
		case S_SRC:
			cfg.games_src = (cfg.games_src + 3 + dir) % 3;
			browsePath[0] = 0;
			gamesChecked = 0;
			break;
		case S_GAMES: pick_games_folder(); transition(); break;
		case S_GAMESUSB: pick_usb_folder(); transition(); break;
		case S_VIEW:   cfg.view = view_next(cfg.view, dir); break;
		case S_VSYNC:  cfg.vsync = !cfg.vsync; break;
		case S_COVERS:
			cfg.cover3d = !cfg.cover3d;
			covers_flush();
			if (cfg.cover3d) status("3D boxes are downloaded as you browse. Without one, the 2D cover is shown.");
			break;
		case S_RESUME: cfg.resume = !cfg.resume; break;
		case S_BEZEL:
			cfg.bezel = bezel_next(cfg.bezel, dir);
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
		case S_SKIP:    cfg.frameskip = !cfg.frameskip; break;
		case S_FFWD:    cfg.ffwd = (cfg.ffwd + 4 + dir) % 4; break;
		case S_FFSPEED: cfg.ffspeed = 2 + (cfg.ffspeed - 2 + 3 + dir) % 3; break;
		case S_FPS:     cfg.show_fps = (cfg.show_fps + 3 + dir) % 3; break;
		case S_AUDIO:   cfg.audio = !cfg.audio; break;
		case S_ASPECT: {
			static const int ord[4] = { WIIU_ASPECT_4_3, WIIU_ASPECT_ZOOM, WIIU_ASPECT_STRETCH, WIIU_ASPECT_INTEGER };
			int k = 0;
			while (k < 4 && ord[k] != cfg.aspect) k++;
			cfg.aspect = ord[(k + 4 + dir) % 4];
			if (cfg.aspect == WIIU_ASPECT_ZOOM) status("Crops the black borders the game draws, then fills the height.");
			break;
		}
		case S_FILTER: cfg.filter = (cfg.filter + 3 + dir) % 3; break;
		case S_FX:     cfg.fx = (cfg.fx + FX_COUNT + dir) % FX_COUNT; break;
		case S_DRC:
			if (!video_screen_modes_ok()) { cfg.drc = DRC_MIRROR; status("Not available with this SDL version."); break; }
			cfg.drc = (cfg.drc + DRC_COUNT + dir) % DRC_COUNT;
			switch (cfg.drc) {
			case DRC_ONLY:    status("Play on the GamePad: the TV stays black. HOME or L3+R3 opens the menu."); break;
			case DRC_TV_ONLY: status("The GamePad screen stays black. HOME or L3+R3 opens the menu."); break;
			case DRC_PANEL:   status("Game on the TV, touch buttons on the GamePad (save, load, pause...)."); break;
			}
			break;
		case S_SCAN:   cfg.scanlines = !cfg.scanlines; break;
		case S_CRT:    cfg.crt = !cfg.crt; break;
		case S_DITHER: cfg.dithering = (cfg.dithering + 3 + dir) % 3; break;
		case S_HIRES:  cfg.hires_fast = (cfg.hires_fast + 3 + dir) % 3;
		               status(cfg.hires_fast == 2 ? "Games that draw 640x480 show half of the lines: faster, a little less sharp."
		                    : cfg.hires_fast == 1 ? "Only when the game runs slow." : "Always the full picture.");
		               video_set_halve(cfg.hires_fast == 2);
		               break;
		case S_PAD:
			cfg.analog = (cfg.analog + 1 + PAD_COUNT + 1 + dir) % (PAD_COUNT + 1) - 1;   /* Auto .. Justifier */
			switch (cfg.analog) {
			case PAD_DUALSHOCK: status("DualShock: some older games only work with Digital."); break;
			case PAD_NEGCON:    status("neGcon: left stick steers, ZR / right stick up = I, ZL / down = II."); break;
			case PAD_MOUSE:     status("Mouse: drag on the GamePad or use the right stick. ZR = left, ZL = right button."); break;
			case PAD_GUNCON:
			case PAD_JUSTIFIER: status("Light gun: touch the GamePad screen to shoot, touch outside the picture to reload."); break;
			}
			break;
		case S_PAD2:   cfg.analog2 = (cfg.analog2 + 1 + PAD_NEGCON + 2 + dir) % (PAD_NEGCON + 2) - 1; break;
		case S_WIDE:
			cfg.widescreen = !cfg.widescreen;
			if (cfg.widescreen) status("Uses the game's widescreen code from its .cht file (a cheat named widescreen or 16:9).");
			break;
		case S_GYRO:
			cfg.gyroaim = !cfg.gyroaim;
			if (cfg.gyroaim) status("GunCon / Justifier: turn the GamePad to aim, L re-centres, ZR shoots.");
			break;
		case S_DEADZONE: cfg.deadzone = (cfg.deadzone + 7 + dir) % 7; break;
		case S_MTAP:   cfg.multitap = !cfg.multitap;
		               status(cfg.multitap ? "Players 1-4 on port 1, player 5 on port 2. Applies on resume."
		                                   : "Two players.");
		               break;
		case S_P1FIRST: cfg.p1first = !cfg.p1first;
		                status("Applies the next time a game is loaded."); break;
		case S_POINTER: cfg.pointer = (cfg.pointer + 3 + dir) % 3;
		                if (cfg.pointer == 1) status("Point the Wii Remote at the GamePad. Its sensor bar is on only while a Wii Remote is connected and used.");
		                else if (cfg.pointer == 2) status("Point the Wii Remote at the TV. Needs the sensor bar of the TV.");
		                break;
		case S_CONTROLS: controls(); transition(); break;
		case S_HOTKEYS:  hotkeys(); transition(); break;
		case S_THEME:    cfg.theme = (cfg.theme + THEME_COUNT + dir) % THEME_COUNT; break;
		case S_SFX:      cfg.menusfx = (cfg.menusfx + 7 + dir) % 7; sfx_play(SFX_OK); break;
		case S_MUSIC:    cfg.menumusic = !cfg.menumusic; audio_music(cfg.menumusic && !emu_game_loaded()); break;
		case S_AUTOSTART:
			cfg.autostart = !cfg.autostart;
			if (cfg.autostart) status("The last game opens when WiiUStation starts. Hold B during the opening to go to the menu.");
			break;
		case S_UPDATE:   update_screen(); if (quitAfterUpdate) return; transition(); break;
		case S_ALLCOVERS: all_covers(); transition(); break;
		case S_BIOSCHECK: bios_screen(); transition(); break;
		case S_AUTOFIX:
			cfg.autofix = !cfg.autofix;
			status(cfg.autofix ? "Known games get the fixes they need. Applies the next time a game is loaded."
			                   : "Applies the next time a game is loaded.");
			break;
		case S_LANG:     cfg.lang = (cfg.lang + 1 + LANG_COUNT + 1 + dir) % (LANG_COUNT + 1) - 1; video_lang_changed(); break;
		case S_RUMBLE: cfg.rumble = !cfg.rumble; break;
		case S_REGION: cfg.region = (cfg.region + 3 + dir) % 3; status("Applies the next time a game is loaded."); break;
		case S_BIOS:   cfg.bios_hle = !cfg.bios_hle; status("Applies the next time a game is loaded."); break;
		case S_LOGO:   cfg.bootlogo = !cfg.bootlogo; break;
		case S_ADV:    advanced(); transition(); break;
		}
	}
}

static void advanced(void)
{
	/* rows: our own options, then the core's, with section titles */
	enum { MAXR = 48 };
	const char *labels[MAXR], *values[MAXR];
	char titles[8][40];
	int kind[MAXR], n = 0, nt = 0;              /* kind: -1 title, -2/-3/-4 ours, >= 0 core option */
	if (!settings_game_scope()) {      /* threads are app-wide, never per game */
		labels[n] = "#Threads"; kind[n++] = -1;
		labels[n] = "GPU rendering on core 2"; kind[n++] = -2;
		labels[n] = "Sound mixing on core 0"; kind[n++] = -3;
	} else {
		labels[n] = "#Sound effects"; kind[n++] = -1;
	}
	labels[n] = "Sound reverb"; kind[n++] = -4;
	if (!settings_game_scope()) {
		labels[n] = "#Picture"; kind[n++] = -1;
		labels[n] = "Direct 16-bit picture"; kind[n++] = -5;
	}
	for (int i = 0; i < core_opt_count() && n < MAXR - 2; i++) {
		if (core_opt_section(i) && nt < 8) {
			snprintf(titles[nt], sizeof(titles[nt]), "#%s", core_opt_section(i));
			labels[n] = titles[nt++]; kind[n++] = -1;
		}
		labels[n] = core_opt_label(i); kind[n++] = i;
	}
	list_t L = { 1, 0 };
	transition();

	for (;;) {
		for (int r = 0; r < n; r++) {
			switch (kind[r]) {
			case -1: values[r] = NULL; break;
			case -2: values[r] = cfg.gpu_thread ? "On" : "Off"; break;
			case -3: values[r] = cfg.spu_thread ? "On" : "Off"; break;
			case -4: values[r] = cfg.reverb ? "On" : "Off"; break;
			case -5: values[r] = cfg.tex565 ? "On" : "Off"; break;
			default: values[r] = core_opt_value(kind[r]); break;
			}
		}
		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		if (b & WIIU_BTN_UP)
			do L.sel = L.sel > 0 ? L.sel - 1 : n - 1; while (kind[L.sel] == -1);
		if (b & WIIU_BTN_DOWN)
			do L.sel = L.sel < n - 1 ? L.sel + 1 : 0; while (kind[L.sel] == -1);
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;

		draw_background();
		int back = draw_header("Advanced", 1);
		int arrow = 0, prev = L.sel;
		int t = list_run(&L, labels, values, n, 1, &arrow, NULL);
		if (kind[L.sel] == -1) { L.sel = prev; t = -1; arrow = 0; }   /* titles can't be picked */
		if (t >= 0) dir = 1;
		if (arrow) dir = arrow;
		{
			const char *help = "Turn these off only if a game shows glitches.";
			int k = kind[L.sel];
			if (k == -2) help = "Draws the picture on another CPU core. Faster. Applies on the next game.";
			else if (k == -3) help = "Mixes sound on another CPU core. Turn off if there is no sound.";
			else if (k == -4) help = "Echo effect of the real console. Off is a little faster.";
			else if (k == -5) help = "Sends the picture to the screen without converting it: much faster in heavy menus. If the picture is black or the colours are wrong, turn it off.";
			else if (k >= 0) help = core_opt_help(k);
			draw_footer(help);
		}
		present();

		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (!dir) continue;
		switch (kind[L.sel]) {
		case -2: cfg.gpu_thread = !cfg.gpu_thread; status("Applies the next time a game is loaded."); break;
		case -3: cfg.spu_thread = !cfg.spu_thread; break;
		case -4: cfg.reverb = !cfg.reverb; break;
		case -5: cfg.tex565 = !cfg.tex565; video_set_tex565(cfg.tex565); break;
		case -1: break;
		default: core_opt_cycle(kind[L.sel], dir); break;
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
		"PlayStation (PS1) emulator for the Wii U, based on PCSX-ReARMed",
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
		SDL_Color panel = th(10, 14, 34);
		fill(90, 120, 1100, 536, panel, 170);
		SDL_RenderSetClipRect(R, &(SDL_Rect){ 90, 124, 1100, 528 });
		for (int i = 0; i < n; i++) {
			int y = 150 + i * LH - (int)scroll;
			if (y < 100 || y > 700) continue;
			if (lines[i][0] == '#')
				text(fontItem, 640, y, tr(lines[i]) + 1, PS_BLUE, 1);
			else
				text(fontSmall, 640, y, lines[i], i < 2 ? WHITE : GREY, 1);
		}
		SDL_RenderSetClipRect(R, NULL);
		{
			char v[64];
			snprintf(v, sizeof(v), tr("Version %s"), app_version());
			draw_footer(v);
		}
		present();
		if (back || (b & (WIIU_BTN_B | WIIU_BTN_A)) || wiiu_quit) return;
	}
}

/* Loading screen shown while the game starts (the load itself blocks).
 * It comes in smoothly: the cover grows into place with a glow, then the
 * name and the spinner fade in; the colours follow the theme. */
static void loading_screen(const char *path)
{
	char id[16] = "";
	const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	char nm[80];
	covers_detect_id(path, id);
	pretty_name(name, nm, sizeof(nm));
	const uint32_t T0 = SDL_GetTicks(), LEN = 450;
	for (int last = 0; last < 2;) {
		uint32_t now = SDL_GetTicks() - T0;
		float p = now >= LEN ? 1.0f : (float)now / LEN;
		if (p >= 1.0f) last++;                       /* two final frames: TV + GamePad */
		float e = 1 - (1 - p) * (1 - p) * (1 - p);     /* ease out */
		SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
		SDL_RenderClear(R);
		for (int y = 0; y < 720; y += 6) {
			SDL_Color c = th(4 + y / 90, 8 + y / 40, 30 + y / 14);
			fill(0, y, 1280, 6, c, 255);
		}
		/* glow behind the cover */
		bgNV = bgNI = 0;
		bg_glow(640, 300, 280 * e, th(30, 90, 200), 90 * e);
		SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(R, NULL, bgV, bgNV, bgIdx, bgNI);
		/* the cover grows from 70% with a fade */
		int sz = (int)(300 * (0.7f + 0.3f * e));
		cover_or_disc(id, 640 - sz / 2, 300 - sz / 2, sz);
		if (p < 1) fill(640 - sz / 2 - 2, 300 - sz / 2 - 2, sz + 4, sz + 4, th(4, 8, 30), (uint8_t)(255 * (1 - e)));
		/* the name and the spinner a moment later */
		float q = p < 0.4f ? 0 : (p - 0.4f) / 0.6f;
		if (q > 0) {
			int dy = (int)(16 * (1 - q));
			SDL_Color sp = PS_BLUE;
			sp.a = (Uint8)(255 * q);
			textAlpha = (uint8_t)(255 * q);
			marquee(fontItem, 640, 500 + dy, 1100, nm, WHITE, 1, 1);
			text(fontSmall, 640, 630 + dy, "Loading...", GREY, 1);
			textAlpha = 255;
			if (q > 0.5f) spinner(640, 580 + dy, 22, sp);
		}
		SDL_RenderPresent(R);
		pump_events();
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

/* ------------------------------------------------------------------------ */
/* controls: button mapping                                                 */
/* ------------------------------------------------------------------------ */
extern const int default_map[16];
extern const int default_wmap[16];
static const char *wiiu_btn_name(int bit)
{
	static const char *n[] = { "A", "B", "X", "Y", "D-Pad Up", "D-Pad Down", "D-Pad Left",
	                           "D-Pad Right", "L", "R", "ZL", "ZR", "+ (Plus)", "- (Minus)",
	                           "L-Stick click", "R-Stick click" };
	return bit >= 0 && bit < 16 ? n[bit] : "None";
}

/* ------------------------------------------------------------------------ */
/* BIOS check                                                               */
/* ------------------------------------------------------------------------ */
static void bios_screen(void)
{
	enum { MAXB = 40 };
	static char names[MAXB][128], vals[MAXB][96];
	const char *labels[MAXB], *values[MAXB];
	int n = 0, have[4] = { 0 };
	DIR *d = opendir(WIIU_BIOS_DIR);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) && n < MAXB) {
			char p[600], model[32], ver[16];
			int region;
			if (e->d_name[0] == '.') continue;
			snprintf(p, sizeof(p), "%s/%s", WIIU_BIOS_DIR, e->d_name);
			int r = bios_identify(p, model, sizeof(model), ver, sizeof(ver), &region);
			snprintf(names[n], 128, "%s", e->d_name);
			if (r == 1) {
				const char *rg = region == 1 ? "USA" : region == 2 ? "Europe" : region == 3 ? "Japan" : "Other";
				snprintf(vals[n], 96, "%s  %s  v%s", tr(rg), model, ver);
				if (region >= 1 && region <= 3) have[region] = 1;
			} else if (r == 0) snprintf(vals[n], 96, "%s", tr("Unknown version (may still work)"));
			else snprintf(vals[n], 96, "%s", tr("Not a BIOS file"));
			labels[n] = names[n];
			values[n] = vals[n];
			n++;
		}
		closedir(d);
	}
	char summary[200];
	if (!have[1] && !have[2] && !have[3])
		snprintf(summary, sizeof(summary), "%s", tr("No known BIOS: games use the built-in one (HLE), which runs most games."));
	else {
		char miss[96] = "";
		const char *rn[4] = { "", "USA", "Europe", "Japan" };
		for (int i = 1; i <= 3; i++)
			if (!have[i]) { if (miss[0]) strcat(miss, ", "); strcat(miss, tr(rn[i])); }
		if (miss[0]) snprintf(summary, sizeof(summary), tr("Missing for: %s (those games use the built-in BIOS)"), miss);
		else snprintf(summary, sizeof(summary), "%s", tr("You have a BIOS for every region."));
	}
	list_t L = { 0, 0 };
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (n) {
			if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : n - 1;
			if (b & WIIU_BTN_DOWN) L.sel = L.sel < n - 1 ? L.sel + 1 : 0;
		}
		draw_background();
		int back = draw_header("BIOS files", 1);
		header_sub("BIOS files", summary, have[1] || have[2] || have[3] ? GREY : PS_YELLOW);
		if (n) list_run(&L, labels, values, n, 0, NULL, NULL);
		else {
			text(fontItem, 640, 330, "The bios folder is empty.", GREY, 1);
			text_wrap(fontSmall, 640, 380, 1000, "Put BIOS files (e.g. scph5501.bin) in WiiUStation/bios.", DIM);
		}
		draw_footer("Put BIOS files (e.g. scph5501.bin) in WiiUStation/bios.");
		present();
		if (back || (b & (WIIU_BTN_B | WIIU_BTN_A)) || wiiu_quit) return;
	}
}

/* ------------------------------------------------------------------------ */
/* updates                                                                  */
/* ------------------------------------------------------------------------ */
/* the release text of the new version: markdown reduced to plain lines, scrollable */
static void notes_screen(void)
{
	enum { MAXL = 200, W = 1100 };
	static char lines[MAXL][160];
	static int head[MAXL];
	int n = 0;
	const char *src = update_notes();
	char raw[512];
	while (*src && n < MAXL - 1) {
		size_t k = strcspn(src, "\n");
		snprintf(raw, sizeof(raw), "%.*s", (int)(k < sizeof(raw) - 1 ? k : sizeof(raw) - 1), src);
		src += k + (src[k] == '\n');
		/* plain text: no ** or `, "- " becomes a bullet, "###" a heading */
		char t[512];
		int h = 0, o = 0;
		const char *q = raw;
		while (*q == '#') { h = 1; q++; }
		while (*q == ' ') q++;
		if (!h && q[0] == '-' && q[1] == ' ') { o += snprintf(t, sizeof(t), "\xe2\x80\xa2 "); q += 2; }
		for (; *q && o < (int)sizeof(t) - 4; q++) {
			if (*q == '*' || *q == '`') continue;
			t[o++] = *q;
		}
		t[o] = 0;
		if (!t[0]) { lines[n][0] = 0; head[n++] = 0; continue; }
		/* wrap at the box width; continuation lines are indented */
		const char *p = t;
		int first = 1;
		while (*p && n < MAXL - 1) {
			int best = 0, len = (int)strlen(p);
			char tmp[512];
			for (int i = 1; i <= len; i++) {
				if (p[i] != ' ' && p[i] != 0) continue;
				int w = 0;
				snprintf(tmp, sizeof(tmp), "%.*s", i, p);
				TTF_SizeUTF8(h ? fontItem : fontSmall, tmp, &w, NULL);
				if (w > W - (first ? 0 : 30) && best) break;
				best = i;
			}
			if (!best) best = len;
			snprintf(lines[n], sizeof(lines[0]), "%s%.*s", first ? "" : "    ", best, p);
			head[n++] = h;
			p += best;
			while (*p == ' ') p++;
			first = 0;
		}
	}
	const int LH = 34, TOP = 110, SHOWN = 16;
	float scroll = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & WIIU_BTN_UP)   scroll -= 3;
		if (b & WIIU_BTN_DOWN) scroll += 3;
		scroll -= T.dy / (float)LH;
		if (scroll > n - SHOWN) scroll = (float)(n - SHOWN);
		if (scroll < 0) scroll = 0;
		draw_background();
		int back = draw_header("What's new", 1);
		if (!n) text(fontItem, 640, 330, "No release notes.", GREY, 1);
		SDL_RenderSetClipRect(R, &(SDL_Rect){ 0, TOP - 10, 1280, SHOWN * LH + 20 });
		for (int i = (int)scroll; i < n && i <= (int)scroll + SHOWN; i++) {
			int y = TOP + (int)((i - scroll) * LH) + LH / 2;
			if (head[i]) text(fontItem, 90, y, lines[i], PS_BLUE, 0);
			else text(fontSmall, 90, y, lines[i], WHITE, 0);
		}
		SDL_RenderSetClipRect(R, NULL);
		draw_footer("Up / Down: scroll");
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
	}
}

static void update_screen(void)
{
	char tag[32] = "", err[128] = "", line1[256], line2[256];
	int state = 0;      /* 0 checking, 1 newer, 2 current, 3 error, 4 downloading, 5 installed, 6 failed */
	int checked = 0, cancelling = 0, leaveAfter = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		draw_background();
		int back = draw_header("Check for updates", 1);
		if (state == 4) {
			float f;
			int st = update_progress(&f);
			if (st == 2) { state = 5; sfx_play(SFX_OK); }
			else if (st == -1) { state = 6; sfx_play(SFX_ERROR); }
			else if (st == 3) {
				state = 1;
				if (leaveAfter) return;          /* Back / B / HOME: the download stopped, leave */
				status("Download cancelled.");
			}
			else {
				SDL_Color bg = th(26, 32, 60), fg = th(0, 140, 220);
				rrect(240, 380, 800, 26, 13, bg, 255);
				rrect(240, 380, (int)(800 * (f < 0.02f ? 0.02f : f)), 26, 13, fg, 255);
				rrect(500, 450, 280, 60, 30, th(26, 32, 60), 255);
				outline(500, 450, 280, 60, th(0, 140, 220), 255, 2);
				text(fontItem, 640, 480, cancelling ? "Cancelling..." : "Cancel", WHITE, 1);
				if (tap_in(500, 450, 280, 60)) { update_cancel(); cancelling = 1; }
			}
		}
		switch (state) {
		case 0: snprintf(line1, sizeof(line1), "%s", tr("Checking GitHub...")); line2[0] = 0; break;
		case 1: snprintf(line1, sizeof(line1), tr("Version %s is available."), tag);
		        snprintf(line2, sizeof(line2), tr("You have %s. Press A to download it."), app_version()); break;
		case 2: snprintf(line1, sizeof(line1), "%s", tr("WiiUStation is up to date."));
		        snprintf(line2, sizeof(line2), tr("Version %s"), app_version()); break;
		case 3: snprintf(line1, sizeof(line1), "%s", err); snprintf(line2, sizeof(line2), tr("Version %s"), app_version()); break;
		case 4: snprintf(line1, sizeof(line1), tr("Downloading %s..."), tag); line2[0] = 0; break;
		case 5: snprintf(line1, sizeof(line1), tr("Version %s is ready."), tag);
		        snprintf(line2, sizeof(line2), "%s", tr("Press A to restart WiiUStation with the new version.")); break;
		case 6: snprintf(line1, sizeof(line1), "%s", tr("The download failed. Nothing was changed."));
		        snprintf(line2, sizeof(line2), "%s", update_error()); break;
		}
		text(fontItem, 640, 300, line1, WHITE, 1);
		if (line2[0]) text_wrap(fontSmall, 640, 345, 1100, line2, GREY);
		int notesTap = 0;
		if (state == 1 || state == 5) {             /* a button for the release text */
			rrect(500, 450, 280, 60, 30, th(26, 32, 60), 255);
			outline(500, 450, 280, 60, th(0, 140, 220), 255, 2);
			text(fontItem, 640, 480, "What's new", WHITE, 1);
			notesTap = tap_in(500, 450, 280, 60);
		}
		draw_footer(state == 1 ? "A: download and install   X: what's new" : state == 5 ? "X: what's new" : state == 4 ? "B: cancel and go back" : NULL);
		present();
		if (notesTap || ((state == 1 || state == 5) && (b & WIIU_BTN_X))) {
			notes_screen();
			transition();
			continue;
		}
		if (!checked) {     /* the check blocks for a moment: after the first frame */
			checked = 1;
			int r = update_check(tag, sizeof(tag), err, sizeof(err));
			state = r == 1 ? 1 : r == 0 ? 2 : 3;
			continue;
		}
		if (state == 4) {                  /* downloading: B / Back / HOME cancel it */
			if (back || (b & WIIU_BTN_B) || wiiu_quit) { update_cancel(); cancelling = 1; leaveAfter = 1; }
			continue;
		}
		cancelling = 0;
		if (state == 5) {                  /* downloaded: A restarts in the new version */
			if ((b & WIIU_BTN_A) || T.tapped) {
				update_install_on_exit();
				quitAfterUpdate = 1;
				return;
			}
			if (back || (b & WIIU_BTN_B) || wiiu_quit) return;   /* not now: the download is kept until the next one */
			continue;
		}
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (state == 1 && ((b & WIIU_BTN_A) || T.tapped)) {
			if (update_start()) state = 4;
			else state = 6;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* hotkeys                                                                  */
/* ------------------------------------------------------------------------ */
static void hotkeys(void)
{
	enum { NR = HK_COUNT + 1 };
	const char *labels[NR], *values[NR];
	list_t L = { 0, 0 };
	transition();
	for (;;) {
		for (int i = 0; i < HK_COUNT; i++) {
			labels[i] = hotkey_name(i);
			values[i] = button_name(cfg.hk[i]);
		}
		labels[HK_COUNT] = "Reset to default";
		values[HK_COUNT] = NULL;
		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : NR - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < NR - 1 ? L.sel + 1 : 0;
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;

		draw_background();
		int back = draw_header("Hotkeys", 1);
		int arrow = 0;
		int t = list_run(&L, labels, values, NR, 1, &arrow, NULL);
		if (t >= 0) dir = 1;
		if (arrow) dir = arrow;
		{
			char h[160];
			if (cfg.hk[HK_MOD] < 0) snprintf(h, sizeof(h), "%s", tr("Hotkeys are off. Choose a hotkey button to use them."));
			else snprintf(h, sizeof(h), tr("In game: hold %s and press the button of an action"), button_name(cfg.hk[HK_MOD]));
			draw_footer(h);
		}
		present();

		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (!dir) continue;
		if (L.sel == HK_COUNT) {
			extern const int default_hotkeys[HK_COUNT];
			memcpy(cfg.hk, default_hotkeys, sizeof(cfg.hk));
			status("Default layout restored.");
			continue;
		}
		/* -1 (none) .. 15 */
		int v = cfg.hk[L.sel] + dir;
		if (v < -1) v = 15;
		if (v > 15) v = -1;
		cfg.hk[L.sel] = v;
	}
}

static const char *wr_name(int bit)
{
	static const char *n[WR_COUNT] = { "1", "2", "A", "B", "+ (Plus)", "- (Minus)", "D-Pad Up", "D-Pad Down",
	                                   "D-Pad Left", "D-Pad Right", "C (Nunchuk)", "Z (Nunchuk)",
	                                   "Nunchuk stick up", "Nunchuk stick down", "Nunchuk stick left",
	                                   "Nunchuk stick right" };
	return bit >= 0 && bit < WR_COUNT ? n[bit] : "None";
}

static void controls(void)
{
	/* display order of the PlayStation buttons -> RETRO_DEVICE_ID_JOYPAD_* */
	static const int order[16] = { 9, 8, 0, 1, 10, 11, 12, 13, 14, 15, 3, 2, 4, 5, 6, 7 };
	static const char *psx[16] = { "Triangle", "Circle", "Cross", "Square", "L1", "R1", "L2", "R2",
	                               "L3", "R3", "Start", "Select", "Up", "Down", "Left", "Right" };
	const char *labels[18], *values[18];
	list_t L = { 0, 0 };
	static int wii;                   /* 0 GamePad / Pro / Classic, 1 Wii Remote + Nunchuk */
	transition();
	for (;;) {
		labels[0] = "Controller";
		values[0] = wii ? "Wii Remote + Nunchuk" : "GamePad, Pro, Classic";
		for (int i = 0; i < 16; i++) {
			labels[1 + i] = psx[i];
			values[1 + i] = wii ? wr_name(cfg.wmap[order[i]]) : wiiu_btn_name(cfg.map[order[i]]);
		}
		labels[17] = "Reset to default";
		values[17] = NULL;
		poll_touch();
		uint32_t b = poll_buttons();
		int dir = 0;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : 17;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < 17 ? L.sel + 1 : 0;
		if (b & (WIIU_BTN_RIGHT | WIIU_BTN_A)) dir = 1;
		if (b & WIIU_BTN_LEFT) dir = -1;

		draw_background();
		int back = draw_header("Controls", 1);
		int arrow = 0;
		int t = list_run(&L, labels, values, 18, 1, &arrow, NULL);
		if (t >= 0) dir = 1;
		if (arrow) dir = arrow;
		draw_footer(wii ? "Wii Remote held sideways, or upright with a Nunchuk (its stick is the left stick)"
		                : "Same layout for every controller");
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) { settings_save(); return; }
		if (!dir) continue;
		if (L.sel == 0) {
			wii = !wii;
		} else if (L.sel == 17) {
			if (wii) memcpy(cfg.wmap, default_wmap, sizeof(cfg.wmap));
			else memcpy(cfg.map, default_map, sizeof(cfg.map));
			status("Default layout restored.");
		} else {
			int id = order[L.sel - 1];
			if (wii) cfg.wmap[id] = (cfg.wmap[id] + 1 + WR_COUNT + 1 + dir) % (WR_COUNT + 1) - 1;   /* None .. stick right */
			else cfg.map[id] = (cfg.map[id] + 16 + dir) % 16;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* download every cover of the library                                      */
/* ------------------------------------------------------------------------ */
static void collect_games(const char *dir, int depth, char (**paths)[512], int *n, int *cap)
{
	entry_t *list = NULL;
	int count = scan_dir(dir, &list);
	collapse_entries(dir, list, &count);
	for (int i = 0; i < count; i++) {
		if (!strcmp(list[i].name, "..")) continue;
		char p[512];
		snprintf(p, sizeof(p), "%s/%s", dir, list[i].name);
		if (list[i].dir) {
			if (depth < 3) collect_games(p, depth + 1, paths, n, cap);
			continue;
		}
		if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *paths = realloc(*paths, *cap * sizeof(**paths)); }
		snprintf((*paths)[(*n)++], 512, "%s", p);
	}
	free(list);
}

static void all_covers(void)
{
	char (*paths)[512] = NULL;
	int n = 0, cap = 0, cur = 0, waiting = 0, done = 0;
	int got = 0, had = 0, missing = 0, noid = 0, failed = 0;
	char id[16] = "";
	uint32_t waitStart = 0;
	int scanned = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		draw_background();
		int back = draw_header("Download all covers", 1);
		if (!scanned) {
			text(fontItem, 640, 330, "Looking for games...", WHITE, 1);
		} else if (!n) {
			text(fontItem, 640, 330, "No games in this folder.", GREY, 1);
		} else {
			char m[160];
			float f = (float)cur / n;
			snprintf(m, sizeof(m), done ? tr("Finished: %d games") : tr("Game %d of %d"), done ? n : cur + 1, n);
			text(fontItem, 640, 260, m, WHITE, 1);
			SDL_Color bg = th(26, 32, 60), fg = th(0, 140, 220);
			rrect(240, 300, 800, 26, 13, bg, 255);
			rrect(240, 300, (int)(800 * (f < 0.02f ? 0.02f : f)), 26, 13, fg, 255);
			snprintf(m, sizeof(m), tr("Downloaded %d   Already had %d   Not in the repository %d"), got, had, missing);
			text(fontSmall, 640, 370, m, GREY, 1);
			snprintf(m, sizeof(m), tr("Without a game ID %d   Failed %d"), noid, failed);
			text(fontSmall, 640, 405, m, DIM, 1);
			if (!done) {
				char nm[96];
				pretty_name(paths[cur], nm, sizeof(nm));
				text(fontSmall, 640, 460, nm, DIM, 1);
			}
		}
		draw_footer(done || !n ? NULL : "B: stop");
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) break;
		if (!scanned) {
			if (src_effective() != 1) collect_games(cfg.games_dir, 0, &paths, &n, &cap);
			if (src_effective() != 0) collect_games(cfg.games_dir_usb, 0, &paths, &n, &cap);
			scanned = 1;
			continue;
		}
		if (done || !n) continue;
		/* one step per frame */
		if (!waiting) {
			if (!covers_detect_id(paths[cur], id) || !id[0]) { noid++; cur++; }
			else if (covers_have(id)) { had++; cur++; }
			else if (covers_missing(id)) { missing++; cur++; }
			else { covers_request(id); waiting = 1; waitStart = SDL_GetTicks(); }
		} else {
			int st = covers_status(id);
			if (covers_have(id)) { got++; waiting = 0; cur++; }
			else if (covers_missing(id)) { missing++; waiting = 0; cur++; }
			else if (st != 1 || SDL_GetTicks() - waitStart > 60000) { failed++; waiting = 0; cur++; }
		}
		if (cur >= n) { done = 1; sfx_play(SFX_OK); }
	}
	free(paths);
}

/* ------------------------------------------------------------------------ */
/* image picker (custom covers)                                             */
/* ------------------------------------------------------------------------ */
static int has_image_ext(const char *n)
{
	const char *d = strrchr(n, '.');
	return d && (!strcasecmp(d, ".png") || !strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg"));
}

static int cmp_name_desc(const void *a, const void *b) { return strcmp((const char *)b, (const char *)a); }

/* lists the images in dir whose name starts with prefix (may be ""),
 * newest first; returns 1 with the chosen path */
static int pick_image(const char *title, const char *dir, const char *prefix, char *out, size_t n)
{
	enum { MAXI = 200 };
	static char names[MAXI][256];
	const char *labels[MAXI];
	int count = 0;
	DIR *d = opendir(dir);
	if (d) {
		struct dirent *e;
		size_t pl = strlen(prefix);
		while ((e = readdir(d)) && count < MAXI)
			if (e->d_name[0] != '.' && has_image_ext(e->d_name) && !strncmp(e->d_name, prefix, pl))
				snprintf(names[count++], 256, "%s", e->d_name);
		closedir(d);
	}
	qsort(names, count, sizeof(names[0]), cmp_name_desc);
	for (int i = 0; i < count; i++) labels[i] = names[i];
	list_t L = { 0, 0 };
	SDL_Texture *prev = NULL;
	int prevFor = -1;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		int pick = -1;
		if (count) {
			if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : count - 1;
			if (b & WIIU_BTN_DOWN) L.sel = L.sel < count - 1 ? L.sel + 1 : 0;
			if (b & WIIU_BTN_A) pick = L.sel;
		}
		draw_background();
		int back = draw_header(title, 1);
		if (!count) {
			text(fontItem, 640, 330, "No images here.", GREY, 1);
			text(fontSmall, 640, 380, dir + strlen(WIIU_SD_ROOT), DIM, 1);
		} else {
			listW = 640;
			int t = list_run(&L, labels, NULL, count, 0, NULL, NULL);
			listW = 1060;
			if (t >= 0) pick = t;
			/* preview of the selected image */
			if (prevFor != L.sel) {
				char p[768];
				if (prev) SDL_DestroyTexture(prev);
				prev = NULL;
				snprintf(p, sizeof(p), "%s/%s", dir, names[L.sel]);
				SDL_Surface *sf = IMG_Load(p);
				if (sf) { prev = SDL_CreateTextureFromSurface(R, sf); SDL_FreeSurface(sf); }
				prevFor = L.sel;
			}
			if (prev) {
				int w, h;
				SDL_QueryTexture(prev, NULL, NULL, &w, &h);
				int bw = 400, bh = h * bw / (w ? w : 1);
				if (bh > 400) { bh = 400; bw = w * bh / (h ? h : 1); }
				SDL_Rect r = { 790 + (420 - bw) / 2, 140 + (420 - bh) / 2, bw, bh };
				SDL_RenderCopy(R, prev, NULL, &r);
			}
		}
		draw_footer(NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) { if (prev) SDL_DestroyTexture(prev); return 0; }
		if (pick >= 0) {
			snprintf(out, n, "%s/%s", dir, names[pick]);
			if (prev) SDL_DestroyTexture(prev);
			return 1;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* game options (- in the game browser)                                     */
/* ------------------------------------------------------------------------ */
/* which collections a game is in; "New collection..." makes one with it */
static void game_collections(entry_t *e, const char *dir)
{
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", dir, e->name);
	list_t L = { 0, 0 };
	transition();
	for (;;) {
		int n = emu_coll_count();
		const char *labels[66], *values[66];
		for (int i = 0; i < n && i < 64; i++) {
			labels[i] = emu_coll_name(i);
			values[i] = emu_coll_has(labels[i], full) ? "Yes" : "No";
		}
		labels[n] = "New collection...";
		values[n] = NULL;
		int rows = n + 1;
		if (L.sel >= rows) L.sel = rows - 1;
		poll_touch();
		uint32_t b = poll_buttons();
		int act = -1;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : rows - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < rows - 1 ? L.sel + 1 : 0;
		if (b & WIIU_BTN_A) act = L.sel;
		draw_background();
		int back = draw_header("Collections", 1);
		int t = list_run(&L, labels, values, rows, 0, NULL, NULL);
		if (t >= 0) act = t;
		draw_footer("A: add to / remove from the collection");
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (act < 0) continue;
		if (act < n) {
			char nm[64];
			snprintf(nm, sizeof(nm), "%s", emu_coll_name(act));
			emu_coll_toggle(nm, full);
			if (cfg.regionf >= 5 + emu_coll_count()) cfg.regionf = 0;   /* its last game was removed */
		} else {
			char nm[64];
			if (text_prompt("Collection name", "", nm, sizeof(nm)) && nm[0]) {
				for (char *c = nm; *c; c++) if (*c == '|') *c = '/';
				if (!emu_coll_has(nm, full)) emu_coll_toggle(nm, full);
				status("Added to the collection.");
			}
			transition();
		}
	}
}

/* the game's details: ID, region, size, play time, saves, cheats, patch */
static long long dir_size_prefix(const char *dir, const char *prefix)
{
	long long total = 0;
	size_t pl = strlen(prefix);
	struct stat st;
	DIR *d = opendir(dir);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		char p[768];
		if (strncmp(e->d_name, prefix, pl) || e->d_name[0] == '.') continue;
		const char *x = strrchr(e->d_name, '.');
		if (x && (!strcasecmp(x, ".ppf") || !strcasecmp(x, ".srm") || !strcasecmp(x, ".m3u") || !strcasecmp(x, ".cht"))) continue;
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		if (stat(p, &st) == 0 && !S_ISDIR(st.st_mode)) total += st.st_size;
	}
	closedir(d);
	return total;
}

static int file_there(const char *fmt, const char *a, const char *b)
{
	char p[768];
	struct stat st;
	snprintf(p, sizeof(p), fmt, a, b);
	return stat(p, &st) == 0;
}

static void game_info(entry_t *e, const char *dir)
{
	enum { I_ID, I_REGION, I_DISCS, I_SIZE, I_TIME, I_LAST, I_STATES, I_RESUME, I_CARD, I_CHEATS, I_PATCH, I_OWN, I_COUNT };
	const char *labels[I_COUNT] = { "Game ID", "Region", "Discs", "Size", "Play time", "Last played",
		"Save states", "Continue where you left off", "Memory card", "Cheats", "Translation patch", "Own settings" };
	static char v[I_COUNT][96];
	const char *values[I_COUNT];
	char key[256], title[96], full[768];
	static uint8_t card[MC_SIZE];
	entry_key(e->name, e->discs, key, sizeof(key));
	pretty_name(key, title, sizeof(title));
	snprintf(full, sizeof(full), "%s/%s", dir, e->name);
	if (!e->idTried) { e->idTried = 1; if (!covers_detect_id(full, e->id)) e->id[0] = 0; }

	snprintf(v[I_ID], 96, "%s", e->id[0] ? e->id : tr("Unknown"));
	{ int r = entry_region(e->name, e->id); snprintf(v[I_REGION], 96, "%s", tr(r == 1 ? "USA" : r == 2 ? "Europe" : r == 3 ? "Japan" : "Unknown")); }
	snprintf(v[I_DISCS], 96, "%d", e->discs > 1 ? e->discs : 1);
	{
		long long sz = dir_size_prefix(dir, key);
		if (sz >= 1024LL * 1024 * 1024) snprintf(v[I_SIZE], 96, "%.2f GB", sz / (1024.0 * 1024 * 1024));
		else snprintf(v[I_SIZE], 96, "%lld MB", sz / (1024 * 1024));
	}
	long long last = 0;
	long secs = emu_playtime(key, &last);
	if (secs) fmt_playtime(secs, v[I_TIME], 96); else snprintf(v[I_TIME], 96, "%s", tr("Never played"));
	if (last) {
		time_t t = (time_t)last;
		struct tm *tm = localtime(&t);
		if (tm) snprintf(v[I_LAST], 96, "%02d/%02d/%04d %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
		else v[I_LAST][0] = 0;
	} else snprintf(v[I_LAST], 96, "-");
	{
		int n = 0;
		for (int i = 0; i < 10; i++) {
			char p[600];
			struct stat st;
			snprintf(p, sizeof(p), "%s/%s.state%d", WIIU_STATE_DIR, key, i);
			if (stat(p, &st) == 0) n++;
		}
		snprintf(v[I_STATES], 96, tr("%d of 10 slots"), n);
	}
	snprintf(v[I_RESUME], 96, "%s", tr(file_there("%s/%s.auto", WIIU_STATE_DIR, key) ? "Yes" : "No"));
	{
		char p[600];
		snprintf(p, sizeof(p), "%s/%s.srm", WIIU_SAVE_DIR, key);
		if (mc_load(p, card)) snprintf(v[I_CARD], 96, tr("%d / 15 used"), 15 - mc_free_blocks(card));
		else snprintf(v[I_CARD], 96, "%s", tr("None yet"));
	}
	snprintf(v[I_CHEATS], 96, "%s", tr(file_there("%s/cheats/%s.cht", WIIU_APP_DIR, key) ||
	                                  (e->id[0] && file_there("%s/cheats/%s.cht", WIIU_APP_DIR, e->id)) ? "Yes" : "No"));
	snprintf(v[I_PATCH], 96, "%s", tr(file_there("%s/patches/%s.ppf", WIIU_APP_DIR, key) || file_there("%s/%s.ppf", dir, key) ||
	                                 (e->id[0] && file_there("%s/patches/%s.ppf", WIIU_APP_DIR, e->id)) ? "Yes" : "No"));
	snprintf(v[I_OWN], 96, "%s", tr(file_there("%s/gamesettings/%s.cfg", WIIU_APP_DIR, key) ? "Yes" : "No"));
	for (int i = 0; i < I_COUNT; i++) values[i] = v[i];

	list_t L = { 0, 0 };
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : I_COUNT - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < I_COUNT - 1 ? L.sel + 1 : 0;
		draw_background();
		int back = draw_header(title, 1);
		listW = 700;
		list_run(&L, labels, values, I_COUNT, 0, NULL, NULL);
		listW = 1060;
		draw_cover(ecover(e), NULL);
		draw_footer(NULL);
		present();
		if (back || (b & (WIIU_BTN_B | WIIU_BTN_A)) || wiiu_quit) return;
	}
}

static void game_options(entry_t *e, const char *dir)
{
	enum { O_INFO, O_COLL, O_ID, O_SHOT, O_IMAGE, O_REMOVE, O_RESETTIME, O_COUNT };
	const char *labels[O_COUNT] = { "Information...", "Collections...", "Type the game ID...", "Cover from a screenshot...",
		"Cover from an image in covers/import...", "Remove custom cover", "Reset play time" };
	const char *values[O_COUNT] = { 0 };
	char key[256], full[768], title[96], img[768], pt[48];
	entry_key(e->name, e->discs, key, sizeof(key));
	snprintf(full, sizeof(full), "%s/%s", dir, e->name);
	pretty_name(key, title, sizeof(title));
	list_t L = { 0, 0 };
	wiiu_mkdirs(WIIU_COVER_DIR "/import");
	transition();
	for (;;) {
		long long last = 0;
		long secs = emu_playtime(key, &last);
		values[O_ID] = e->id[0] ? e->id : "?";
		values[O_INFO] = NULL;
		fmt_playtime(secs, pt, sizeof(pt));
		values[O_RESETTIME] = secs ? pt : NULL;
		values[O_REMOVE] = NULL;
		poll_touch();
		uint32_t b = poll_buttons();
		int act = -1;
		if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : O_COUNT - 1;
		if (b & WIIU_BTN_DOWN) L.sel = L.sel < O_COUNT - 1 ? L.sel + 1 : 0;
		if (b & WIIU_BTN_A) act = L.sel;
		draw_background();
		int back = draw_header(title, 1);
		int t = list_run(&L, labels, values, O_COUNT, 0, NULL, NULL);
		if (t >= 0) act = t;
		draw_footer("Covers you choose here replace the downloaded ones");
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		switch (act) {
		case O_INFO:
			game_info(e, dir);
			transition();
			break;
		case O_COLL:
			game_collections(e, dir);
			transition();
			break;
		case O_ID: {
			char in[32], id[16];
			if (text_prompt("Game ID (e.g. SLUS-00594)", e->id, in, sizeof(in))) {
				if (covers_normalise_id(in, id)) {
					covers_remember_id(full, id);
					snprintf(e->id, sizeof(e->id), "%s", id);
					e->idTried = 1;
					covers_request(id);
					status("Game ID saved. The cover is downloaded in the background.");
				} else status("That is not a game ID (it looks like SLUS-00594).");
			}
			transition();
			break;
		}
		case O_SHOT:
		case O_IMAGE: {
			char prefix[260];
			snprintf(prefix, sizeof(prefix), "%s-", key);
			int ok = act == O_SHOT ? pick_image("Screenshots", WIIU_APP_DIR "/screenshots", prefix, img, sizeof(img))
			                       : pick_image("covers/import", WIIU_COVER_DIR "/import", "", img, sizeof(img));
			if (ok) {
				status(covers_set_custom(key, img) ? "Cover changed." : "Could not copy the image.");
				e->coverChecked = 0;
			}
			transition();
			break;
		}
		case O_REMOVE:
			covers_remove_custom(key);
			e->coverChecked = 0;
			status("Custom cover removed.");
			break;
		case O_RESETTIME:
			emu_playtime_reset(key);
			status("Play time reset.");
			break;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* memory card manager                                                      */
/* ------------------------------------------------------------------------ */
#define MAXCARDS 300
static char cardNames[MAXCARDS][256];

static int is_card_file(const char *n)
{
	const char *d = strrchr(n, '.');
	return d && (!strcasecmp(d, ".srm") || !strcasecmp(d, ".mcr") || !strcasecmp(d, ".mcd") || !strcasecmp(d, ".mc"));
}

/* card files in saves/ that are real 128 KB cards; returns the count */
static int scan_cards(void)
{
	int n = 0;
	struct stat st;
	DIR *d = opendir(WIIU_SAVE_DIR);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < MAXCARDS) {
		char p[600];
		if (e->d_name[0] == '.' || !is_card_file(e->d_name)) continue;
		snprintf(p, sizeof(p), "%s/%s", WIIU_SAVE_DIR, e->d_name);
		if (stat(p, &st) == 0 && st.st_size == MC_SIZE) snprintf(cardNames[n++], 256, "%s", e->d_name);
	}
	closedir(d);
	qsort(cardNames, n, sizeof(cardNames[0]), (int (*)(const void *, const void *))strcasecmp);
	return n;
}

/* pick a card; skip = a file name not to offer (the source of a copy) */
static int pick_card(const char *title, const char *skip, char *out, size_t on)
{
	int n = scan_cards(), m = 0;
	const char *labels[MAXCARDS];
	static char shown[MAXCARDS][256];
	for (int i = 0; i < n; i++) {
		if (skip && !strcmp(cardNames[i], skip)) continue;
		snprintf(shown[m], 256, "%s", cardNames[i]);
		labels[m] = shown[m];
		m++;
	}
	list_t L = { 0, 0 };
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		int pick = -1;
		if (m) {
			if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : m - 1;
			if (b & WIIU_BTN_DOWN) L.sel = L.sel < m - 1 ? L.sel + 1 : 0;
			if (b & WIIU_BTN_A) pick = L.sel;
		}
		draw_background();
		int back = draw_header(title, 1);
		if (m) { int t = list_run(&L, labels, NULL, m, 0, NULL, NULL); if (t >= 0) pick = t; }
		else text(fontItem, 640, 360, "No other memory cards.", GREY, 1);
		draw_footer(NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return 0;
		if (pick >= 0) { snprintf(out, on, "%s", shown[pick]); return 1; }
	}
}

/* One memory card like the PlayStation's own screen: the 15 blocks as a grid of
 * big, animated icons, with the selected save's details beside it. */
static void card_view(const char *file)
{
	enum { COLS = 5, CW = 140, GAP = 10, GX = 60, GY = 128 };
	static uint8_t card[MC_SIZE], other[MC_SIZE];
	static mc_save_t saves[15];
	SDL_Texture *icons[15][3] = { { 0 } };
	int owner[15];                         /* block cell -> index in saves, or -1 */
	int part[15];                          /* position of the cell in its save (0 = the first block) */
	char path[600], title[96], inuse[600];
	snprintf(path, sizeof(path), "%s/%s", WIIU_SAVE_DIR, file);
	pretty_name(file, title, sizeof(title));
	int locked = emu_memcard_path(inuse, sizeof(inuse)) && !strcmp(inuse, path);
	int n = 0, cell = 0, reload = 1;
	uint32_t delArm = 0;
	transition();
	for (;;) {
		if (reload) {
			for (int i = 0; i < 15; i++)
				for (int f = 0; f < 3; f++) if (icons[i][f]) { SDL_DestroyTexture(icons[i][f]); icons[i][f] = NULL; }
			n = mc_load(path, card) ? mc_list(card, saves, 15) : 0;
			for (int c = 0; c < 15; c++) { owner[c] = -1; part[c] = 0; }
			for (int i = 0; i < n; i++) {
				int fr = saves[i].frames > 0 ? saves[i].frames : 1;
				for (int f = 0; f < fr; f++) {
					SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(saves[i].anim[f], 16, 16, 32, 64, SDL_PIXELFORMAT_ARGB8888);
					if (sf) { icons[i][f] = SDL_CreateTextureFromSurface(R, sf); SDL_FreeSurface(sf); }
					if (icons[i][f]) {
						SDL_SetTextureScaleMode(icons[i][f], SDL_ScaleModeNearest);
						SDL_SetTextureBlendMode(icons[i][f], SDL_BLENDMODE_BLEND);
					}
				}
				for (int k = 0; k < saves[i].blocks; k++) {
					int c = saves[i].chain[k] - 1;
					if (c >= 0 && c < 15) { owner[c] = i; part[c] = k; }
				}
			}
			if (cell < 0 || cell > 14) cell = 0;
			reload = 0;
		}
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & WIIU_BTN_LEFT)  { if (cell % COLS > 0) cell--; }
		if (b & WIIU_BTN_RIGHT) { if (cell % COLS < COLS - 1) cell++; }
		if (b & WIIU_BTN_UP)    { if (cell >= COLS) cell -= COLS; }
		if (b & WIIU_BTN_DOWN)  { if (cell + COLS < 15) cell += COLS; }

		draw_background();
		int back = draw_header(title, 1);
		{
			char info[96];
			snprintf(info, sizeof(info), tr("%d of 15 blocks free"), mc_free_blocks(card));
			header_sub(title, info, GREY);
		}
		uint32_t tick = SDL_GetTicks();
		/* the grid */
		for (int c = 0; c < 15; c++) {
			int x = GX + (c % COLS) * (CW + GAP), y = GY + (c / COLS) * (CW + GAP);
			if (hover_in(x, y, CW, CW)) cell = c;
			if (tap_in(x, y, CW, CW)) cell = c;
			int o = owner[c];
			int mine = o >= 0 && cell >= 0 && owner[cell] == o;          /* a block of the selected save */
			rrect(x, y, CW, CW, 16, th(14, 18, 40), o >= 0 ? 215 : 130);
			if (o >= 0) {
				int fr = saves[o].frames > 0 ? saves[o].frames : 1;
				SDL_Texture *t = icons[o][(tick / 260) % fr];
				if (t) {
					SDL_Rect d = { x + (CW - 96) / 2, y + (CW - 96) / 2, 96, 96 };
					SDL_SetTextureAlphaMod(t, part[c] == 0 ? 255 : 120);   /* the next blocks of a long save are dimmed */
					SDL_RenderCopy(R, t, NULL, &d);
				}
				if (part[c] > 0) text(fontSmall, x + CW - 14, y + CW - 14, "...", GREY, 2);
			} else {
				text(fontSmall, x + CW / 2, y + CW / 2, "-", DIM, 1);
			}
			if (c == cell) outline(x - 2, y - 2, CW + 4, CW + 4, th(0, 140, 220), 255, 4);
			else if (mine) outline(x, y, CW, CW, th(0, 140, 220), 140, 2);
		}
		/* the selected save, on the right */
		{
			const int PX = GX + COLS * (CW + GAP) + 20, PY = GY, PW = 1280 - PX - 40, PH = 3 * (CW + GAP) - GAP;
			rrect(PX, PY, PW, PH, 18, th(14, 18, 40), 215);
			int o = cell >= 0 ? owner[cell] : -1;
			if (o >= 0) {
				int fr = saves[o].frames > 0 ? saves[o].frames : 1;
				SDL_Texture *t = icons[o][(tick / 260) % fr];
				if (t) {
					SDL_Rect d = { PX + PW / 2 - 64, PY + 24, 128, 128 };
					SDL_SetTextureAlphaMod(t, 255);
					SDL_RenderCopy(R, t, NULL, &d);
				}
				text_wrap(fontItem, PX + PW / 2, PY + 190, PW - 40, saves[o].title, WHITE);
				char line[96];
				const char *reg = saves[o].fname[1] == 'A' ? "USA" : saves[o].fname[1] == 'E' ? "Europe" : saves[o].fname[1] == 'I' ? "Japan" : "";
				snprintf(line, sizeof(line), "%.10s%s%s", saves[o].fname + 2, reg[0] ? "   \xc2\xb7   " : "", reg);
				text(fontSmall, PX + PW / 2, PY + PH - 90, line, GREY, 1);
				snprintf(line, sizeof(line), tr(saves[o].blocks > 1 ? "%d blocks" : "%d block"), saves[o].blocks);
				text(fontSmall, PX + PW / 2, PY + PH - 56, line, GREY, 1);
			} else {
				if (n) text(fontItem, PX + PW / 2, PY + PH / 2 - 14, "Free block", GREY, 1);
				else text_wrap(fontItem, PX + PW / 2, PY + PH / 2 - 30, PW - 40, "This memory card is empty.", GREY);   /* breaks into lines to fit the panel */
			}
		}
		draw_footer(locked ? "This card belongs to the game that is running: close the game to change it" :
		            n ? "Y: delete   X: copy to another card" : NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) break;
		int sel = cell >= 0 ? owner[cell] : -1;
		if (!n || locked || sel < 0) continue;
		if (b & WIIU_BTN_Y) {
			if (delArm && SDL_GetTicks() - delArm < 3000) {
				mc_delete(card, saves[sel].first);
				status(mc_save(path, card) ? "Save deleted." : "Could not write the memory card.");
				delArm = 0;
				reload = 1;
			} else {
				delArm = SDL_GetTicks();
				status("Press Y again to delete this save.");
			}
		}
		if (b & WIIU_BTN_X) {
			char target[256], tpath[600];
			if (pick_card("Copy to which card?", file, target, sizeof(target))) {
				snprintf(tpath, sizeof(tpath), "%s/%s", WIIU_SAVE_DIR, target);
				int busy = emu_memcard_path(inuse, sizeof(inuse)) && !strcmp(inuse, tpath);
				if (busy) status("That card belongs to the game that is running.");
				else if (!mc_load(tpath, other)) status("Could not read that memory card.");
				else {
					int r = mc_copy(card, saves[sel].first, other);
					status(r == -1 ? "Not enough free blocks on that card." :
					       r == -2 ? "That card already has this save." :
					       mc_save(tpath, other) ? "Save copied." : "Could not write the memory card.");
				}
			}
			transition();
		}
	}
	for (int i = 0; i < 15; i++)
		for (int f = 0; f < 3; f++) if (icons[i][f]) SDL_DestroyTexture(icons[i][f]);
}

static void memcards(void)
{
	static uint8_t card[MC_SIZE];
	int n = scan_cards();
	static char shown[MAXCARDS][128], usage[MAXCARDS][24];
	const char *labels[MAXCARDS], *values[MAXCARDS];
	for (int i = 0; i < n; i++) {
		pretty_name(cardNames[i], shown[i], sizeof(shown[i]));
		labels[i] = shown[i];
		char p[600];
		snprintf(p, sizeof(p), "%s/%s", WIIU_SAVE_DIR, cardNames[i]);
		if (mc_load(p, card)) snprintf(usage[i], sizeof(usage[i]), tr("%d / 15 used"), 15 - mc_free_blocks(card));
		else snprintf(usage[i], sizeof(usage[i]), "?");
		values[i] = usage[i];
	}
	list_t L = { 0, 0 };
	uint32_t delArm = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		int open = -1;
		if (n) {
			if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : n - 1;
			if (b & WIIU_BTN_DOWN) L.sel = L.sel < n - 1 ? L.sel + 1 : 0;
			if (b & WIIU_BTN_A) open = L.sel;
		}
		draw_background();
		int back = draw_header("Memory cards", 1);
		if (n) { int t = list_run(&L, labels, values, n, 0, NULL, NULL); if (t >= 0) open = t; }
		else {
			text(fontItem, 640, 330, "No memory cards yet.", GREY, 1);
			text_wrap(fontSmall, 640, 380, 1000, "Each game gets one in WiiUStation/saves. Cards from other emulators (.mcr, .mcd) can be copied there too.", DIM);
		}
		draw_footer(n ? "Y: delete card   Cards from other emulators (.mcr, .mcd) can be copied to WiiUStation/saves" : NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) return;
		if (n && (b & WIIU_BTN_Y)) {
			if (delArm && SDL_GetTicks() - delArm < 3000) {
				char p[600], inuse[600];
				snprintf(p, sizeof(p), "%s/%s", WIIU_SAVE_DIR, cardNames[L.sel]);
				delArm = 0;
				if (emu_memcard_path(inuse, sizeof(inuse)) && !strcmp(inuse, p))
					status("That card belongs to the game that is running.");
				else if (remove(p) != 0)
					status("Could not delete the memory card.");
				else {
					status("Memory card deleted.");
					n = scan_cards();
					for (int i = 0; i < n; i++) {
						pretty_name(cardNames[i], shown[i], sizeof(shown[i]));
						labels[i] = shown[i];
						snprintf(p, sizeof(p), "%s/%s", WIIU_SAVE_DIR, cardNames[i]);
						if (mc_load(p, card)) snprintf(usage[i], sizeof(usage[i]), tr("%d / 15 used"), 15 - mc_free_blocks(card));
						else snprintf(usage[i], sizeof(usage[i]), "?");
						values[i] = usage[i];
					}
					if (L.sel >= n) L.sel = n ? n - 1 : 0;
				}
			} else {
				delArm = SDL_GetTicks();
				status("Press Y again to delete this whole card.");
			}
		}
		if (open >= 0) {
			card_view(cardNames[open]);
			transition();
			char p[600];
			snprintf(p, sizeof(p), "%s/%s", WIIU_SAVE_DIR, cardNames[open]);
			if (mc_load(p, card)) snprintf(usage[open], sizeof(usage[open]), tr("%d / 15 used"), 15 - mc_free_blocks(card));
		}
	}
}

/* ------------------------------------------------------------------------ */
/* screenshot gallery                                                       */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
static void gallery(void)
{
	enum { MAXG = 400, COLS = 4, TW = 276, TH = 207, GX = 64, GY = 130, CW = 292, CH = 250, VIS = 2 };
	static char names[MAXG][256];
	const char *dir = WIIU_APP_DIR "/screenshots";
	int count = 0, sel = 0, view = 0;
	float scroll = 0;
	struct { int idx; SDL_Texture *t; uint32_t used; } cache[16];
	uint32_t clock = 0, delArm = 0;
	memset(cache, 0, sizeof(cache));
	for (int i = 0; i < 16; i++) cache[i].idx = -1;

	DIR *d = opendir(dir);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) && count < MAXG)
			if (e->d_name[0] != '.' && has_image_ext(e->d_name)) snprintf(names[count++], 256, "%s", e->d_name);
		closedir(d);
	}
	qsort(names, count, sizeof(names[0]), cmp_name_desc);    /* newest first */

	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (count) {
			int cols = view ? 1 : COLS;
			if (b & WIIU_BTN_LEFT)  sel = sel > 0 ? sel - 1 : 0;
			if (b & WIIU_BTN_RIGHT) sel = sel < count - 1 ? sel + 1 : sel;
			if (!view && (b & WIIU_BTN_UP))   sel = sel >= cols ? sel - cols : sel;
			if (!view && (b & WIIU_BTN_DOWN)) sel = sel + cols < count ? sel + cols : sel;
			if (b & WIIU_BTN_A) view = !view;
			if (b & WIIU_BTN_Y) {
				if (delArm && SDL_GetTicks() - delArm < 3000) {
					char p[768];
					snprintf(p, sizeof(p), "%s/%s", dir, names[sel]);
					remove(p);
					memmove(names[sel], names[sel + 1], (count - sel - 1) * sizeof(names[0]));
					count--;
					for (int i = 0; i < 16; i++) { if (cache[i].t) SDL_DestroyTexture(cache[i].t); cache[i].t = NULL; cache[i].idx = -1; }
					if (sel >= count) sel = count - 1;
					if (sel < 0) sel = 0;
					if (!count) view = 0;
					delArm = 0;
					status("Screenshot deleted.");
				} else {
					delArm = SDL_GetTicks();
					status("Press Y again to delete this screenshot.");
				}
			}
		}
		if (view && T.tapped) view = 0;
		if ((b & WIIU_BTN_B) && view) { view = 0; b &= ~WIIU_BTN_B; }

		draw_background();
		int back = draw_header("Gallery", 1);
		/* textures: a small cache, loaded on demand */
		#define GAL_TEX(i) ({ SDL_Texture *_t = NULL; int _v = 0; \
			for (int _k = 0; _k < 16; _k++) if (cache[_k].idx == (i)) { _t = cache[_k].t; cache[_k].used = ++clock; _v = 1; break; } \
			if (!_v) { int _o = 0; for (int _k = 1; _k < 16; _k++) if (cache[_k].used < cache[_o].used) _o = _k; \
				char _p[768]; snprintf(_p, sizeof(_p), "%s/%s", dir, names[i]); \
				SDL_Surface *_s = IMG_Load(_p); if (cache[_o].t) SDL_DestroyTexture(cache[_o].t); \
				cache[_o].t = _s ? SDL_CreateTextureFromSurface(R, _s) : NULL; if (_s) SDL_FreeSurface(_s); \
				cache[_o].idx = (i); cache[_o].used = ++clock; _t = cache[_o].t; } _t; })
		if (!count) {
			text(fontItem, 640, 330, "No screenshots yet.", GREY, 1);
			text_wrap(fontSmall, 640, 380, 1000, "Take them from the menu during a game, or with a hotkey.", DIM);
		} else if (view) {
			SDL_Texture *t = GAL_TEX(sel);
			if (t) {
				SDL_Rect r = { 1280 / 2 - 336, 110, 672, 504 };
				SDL_RenderCopy(R, t, NULL, &r);
			}
			char m[300];
			snprintf(m, sizeof(m), "%d / %d   %s", sel + 1, count, names[sel]);
			text(fontSmall, 640, 640, m, GREY, 1);
		} else {
			int rows = (count + COLS - 1) / COLS, selRow = sel / COLS;
			float target = scroll;
			if (selRow < target) target = selRow;
			if (selRow >= target + VIS) target = selRow - VIS + 1;
			scroll += (target - scroll) * 0.3f;
			SDL_RenderSetClipRect(R, &(SDL_Rect){ 0, GY - 8, 1280, 664 - GY + 8 });
			for (int r = (int)scroll; r <= (int)scroll + VIS && r < rows; r++)
				for (int c = 0; c < COLS; c++) {
					int i = r * COLS + c;
					if (i >= count) break;
					int x = GX + c * CW, y = GY + (int)((r - scroll) * CH);
					if (i == sel) rrect(x - 6, y - 6, TW + 12, TH + 12, 12, th(0, 140, 220), 255);
					SDL_Texture *t = GAL_TEX(i);
					SDL_Rect dr = { x, y, TW, TH };
					if (t) SDL_RenderCopy(R, t, NULL, &dr);
					else fill(x, y, TW, TH, th(26, 32, 60), 255);
					if (tap_in(x, y, TW, TH)) { if (sel == i) view = 1; sel = i; }
					else if (!view && hover_in(x, y, TW, TH)) sel = i;
				}
			SDL_RenderSetClipRect(R, NULL);
		}
		#undef GAL_TEX
		draw_footer(count ? (view ? "Left / Right: previous / next   Y: delete" : "A: view   Y: delete") : NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) break;
	}
	for (int i = 0; i < 16; i++) if (cache[i].t) SDL_DestroyTexture(cache[i].t);
}

/* ------------------------------------------------------------------------ */
/* home screen                                                              */
/* ------------------------------------------------------------------------ */
enum { H_RESUME, H_SAVE, H_LOAD, H_CHEATS, H_SHOT, H_RESET, H_DISC, H_RECENT,
       H_GAMES, H_SETTINGS, H_CREDITS, H_EXIT, H_GALLERY, H_CARDS, H_PICKDIR };
typedef struct { SDL_Rect r; int kind, arg; } hitem_t;

static void icon(int kind, float cx, float cy, float s, SDL_Color c)
{
	switch (kind) {
	case H_GAMES:     /* 2x2 tiles */
		for (int i = 0; i < 4; i++)
			rrect((int)(cx - s / 2 + (i & 1) * s * 0.55f), (int)(cy - s / 2 + (i >> 1) * s * 0.55f),
			      (int)(s * 0.45f), (int)(s * 0.45f), (int)(s * 0.1f), c, 255);
		break;
	case H_SETTINGS:  /* gear */
		for (int i = 0; i < 8; i++) {
			float a = i * 0.785398f;
			bar(cx + cosf(a) * s * 0.28f, cy + sinf(a) * s * 0.28f,
			    cx + cosf(a) * s * 0.5f, cy + sinf(a) * s * 0.5f, s * 0.16f, c);
		}
		ring(cx, cy, s * 0.36f, s * 0.14f, c);
		break;
	case H_CREDITS:   /* "i" in a circle */
		ring(cx, cy, s * 0.48f, s * 0.08f, c);
		rrect((int)(cx - s * 0.06f), (int)(cy - s * 0.08f), (int)(s * 0.12f), (int)(s * 0.36f), 3, c, 255);
		rrect((int)(cx - s * 0.06f), (int)(cy - s * 0.28f), (int)(s * 0.12f), (int)(s * 0.12f), 6, c, 255);
		break;
	case H_GALLERY:   /* picture: frame, sun, mountain */
		rframe((int)(cx - s * 0.5f), (int)(cy - s * 0.4f), (int)s, (int)(s * 0.8f), (int)(s * 0.1f), (int)(s * 0.08f), c, 255);
		{
			float m[6] = { cx - s * 0.36f, cy + s * 0.28f, cx - s * 0.06f, cy - s * 0.08f, cx + s * 0.2f, cy + s * 0.28f };
			poly(m, 3, c, 255);
			ring(cx + s * 0.2f, cy - s * 0.14f, s * 0.09f, s * 0.09f, c);
		}
		break;
	case H_CARDS: {   /* memory card: body with a notch and two stripes */
		float w = s * 0.62f, h = s * 0.86f, x0 = cx - w / 2, y0 = cy - h / 2;
		float q[10] = { x0, y0, x0 + w * 0.72f, y0, x0 + w, y0 + h * 0.2f, x0 + w, y0 + h, x0, y0 + h };
		poly(q, 5, c, 255);
		for (int i = 0; i < 2; i++)
			rrect((int)(x0 + w * 0.16f), (int)(y0 + h * (0.52f + i * 0.2f)), (int)(w * 0.68f), (int)(h * 0.08f), 2, th(30, 36, 64), 255);
		break;
	}
	case H_EXIT: {    /* power symbol */
		const int N = 20;
		for (int i = 2; i < N - 2; i++) {
			float a0 = -1.5707963f + i * 6.2831853f / N, a1 = a0 + 6.2831853f / N;
			float r = s * 0.42f, t = s * 0.1f;
			float q[8] = { cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r,
			               cx + cosf(a1) * (r - t), cy + sinf(a1) * (r - t), cx + cosf(a0) * (r - t), cy + sinf(a0) * (r - t) };
			poly(q, 4, c, 255);
		}
		bar(cx, cy - s * 0.5f, cx, cy - s * 0.05f, s * 0.1f, c);
		break;
	}
	default: break;
	}
}


/* ------------------------------------------------------------------------ */
/* save-state slots with thumbnails                                         */
/* ------------------------------------------------------------------------ */
static SDL_Texture *thumbs[10];

static void thumbs_free(void)
{
	for (int i = 0; i < 10; i++) if (thumbs[i]) { SDL_DestroyTexture(thumbs[i]); thumbs[i] = NULL; }
}

static void thumbs_load(void)
{
	char p[600];
	thumbs_free();
	for (int i = 0; i < 10; i++) {
		if (!emu_state_exists(i)) continue;
		emu_state_thumb(i, p, sizeof(p));
		SDL_Surface *s = IMG_Load(p);
		if (s) { thumbs[i] = SDL_CreateTextureFromSurface(R, s); SDL_FreeSurface(s); }
	}
}

static void slot_date(int i, char *out, size_t n)
{
	time_t t = (time_t)emu_state_time(i);
	struct tm *tm = t ? localtime(&t) : NULL;
	if (!tm) { snprintf(out, n, "%s", tr("Empty")); return; }
	snprintf(out, n, "%02d/%02d/%04d  %02d:%02d", tm->tm_mday, tm->tm_mon + 1,
	         tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
}

/* save = 1: pick a slot to save to; 0: pick a slot to load.
 * Left: the 10 slots with a small picture and the date. Right: a big
 * capture of the selected slot, so you can see where in the game it is.
 * Returns 1 when a state was loaded (go back to the game). */
static int slot_picker(int save)
{
	enum { LX = 40, LY = 118, LW = 420, RH = 52, PX = 500, PY = 118, PW = 740, PH = 518 };
	int sel = 0;
	float fy = 0;
	int finit = 0;
	char m[96], date[10][32];
	thumbs_load();
	for (int i = 0; i < 10; i++) slot_date(i, date[i], sizeof(date[i]));
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		int activate = 0;
		if (b & WIIU_BTN_UP)   sel = (sel + 9) % 10;
		if (b & WIIU_BTN_DOWN) sel = (sel + 1) % 10;
		if (b & WIIU_BTN_A) activate = 1;

		draw_background();
		int back = draw_header(save ? "Save state" : "Load state", 1);
		SDL_Color card = th(14, 18, 40), hl = th(0, 140, 220), slotc = th(26, 32, 60);

		/* ---- slot list ---- */
		for (int i = 0; i < 10; i++) {
			int y = LY + i * RH;
			if (tap_in(LX, y, LW, RH - 6)) { if (sel == i) activate = 1; sel = i; }
			else if (hover_in(LX, y, LW, RH - 6)) sel = i;
		}
		if (!finit) { fy = LY + sel * RH; finit = 1; }
		fy += (LY + sel * RH - fy) * 0.35f;
		rrect(LX, (int)fy, LW, RH - 6, 14, hl, 255);
		for (int i = 0; i < 10; i++) {
			int y = LY + i * RH, on = i == sel;
			if (!on) rrect(LX, y, LW, RH - 6, 14, card, 200);
			/* mini picture, 4:3 */
			SDL_Rect d = { LX + 8, y + 5, 48, 36 };
			if (thumbs[i]) SDL_RenderCopy(R, thumbs[i], NULL, &d);
			else rrect(d.x, d.y, d.w, d.h, 6, slotc, 255);
			snprintf(m, sizeof(m), tr("Slot %d"), i);
			text(fontSmall, LX + 72, y + (RH - 6) / 2, m, on ? WHITE : GREY, 0);
			text(fontSmall, LX + LW - 16, y + (RH - 6) / 2, date[i], on ? WHITE : DIM, 2);
		}

		/* ---- big preview of the selected slot ---- */
		rrect(PX, PY, PW, PH, 22, card, 255);
		{
			int ih = PH - 78, iw = ih * 4 / 3;           /* 4:3 picture */
			int ix = PX + (PW - iw) / 2, iy = PY + 14;
			if (thumbs[sel]) {
				SDL_Rect d = { ix, iy, iw, ih };
				SDL_SetTextureScaleMode(thumbs[sel], SDL_ScaleModeLinear);
				SDL_RenderCopy(R, thumbs[sel], NULL, &d);
			} else {
				rrect(ix, iy, iw, ih, 14, slotc, 255);
				text(fontItem, ix + iw / 2, iy + ih / 2,
				     emu_state_time(sel) ? "No picture for this slot" : "Empty slot", DIM, 1);
			}
			if (tap_in(ix, iy, iw, ih)) activate = 1;
			snprintf(m, sizeof(m), tr("Slot %d"), sel);
			text(fontItem, PX + 28, PY + PH - 32, m, WHITE, 0);
			text(fontSmall, PX + PW - 28, PY + PH - 32,
			     emu_state_time(sel) ? date[sel] : "Empty", GREY, 2);
		}

		if (save)
			draw_footer(emu_state_time(sel) ? "A: overwrite this slot" : "A: save here");
		else
			draw_footer(emu_state_time(sel) ? "A: load this slot" : "This slot is empty");
		present();

		if (back || (b & WIIU_BTN_B) || wiiu_quit) { thumbs_free(); return 0; }
		if (!activate) continue;
		if (save) {
			snprintf(m, sizeof(m), emu_state_save(sel) ? tr("Saved to slot %d.") : tr("Could not save to slot %d."), sel);
			status(m);
			thumbs_load();
			slot_date(sel, date[sel], sizeof(date[sel]));
		} else {
			if (emu_state_load(sel)) { thumbs_free(); return 1; }
			snprintf(m, sizeof(m), tr("Slot %d is empty."), sel);
			status(m);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* cheats                                                                   */
/* ------------------------------------------------------------------------ */
static void cheats_screen(void)
{
	const char **labels, **values;
	emu_cheats_poll();
	int n = emu_cheat_count();
	list_t L = { 0, 0 };
	labels = malloc((n + 1) * sizeof(char *));
	values = malloc((n + 1) * sizeof(char *));
	transition();
	for (;;) {
		if (!n) {                     /* the download may finish while this is open */
			emu_cheats_poll();
			int nn = emu_cheat_count();
			if (nn) {
				n = nn;
				labels = realloc(labels, (n + 1) * sizeof(char *));
				values = realloc(values, (n + 1) * sizeof(char *));
			}
		}
		for (int i = 0; i < n; i++) {
			labels[i] = emu_cheat_name(i);
			values[i] = emu_cheat_enabled(i) ? "On" : "Off";
		}
		poll_touch();
		uint32_t b = poll_buttons();
		int toggle = -1;
		if (n) {
			if (b & WIIU_BTN_UP)   L.sel = L.sel > 0 ? L.sel - 1 : n - 1;
			if (b & WIIU_BTN_DOWN) L.sel = L.sel < n - 1 ? L.sel + 1 : 0;
			if (b & (WIIU_BTN_A | WIIU_BTN_LEFT | WIIU_BTN_RIGHT)) toggle = L.sel;
		}
		draw_background();
		int back = draw_header("Cheats", 1);
		if (!n && emu_cheats_downloading()) {
			text(fontItem, 640, 330, "Downloading the cheats for this game...", GREY, 1);
			spinner(640, 420, 26, PS_BLUE);
		} else if (!n) {
			text(fontItem, 640, 330, "No cheats for this game.", GREY, 1);
			text_wrap(fontSmall, 640, 380, 1000, "Copy a libretro .cht file to WiiUStation/cheats,", DIM);
			text_wrap(fontSmall, 640, 414, 1000, "named like the game file or its ID (e.g. SCUS-94244.cht).", DIM);
		} else {
			int arrow = 0;
			int t = list_run(&L, labels, values, n, 1, &arrow, NULL);
			if (t >= 0 || arrow) toggle = L.sel;
		}
		draw_footer(n ? "Cheats are saved per game" : NULL);
		present();
		if (back || (b & WIIU_BTN_B) || wiiu_quit) break;
		if (toggle >= 0) emu_cheat_toggle(toggle);
	}
	free(labels);
	free(values);
}

/* ------------------------------------------------------------------------ */
/* "continue where you left off?"                                           */
/* ------------------------------------------------------------------------ */
static int ask_resume(void)
{
	int sel = 0;
	poll_buttons();
	T.down = T.tapped = 0;
	for (int frames = 0;; frames++) {
		/* buttons as wide as their (translated) text */
		int w1 = 0, w2 = 0;
		TTF_SizeUTF8(fontItem, tr("Continue"), &w1, NULL);
		TTF_SizeUTF8(fontItem, tr("Start over"), &w2, NULL);
		int bw = (w1 > w2 ? w1 : w2) + 64;
		if (bw < 220) bw = 220;
		if (bw > 320) bw = 320;
		SDL_Rect yes = { 640 - 20 - bw, 420, bw, 70 }, no = { 640 + 20, 420, bw, 70 };
		int cw = bw * 2 + 40 + 120 > 680 ? bw * 2 + 40 + 120 : 680;
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & (WIIU_BTN_LEFT | WIIU_BTN_RIGHT)) sel = !sel;
		draw_background();
		draw_header(NULL, 0);
		SDL_Color card = th(14, 18, 40), hl = th(0, 140, 220), btn = th(40, 46, 74);
		rrect(640 - cw / 2, 220, cw, 310, 26, card, 240);
		text(fontTitle, 640, 290, "Continue where you left off?", WHITE, 1);
		char nm[80];
		pretty_name(emu_game_name(), nm, sizeof(nm));
		text(fontSmall, 640, 345, nm, GREY, 1);
		rrect(yes.x, yes.y, yes.w, yes.h, 35, sel == 0 ? hl : btn, 255);
		rrect(no.x, no.y, no.w, no.h, 35, sel == 1 ? hl : btn, 255);
		char f1[64], f2[64];
		fit_text(fontItem, tr("Continue"), bw - 24, f1, sizeof(f1));
		fit_text(fontItem, tr("Start over"), bw - 24, f2, sizeof(f2));
		text(fontItem, yes.x + yes.w / 2, yes.y + yes.h / 2, f1, WHITE, 1);
		text(fontItem, no.x + no.w / 2, no.y + no.h / 2, f2, WHITE, 1);
		present();
		if (wiiu_quit) return 0;
		if (tap_in(yes.x, yes.y, yes.w, yes.h)) return 1;
		if (tap_in(no.x, no.y, no.w, no.h)) return 0;
		if (b & WIIU_BTN_A) return sel == 0;
		if (b & WIIU_BTN_B) return 0;
	}
}

/* "Which disc?" - returns 0-based disc or -1 */
static int choose_disc(const char *title, int ndisc, int current)
{
	int sel = current;
	poll_buttons();
	T.down = T.tapped = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & WIIU_BTN_LEFT)  sel = (sel + ndisc - 1) % ndisc;
		if (b & WIIU_BTN_RIGHT) sel = (sel + 1) % ndisc;
		draw_background();
		int back = draw_header(NULL, 1);
		SDL_Color card = th(14, 18, 40), hl = th(0, 140, 220), btn = th(40, 46, 74);
		int w = 170, gap = 20, tot = ndisc * w + (ndisc - 1) * gap;
		if (tot > 1140) { w = (1140 - (ndisc - 1) * gap) / ndisc; tot = 1140; }
		rrect(70, 200, 1140, 340, 26, card, 240);
		text(fontTitle, 640, 260, "Choose a disc", WHITE, 1);
		text(fontSmall, 640, 306, title, GREY, 1);
		for (int i = 0; i < ndisc; i++) {
			int x = 640 - tot / 2 + i * (w + gap);
			SDL_Rect r = { x, 350, w, 140 };
			if (tap_in(r.x, r.y, r.w, r.h)) { transition(); return i; }
			if (hover_in(r.x, r.y, r.w, r.h)) sel = i;
			rrect(r.x, r.y, r.w, r.h, 20, i == sel ? hl : btn, 255);
			ring(r.x + w / 2.0f, r.y + 54, 30, 18, WHITE);
			char d[16];
			snprintf(d, sizeof(d), tr("Disc %d"), i + 1);
			text(fontItem, r.x + w / 2, r.y + 112, d, WHITE, 1);
		}
		draw_footer(NULL);
		present();
		if (wiiu_quit || back || (b & WIIU_BTN_B)) return -1;
		if (b & WIIU_BTN_A) { transition(); return sel; }
	}
}

/* In game: switch to another disc of a multi-disc game, or add one. */
static int disc_picker_ingame(void)
{
	char path[512], nm[160];
	int c = emu_disk_count();
	if (c > 1) {
		pretty_name(emu_game_name(), nm, sizeof(nm));
		int d = choose_disc(nm, c, emu_disk_index());
		if (d < 0) return 0;
		if (d != emu_disk_index()) {
			emu_disk_set(d);
			char m[64];
			snprintf(m, sizeof(m), tr("Disc %d inserted."), d + 1);
			video_osd(m);
		}
		return 1;
	}
	if (browse("Change disc", path, sizeof(path), 0)) {
		if (emu_disk_append(path)) return 1;
		status("Disc change failed.");
	}
	return 0;
}

/* Starts a game: loading screen, load, optional resume. 1 = running. */
static int start_game(const char *path)
{
	loading_screen(pendingDisc[0] ? pendingDisc : path);
	int rc = pendingDisc[0] ? emu_load_game_disc(path, pendingIndex, pendingDisc) : emu_load_game(path);
	pendingDisc[0] = 0;
	pendingIndex = 0;
	if (rc != 0) {
		char m[256];
		snprintf(m, sizeof(m), tr("Could not load the game: %s (details in last_error.txt)"), emu_load_error());
		status(m);
		return 0;
	}
	if (cfg.resume && emu_state_exists(-1) && (autoResume || ask_resume())) {
		if (!emu_state_load(-1)) status("Could not continue: the saved state is unreadable.");
	}
	video_fade_in();
	return 1;
}

/* d-pad spatial navigation between the items */
static int nav(const hitem_t *it, int n, int cur, int dx, int dy)
{
	int best = cur;
	float bestScore = 1e9f;
	float cx = it[cur].r.x + it[cur].r.w / 2.0f, cy = it[cur].r.y + it[cur].r.h / 2.0f;
	for (int i = 0; i < n; i++) {
		if (i == cur) continue;
		float x = it[i].r.x + it[i].r.w / 2.0f - cx, y = it[i].r.y + it[i].r.h / 2.0f - cy;
		float along = x * dx + y * dy, across = fabsf(x * dy) + fabsf(y * dx);
		if (along <= 4) continue;
		float score = along + across * 2.2f;
		if (score < bestScore) { bestScore = score; best = i; }
	}
	return best;
}

static void pill(const SDL_Rect *r, const char *label, int focused)
{
	SDL_Color bg = th(40, 46, 74);
	if (!focused) rrect(r->x, r->y, r->w, r->h, r->h / 2, bg, 255);
	text(fontSmall, r->x + r->w / 2, r->y + r->h / 2, label, WHITE, 1);
}

/* icons for the in-game action cards */
static void action_icon(int kind, float cx, float cy, float s, SDL_Color c)
{
	switch (kind) {
	case H_SAVE: case H_LOAD: {     /* arrow into / out of a tray */
		float dir = kind == H_SAVE ? 1 : -1;
		bar(cx, cy - s * 0.42f * dir, cx, cy + s * 0.12f * dir, s * 0.12f, c);
		float tip = cy + s * 0.2f * dir;
		float tri[6] = { cx - s * 0.22f, tip - s * 0.18f * dir, cx + s * 0.22f, tip - s * 0.18f * dir, cx, tip };
		poly(tri, 3, c, 255);
		rrect((int)(cx - s * 0.45f), (int)(cy + s * 0.32f), (int)(s * 0.9f), (int)(s * 0.12f), 3, c, 255);
		break;
	}
	case H_CHEATS:                   /* star */
		star(cx, cy, s * 0.48f, c);
		break;
	case H_SHOT:                     /* camera */
		rrect((int)(cx - s * 0.48f), (int)(cy - s * 0.28f), (int)(s * 0.96f), (int)(s * 0.66f), 8, c, 255);
		rrect((int)(cx - s * 0.18f), (int)(cy - s * 0.42f), (int)(s * 0.36f), (int)(s * 0.2f), 4, c, 255);
		ring(cx, cy + s * 0.05f, s * 0.22f, s * 0.08f, th(14, 18, 40));
		break;
	case H_RESET: {                  /* circular arrow */
		const int N = 18;
		for (int i = 3; i < N; i++) {
			float a0 = i * 6.2831853f / N, a1 = a0 + 6.2831853f / N, r = s * 0.42f, t = s * 0.11f;
			float q[8] = { cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r,
			               cx + cosf(a1) * (r - t), cy + sinf(a1) * (r - t), cx + cosf(a0) * (r - t), cy + sinf(a0) * (r - t) };
			poly(q, 4, c, 255);
		}
		float ax = cx + s * 0.42f, ay = cy + s * 0.08f;
		float tri[6] = { ax - s * 0.2f, ay - s * 0.05f, ax + s * 0.12f, ay - s * 0.05f, ax - s * 0.04f, ay + s * 0.25f };
		poly(tri, 3, c, 255);
		break;
	}
	case H_DISC:                     /* CD */
		ring(cx, cy, s * 0.48f, s * 0.3f, c);
		ring(cx, cy, s * 0.12f, s * 0.05f, c);
		break;
	default: break;
	}
}



/* opening: the four shapes fly in and turn into a ring around the logo,
 * with a short chime. Any button or a touch skips it. */
static void intro(void)
{
	static const SDL_Color tint[4] = {
		{ 120, 235, 190, 255 }, { 255, 140, 140, 255 }, { 150, 190, 255, 255 }, { 255, 150, 235, 255 }
	};
	int chimed = 0;
	menu_fonts();
	draw_header(NULL, 0);              /* loads the logo */
	/* everything the animation needs is made before it starts, so it runs
	 * without a stall: the sounds and audio device, the caption's text and
	 * the first shapes drawn */
	sfx_prepare();
	bgNV = bgNI = 0;
	bg_ngon(640, 330, 60, 28, 7, 0, (SDL_Color){ 0, 0, 0, 0 });
	SDL_RenderGeometry(R, NULL, bgV, bgNV, bgIdx, bgNI);
	SDL_SetRenderDrawColor(R, 4, 6, 18, 255);
	SDL_RenderClear(R);
	SDL_RenderPresent(R);
	poll_buttons();
	const uint32_t T0 = SDL_GetTicks(), LEN = 2600;
	for (;;) {
		uint32_t now = SDL_GetTicks() - T0;
		float t = now / 1000.0f;
		if (now >= LEN || wiiu_quit) break;
		wiiu_input_update();
		uint32_t pr = wiiu_input_menu_pressed() | (wiiu_input_menu_held() & WIIU_BTN_B);
		if (pr & WIIU_BTN_B) introCancel = 1;
		if (pr) break;
		if (!chimed && now > 350) { sfx_play(SFX_INTRO); chimed = 1; }

		SDL_SetRenderDrawColor(R, 4, 6, 18, 255);
		SDL_RenderClear(R);
		/* soft light growing behind the logo */
		float glow = t < 1.0f ? t : 1.0f;
		bgNV = bgNI = 0;
		bg_glow(640, 330, 60 + 300 * glow, (SDL_Color){ 20, 60, 140, 255 }, 150 * glow);
		SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(R, NULL, bgV, bgNV, bgIdx, bgNI);
		/* the shapes: from the corners to their place on a circle, turning */
		bgNV = bgNI = 0;
		float k = t < 1.1f ? t / 1.1f : 1.0f;
		k = 1 - (1 - k) * (1 - k) * (1 - k);            /* ease out */
		for (int i = 0; i < 4; i++) {
			float sx = i == 0 || i == 3 ? -120 : 1400, sy = i < 2 ? -120 : 840;
			float a = -1.5707963f + i * 1.5707963f + t * 0.6f;
			float ex = 640 + cosf(a) * 390, ey = 330 + sinf(a) * 215;
			float x = sx + (ex - sx) * k, y = sy + (ey - sy) * k, rot = (1 - k) * 6 + t;
			SDL_Color c = tint[i];
			c.a = (Uint8)(200 * (k < 1 ? k : 1));
			float sz = 54, w = 7;
			switch (i) {
			case 0: bg_ngon(x, y, sz * 0.62f, 3, w, rot - 1.5707963f, c); break;
			case 1: bg_ngon(x, y, sz * 0.5f, 28, w, rot, c); break;
			case 2: bg_bar(x, y, sz, w, rot + 0.785398f, c); bg_bar(x, y, sz, w, rot - 0.785398f, c); break;
			default: bg_ngon(x, y, sz * 0.6f, 4, w, rot + 0.785398f, c); break;
			}
		}
		SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(R, NULL, bgV, bgNV, bgIdx, bgNI);
		/* the logo grows in */
		if (logoTex && t > 0.4f) {
			float p = (t - 0.4f) / 0.6f;
			if (p > 1) p = 1;
			float sc = 0.7f + 0.3f * (1 - (1 - p) * (1 - p));
			int h = (int)(200 * sc), w = logoW * h / logoH;
			SDL_Rect d = { 640 - w / 2, 330 - h / 2, w, h };
			SDL_SetTextureAlphaMod(logoTex, (Uint8)(255 * p));
			SDL_RenderCopy(R, logoTex, NULL, &d);
			SDL_SetTextureAlphaMod(logoTex, 255);
		}
		/* fade out at the end */
		if (now > LEN - 400) fill(0, 0, 1280, 720, (SDL_Color){ 0, 0, 0 }, (uint8_t)((now - (LEN - 400)) * 255 / 400));
		SDL_RenderPresent(R);
		pump_events();
		SDL_Delay(1);
	}
	poll_buttons();
}

/* is there any game in the games folder (or one folder below)? Checked
 * again whenever gamesChecked is cleared. */
static int gamesFound;
static int dir_has_games(const char *dir)
{
	int found = 0;
	DIR *d = opendir(fs_path(dir));
	if (!d) return 0;
	struct dirent *ent;
	while (!found && (ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (!(ent->d_type & DT_DIR)) { if (is_game_file(ent->d_name)) found = 1; continue; }
		char sub[768];
		snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name);
		DIR *d2 = opendir(sub);
		if (!d2) continue;
		struct dirent *e2;
		while ((e2 = readdir(d2)) != NULL)
			if (e2->d_name[0] != '.' && !(e2->d_type & DT_DIR) && is_game_file(e2->d_name)) { found = 1; break; }
		closedir(d2);
	}
	closedir(d);
	return found;
}
static int games_folder_ok(void)
{
	if (gamesChecked) return gamesFound;
	gamesChecked = 1;
	gamesFound = 0;
	if (!cfg.games_chosen) return 0;        /* a new installation: the home screen asks for the folder */
	int src = src_effective();
	gamesFound = (src != 1 && dir_has_games(cfg.games_dir)) || (src != 0 && dir_has_games(cfg.games_dir_usb));
	return gamesFound;
}

/* the folder pickers: the SD card (usb = 0) or the USB drive; 1 if a folder was chosen */
static int pick_folder(int usb)
{
	char dirp[512];
	if (!browse(usb ? "USB games folder" : usbfs_mounted() ? "SD games folder" : "Games folder", dirp, sizeof(dirp), usb ? 2 : 1)) return 0;
	if (usb) { strncpy(cfg.games_dir_usb, dirp, sizeof(cfg.games_dir_usb) - 1); cfg.games_dir_usb[sizeof(cfg.games_dir_usb) - 1] = 0; }
	else { strncpy(cfg.games_dir, dirp, sizeof(cfg.games_dir) - 1); cfg.games_dir[sizeof(cfg.games_dir) - 1] = 0; }
	browsePath[0] = 0;
	gamesChecked = 0;
	cfg.games_chosen = 1;
	settings_save();
	status("Games folder saved.");
	return 1;
}
static int pick_games_folder(void) { return pick_folder(0); }
static int pick_usb_folder(void) { return pick_folder(1); }

/* the first start: where are the games? (only asked when a USB drive is there) */
static int choose_source(void)
{
	int sel = 0;
	poll_buttons();
	T.down = T.tapped = 0;
	transition();
	for (;;) {
		poll_touch();
		uint32_t b = poll_buttons();
		if (b & WIIU_BTN_LEFT)  sel = (sel + 2) % 3;
		if (b & WIIU_BTN_RIGHT) sel = (sel + 1) % 3;
		draw_background();
		int back = draw_header(NULL, 1);
		SDL_Color card = th(14, 18, 40), hl = th(0, 140, 220), btn = th(40, 46, 74);
		static const char *const names[3] = { "SD card", "USB drive", "SD + USB" };
		rrect(70, 200, 1140, 340, 26, card, 240);
		text(fontTitle, 640, 260, "Where are your games?", WHITE, 1);
		text(fontSmall, 640, 306, "A USB drive was found.", GREY, 1);
		for (int i = 0; i < 3; i++) {
			SDL_Rect r = { 100 + i * 370, 350, 340, 140 };
			if (tap_in(r.x, r.y, r.w, r.h)) { transition(); return i; }
			if (hover_in(r.x, r.y, r.w, r.h)) sel = i;
			rrect(r.x, r.y, r.w, r.h, 20, i == sel ? hl : btn, 255);
			text(fontItem, r.x + r.w / 2, r.y + 70, names[i], WHITE, 1);
		}
		draw_footer(NULL);
		present();
		if (wiiu_quit || back || (b & WIIU_BTN_B)) return -1;
		if (b & WIIU_BTN_A) { transition(); return sel; }
	}
}

/* the home screen's "Choose games folder": with a USB drive, first where the games are */
static int pick_games_setup(void)
{
	if (!usbfs_mounted()) usbfs_mount();
	if (!usbfs_mounted()) return pick_games_folder();
	int src = choose_source();
	if (src < 0) return 0;
	int old = cfg.games_src, ok = 1;
	cfg.games_src = src;
	if (src != 1) ok = pick_folder(0);
	if (ok && src != 0) ok = pick_folder(1);
	if (!ok) cfg.games_src = old;
	settings_save();
	gamesChecked = 0;
	return ok;
}

int menu_run(void)
{
	hitem_t it[40];
	int sel = -1, n, lastLoaded = -1;
	float fx = 0, fy = 0, fw = 0, fh = 0;
	char path[512], m[128];

	menu_fonts();
	update_check_background();
	{
		if (!introShown) {
			introShown = 1;
			intro();
			/* "open the last game at start" (B during the opening skips it) */
			struct stat st;
			if (cfg.autostart && !introCancel && !emu_game_loaded() && emu_recent_count() > 0 &&
			    stat(emu_recent(0), &st) == 0) {
				char last[512];
				snprintf(last, sizeof(last), "%s", emu_recent(0));
				autoResume = 1;
				int ok = start_game(last);
				autoResume = 0;
				if (ok) return 1;
			}
		}
	}
	poll_buttons();
	T.down = T.tapped = 0;
	touchBlock = 1;
	transition();

	for (;;) {
		int loaded = emu_game_loaded();
		int nrec = emu_recent_count();
		n = 0;
		if (loaded != lastLoaded) { sel = -1; lastLoaded = loaded; }

		/* ---- layout ---- */
		int tileY, tileH, ntiles;
		if (loaded) {
			it[n++] = (hitem_t){ { 960, 190, 240, 76 }, H_RESUME, 0 };
			static const int acts[6] = { H_SAVE, H_LOAD, H_CHEATS, H_SHOT, H_RESET, H_DISC };
			for (int i = 0; i < 6; i++)
				it[n++] = (hitem_t){ { 60 + i * 196, 330, 180, 140 }, acts[i], 0 };
			tileY = 500; tileH = 120; ntiles = 3;
		} else {
			int rx = 60;
			for (int i = 0; i < nrec && i < 6; i++) {
				it[n++] = (hitem_t){ { rx, 168, 170, 170 }, H_RECENT, i };
				rx += 196;
			}
			tileY = 436; tileH = 190; ntiles = 6;
			/* no game played yet and none in the games folder: a button to
			 * choose the folder */
			if (!nrec && !games_folder_ok())
				it[n++] = (hitem_t){ { 420, 262, 440, 60 }, H_PICKDIR, 0 };
		}
		{
			/* in game: Games, Settings, Close game (no Credits) */
			static const int full[6] = { H_GAMES, H_CARDS, H_GALLERY, H_SETTINGS, H_CREDITS, H_EXIT };
			static const int game[3] = { H_GAMES, H_SETTINGS, H_EXIT };
			int w = ntiles == 6 ? 178 : 380, gap = ntiles == 6 ? 18 : 20;
			for (int i = 0; i < ntiles; i++)
				it[n++] = (hitem_t){ { 60 + i * (w + gap), tileY, w, tileH }, ntiles == 6 ? full[i] : game[i], 0 };
		}
		if (sel < 0 || sel >= n) {
			sel = 0;
			int want = loaded ? H_RESUME : nrec ? H_RECENT : games_folder_ok() ? H_GAMES : H_PICKDIR;
			for (int i = 0; i < n; i++) if (it[i].kind == want) { sel = i; break; }
			fx = it[sel].r.x; fy = it[sel].r.y; fw = it[sel].r.w; fh = it[sel].r.h;
		}
		if (wiiu_quit) return 0;

		/* ---- input ---- */
		poll_touch();
		uint32_t b = poll_buttons();
		int activate = 0;
		audio_music(cfg.menumusic && !loaded);          /* the menu music: only without a game */
		if (b & WIIU_BTN_LEFT)  sel = nav(it, n, sel, -1, 0);
		if (b & WIIU_BTN_RIGHT) sel = nav(it, n, sel, 1, 0);
		if (b & WIIU_BTN_UP)    sel = nav(it, n, sel, 0, -1);
		if (b & WIIU_BTN_DOWN)  sel = nav(it, n, sel, 0, 1);
		if (b & WIIU_BTN_A) activate = 1;
		if ((b & WIIU_BTN_B) && loaded) return 1;
		for (int i = 0; i < n; i++)
			if (!T.tapped && hover_in(it[i].r.x, it[i].r.y, it[i].r.w, it[i].r.h))
				sel = i;                                      /* the frame follows the pointer */
		for (int i = 0; i < n; i++)
			if (tap_in(it[i].r.x, it[i].r.y, it[i].r.w, it[i].r.h)) {
				sel = i;
				activate = 1;
				/* the frame jumps there instead of sliding across the screen */
				fx = it[i].r.x; fy = it[i].r.y; fw = it[i].r.w; fh = it[i].r.h;
			}

		/* ---- draw ---- */
		draw_background();
		draw_header(NULL, 0);
		{   /* a newer version on GitHub (checked in the background): a notice
		     * in the header; X or a tap on it opens the update screen */
			const char *upd = update_available();
			if (upd) {
				char msg[160];
				snprintf(msg, sizeof(msg), tr("Version %s is available."), upd);
				int tw = 0;
				TTF_SizeUTF8(fontSmall, msg, &tw, NULL);
				int pw = tw + 84, px = 640 - pw / 2;
				float pulse = 0.5f + 0.5f * sinf(SDL_GetTicks() / 300.0f);
				rrect(px, 32, pw, 42, 21, th(0, 140, 220), (uint8_t)(190 + 50 * pulse));
				wiiu_button(px + 26, 53, "X");
				text(fontSmall, px + 50, 53, msg, WHITE, 0);
				if ((b & WIIU_BTN_X) || tap_in(px, 32, pw, 42)) {
					sfx_play(SFX_OK);
					update_screen();
					if (quitAfterUpdate) return 0;
					transition();
					continue;
				}
			}
		}
		SDL_Color card = th(14, 18, 40);
		if (loaded) {
			rrect(50, 120, 1180, 180, 22, card, 220);
			cover_or_disc(emu_game_id(), 70, 135, 150);
			char nm[96], pl[64];
			pretty_name(emu_game_name(), nm, sizeof(nm));
			text(fontSmall, 245, 168, "NOW PLAYING", PS_BLUE, 0);
			text(fontTitle, 245, 214, nm, WHITE, 0);
			int np = emu_player_count();
			snprintf(pl, sizeof(pl), tr(np > 1 ? "%d players" : "1 player"), np);
			text(fontSmall, 245, 258, pl, DIM, 0);
		} else {
			text(fontItem, 60, 140, nrec ? "Recently played" : "Welcome", WHITE, 0);
			if (!nrec) {
				rrect(50, 168, 1180, 170, 22, card, 200);
				if (games_folder_ok()) {
					text(fontItem, 640, 250, "Tap Games to choose a game.", WHITE, 1);
				} else {
					text(fontItem, 640, 214, "Choose the folder with your games to start.", WHITE, 1);
				}
			}
		}

		/* animated focus frame */
		{
			SDL_Rect r = it[sel].r;
			/* the size changes at once; only the position glides */
			fx += (r.x - fx) * 0.35f; fy += (r.y - fy) * 0.35f;
			fw = r.w; fh = r.h;
			SDL_Color hl = th(0, 140, 220);
			int rad = it[sel].kind == H_RESUME ? (int)(fh / 2) : 18;
			rrect((int)fx - 6, (int)fy - 6, (int)fw + 12, (int)fh + 12, rad + 6, hl, 255);
		}

		for (int i = 0; i < n; i++) {
			SDL_Rect r = it[i].r;
			int f = i == sel;
			switch (it[i].kind) {
			case H_RESUME: {
				SDL_Color go = th(0, 120, 200);
				rrect(r.x, r.y, r.w, r.h, r.h / 2, go, 255);
				float tri[6] = { r.x + 40, r.y + 22, r.x + 40, r.y + r.h - 22, r.x + 66, r.y + r.h / 2.0f };
				poly(tri, 3, WHITE, 255);
				text(fontItem, r.x + 82, r.y + r.h / 2, "Resume", WHITE, 0);
				break;
			}
			case H_SAVE: case H_LOAD: case H_CHEATS: case H_SHOT: case H_RESET: case H_DISC: {
				static const char *lab[] = { "", "Save state", "Load state", "Cheats", "Screenshot", "Reset", "" };
				const char *l = lab[it[i].kind];
				if (it[i].kind == H_DISC) {
					if (emu_disk_count() > 1) snprintf(m, sizeof(m), tr("Disc %d of %d"), emu_disk_index() + 1, emu_disk_count());
					else snprintf(m, sizeof(m), "%s", tr("Change disc"));
					l = m;
				}
				rrect(r.x, r.y, r.w, r.h, 18, card, 255);
				action_icon(it[i].kind, r.x + r.w / 2.0f, r.y + 54, 46, f ? WHITE : GREY);
				{   /* long translations are cut to the card */
					char fl[96];
					(void)fl;
					marquee(fontSmall, r.x + r.w / 2, r.y + r.h - 30, r.w - 10, l, f ? WHITE : GREY, 1, f);
				}
				break;
			}
			case H_PICKDIR:
				rrect(r.x, r.y, r.w, r.h, r.h / 2, th(0, 140, 220), 255);
				{
					int tw = 0;
					TTF_SizeUTF8(fontItem, tr("Choose games folder"), &tw, NULL);
					text(fontItem, r.x + r.w / 2, r.y + r.h / 2, "Choose games folder", WHITE, 1);
				}
				break;
			case H_RECENT: {
				const char *p = emu_recent(it[i].arg);
				char id[16] = "", nm[96], sh[96], key[256];
				const char *sl = strrchr(p, '/');
				snprintf(key, sizeof(key), "%s", sl ? sl + 1 : p);
				if (strrchr(key, '.')) *strrchr(key, '.') = 0;
				if (!covers_custom_id(key, id)) covers_detect_id(p, id);
				cover_or_disc(id, r.x, r.y, r.w);
				pretty_name(p, nm, sizeof(nm));
				(void)sh;
				marquee(fontSmall, r.x + r.w / 2, r.y + r.h + 26, r.w - 4, nm, f ? WHITE : GREY, 1, f);
				break;
			}
			default: {
				const char *tl = it[i].kind == H_GAMES ? "Games" : it[i].kind == H_SETTINGS ? "Settings" :
				                 it[i].kind == H_CREDITS ? "Credits" : it[i].kind == H_GALLERY ? "Gallery" :
				                 it[i].kind == H_CARDS ? "Memory cards" : loaded ? "Close game" : "Exit";
				rrect(r.x, r.y, r.w, r.h, 18, card, 220);
				if (r.h > 150) {
					char fl[96];
					icon(it[i].kind, r.x + r.w / 2.0f, r.y + r.h / 2.0f - 22, 64, WHITE);
					int tw = 0;
					TTF_Font *tf = fontItem;
					if (TTF_SizeUTF8(fontItem, tr(tl), &tw, NULL) == 0 && tw > r.w - 16) tf = fontSmall;
					(void)fl;
					marquee(tf, r.x + r.w / 2, r.y + r.h - 34, r.w - 12, tl, WHITE, 1, f);
				} else {
					icon(it[i].kind, r.x + 64, r.y + r.h / 2.0f, 48, WHITE);
					text(fontItem, r.x + 116, r.y + r.h / 2, tl, WHITE, 0);
				}
				break;
			}
			}
		}
		draw_footer(loaded ? "In game: HOME / touch / L3+R3 = this menu" : NULL);
		present();

		if (!activate) continue;
		switch (it[sel].kind) {
		case H_RESUME:
			return 1;
		case H_SAVE:
			slot_picker(1);
			transition();
			break;
		case H_LOAD:
			if (slot_picker(0)) return 1;
			transition();
			break;
		case H_CHEATS:
			cheats_screen();
			transition();
			break;
		case H_SHOT:
			status(emu_screenshot() ? "Screenshot saved to WiiUStation/screenshots." : "Could not save the screenshot.");
			break;
		case H_RESET:
			emu_reset();
			return 1;
		case H_DISC:
			if (disc_picker_ingame()) return 1;
			transition();
			break;
		case H_RECENT:
			snprintf(path, sizeof(path), "%s", emu_recent(it[sel].arg));
			if (loaded && !strcmp(path, emu_game_path())) return 1;
			if (start_game(path)) return 1;
			transition();
			break;
		case H_PICKDIR:
			pick_games_setup();
			sel = -1;
			transition();
			break;
		case H_GAMES:
			if (browse("Games", path, sizeof(path), 0) && start_game(path)) return 1;
			transition();
			break;
		case H_SETTINGS:
			settings();
			if (quitAfterUpdate) return 0;
			transition();
			break;
		case H_CREDITS:
			credits();
			transition();
			break;
		case H_GALLERY:
			gallery();
			transition();
			break;
		case H_CARDS:
			memcards();
			transition();
			break;
		case H_EXIT:
			if (loaded) {                    /* Close game: back to this menu */
				emu_unload_game();
				video_set_game_id("");
				transition();
				break;
			}
			return 0;
		}
	}
}
