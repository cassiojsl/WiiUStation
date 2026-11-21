/*
 * WiiUStation - video output.
 *
 * Frames from PCSX-ReARMed (RGB565 or XRGB8888) are uploaded to a GX2
 * texture through SDL2's Wii U renderer, then scaled by the GPU to the TV
 * (720p/1080p) and to the GamePad at the same time.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
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

void video_upload(const void *data, unsigned w, unsigned h, size_t pitch)
{
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

static void compute_dest(SDL_Rect *r)
{
	const int W = 1280, H = 720;
	switch (cfg.aspect) {
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

void video_draw_game(int dim)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!haveFrame) return;

	if (lastFilter != cfg.filter) {
		SDL_SetTextureScaleMode(frameTex, cfg.filter == WIIU_FILTER_NEAREST ?
		                        SDL_ScaleModeNearest : SDL_ScaleModeLinear);
		lastFilter = cfg.filter;
	}
	SDL_Rect src = { 0, 0, frameW, frameH }, dst;
	compute_dest(&dst);
	SDL_RenderCopy(renderer, frameTex, &src, &dst);

	if (cfg.scanlines) {
		int lines = frameH > 288 ? frameH / 2 : frameH;
		int step = lines ? dst.h / lines : 0;
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 70);
		if (step >= 2)
			for (int y = dst.y + step - 1; y < dst.y + dst.h; y += step)
				SDL_RenderDrawLine(renderer, dst.x, y, dst.x + dst.w, y);
	}
	if (dim) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
		SDL_RenderFillRect(renderer, NULL);
	}
}

void video_present(void)
{
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

int pump_events(void)
{
	SDL_Event ev;
	int background = 0;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) wiiu_quit = 1;
		else if (ev.type == SDL_APP_WILLENTERBACKGROUND) background = 1;
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
