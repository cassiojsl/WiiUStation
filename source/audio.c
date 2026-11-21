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

/* a few sine partials with an exponential fade; notes in Hz, one after another */
static void synth(int id, const float *notes, int nnotes, float noteSec, float decay, float vol)
{
	int per = (int)(noteSec * SFX_RATE), tail = (int)(decay * SFX_RATE);
	int len = per * (nnotes - 1) + tail;
	int16_t *d = calloc(len, sizeof(int16_t));
	if (!d) return;
	for (int n = 0; n < nnotes; n++) {
		int start = n * per;
		for (int i = 0; start + i < len && i < tail; i++) {
			float t = (float)i / SFX_RATE;
			float env = expf(-t * 5.0f / decay) * (i < 96 ? i / 96.0f : 1.0f);
			float v = sinf(6.2831853f * notes[n] * t) + 0.3f * sinf(6.2831853f * notes[n] * 2 * t)
			          + 0.12f * sinf(6.2831853f * notes[n] * 3 * t);
			int x = d[start + i] + (int)(v * env * vol * 9000.0f);
			d[start + i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		}
	}
	sfxData[id] = d;
	sfxLen[id] = len;
}

static void sfx_build(void)
{
	static int built;
	if (built) return;
	built = 1;
	const float move[1] = { 1318.5f };                         /* E6, a soft tick */
	const float ok[2] = { 987.8f, 1318.5f };                   /* B5 -> E6 */
	const float back[2] = { 1174.7f, 880.0f };                 /* D6 -> A5 */
	const float err[2] = { 311.1f, 293.7f };
	const float intro[5] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1568.0f };   /* C major, up */
	synth(SFX_MOVE, move, 1, 0, 0.05f, 0.35f);
	synth(SFX_OK, ok, 2, 0.05f, 0.12f, 0.45f);
	synth(SFX_BACK, back, 2, 0.05f, 0.10f, 0.4f);
	synth(SFX_ERROR, err, 2, 0.08f, 0.15f, 0.45f);
	synth(SFX_INTRO, intro, 5, 0.11f, 1.6f, 0.5f);
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
