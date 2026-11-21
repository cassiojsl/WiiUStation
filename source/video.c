/*
 * WiiUStation - video output.
 *
 * Frames from PCSX-ReARMed (RGB565 or XRGB8888) are uploaded to a GX2
 * texture through SDL2's Wii U renderer, then scaled by the GPU to the TV
 * (720p/1080p) and to the GamePad at the same time.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <coreinit/memory.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "platform.h"
#include "libretro.h"

#define TEX_W 1024
#define TEX_H 512

volatile int wiiu_quit = 0;

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *frameTex;
static int           pixfmt = RETRO_PIXEL_FORMAT_RGB565;
static int           texFmt = -1;
static int           frameW = 320, frameH = 240;
static int           lastFilter = -1;
static int           haveFrame;
static unsigned      framesIn, framesNull;

static TTF_Font     *osdFont, *fpsBig, *fpsSmall;
static char          osdMsg[128];
static uint32_t      osdUntil;

void *video_open_font(int size)
{
	void *data = NULL;
	uint32_t len = 0;
	if (!OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &data, &len))
		return NULL;
	return TTF_OpenFontRW(SDL_RWFromConstMem(data, (int)len), 1, size);
}

/* Frames are always converted to opaque ARGB8888 before upload: that is
 * the texture path proven to work with SDL2's Wii U renderer (RGB565 gave
 * a black screen), and it forces alpha to 0xFF (XRGB8888 frames may carry
 * 0 in the unused byte). */
static uint32_t *conv;
static uint32_t lut565[0x10000];
static uint64_t convTicks;          /* time spent converting frames (performance readout) */

static void build_lut(void)
{
	for (int i = 0; i < 0x10000; i++) {
		uint32_t r = (i >> 11) & 0x1f, g = (i >> 5) & 0x3f, b = i & 0x1f;
		r = (r << 3) | (r >> 2);
		g = (g << 2) | (g >> 4);
		b = (b << 3) | (b >> 2);
		lut565[i] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
}

static void create_texture(void)
{
	if (frameTex) return;
	frameTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
	                             SDL_TEXTUREACCESS_STREAMING, TEX_W, TEX_H);
	SDL_SetTextureBlendMode(frameTex, SDL_BLENDMODE_NONE);
	if (!conv) conv = (uint32_t *)SDL_malloc(TEX_W * TEX_H * 4);
	build_lut();
	texFmt = 0;
	lastFilter = -1;
	haveFrame = 0;
}

int video_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
		return -1;
	window = SDL_CreateWindow("WiiUStation", SDL_WINDOWPOS_UNDEFINED,
	                          SDL_WINDOWPOS_UNDEFINED, 1280, 720, 0);
	if (!window) return -1;
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) return -1;
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	create_texture();
	TTF_Init();
	IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
	osdFont = video_open_font(26);
	fpsBig = video_open_font(30);
	fpsSmall = video_open_font(16);
	return 0;
}

void video_shutdown(void)
{
	if (osdFont) TTF_CloseFont(osdFont);
	if (fpsBig) TTF_CloseFont(fpsBig);
	if (fpsSmall) TTF_CloseFont(fpsSmall);
	TTF_Quit();
	if (frameTex) SDL_DestroyTexture(frameTex);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
}

void *video_renderer(void) { return renderer; }

void video_set_pixel_format(int fmt)
{
	pixfmt = fmt;
}

/* ---- auto zoom: remove the black bars the game draws above and below ----
 * Only the top and bottom are cropped. The borders are measured during the
 * first ~2 seconds of real picture, the smallest value seen is kept (so no
 * picture is ever cut), and then the crop is locked: no zooming in and out
 * with the scenes. The lock is kept per resolution (games switch between
 * 240 and 480 lines). If picture ever shows up inside the cropped area (an
 * FMV with bars followed by gameplay without them) the crop is released at
 * once and measured again. */
#define ZSLOTS 4
static struct {
	int w, h;
	int minT, minB, samples, locked;
	int lockT, lockB, bad;
} zs[ZSLOTS], *zc;
static int   zNext;
static float cropT, cropB;                 /* eased towards the lock */

static int px_dark(const void *row, unsigned x)
{
	unsigned r, g, b;
	if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
		uint32_t v = ((const uint32_t *)row)[x];
		r = (v >> 16) & 0xff; g = (v >> 8) & 0xff; b = v & 0xff;
	} else {
		uint16_t v = ((const uint16_t *)row)[x];
		r = (v >> 11) << 3; g = ((v >> 5) & 0x3f) << 2; b = (v & 0x1f) << 3;
	}
	return r + g + b < 16;          /* a real border is black; dark scenes are not */
}

static int row_dark(const void *data, size_t pitch, unsigned y, unsigned w)
{
	const uint8_t *row = (const uint8_t *)data + (size_t)y * pitch;
	for (unsigned i = 0; i < 48; i++)
		if (!px_dark(row, (i * w + w / 2) / 48 % w)) return 0;
	return 1;
}

static void measure_borders(const void *data, unsigned w, unsigned h, size_t pitch)
{
	unsigned t = 0, b = 0;
	if (!zc || zc->w != (int)w || zc->h != (int)h) {   /* new resolution */
		int i;
		for (i = 0; i < ZSLOTS; i++)
			if (zs[i].w == (int)w && zs[i].h == (int)h) break;
		if (i == ZSLOTS) {
			i = zNext++ % ZSLOTS;
			zs[i].w = w; zs[i].h = h;
			zs[i].minT = zs[i].minB = 1 << 30;
			zs[i].samples = zs[i].locked = zs[i].bad = 0;
		}
		if (zc && zc->h > 0) {       /* keep the eased crop in the new resolution's pixels */
			cropT = cropT * h / zc->h;
			cropB = cropB * h / zc->h;
		}
		zc = &zs[i];
	}
	while (t < h / 5 && row_dark(data, pitch, t, w)) t++;
	while (b < h / 5 && row_dark(data, pitch, h - 1 - b, w)) b++;
	if (t >= h / 5 || b >= h / 5) return;           /* black / fading / dark frame: no info */

	if (zc->locked) {
		/* picture inside the cropped area? then the lock is wrong: undo it now */
		if ((int)t < zc->lockT - 1 || (int)b < zc->lockB - 1) {
			if (++zc->bad >= 2) {
				zc->locked = 0;
				zc->samples = 0;
				zc->bad = 0;
				zc->minT = t;
				zc->minB = b;
				if (cropT > (float)t) cropT = (float)t;   /* snap, do not ease */
				if (cropB > (float)b) cropB = (float)b;
			}
		} else {
			zc->bad = 0;
		}
		return;
	}
	if ((int)t < zc->minT) zc->minT = t;
	if ((int)b < zc->minB) zc->minB = b;
	if (++zc->samples >= 8) {                       /* ~2 s of picture */
		zc->lockT = zc->minT;
		zc->lockB = zc->minB;
		/* keep it symmetric so the picture stays centred */
		if (zc->lockT > zc->lockB + 4) zc->lockT = zc->lockB + 4;
		if (zc->lockB > zc->lockT + 4) zc->lockB = zc->lockT + 4;
		zc->locked = 1;
		zc->bad = 0;
	}
}

