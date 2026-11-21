/*
 * WiiUStation - audio output (SDL2 -> Wii U AX), 16-bit stereo.
 * The emulator thread pushes samples into a ring buffer; the SDL audio
 * callback drains it.  main.c paces emulation on the fill level
 * ("audio sync"), which keeps sound smooth and speed exact.
 */
#include <SDL2/SDL.h>
#include <string.h>
#include "platform.h"

#define RING_FRAMES 16384            /* stereo frames, power of two */
static int16_t ring[RING_FRAMES * 2];
static volatile uint32_t rpos, wpos; /* in frames */
static int opened;
static double outRate = 48000.0, inRate = 44100.0;
static double resamplePos;
static int16_t lastL, lastR;

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
		out[i * 2] = lastL;
		out[i * 2 + 1] = lastR;
	}
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
	rpos = wpos = 0;
	resamplePos = 0;
	opened = 1;
	return 0;
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
