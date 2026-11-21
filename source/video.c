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

static TTF_Font     *osdFont;
static char          osdMsg[128];
static uint32_t      osdUntil;
static char          fpsText[32];

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
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer) return -1;
	SDL_RenderSetLogicalSize(renderer, 1280, 720);
	create_texture();
	TTF_Init();
	IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
	osdFont = video_open_font(26);
	return 0;
}

void video_shutdown(void)
{
	if (osdFont) TTF_CloseFont(osdFont);
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
 * with the scenes. It is measured again only if the resolution changes. */
static int   zoomW, zoomH;                 /* resolution the lock belongs to */
static int   minT, minB, samples, locked;
static int   lockT, lockB;
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
	return r + g + b < 40;
}

static int row_dark(const void *data, size_t pitch, unsigned y, unsigned w)
{
	const uint8_t *row = (const uint8_t *)data + (size_t)y * pitch;
	for (unsigned i = 0; i < 32; i++)
		if (!px_dark(row, i * w / 32)) return 0;
	return 1;
}

static void measure_borders(const void *data, unsigned w, unsigned h, size_t pitch)
{
	unsigned t = 0, b = 0;
	if ((int)w != zoomW || (int)h != zoomH) {      /* new resolution: measure again */
		zoomW = w; zoomH = h;
		minT = minB = 1 << 30;
		samples = 0;
		locked = 0;
	}
	if (locked) return;
	while (t < h / 5 && row_dark(data, pitch, t, w)) t++;
	while (b < h / 5 && row_dark(data, pitch, h - 1 - b, w)) b++;
	if (t >= h / 5 && b >= h / 5) return;           /* black / fading frame: no info */
	if ((int)t < minT) minT = t;
	if ((int)b < minB) minB = b;
	if (++samples >= 8) {                           /* ~2 s of picture */
		lockT = minT;
		lockB = minB;
		/* keep it symmetric so the picture stays centred */
		if (lockT > lockB + 4) lockT = lockB + 4;
		if (lockB > lockT + 4) lockB = lockT + 4;
		locked = 1;
	}
}

static void crop_rect(SDL_Rect *src)
{
	src->x = 0; src->y = 0; src->w = frameW; src->h = frameH;
	if (cfg.aspect != WIIU_ASPECT_ZOOM) return;
	float t = locked ? lockT : 0, b = locked ? lockB : 0;
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
	if (cfg.aspect == WIIU_ASPECT_ZOOM && (n++ % 15) == 0)
		measure_borders(data, w, h, pitch);
	frameW = w;
	frameH = h;
	haveFrame = 1;
}

void video_osd(const char *msg)
{
	strncpy(osdMsg, msg, sizeof(osdMsg) - 1);
	osdMsg[sizeof(osdMsg) - 1] = 0;
	osdUntil = SDL_GetTicks() + 2500;
}

void video_set_fps_text(const char *txt)
{
	strncpy(fpsText, txt, sizeof(fpsText) - 1);
}

static void compute_dest(SDL_Rect *r, const SDL_Rect *src)
{
	const int W = 1280, H = 720;
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
		int baseH = frameH > 288 ? frameH / 2 : frameH;
		int s = H / (baseH ? baseH : 240);
		if (s < 1) s = 1;
		r->h = baseH * s;
		r->w = r->h * 4 / 3;
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

static void draw_text(int x, int y, const char *s, SDL_Color c)
{
	if (!osdFont || !s || !*s) return;
	SDL_Surface *surf = TTF_RenderUTF8_Blended(osdFont, s, c);
	if (!surf) return;
	SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, surf);
	SDL_Rect bg = { x - 6, y - 2, surf->w + 12, surf->h + 4 };
	SDL_Rect dst = { x, y, surf->w, surf->h };
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
	SDL_RenderFillRect(renderer, &bg);
	SDL_RenderCopy(renderer, t, NULL, &dst);
	SDL_DestroyTexture(t);
	SDL_FreeSurface(surf);
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
	SDL_SetTextureScaleMode(frameTex, SDL_ScaleModeNearest);
	SDL_SetRenderTarget(renderer, sharpTex);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	SDL_RenderCopy(renderer, frameTex, src, &mid);
	SDL_SetRenderTarget(renderer, NULL);
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
	{ BEZEL_TV,        "Retro TV",     "tv" },
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
					SDL_SetTextureScaleMode(builtinTex, SDL_ScaleModeLinear);
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

static void draw_ambient(const SDL_Rect *src, const SDL_Rect *dst)
{
	/* The frame itself, blown up past the screen edges: the bilinear
	 * upscale of a 320x240 image is naturally very soft. Darkened. */
	if (dst->x <= 0 && dst->y <= 0) return;
	SDL_Rect s = { src->x + src->w / 8, src->y + src->h / 8, src->w * 3 / 4, src->h * 3 / 4 };
	SDL_Rect full = { -160, -90, 1600, 900 };
	SDL_SetTextureScaleMode(frameTex, SDL_ScaleModeLinear);
	SDL_RenderCopy(renderer, frameTex, &s, &full);
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

void video_draw_game(int dim)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!haveFrame) return;

	SDL_Rect src, dst;
	crop_rect(&src);
	compute_dest(&dst, &src);
	if (cfg.bezel == BEZEL_AMBIENT) draw_ambient(&src, &dst);

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
void video_fade_in(void) { gameFade = SDL_GetTicks(); }

void video_present(void)
{
	uint32_t dt = SDL_GetTicks() - gameFade;
	if (gameFade && dt < 400) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(255 - dt * 255 / 400));
		SDL_RenderFillRect(renderer, NULL);
	}
	SDL_Color green = { 150, 255, 150, 255 }, white = { 255, 255, 255, 255 };
	if (cfg.show_fps && fpsText[0]) {
		char t[128];
		snprintf(t, sizeof(t), "%s  %dx%d %s  frames %u/%u", fpsText, frameW, frameH,
		         pixfmt == RETRO_PIXEL_FORMAT_XRGB8888 ? "XRGB8888" : "RGB565",
		         framesIn, framesNull);
		draw_text(16, 12, t, green);
	}
	if (osdMsg[0] && SDL_GetTicks() < osdUntil) draw_text(16, 720 - 48, osdMsg, white);
	SDL_RenderPresent(renderer);
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