static void crop_rect(SDL_Rect *src)
{
	src->x = 0; src->y = 0; src->w = frameW; src->h = frameH;
	if (cfg.aspect != WIIU_ASPECT_ZOOM) return;
	int on = zc && zc->locked && zc->h == frameH;
	float t = on ? zc->lockT : 0, b = on ? zc->lockB : 0;
	cropT += (t - cropT) * 0.08f;                   /* one smooth move, once */
	cropB += (b - cropB) * 0.08f;
	src->y = (int)(cropT + 0.5f);
	src->h = frameH - src->y - (int)(cropB + 0.5f);
	if (src->h < frameH * 3 / 4) { src->y = 0; src->h = frameH; }
}

void video_upload(const void *data, unsigned w, unsigned h, size_t pitch)
{
	static unsigned n;
	if (!data || !w || !h) { framesNull++; return; }   /* duped frame */
	framesIn++;
	if (w > TEX_W) w = TEX_W;
	if (h > TEX_H) h = TEX_H;
	SDL_Rect r = { 0, 0, (int)w, (int)h };
	const uint64_t c0 = SDL_GetPerformanceCounter();
	if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
		for (unsigned y = 0; y < h; y++) {
			const uint32_t *src = (const uint32_t *)((const uint8_t *)data + y * pitch);
			uint32_t *dst = conv + y * w;
			for (unsigned x = 0; x < w; x++) dst[x] = src[x] | 0xff000000u;
		}
	} else {
		for (unsigned y = 0; y < h; y++) {
			const uint16_t *src = (const uint16_t *)((const uint8_t *)data + y * pitch);
			uint32_t *dst = conv + y * w;
			for (unsigned x = 0; x < w; x++) dst[x] = lut565[src[x]];
		}
	}
	SDL_UpdateTexture(frameTex, &r, conv, (int)(w * 4));
	convTicks += SDL_GetPerformanceCounter() - c0;

	if (cfg.aspect == WIIU_ASPECT_ZOOM && ((n++ % 15) == 0 || (int)w != frameW || (int)h != frameH))
		measure_borders(data, w, h, pitch);
	frameW = w;
	frameH = h;
	haveFrame = 1;
}

void video_osd(const char *msg)
{
	msg = tr(msg);
	strncpy(osdMsg, msg, sizeof(osdMsg) - 1);
	osdMsg[sizeof(osdMsg) - 1] = 0;
	osdUntil = SDL_GetTicks() + 2500;
}

static int fpsValue = -1, fpsDrawn = -2, fpsTarget = 60;
static SDL_Texture *fpsTex;
static int fpsTexW, fpsTexH;

void video_set_fps(int fps, int target) { fpsValue = fps; fpsTarget = target > 0 ? target : 60; }

/* milliseconds the picture conversion + upload took since the last call */
double video_take_conv_ms(void)
{
	double ms = (double)convTicks * 1000.0 / (double)SDL_GetPerformanceFrequency();
	convTicks = 0;
	return ms;
}

/* the per-frame averages shown under the FPS badge (milliseconds) */
static char timingLine[96];
static SDL_Texture *timTex;
static int timTexW, timTexH, timDirty;
void video_set_timing(double emu, double conv_, double draw, double wait, int vsync)
{
	char s[96];
	snprintf(s, sizeof(s), "emu %.1f (picture %.1f)   draw %.1f   wait %.1f ms   VSync %s",
	         emu, conv_, draw, wait, vsync ? "on" : "off");
	if (strcmp(s, timingLine)) { strcpy(timingLine, s); timDirty = 1; }
}

static int wideOn;
void video_set_widescreen(int on) { wideOn = on; }

static int crossOn, crossX, crossY;
void video_set_crosshair(int on, int gx, int gy) { crossOn = on; crossX = gx; crossY = gy; }

static void compute_dest(SDL_Rect *r, const SDL_Rect *src)
{
	const int W = 1280, H = 720;
	if (wideOn) {           /* the game draws 16:9 itself (widescreen code) */
		r->x = 0; r->y = 0; r->w = W; r->h = H;
		return;
	}
	switch (cfg.aspect) {
	case WIIU_ASPECT_ZOOM: {
		/* the full frame is 4:3; the cropped part keeps the same pixel shape
		 * and is scaled to the full height (so it gets a bit wider) */
		float aspect = (4.0f / 3.0f) * ((float)src->w / frameW) / ((float)src->h / frameH);
		r->h = H;
		r->w = (int)(H * aspect);
		if (r->w > W) { r->w = W; r->h = (int)(W / aspect); }
		r->x = (W - r->w) / 2;
		r->y = (H - r->h) / 2;
		break;
	}
	case WIIU_ASPECT_STRETCH:
		r->x = 0; r->y = 0; r->w = W; r->h = H;
		break;
	case WIIU_ASPECT_INTEGER: {
		/* square pixels, the largest whole multiple that fits: for 320x240
		 * games that is 960x720, the same as 4:3; other widths (256, 368,
		 * 512...) keep their own shape instead of being stretched to 4:3 */
		int baseH = frameH > 288 ? frameH / 2 : frameH;
		int baseW = frameW >= 512 ? frameW / 2 : frameW;
		int s = H / (baseH ? baseH : 240);
		while (s > 1 && baseW * s > W) s--;
		if (s < 1) s = 1;
		r->h = baseH * s;
		r->w = baseW * s;
		r->x = (W - r->w) / 2;
		r->y = (H - r->h) / 2;
		break;
	}
	default:
		r->h = H;
		r->w = H * 4 / 3;
		r->x = (W - r->w) / 2;
		r->y = 0;
		break;
	}
}

