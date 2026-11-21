/*
 * WiiUStation - audio output (SDL2 -> Wii U AX), 16-bit stereo.
 * The emulator thread pushes samples into a ring buffer; the SDL audio
 * callback drains it.  main.c paces emulation on the fill level
 * ("audio sync"), which keeps sound smooth and speed exact.
 */
#include <SDL2/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "platform.h"
#ifdef __WIIU__
#include <sndcore2/device.h>
#endif
static void route_apply(void);

#define RING_FRAMES 16384            /* stereo frames, power of two */
static int16_t ring[RING_FRAMES * 2];
static volatile uint32_t rpos, wpos; /* in frames */
static int opened;
static double outRate = 48000.0, inRate = 44100.0;
static double resamplePos;
static int16_t lastL, lastR;

/* ---- menu sound effects: synthesised once, mixed into the output ---- */
#define SFX_RATE 48000
static int16_t *sfxData[SFX_COUNT];
static int sfxLen[SFX_COUNT];
static const int16_t *volatile sfxPlay;
static volatile int sfxPos, sfxEnd;

static void callback(void *ud, Uint8 *stream, int len)
{
	int16_t *out = (int16_t *)stream;
	int frames = len / 4;
	(void)ud;
	for (int i = 0; i < frames; i++) {
		if (rpos != wpos) {
			uint32_t r = rpos & (RING_FRAMES - 1);
			lastL = ring[r * 2];
			lastR = ring[r * 2 + 1];
			rpos++;
		}
		int l = lastL, r = lastR;
		if (sfxPlay && sfxPos < sfxEnd) {
			l += sfxPlay[sfxPos];
			r += sfxPlay[sfxPos];
			sfxPos++;
		}
		out[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		out[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
}

/* styles of menu sound (cfg.menusfx): 1 classic, 2 soft, 3 retro 8-bit,
 * 4 crystal, 5 marimba, 6 click */
enum { ST_CLASSIC = 1, ST_SOFT, ST_RETRO, ST_CRYSTAL, ST_MARIMBA, ST_CLICK, ST_COUNT };

static float wave(int style, float f, float t)
{
	const float w = 6.2831853f * f * t;
	switch (style) {
	case ST_SOFT:    return sinf(w) + 0.15f * sinf(2 * w);
	case ST_RETRO:   return (sinf(w) >= 0 ? 0.55f : -0.55f);                     /* square */
	case ST_CRYSTAL: return sinf(w) + 0.5f * sinf(2.76f * w) + 0.25f * sinf(5.4f * w); /* bell */
	case ST_MARIMBA: return sinf(w) + 0.35f * sinf(4.0f * w) * expf(-t * 60);    /* wood */
	default:         return sinf(w) + 0.3f * sinf(2 * w) + 0.12f * sinf(3 * w);
	}
}

/* notes in Hz, one after another, each with an exponential fade */
static void synth(int id, int style, const float *notes, int nnotes, float noteSec, float decay, float vol)
{
	int per = (int)(noteSec * SFX_RATE), tail = (int)(decay * SFX_RATE);
	int len = per * (nnotes - 1) + tail;
	int16_t *d = calloc(len, sizeof(int16_t));
	if (!d) return;
	uint32_t seed = 12345;
	for (int n = 0; n < nnotes; n++) {
		int start = n * per;
		for (int i = 0; start + i < len && i < tail; i++) {
			float t = (float)i / SFX_RATE;
			float env = expf(-t * 5.0f / decay) * (i < 96 ? i / 96.0f : 1.0f);
			float v;
			if (style == ST_CLICK) {           /* filtered noise burst */
				seed = seed * 1103515245u + 12345u;
				v = ((int)(seed >> 16 & 0x7fff) - 16384) / 16384.0f * expf(-t * 900) + 0.4f * sinf(6.2831853f * notes[n] * t) * expf(-t * 200);
			} else v = wave(style, notes[n], t);
			int x = d[start + i] + (int)(v * env * vol * 9000.0f);
			d[start + i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		}
	}
	sfxData[id] = d;
	sfxLen[id] = len;
}

static void sfx_build(void)
{
	static int builtFor = -1;
	int st = cfg.menusfx;
	if (st < ST_CLASSIC || st >= ST_COUNT) st = ST_CLASSIC;
	if (builtFor == st) return;
	/* stop what is playing before replacing the sounds */
	SDL_LockAudio();
	sfxPlay = NULL;
	SDL_UnlockAudio();
	for (int i = 0; i < SFX_COUNT; i++) { free(sfxData[i]); sfxData[i] = NULL; }
	builtFor = st;
	const float intro[5] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1568.0f };   /* C major, up */
	const float err[2] = { 311.1f, 293.7f };
	switch (st) {
	case ST_SOFT: {
		const float mv[1] = { 659.3f }, ok[2] = { 523.3f, 784.0f }, bk[2] = { 587.3f, 440.0f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.06f, 0.30f);
		synth(SFX_OK, st, ok, 2, 0.06f, 0.14f, 0.40f);
		synth(SFX_BACK, st, bk, 2, 0.06f, 0.12f, 0.36f);
		break;
	}
	case ST_RETRO: {
		const float mv[1] = { 1046.5f }, ok[3] = { 784.0f, 1046.5f, 1568.0f }, bk[2] = { 784.0f, 523.3f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.035f, 0.22f);
		synth(SFX_OK, st, ok, 3, 0.035f, 0.07f, 0.24f);
		synth(SFX_BACK, st, bk, 2, 0.04f, 0.07f, 0.22f);
		break;
	}
	case ST_CRYSTAL: {
		const float mv[1] = { 2093.0f }, ok[2] = { 1568.0f, 2093.0f }, bk[2] = { 1760.0f, 1318.5f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.18f, 0.20f);
		synth(SFX_OK, st, ok, 2, 0.07f, 0.35f, 0.26f);
		synth(SFX_BACK, st, bk, 2, 0.07f, 0.30f, 0.24f);
		break;
	}
	case ST_MARIMBA: {
		const float mv[1] = { 784.0f }, ok[2] = { 659.3f, 987.8f }, bk[2] = { 880.0f, 587.3f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.09f, 0.40f);
		synth(SFX_OK, st, ok, 2, 0.07f, 0.16f, 0.45f);
		synth(SFX_BACK, st, bk, 2, 0.07f, 0.14f, 0.42f);
		break;
	}
	case ST_CLICK: {
		const float mv[1] = { 3000.0f }, ok[2] = { 2400.0f, 3200.0f }, bk[2] = { 2600.0f, 1800.0f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.02f, 0.35f);
		synth(SFX_OK, st, ok, 2, 0.03f, 0.03f, 0.40f);
		synth(SFX_BACK, st, bk, 2, 0.03f, 0.03f, 0.38f);
		break;
	}
	default: {
		const float mv[1] = { 1318.5f }, ok[2] = { 987.8f, 1318.5f }, bk[2] = { 1174.7f, 880.0f };
		synth(SFX_MOVE, st, mv, 1, 0, 0.05f, 0.35f);
		synth(SFX_OK, st, ok, 2, 0.05f, 0.12f, 0.45f);
		synth(SFX_BACK, st, bk, 2, 0.05f, 0.10f, 0.4f);
		break;
	}
	}
	synth(SFX_ERROR, st == ST_CLICK ? ST_CLASSIC : st, err, 2, 0.08f, 0.15f, 0.40f);
	synth(SFX_INTRO, st == ST_CLICK || st == ST_RETRO ? ST_CLASSIC : st, intro, 5, 0.11f, 1.6f, 0.5f);
}

void sfx_play(int id)
{
	if (!cfg.menusfx || id < 0 || id >= SFX_COUNT) return;
	sfx_build();
	if (!sfxData[id]) return;
	if (!opened && audio_init(44100.0) < 0) return;
	SDL_LockAudio();
	rpos = wpos;                 /* drop what the game left in the buffer */
	sfxPlay = sfxData[id];
	sfxPos = 0;
	sfxEnd = sfxLen[id];
	SDL_UnlockAudio();
	SDL_PauseAudio(0);
}

/* builds the sounds and opens the audio device ahead of time, so the first
 * sound (the opening chime) does not stall the animation */
void sfx_prepare(void)
{
	if (!cfg.menusfx) return;
	sfx_build();
	if (!opened) audio_init(44100.0);
}

int audio_init(double rate)
{
	SDL_AudioSpec want, have;
	inRate = rate > 1000 ? rate : 44100.0;
	if (opened) { SDL_CloseAudio(); opened = 0; }
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	memset(&want, 0, sizeof(want));
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = callback;
	if (SDL_OpenAudio(&want, &have) < 0) return -1;
	outRate = have.freq;
	route_apply();
	rpos = wpos = 0;
	resamplePos = 0;
	opened = 1;
	return 0;
}

/* which screens play the sound: during a game "GamePad only" mutes the TV
 * and "TV only" mutes the GamePad; everywhere else both play */
static int routeTv = 1, routeDrc = 1;

static void route_apply(void)
{
#ifdef __WIIU__
	AXSetDeviceVolume(AX_DEVICE_TYPE_TV, 0, routeTv ? 0x8000 : 0);
	AXSetDeviceVolume(AX_DEVICE_TYPE_DRC, 0, routeDrc ? 0x8000 : 0);
#endif
}

void audio_route(int tv, int drc)
{
	routeTv = tv;
	routeDrc = drc;
	if (opened) route_apply();
}

void audio_shutdown(void)
{
	if (opened) SDL_CloseAudio();
	opened = 0;
}

void audio_pause(int pause)
{
	if (opened) SDL_PauseAudio(pause);
}

/* Linear resampling from the core's rate (44.1 kHz) to the device rate. */
void audio_push(const int16_t *in, size_t count)
{
	double step = inRate / outRate;
	if (!opened || !cfg.audio) return;
	/* dynamic rate control: with vsync the screen sets the pace (60 Hz)
	 * while the game makes sound for 59.94 frames a second; stretch or
	 * squeeze the sound by up to 0.5% to keep the buffer level steady
	 * (no crackles, not audible as pitch) */
	{
		double target = (double)audio_target_frames();
		double fill = (double)(wpos - rpos);
		double d = (target - fill) / target;
		if (d > 1) d = 1;
		if (d < -1) d = -1;
		step *= 1.0 - 0.005 * d;
	}
	SDL_LockAudio();
	while (resamplePos < (double)count) {
		size_t i = (size_t)resamplePos;
		double f = resamplePos - i;
		size_t j = i + 1 < count ? i + 1 : i;
		if (wpos - rpos >= RING_FRAMES - 1) break;   /* full: drop */
		uint32_t w = wpos & (RING_FRAMES - 1);
		ring[w * 2]     = (int16_t)(in[i * 2] + (in[j * 2] - in[i * 2]) * f);
		ring[w * 2 + 1] = (int16_t)(in[i * 2 + 1] + (in[j * 2 + 1] - in[i * 2 + 1]) * f);
		wpos++;
		resamplePos += step;
	}
	resamplePos -= (double)count;
	SDL_UnlockAudio();
}

size_t audio_queued_frames(void)
{
	return (size_t)(wpos - rpos);
}

size_t audio_target_frames(void)
{
	/* about three video frames of audio + one device buffer */
	return (size_t)(outRate / 60.0 * 3) + 1024;
}
