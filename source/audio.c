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

static volatile unsigned audUnder, audDrop;   /* game sound: ring ran dry / was full (for the log) */
static volatile Uint32 audLastPush;
static int audInUnder;

void audio_take_stats(unsigned *underruns, unsigned *drops)
{
	*underruns = audUnder;
	*drops = audDrop;
	audUnder = audDrop = 0;
}

static void music_mix(float *l, float *r);

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
			audInUnder = 0;
		} else if (!audInUnder && SDL_GetTicks() - audLastPush < 300) {
			audInUnder = 1;               /* a game is feeding the ring and it ran dry */
			audUnder++;
		}
		int l = lastL, r = lastR;
		if (sfxPlay && sfxPos < sfxEnd) {
			l += sfxPlay[sfxPos];
			r += sfxPlay[sfxPos];
			sfxPos++;
		}
		{   /* the menu music (a few % of full scale) */
			float ml, mr;
			music_mix(&ml, &mr);
			l += (int)(ml * 32767.0f);
			r += (int)(mr * 32767.0f);
		}
		out[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		out[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
}

/* styles of menu sound (cfg.menusfx): 1 classic, 2 soft, 3 retro 8-bit,
 * 4 crystal, 5 marimba, 6 click */
enum { ST_CLASSIC = 1, ST_SOFT, ST_RETRO, ST_CRYSTAL, ST_MARIMBA, ST_CLICK, ST_COUNT };

/* a sine table: the sounds are made from table lookups and running
 * products instead of sinf / expf per sample, so changing the style in
 * Settings is instant */
#define SINN 4096
static float sinTab[SINN];
static inline float tsin(float cycles)
{
	return sinTab[(int)(cycles * SINN) & (SINN - 1)];
}


/* ------------------------------------------------------------------------ */
/* menu music: a slow, soft, generative ambient piece, made here from sine  */
/* waves (no recording). Four chords of 8 seconds fade into each other      */
/* (pads and a low note), a few bell notes from the chords sound now and    */
/* then, and a stereo echo spreads it. Quiet by design.                     */
/* ------------------------------------------------------------------------ */
static volatile int musicTarget;       /* 1 = should play */
static float musicGain;                /* 0..1, faded */
static int musicWasOn;
enum { MU_CHORDS = 4, MU_NOTES = 5, MU_BELLS = 8, MU_ECHO_L = 17900, MU_ECHO_R = 25300 };
static const int MU_CH[MU_CHORDS][MU_NOTES] = {      /* MIDI notes: Dmaj9, Bm9, Gmaj9, Asus add9 */
	{ 50, 57, 61, 64, 66 }, { 47, 54, 57, 61, 62 }, { 43, 50, 54, 57, 59 }, { 45, 52, 55, 59, 62 },
};
static float muFreq[128];
static float muPad[MU_CHORDS][MU_NOTES][2];            /* oscillator phases */
static float muBass[MU_CHORDS];
static struct { float ph, ph2, inc, env, dec, panL, panR; int on; } muBell[MU_BELLS];
static float muDlL[MU_ECHO_L], muDlR[MU_ECHO_R];
static int muPosL, muPosR;
static uint32_t muT, muRng, muStep;

static uint32_t mu_rand(void)
{
	muRng = muRng * 1664525u + 1013904223u;
	return muRng >> 8;
}

static void music_reset(void)
{
	if (!sinTab[SINN / 4]) for (int i = 0; i < SINN; i++) sinTab[i] = sinf(6.2831853f * i / SINN);
	for (int n = 0; n < 128; n++) muFreq[n] = 440.0f * powf(2.0f, (n - 69) / 12.0f);
	memset(muPad, 0, sizeof(muPad));
	memset(muBass, 0, sizeof(muBass));
	memset(muBell, 0, sizeof(muBell));
	memset(muDlL, 0, sizeof(muDlL));
	memset(muDlR, 0, sizeof(muDlR));
	muPosL = muPosR = 0;
	muT = 0;
	muStep = 0xffffffffu;
	muRng = 2024;
}

/* one stereo sample of the music (before the master volume), in -1..1 */
static void music_sample(float *outL, float *outR)
{
	const double rate = outRate;
	const uint32_t L = (uint32_t)(8.0 * rate), cycle = L * MU_CHORDS;
	const float inv = 1.0f / (float)rate;
	float padL = 0, padR = 0;

	/* pads and the low note: each chord has a sin^2 window two chords long */
	for (int i = 0; i < MU_CHORDS; i++) {
		int64_t d = (int64_t)muT - (int64_t)i * L + L / 2;
		if (d < 0) d += cycle;
		if (d >= (int64_t)cycle) d -= cycle;
		float u = (float)d / (2.0f * (float)L);
		if (u >= 1.0f) continue;
		float sw = tsin(u * 0.5f), w = sw * sw;
		float sum = 0;
		for (int k = 0; k < MU_NOTES; k++) {
			float f = muFreq[MU_CH[i][k]];
			muPad[i][k][0] += f * 0.9988f * inv; if (muPad[i][k][0] >= 1) muPad[i][k][0] -= 1;
			muPad[i][k][1] += f * 1.0012f * inv; if (muPad[i][k][1] >= 1) muPad[i][k][1] -= 1;
			float o = tsin(muPad[i][k][0]) + tsin(muPad[i][k][1]);
			float amp = 0.030f - 0.003f * k;               /* the high notes a little softer */
			sum += o * amp * (k & 1 ? 0.8f : 1.0f);
			padL += o * amp * w * (k & 1 ? 0.65f : 1.0f);   /* a bit of stereo width: alternate notes lean */
			padR += o * amp * w * (k & 1 ? 1.0f : 0.65f);
		}
		(void)sum;
		muBass[i] += muFreq[MU_CH[i][0] - 12] * inv; if (muBass[i] >= 1) muBass[i] -= 1;
		float b = tsin(muBass[i]) * 0.070f * w;
		padL += b;
		padR += b;
	}
	/* a slow swell over the whole thing */
	float swell = 0.92f + 0.08f * tsin((float)muT * inv / 11.0f);
	padL *= swell;
	padR *= swell;

	/* bell notes: every half second, sometimes */
	if (muT % (L / 16) == 0 && muStep != muT / (L / 16)) {
		muStep = muT / (L / 16);
		int chord = (int)((muT / L) % MU_CHORDS);
		if (mu_rand() % 100 < 56) {
			int note = MU_CH[chord][mu_rand() % MU_NOTES] + 12 * (1 + (int)(mu_rand() % 2));
			for (int b = 0; b < MU_BELLS; b++)
				if (!muBell[b].on) {
					float pan = (mu_rand() & 1) ? 0.30f : 0.70f;
					muBell[b].on = 1;
					muBell[b].ph = muBell[b].ph2 = 0;
					muBell[b].inc = muFreq[note > 120 ? 120 : note] * inv;
					muBell[b].env = 1.0f;
					muBell[b].dec = expf(-1.0f / (1.1f * (float)rate));
					muBell[b].panL = 1.0f - pan;
					muBell[b].panR = pan;
					break;
				}
		}
	}
	float bellL = 0, bellR = 0;
	for (int b = 0; b < MU_BELLS; b++) {
		if (!muBell[b].on) continue;
		muBell[b].ph += muBell[b].inc; if (muBell[b].ph >= 1) muBell[b].ph -= 1;
		muBell[b].ph2 += muBell[b].inc * 2.76f; if (muBell[b].ph2 >= 1) muBell[b].ph2 -= 1;
		float v = (tsin(muBell[b].ph) + 0.18f * tsin(muBell[b].ph2)) * muBell[b].env * 0.060f;
		muBell[b].env *= muBell[b].dec;
		if (muBell[b].env < 0.002f) muBell[b].on = 0;
		bellL += v * muBell[b].panL * 1.4f;
		bellR += v * muBell[b].panR * 1.4f;
	}

	/* a cross-fed stereo echo (different lengths left and right) */
	float wetL = muDlL[muPosL], wetR = muDlR[muPosR];
	float inL = padL * 0.25f + bellL * 0.8f, inR = padR * 0.25f + bellR * 0.8f;
	muDlL[muPosL] = inL + wetR * 0.46f;
	muDlR[muPosR] = inR + wetL * 0.46f;
	if (++muPosL >= MU_ECHO_L) muPosL = 0;
	if (++muPosR >= MU_ECHO_R) muPosR = 0;

	*outL = padL + bellL + wetL * 0.60f;
	*outR = padR + bellR + wetR * 0.60f;
	if (++muT >= cycle) muT = 0;
}

/* mixed into the sound device's callback: fades in and out, silent when off */
static void music_mix(float *l, float *r)
{
	const float step = 1.0f / (float)(1.5 * outRate);
	if (musicTarget && !musicWasOn) { music_reset(); musicWasOn = 1; }
	if (musicTarget) { if (musicGain < 1) musicGain += step; if (musicGain > 1) musicGain = 1; }
	else if (musicGain > 0) { musicGain -= step * 1.5f; if (musicGain < 0) musicGain = 0; }
	if (musicGain <= 0) { musicWasOn = musicTarget; *l = *r = 0; return; }
	float ml, mr;
	music_sample(&ml, &mr);
	const float vol = 0.60f * musicGain;       /* the master volume: low */
	*l = ml * vol;
	*r = mr * vol;
}

/* the menu music on or off (fades); opens the sound device if it is closed */
void audio_music(int on)
{
	on = on ? 1 : 0;
	if (on && !opened) audio_init(44100.0);
	musicTarget = on;
}

/* p: phase in cycles; wood: the fading factor of the marimba's overtone */
static float wave(int style, float p, float wood)
{
	switch (style) {
	case ST_SOFT:    return tsin(p) + 0.15f * tsin(2 * p);
	case ST_RETRO:   return (p - (int)p) < 0.5f ? 0.55f : -0.55f;                     /* square */
	case ST_CRYSTAL: return tsin(p) + 0.5f * tsin(2.76f * p) + 0.25f * tsin(5.4f * p); /* bell */
	case ST_MARIMBA: return tsin(p) + 0.35f * tsin(4.0f * p) * wood;                 /* wood */
	default:         return tsin(p) + 0.3f * tsin(2 * p) + 0.12f * tsin(3 * p);
	}
}

/* notes in Hz, one after another, each with an exponential fade */
static void synth(int id, int style, const float *notes, int nnotes, float noteSec, float decay, float vol)
{
	if (!sinTab[SINN / 4]) for (int i = 0; i < SINN; i++) sinTab[i] = sinf(6.2831853f * i / SINN);
	int per = (int)(noteSec * SFX_RATE), tail = (int)(decay * SFX_RATE);
	int len = per * (nnotes - 1) + tail;
	int16_t *d = calloc(len, sizeof(int16_t));
	if (!d) return;
	uint32_t seed = 12345;
	/* per-sample factors of the fades */
	const float kEnv = expf(-5.0f / decay / SFX_RATE), k60 = expf(-60.0f / SFX_RATE),
	            k900 = expf(-900.0f / SFX_RATE), k200 = expf(-200.0f / SFX_RATE);
	for (int n = 0; n < nnotes; n++) {
		int start = n * per;
		float env = 1, e60 = 1, e900 = 1, e200 = 1, p = 0, dp = notes[n] / SFX_RATE;
		for (int i = 0; start + i < len && i < tail; i++) {
			float v;
			if (style == ST_CLICK) {           /* filtered noise burst */
				seed = seed * 1103515245u + 12345u;
				v = ((int)(seed >> 16 & 0x7fff) - 16384) / 16384.0f * e900 + 0.4f * tsin(p) * e200;
			} else v = wave(style, p, e60);
			float a = env * (i < 96 ? i / 96.0f : 1.0f);
			int x = d[start + i] + (int)(v * a * vol * 9000.0f);
			d[start + i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
			p += dp;
			if (p >= 1024.0f) p -= 1024.0f;    /* keep the float precise */
			env *= kEnv; e60 *= k60; e900 *= k900; e200 *= k200;
		}
	}
	sfxData[id] = d;
	sfxLen[id] = len;
}

static void sfx_build(void)
{
	static int builtFor = -1;
	int st = cfg.menusfx;
	if (st < ST_CLASSIC || st >= ST_COUNT) st = ST_SOFT;
	if (builtFor == st) return;
	/* stop what is playing before replacing the sounds */
	SDL_LockAudio();
	sfxPlay = NULL;
	SDL_UnlockAudio();
	for (int i = 0; i < SFX_COUNT; i++) { free(sfxData[i]); sfxData[i] = NULL; }
	builtFor = st;
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
	/* the opening chime is made when it is played (intro_build) */
}

static void intro_build(void)
{
	const float intro[5] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1568.0f };   /* C major, up */
	int st = cfg.menusfx;
	if (st < ST_CLASSIC || st >= ST_COUNT) st = ST_SOFT;
	if (!sfxData[SFX_INTRO])
		synth(SFX_INTRO, st == ST_CLICK || st == ST_RETRO ? ST_CLASSIC : st, intro, 5, 0.11f, 1.6f, 0.5f);
}

void sfx_play(int id)
{
	if (!cfg.menusfx || id < 0 || id >= SFX_COUNT) return;
	sfx_build();
	if (id == SFX_INTRO) intro_build();
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
	intro_build();
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
	audLastPush = SDL_GetTicks();
	SDL_LockAudio();
	while (resamplePos < (double)count) {
		size_t i = (size_t)resamplePos;
		double f = resamplePos - i;
		size_t j = i + 1 < count ? i + 1 : i;
		if (wpos - rpos >= RING_FRAMES - 1) { audDrop++; break; }   /* full: drop */
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