/* filled rounded rectangle, any alpha (one triangle fan, no overlaps) */
static void fill_rrect(int x, int y, int w, int h, int r, SDL_Color c)
{
	enum { SEG = 6 };
	SDL_Vertex v[1 + 4 * (SEG + 1)];
	int idx[3 * 4 * (SEG + 1)], n = 1, m = 0;
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	const float cx[4] = { x + w - r, x + r, x + r, x + w - r };
	const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
	v[0].position.x = x + w / 2.0f; v[0].position.y = y + h / 2.0f;
	v[0].color = c;
	for (int q = 0; q < 4; q++)
		for (int i = 0; i <= SEG; i++) {
			float a = (q * 90 + i * 90.0f / SEG) * 3.14159265f / 180.0f;
			v[n].position.x = cx[q] + cosf(a) * r;
			v[n].position.y = cy[q] - sinf(a) * r;
			v[n].color = c;
			v[n].tex_coord.x = v[n].tex_coord.y = 0;
			n++;
		}
	for (int i = 1; i < n; i++) {
		idx[m++] = 0; idx[m++] = i; idx[m++] = i + 1 < n ? i + 1 : 1;
	}
	SDL_RenderGeometry(renderer, NULL, v, n, idx, m);
}

static void draw_text(int x, int y, const char *s, SDL_Color c)
{
	if (!osdFont || !s || !*s) return;
	SDL_Surface *surf = TTF_RenderUTF8_Blended(osdFont, s, c);
	if (!surf) return;
	SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, surf);
	SDL_Rect dst = { x, y, surf->w, surf->h };
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	fill_rrect(x - 16, y - 8, surf->w + 32, surf->h + 16, (surf->h + 16) / 2,
	           (SDL_Color){ 12, 14, 20, 200 });
	SDL_RenderCopy(renderer, t, NULL, &dst);
	SDL_DestroyTexture(t);
	SDL_FreeSurface(surf);
}

/* FPS badge: a dark pill with a coloured dot (green = full speed,
 * amber = a bit slow, red = slow), the number and a small "FPS" label.
 * The text is rendered only when the number changes (once a second). */
static void draw_fps(void)
{
	if (fpsValue < 0 || !fpsBig || !fpsSmall) return;
	if (fpsValue != fpsDrawn || !fpsTex) {
		char num[16];
		SDL_Color white = { 255, 255, 255, 255 }, grey = { 170, 178, 190, 255 };
		snprintf(num, sizeof(num), "%d", fpsValue);
		SDL_Surface *a = TTF_RenderUTF8_Blended(fpsBig, num, white);
		SDL_Surface *b = TTF_RenderUTF8_Blended(fpsSmall, "FPS", grey);
		if (fpsTex) { SDL_DestroyTexture(fpsTex); fpsTex = NULL; }
		if (a && b) {
			SDL_Surface *c = SDL_CreateRGBSurfaceWithFormat(0, a->w + 6 + b->w, a->h, 32,
			                                                SDL_PIXELFORMAT_ARGB8888);
			if (c) {
				SDL_Rect ra = { 0, 0, a->w, a->h };
				/* "FPS" sits on the number's baseline */
				SDL_Rect rb = { a->w + 6, a->h - b->h - (a->h - TTF_FontAscent(fpsBig)) / 3, b->w, b->h };
				SDL_SetSurfaceBlendMode(a, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(a, NULL, c, &ra);
				SDL_SetSurfaceBlendMode(b, SDL_BLENDMODE_NONE);
				SDL_BlitSurface(b, NULL, c, &rb);
				fpsTex = SDL_CreateTextureFromSurface(renderer, c);
				fpsTexW = c->w; fpsTexH = c->h;
				SDL_FreeSurface(c);
			}
		}
		if (a) SDL_FreeSurface(a);
		if (b) SDL_FreeSurface(b);
		fpsDrawn = fpsValue;
	}
	if (!fpsTex) return;
	const int x = 20, y = 18, padL = 34, padR = 16, h = fpsTexH + 10;
	int target = fpsTarget;
	SDL_Color dot = fpsValue >= target - 3 ? (SDL_Color){ 70, 220, 120, 255 } :
	                fpsValue >= target * 3 / 4 ? (SDL_Color){ 255, 190, 60, 255 } :
	                (SDL_Color){ 255, 80, 80, 255 };
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	fill_rrect(x, y, padL + fpsTexW + padR, h, h / 2, (SDL_Color){ 10, 12, 18, 190 });
	fill_rrect(x + 14, y + h / 2 - 5, 10, 10, 5, dot);
	SDL_Rect d = { x + padL, y + 5, fpsTexW, fpsTexH };
	SDL_RenderCopy(renderer, fpsTex, NULL, &d);
	if (timingLine[0] && fpsSmall) {            /* where the frame time goes */
		if (timDirty || !timTex) {
			SDL_Surface *t = TTF_RenderUTF8_Blended(fpsSmall, timingLine, (SDL_Color){ 220, 226, 235, 255 });
			if (timTex) { SDL_DestroyTexture(timTex); timTex = NULL; }
			if (t) { timTex = SDL_CreateTextureFromSurface(renderer, t); timTexW = t->w; timTexH = t->h; SDL_FreeSurface(t); }
			timDirty = 0;
		}
		if (timTex) {
			fill_rrect(x, y + h + 6, timTexW + 24, timTexH + 8, (timTexH + 8) / 2, (SDL_Color){ 10, 12, 18, 190 });
			SDL_Rect td = { x + 12, y + h + 10, timTexW, timTexH };
			SDL_RenderCopy(renderer, timTex, NULL, &td);
		}
	}
}

/* ---- post-processing (all done by the GPU, almost free for the CPU) ---- */
static SDL_Texture *sharpTex;      /* integer-prescaled frame (sharp bilinear) */
static SDL_Texture *crtTex;        /* aperture-grille mask, multiplied */
static SDL_Texture *scanTex;       /* one dark line every 2nd texel, multiplied */

static SDL_Texture *make_pattern(int w, int h, uint32_t (*px)(int x, int y), SDL_BlendMode bm)
{
	SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
	                                   SDL_TEXTUREACCESS_STATIC, w, h);
	uint32_t *buf = (uint32_t *)SDL_malloc(w * h * 4);
	if (!t || !buf) { SDL_free(buf); return t; }
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) buf[y * w + x] = px(x, y);
	SDL_UpdateTexture(t, NULL, buf, w * 4);
	SDL_free(buf);
	SDL_SetTextureBlendMode(t, bm);
	return t;
}

static uint32_t crt_px(int x, int y)
{
	/* RGB aperture grille, 3 screen pixels per triad, soft */
	(void)y;
	switch (x % 3) {
	case 0:  return 0xffffb4b4;
	case 1:  return 0xffb4ffb4;
	default: return 0xffb4b4ff;
	}
}

static uint32_t scan_px(int x, int y)
{
	(void)x;
	return (y & 1) ? 0xff8c8c8c : 0xffffffff;
}

static void draw_sharp_bilinear(const SDL_Rect *src, const SDL_Rect *dst)
{
	/* Nearest-neighbour upscale by an integer factor, then a bilinear
	 * downscale to the final size: crisp pixels without shimmering. */
	int kx = (dst->w + src->w - 1) / src->w;
	int ky = (dst->h + src->h - 1) / src->h;
	if (kx < 1) kx = 1;
	if (ky < 1) ky = 1;
	while (src->w * kx > 2048) kx--;
	while (src->h * ky > 2048) ky--;
	if (!sharpTex) {
		/* Use the renderer's native format for the render target: an
		 * ARGB8888 target came out blue on the Wii U renderer. */
		SDL_RendererInfo info;
		Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
		if (SDL_GetRendererInfo(renderer, &info) == 0 && info.num_texture_formats > 0)
			fmt = info.texture_formats[0];
		sharpTex = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_TARGET, 2048, 2048);
		if (!sharpTex) { SDL_RenderCopy(renderer, frameTex, src, dst); return; }
		SDL_SetTextureScaleMode(sharpTex, SDL_ScaleModeLinear);
		SDL_SetTextureBlendMode(sharpTex, SDL_BLENDMODE_NONE);
	}
	SDL_Rect mid = { 0, 0, src->w * kx, src->h * ky };
	SDL_Texture *prevTarget = SDL_GetRenderTarget(renderer);
	SDL_SetTextureScaleMode(frameTex, SDL_ScaleModeNearest);
	SDL_SetRenderTarget(renderer, sharpTex);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	SDL_RenderCopy(renderer, frameTex, src, &mid);
	SDL_SetRenderTarget(renderer, prevTarget);
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	SDL_RenderCopy(renderer, sharpTex, &mid, dst);
	lastFilter = -1;
}

/* ---- bezels ---- */
static char bezelId[16];
static SDL_Texture *bezelTex;
static char bezelLoaded[32] = "-";

void video_set_game_id(const char *id)
{
	strncpy(bezelId, id ? id : "", sizeof(bezelId) - 1);
}

static SDL_Texture *custom_bezel(void)
{
	/* bezels/<GAME-ID>.png, else bezels/default.png; 1280x720 with a
	 * transparent hole where the game goes */
	char want[32], path[256];
	snprintf(want, sizeof(want), "%s", bezelId[0] ? bezelId : "default");
	if (strcmp(want, bezelLoaded)) {
		SDL_Surface *s = NULL;
		if (bezelTex) { SDL_DestroyTexture(bezelTex); bezelTex = NULL; }
		if (bezelId[0]) {
			snprintf(path, sizeof(path), "%s/%s.png", WIIU_BEZEL_DIR, bezelId);
			s = IMG_Load(path);
		}
		if (!s) {
			snprintf(path, sizeof(path), "%s/default.png", WIIU_BEZEL_DIR);
			s = IMG_Load(path);
		}
		if (s) {
			bezelTex = SDL_CreateTextureFromSurface(renderer, s);
			SDL_SetTextureBlendMode(bezelTex, SDL_BLENDMODE_BLEND);
			SDL_FreeSurface(s);
		}
		strcpy(bezelLoaded, want);
	}
	return bezelTex;
}

/* built-in bezels: content/bezels/<file>.png, 1280x720 with a 960x720 hole */
static const struct { int id; const char *name, *file; } BEZELS[] = {
	{ BEZEL_OFF,       "Off",          NULL },
	{ BEZEL_AMBIENT,   "Ambient",      NULL },
	{ BEZEL_TUBETV,    "Tube TV",      "tubetv" },
	{ BEZEL_CLASSICTV, "Classic TV",   "classictv" },
	{ BEZEL_TV,        "Retro TV",     "tv" },
	{ BEZEL_PIXELSKY,  "Pixel sky",    "pixelsky" },
	{ BEZEL_PIXELCITY, "Pixel city",   "pixelcity" },
	{ BEZEL_PIXELCASTLE, "Pixel castle", "pixelcastle" },
	{ BEZEL_GREY,      "Grey console", "grey" },
	{ BEZEL_CARBON,    "Carbon",       "carbon" },
	{ BEZEL_SYNTHWAVE, "Synthwave",    "synthwave" },
	{ BEZEL_WOOD,      "Wood",         "wood" },
	{ BEZEL_WHITE,     "White",        "white" },
	{ BEZEL_SPACE,     "Space",        "space" },
	{ BEZEL_CUSTOM,    "Custom image", NULL },
};
#define NBEZELS (int)(sizeof(BEZELS) / sizeof(BEZELS[0]))

static int bezel_pos(int b)
{
	for (int i = 0; i < NBEZELS; i++)
		if (BEZELS[i].id == b) return i;
	return 0;
}

const char *bezel_name(int b) { return BEZELS[bezel_pos(b)].name; }

int bezel_next(int b, int dir)
{
	return BEZELS[(bezel_pos(b) + NBEZELS + dir) % NBEZELS].id;
}

static SDL_Texture *builtinTex;
static int builtinLoaded = -1;

static SDL_Texture *builtin_bezel(int b)
{
	if (b != builtinLoaded) {
		const char *f = BEZELS[bezel_pos(b)].file;
		if (builtinTex) { SDL_DestroyTexture(builtinTex); builtinTex = NULL; }
		if (f) {
			char path[128];
			snprintf(path, sizeof(path), "fs:/vol/content/bezels/%s.png", f);
			SDL_Surface *s = IMG_Load(path);
			if (s) {
				builtinTex = SDL_CreateTextureFromSurface(renderer, s);
				if (builtinTex) {
					SDL_SetTextureBlendMode(builtinTex, SDL_BLENDMODE_BLEND);
					/* pixel art stays crisp when scaled */
					SDL_SetTextureScaleMode(builtinTex, !strncmp(f, "pixel", 5) ?
					                        SDL_ScaleModeNearest : SDL_ScaleModeLinear);
				}
				SDL_FreeSurface(s);
			}
		}
		builtinLoaded = b;
	}
	return builtinTex;
}

static void draw_builtin_bezel(int b, const SDL_Rect *dst)
{
	/* scale the art so its hole sits exactly around the picture, whatever
	 * the screen mode (4:3, zoom, integer); the outer edge may go off screen */
	SDL_Texture *t = builtin_bezel(b);
	if (!t) return;
	float sx = dst->w / 960.0f, sy = dst->h / 720.0f, k = sx > sy ? sx : sy;
	int bw = (int)(1280 * k + 0.5f), bh = (int)(720 * k + 0.5f);
	SDL_Rect r = { dst->x + dst->w / 2 - bw / 2, dst->y + dst->h / 2 - bh / 2, bw, bh };
	SDL_RenderCopy(renderer, t, NULL, &r);
}

static Uint32 native_format(void);
static SDL_Texture *ambTex;   /* tiny copy of the picture for the Ambient border */

static void draw_ambient(const SDL_Rect *src, const SDL_Rect *dst)
{
	/* The frame itself, blown up past the screen edges: the bilinear
	 * upscale of a 320x240 image is naturally very soft. Darkened. */
	if (dst->x <= 0 && dst->y <= 0) return;
	SDL_Rect s = { src->x + src->w / 8, src->y + src->h / 8, src->w * 3 / 4, src->h * 3 / 4 };
	SDL_Rect full = { -160, -90, 1600, 900 };
	SDL_SetTextureScaleMode(frameTex, SDL_ScaleModeLinear);
	/* a strong blur: shrink the picture to a few dozen pixels, then stretch
	 * that back over the screen (linear filtering does the rest) */
	if (!ambTex) {
		ambTex = SDL_CreateTexture(renderer, native_format(), SDL_TEXTUREACCESS_TARGET, 64, 64);
		if (ambTex) SDL_SetTextureScaleMode(ambTex, SDL_ScaleModeLinear);
	}
	if (ambTex) {
		SDL_Texture *prev = SDL_GetRenderTarget(renderer);
		SDL_Rect tiny = { 0, 0, 32, 24 };
		SDL_SetRenderTarget(renderer, ambTex);
		SDL_RenderSetLogicalSize(renderer, 0, 0);
		SDL_RenderCopy(renderer, frameTex, &s, &tiny);
		SDL_SetRenderTarget(renderer, prev);
		SDL_RenderSetLogicalSize(renderer, 1280, 720);
		SDL_Rect in = { 1, 1, 30, 22 };          /* away from the edge texels */
		SDL_SetTextureBlendMode(ambTex, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(renderer, ambTex, &in, &full);
	} else {
		SDL_RenderCopy(renderer, frameTex, &s, &full);
	}
	lastFilter = -1;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 175);
	SDL_RenderFillRect(renderer, NULL);
	/* soft shadow around the game picture */
	for (int i = 1; i <= 6; i++) {
		SDL_Rect sh = { dst->x - i * 3, dst->y - i * 3, dst->w + i * 6, dst->h + i * 6 };
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 30);
		SDL_RenderFillRect(renderer, &sh);
	}
}

static SDL_Rect lastSrc, lastDst;     /* where the picture was drawn */

/* ---- screen effects ---- */
static SDL_Texture *fxTex;      /* 1280x720 target: the picture before the CRT curve */
static SDL_Texture *glowTex;    /* small target: blurred copy for the glow */
static SDL_Texture *lcdTex;     /* pixel grid, multiplied */
static int lcdCell, lcdW, lcdH;

static Uint32 native_format(void)
{
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(renderer, &info) == 0 && info.num_texture_formats > 0)
		return info.texture_formats[0];
	return SDL_PIXELFORMAT_RGBA8888;
}

/* glow: a 1/4-size copy of the picture, scaled back up (blurred) and added */
static void draw_glow(const SDL_Rect *src, const SDL_Rect *dst)
{
	if (!glowTex) {
		glowTex = SDL_CreateTexture(renderer, native_format(), SDL_TEXTUREACCESS_TARGET, 256, 256);
		if (!glowTex) return;
		SDL_SetTextureScaleMode(glowTex, SDL_ScaleModeLinear);
	}
	SDL_Texture *prev = SDL_GetRenderTarget(renderer);
	SDL_Rect small = { 0, 0, src->w / 4 > 0 ? src->w / 4 : 1, src->h / 4 > 0 ? src->h / 4 : 1 };
	if (small.w > 256) small.w = 256;
	if (small.h > 256) small.h = 256;
	SDL_SetTextureScaleMode(frameTex, SDL_ScaleModeLinear);
	SDL_SetRenderTarget(renderer, glowTex);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	SDL_RenderCopy(renderer, frameTex, src, &small);
	SDL_SetRenderTarget(renderer, prev);
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	lastFilter = -1;
	SDL_SetTextureBlendMode(glowTex, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(glowTex, 255);
	SDL_SetTextureColorMod(glowTex, 52, 52, 52);
	SDL_Rect big = { dst->x - dst->w / 40, dst->y - dst->h / 40, dst->w + dst->w / 20, dst->h + dst->h / 20 };
	SDL_RenderCopy(renderer, glowTex, &small, &big);
}

/* LCD grid: a thin dark line between the game's pixels */
static void draw_lcd(const SDL_Rect *src, const SDL_Rect *dst)
{
	int cell = (dst->w + src->w / 2) / src->w;
	if (cell < 2) cell = 2;
	if (cell > 4) cell = 4;
	while (frameW * cell > 2048 || frameH * cell > 2048) cell--;
	if (cell < 2) return;
	if (!lcdTex || cell != lcdCell || frameW != lcdW || frameH != lcdH) {
		if (lcdTex) SDL_DestroyTexture(lcdTex);
		int w = frameW * cell, h = frameH * cell;
		SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
		lcdTex = NULL;
		if (!sf) return;
		for (int y = 0; y < h; y++) {
			uint32_t *row = (uint32_t *)((uint8_t *)sf->pixels + y * sf->pitch);
			int ly = y % cell == cell - 1;
			for (int x = 0; x < w; x++) {
				int lx = x % cell == cell - 1;
				row[x] = lx && ly ? 0xff8a8a8a : (lx || ly) ? 0xffb8b8b8 : 0xffffffff;
			}
		}
		lcdTex = SDL_CreateTextureFromSurface(renderer, sf);
		SDL_FreeSurface(sf);
		if (!lcdTex) return;
		SDL_SetTextureBlendMode(lcdTex, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(lcdTex, SDL_ScaleModeLinear);
		lcdCell = cell; lcdW = frameW; lcdH = frameH;
	}
	SDL_Rect s = { src->x * cell, src->y * cell, src->w * cell, src->h * cell };
	SDL_RenderCopy(renderer, lcdTex, &s, dst);
}

/* CRT curve: the finished picture (in fxTex) drawn on a bulging mesh, with
 * the corners pulled in and darkened like the glass of a tube TV */
static void draw_curved(const SDL_Rect *dst)
{
	enum { NX = 32, NY = 24 };
	static SDL_Vertex v[(NX + 1) * (NY + 1)];
	static int idx[NX * NY * 6];
	const float k = 0.07f;
	int n = 0, m = 0;
	for (int j = 0; j <= NY; j++)
		for (int i = 0; i <= NX; i++) {
			float tx = (float)i / NX, ty = (float)j / NY;
			float u = tx * 2 - 1, w = ty * 2 - 1, r2 = u * u + w * w;
			float sc = (1 - k * r2) / (1 - k);
			float px = dst->x + dst->w * (0.5f + 0.5f * u * sc);
			float py = dst->y + dst->h * (0.5f + 0.5f * w * sc);
			float shade = 1 - 0.30f * r2 * r2 / 4;
			Uint8 c = (Uint8)(255 * (shade < 0 ? 0 : shade));
			v[n].position.x = px; v[n].position.y = py;
			v[n].color = (SDL_Color){ c, c, c, 255 };
			v[n].tex_coord.x = (dst->x + tx * dst->w) / 1280.0f;
			v[n].tex_coord.y = (dst->y + ty * dst->h) / 720.0f;
			n++;
		}
	for (int j = 0; j < NY; j++)
		for (int i = 0; i < NX; i++) {
			int a = j * (NX + 1) + i, b = a + 1, c = a + NX + 1, d = c + 1;
			idx[m++] = a; idx[m++] = b; idx[m++] = d;
			idx[m++] = a; idx[m++] = d; idx[m++] = c;
		}
	SDL_SetTextureBlendMode(fxTex, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(fxTex, SDL_ScaleModeLinear);
	SDL_RenderGeometry(renderer, fxTex, v, n, idx, m);
}

int video_screen_to_game(int sx, int sy, int *gx, int *gy)
{
	if (!haveFrame || lastDst.w <= 0 || lastDst.h <= 0 || frameW <= 0 || frameH <= 0) return 0;
	float u = (float)(sx - lastDst.x) / lastDst.w, v = (float)(sy - lastDst.y) / lastDst.h;
	if (u < 0 || u >= 1 || v < 0 || v >= 1) return 0;
	/* back through the auto-zoom crop to the whole frame */
	float fx = (lastSrc.x + u * lastSrc.w) / frameW, fy = (lastSrc.y + v * lastSrc.h) / frameH;
	*gx = (int)(fx * 65534.0f) - 32767;
	*gy = (int)(fy * 65534.0f) - 32767;
	return 1;
}

void video_draw_game(int dim)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!haveFrame) return;

	SDL_Rect src, dst;
	crop_rect(&src);
	compute_dest(&dst, &src);
	lastSrc = src;
	lastDst = dst;
	if (cfg.bezel == BEZEL_AMBIENT) draw_ambient(&src, &dst);

	/* CRT curve: draw the picture into fxTex first, then bend it */
	int curve = cfg.fx == FX_CURVE;
	if (curve && !fxTex) {
		fxTex = SDL_CreateTexture(renderer, native_format(), SDL_TEXTUREACCESS_TARGET, 1280, 720);
		if (!fxTex) curve = 0;
	}
	SDL_Texture *curvePrev = SDL_GetRenderTarget(renderer);
	if (curve) {
		SDL_SetRenderTarget(renderer, fxTex);
		SDL_RenderSetLogicalSize(renderer, 1280, 720);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
	}

	if (cfg.filter == WIIU_FILTER_SHARP_BILINEAR) {
		draw_sharp_bilinear(&src, &dst);
	} else {
		if (lastFilter != cfg.filter) {
			SDL_SetTextureScaleMode(frameTex, cfg.filter == WIIU_FILTER_NEAREST ?
			                        SDL_ScaleModeNearest : SDL_ScaleModeLinear);
			lastFilter = cfg.filter;
		}
		SDL_RenderCopy(renderer, frameTex, &src, &dst);
	}

	if (cfg.scanlines) {
		/* one texel per PSX line: 2 texels per line in the pattern */
		int lines = frameH > 288 ? frameH / 2 : frameH;
		if (!scanTex) {
			scanTex = make_pattern(1, 1024, scan_px, SDL_BLENDMODE_MOD);
			if (scanTex) SDL_SetTextureScaleMode(scanTex, SDL_ScaleModeLinear);
		}
		if (scanTex) {
			SDL_Rect s = { 0, 0, 1, lines * 2 > 1024 ? 1024 : lines * 2 };
			SDL_RenderCopy(renderer, scanTex, &s, &dst);
		}
	}
	if (cfg.crt) {
		if (!crtTex) {
			crtTex = make_pattern(1280, 1, crt_px, SDL_BLENDMODE_MOD);
			if (crtTex) SDL_SetTextureScaleMode(crtTex, SDL_ScaleModeNearest);
		}
		if (crtTex) {
			SDL_Rect s = { 0, 0, dst.w, 1 };
			SDL_RenderCopy(renderer, crtTex, &s, &dst);
		}
	}
	if (cfg.fx == FX_LCD) draw_lcd(&src, &dst);
	if (cfg.fx == FX_GLOW) draw_glow(&src, &dst);
	if (curve) {
		SDL_SetRenderTarget(renderer, curvePrev);
		SDL_RenderSetLogicalSize(renderer, 1280, 720);
		draw_curved(&dst);
	}
	if (crossOn && dst.w > 0) {
		/* light-gun crosshair (aiming by motion) */
		float fx = (crossX + 32767.0f) / 65534.0f * frameW, fy = (crossY + 32767.0f) / 65534.0f * frameH;
		int sx = dst.x + (int)((fx - src.x) * dst.w / src.w), sy = dst.y + (int)((fy - src.y) * dst.h / src.h);
		SDL_Rect hl[4] = { { sx - 18, sy - 2, 12, 4 }, { sx + 6, sy - 2, 12, 4 },
		                   { sx - 2, sy - 18, 4, 12 }, { sx - 2, sy + 6, 4, 12 } };
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
		for (int i = 0; i < 4; i++) { SDL_Rect o = { hl[i].x - 1, hl[i].y - 1, hl[i].w + 2, hl[i].h + 2 }; SDL_RenderFillRect(renderer, &o); }
		SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
		SDL_RenderFillRects(renderer, hl, 4);
	}
	if (cfg.bezel == BEZEL_CUSTOM) {
		SDL_Texture *b = custom_bezel();
		if (b) SDL_RenderCopy(renderer, b, NULL, NULL);
	} else if (cfg.bezel >= BEZEL_TV) {
		draw_builtin_bezel(cfg.bezel, &dst);
	}
	if (dim) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
		SDL_RenderFillRect(renderer, NULL);
	}
}

/* the game drawn with the current picture settings into a texture, for the
 * live preview in the settings menu */
static SDL_Texture *previewTex;

void *video_preview(void)
{
	if (!haveFrame) return NULL;
	if (!previewTex) {
		previewTex = SDL_CreateTexture(renderer, native_format(), SDL_TEXTUREACCESS_TARGET, 1280, 720);
		if (!previewTex) return NULL;
		SDL_SetTextureScaleMode(previewTex, SDL_ScaleModeLinear);
		SDL_SetTextureBlendMode(previewTex, SDL_BLENDMODE_NONE);
	}
	SDL_Texture *prev = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, previewTex);
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	video_draw_game(0);
	SDL_SetRenderTarget(renderer, prev);
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	return previewTex;
}

int video_save_frame(const char *path, int width)
{
	/* conv holds the last frame as ARGB8888, frameW x frameH, stride frameW */
	SDL_Surface *src, *dst;
	int ok;
	if (!haveFrame || !conv) return 0;
	src = SDL_CreateRGBSurfaceWithFormatFrom(conv, frameW, frameH, 32, frameW * 4,
	                                         SDL_PIXELFORMAT_ARGB8888);
	if (!src) return 0;
	if (width > 0) {
		int h = width * 3 / 4;
		dst = SDL_CreateRGBSurfaceWithFormat(0, width, h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (dst) SDL_BlitScaled(src, NULL, dst, NULL);
	} else {
		/* full size, stretched to 4:3 like on screen */
		int h = frameH, w = frameH * 4 / 3;
		if (frameH <= 288) { h = frameH * 2; w = h * 4 / 3; }
		dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (dst) SDL_BlitScaled(src, NULL, dst, NULL);
	}
	ok = dst && IMG_SavePNG(dst, path) == 0;
	if (dst) SDL_FreeSurface(dst);
	SDL_FreeSurface(src);
	return ok;
}

static uint32_t gameFade;
void video_fade_in(void) { gameFade = SDL_GetTicks(); fpsValue = -1; }

/* ---- TV / GamePad screens ----
 * SDL's Wii U renderer copies each presented frame to the TV and/or the
 * GamePad depending on the window flags SDL_WINDOW_WIIU_TV_ONLY /
 * _GAMEPAD_ONLY, and only swaps when _PREVENT_SWAP is not set. SDL has no
 * call to change them after the window is made, so the flags word is
 * reached through the start of SDL_Window (SDL 2.32 layout), and only after
 * checking that it really is the flags word. */
struct win_head {
	const void *magic;
	Uint32 id;
	char *title;
	SDL_Surface *icon;
	int x, y, w, h, min_w, min_h, max_w, max_h;
	Uint32 flags;
};
static Uint32 *winFlags;
static int winChecked, ingame, blankFrames;
static Uint32 baseFlags;

int video_screen_modes_ok(void)
{
	if (!winChecked) {
		winChecked = 1;
		struct win_head *w = (struct win_head *)window;
		Uint32 f = SDL_GetWindowFlags(window);
		if (w && w->flags == f && w->w == 1280 && w->h == 720) {
			/* toggle a Wii U-only bit and see SDL report it */
			w->flags = f | SDL_WINDOW_WIIU_PREVENT_SWAP;
			if (SDL_GetWindowFlags(window) == (f | SDL_WINDOW_WIIU_PREVENT_SWAP)) winFlags = &w->flags;
			w->flags = f;
			baseFlags = f & ~(SDL_WINDOW_WIIU_TV_ONLY | SDL_WINDOW_WIIU_GAMEPAD_ONLY | SDL_WINDOW_WIIU_PREVENT_SWAP);
		}
	}
	return winFlags != NULL;
}

/* vertical sync: wait for the screen's refresh (no tearing) */
void video_set_vsync(int on)
{
	static int cur = -1;
	if (on != cur) { SDL_RenderSetVSync(renderer, on); cur = on; }
}

void video_set_ingame(int on)
{
	ingame = on;
	/* the sound follows the picture */
	int mode = on && video_screen_modes_ok() ? cfg.drc : DRC_MIRROR;
	audio_route(mode != DRC_ONLY, mode != DRC_TV_ONLY);
	blankFrames = 3;     /* clear the screen that stops showing the game */
}

static void set_targets(Uint32 extra)
{
	if (winFlags) *winFlags = baseFlags | extra;
}

/* control panel (GamePad screen while the game is on the TV) */
static int panelSlot, panelFF, panelPaused;
static char panelGame[64];
static const struct { int action; const char *label; } PANEL[8] = {
	{ HK_SAVE, "Save state" }, { HK_LOAD, "Load state" }, { HK_SLOTDOWN, "Slot -" }, { HK_SLOTUP, "Slot +" },
	{ HK_SHOT, "Screenshot" }, { HK_FFWD, "Fast forward" }, { HK_PAUSE, "Pause" }, { PANEL_MENU, "Menu" },
};
#define PANEL_BTN(i, r) SDL_Rect r = { 40 + ((i) % 4) * 305, 400 + ((i) / 4) * 150, 285, 130 }

void video_panel_state(int slot, int ff, int paused, const char *game)
{
	panelSlot = slot; panelFF = ff; panelPaused = paused;
	snprintf(panelGame, sizeof(panelGame), "%s", game ? game : "");
	char *dot = strrchr(panelGame, '.');
	if (dot) *dot = 0;
}

int video_panel_hit(int x, int y)
{
	for (int i = 0; i < 8; i++) {
		PANEL_BTN(i, r);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return PANEL[i].action;
	}
	return -1;
}

static SDL_Texture *panelText[12];
static int panelTextW[12], panelTextH[12];

static SDL_Texture *panelTex;   /* the finished panel, redrawn only when it changes */
static char panelKey[300];

void video_lang_changed(void)
{
	panelKey[0] = 0;
	for (int i = 0; i < 12; i++)
		if (panelText[i]) { SDL_DestroyTexture(panelText[i]); panelText[i] = NULL; }
}

static void panel_label(int k, const char *s, int cx, int cy)
{
	/* labels are rendered once and kept */
	if (!panelText[k] && osdFont) {
		SDL_Color white = { 255, 255, 255, 255 };
		SDL_Surface *sf = TTF_RenderUTF8_Blended(osdFont, tr(s), white);
		if (sf) {
			panelText[k] = SDL_CreateTextureFromSurface(renderer, sf);
			panelTextW[k] = sf->w; panelTextH[k] = sf->h;
			SDL_FreeSurface(sf);
		}
	}
	if (panelText[k]) {
		SDL_Rect d = { cx - panelTextW[k] / 2, cy - panelTextH[k] / 2, panelTextW[k], panelTextH[k] };
		SDL_RenderCopy(renderer, panelText[k], NULL, &d);
	}
}

/* a text line that is only re-rendered when it changes */
static void panel_line(int k, int x, int y, const char *str)
{
	static char last[4][96];
	static SDL_Texture *tex[4];
	static int tw[4], th[4];
	if (!str || !*str) return;
	if (!tex[k] || strcmp(last[k], str)) {
		SDL_Color white = { 255, 255, 255, 255 };
		if (tex[k]) { SDL_DestroyTexture(tex[k]); tex[k] = NULL; }
		snprintf(last[k], sizeof(last[k]), "%s", str);
		SDL_Surface *sf = osdFont ? TTF_RenderUTF8_Blended(osdFont, str, white) : NULL;
		if (!sf) return;
		tex[k] = SDL_CreateTextureFromSurface(renderer, sf);
		tw[k] = sf->w; th[k] = sf->h;
		SDL_FreeSurface(sf);
	}
	if (tex[k]) {
		SDL_Rect d = { x, y, tw[k] > 1200 - x ? 1200 - x : tw[k], th[k] };
		SDL_Rect sr = { 0, 0, d.w, th[k] };
		SDL_RenderCopy(renderer, tex[k], &sr, &d);
	}
}


static void draw_panel(void)
{
	char key[300];
	int osd = osdMsg[0] && SDL_GetTicks() < osdUntil;
	snprintf(key, sizeof(key), "%s|%d|%d|%d|%s|%d", panelGame, panelSlot, panelFF, panelPaused,
	         osd ? osdMsg : "", lang_current());
	if (!panelTex) {
		panelTex = SDL_CreateTexture(renderer, native_format(), SDL_TEXTUREACCESS_TARGET, 1280, 720);
		panelKey[0] = 0;
	}
	if (panelTex && strcmp(key, panelKey)) {
		SDL_Texture *prev = SDL_GetRenderTarget(renderer);
		SDL_SetRenderTarget(renderer, panelTex);
		SDL_RenderSetLogicalSize(renderer, 1280, 720);
		SDL_SetRenderDrawColor(renderer, 8, 12, 30, 255);
		SDL_RenderClear(renderer);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		char t[96];
		panel_line(0, 60, 60, panelGame);
		snprintf(t, sizeof(t), tr("Quick slot: %d"), panelSlot);
		panel_line(1, 60, 140, t);
		panel_line(2, 60, 220, panelPaused ? tr("Paused") : panelFF ? tr(">> Fast forward") : " ");
		panel_line(3, 60, 300, osd ? osdMsg : " ");
		for (int i = 0; i < 8; i++) {
			PANEL_BTN(i, r);
			int on = (PANEL[i].action == HK_FFWD && panelFF) || (PANEL[i].action == HK_PAUSE && panelPaused);
			SDL_Color c = on ? (SDL_Color){ 0, 140, 220, 255 } :
			              PANEL[i].action == PANEL_MENU ? (SDL_Color){ 60, 40, 90, 255 } : (SDL_Color){ 30, 40, 72, 255 };
			fill_rrect(r.x, r.y, r.w, r.h, 22, c);
			panel_label(i, PANEL[i].label, r.x + r.w / 2, r.y + r.h / 2);
		}
		SDL_SetRenderTarget(renderer, prev);
		SDL_RenderSetLogicalSize(renderer, 1280, 720);
		snprintf(panelKey, sizeof(panelKey), "%s", key);
	}
	if (panelTex) {
		SDL_SetTextureBlendMode(panelTex, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(renderer, panelTex, NULL, NULL);
	}
}

void video_present(void)
{
	uint32_t dt = SDL_GetTicks() - gameFade;
	if (gameFade && dt < 400) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(255 - dt * 255 / 400));
		SDL_RenderFillRect(renderer, NULL);
	}
	SDL_Color white = { 255, 255, 255, 255 };
	if (cfg.show_fps) draw_fps();
	if (osdMsg[0] && SDL_GetTicks() < osdUntil) draw_text(36, 720 - 56, osdMsg, white);

	int mode = ingame && video_screen_modes_ok() ? cfg.drc : DRC_MIRROR;
	switch (mode) {
	case DRC_ONLY:
	case DRC_TV_ONLY: {
		Uint32 show = mode == DRC_ONLY ? SDL_WINDOW_WIIU_GAMEPAD_ONLY : SDL_WINDOW_WIIU_TV_ONLY;
		if (blankFrames > 0) {
			/* the game to one screen, then black to the other (both buffers) */
			set_targets(show | SDL_WINDOW_WIIU_PREVENT_SWAP);
			SDL_RenderPresent(renderer);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			set_targets(mode == DRC_ONLY ? SDL_WINDOW_WIIU_TV_ONLY : SDL_WINDOW_WIIU_GAMEPAD_ONLY);
			SDL_RenderPresent(renderer);
			blankFrames--;
		} else {
			set_targets(show);
			SDL_RenderPresent(renderer);
		}
		break;
	}
	case DRC_PANEL:
		set_targets(SDL_WINDOW_WIIU_TV_ONLY | SDL_WINDOW_WIIU_PREVENT_SWAP);
		SDL_RenderPresent(renderer);
		draw_panel();
		set_targets(SDL_WINDOW_WIIU_GAMEPAD_ONLY);
		SDL_RenderPresent(renderer);
		break;
	default:
		set_targets(0);
		SDL_RenderPresent(renderer);
		break;
	}
	set_targets(0);
}

static char textIn[128];
static int textState;      /* 0 idle, 1 typing, 2 confirmed */

void video_text_begin(void)
{
	textIn[0] = 0;
	textState = 1;
	SDL_StartTextInput();
}

int video_text_poll(char *buf, size_t n)
{
	if (textState == 2) {
		snprintf(buf, n, "%s", textIn);
		return 1;
	}
	return 0;
}

int video_text_active(void) { return SDL_IsTextInputActive(); }

void video_text_end(void)
{
	textState = 0;
	if (SDL_IsTextInputActive()) SDL_StopTextInput();
}

int pump_events(void)
{
	SDL_Event ev;
	int background = 0;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) wiiu_quit = 1;
		else if (ev.type == SDL_APP_WILLENTERBACKGROUND) background = 1;
		else if (ev.type == SDL_TEXTINPUT && textState == 1) {
			/* the Wii U keyboard sends the whole text when OK is pressed */
			snprintf(textIn, sizeof(textIn), "%s", ev.text.text);
			textState = 2;
		}
	}
	/* HOME Menu: Cafe OS takes the GPU away; freeze until we get it back. */
	if (background && !wiiu_quit) {
		int wasPaused = SDL_GetAudioStatus() != SDL_AUDIO_PLAYING;
		audio_pause(1);
		emu_background_save();     /* the app may be closed from there */
		wiiu_input_rumble(0, 0);
		for (;;) {
			if (!SDL_WaitEventTimeout(&ev, 100)) continue;
			if (ev.type == SDL_QUIT) { wiiu_quit = 1; break; }
			if (ev.type == SDL_APP_DIDENTERFOREGROUND) break;
		}
		if (!wasPaused) audio_pause(0);
	}
	return !wiiu_quit;
}
