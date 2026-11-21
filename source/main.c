/*
 * WiiUStation - standalone PlayStation emulator for the Wii U (Aroma).
 *
 * The emulator is PCSX-ReARMed, linked in statically through its libretro
 * API.  This file is the "host": it answers the core's environment calls,
 * feeds it input, takes its video/audio, runs the frame loop on core 1 and
 * handles saves, states and settings.  No RetroArch involved.
 */
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sysapp/launch.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "platform.h"
#include "libretro.h"

wiiu_settings cfg;
extern const int default_map[16];
static void recent_add(const char *path);
static void cheats_load(void);

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */
int wiiu_mkdirs(const char *path)
{
	char tmp[512];
	size_t skip = strlen(WIIU_SD_ROOT);
	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;
	for (char *p = tmp + skip + 1; *p; p++)
		if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
	return mkdir(tmp, 0777) == 0 || errno == EEXIST;
}

static int read_file(const char *path, void *buf, size_t size)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) return -1;
	n = fread(buf, 1, size, f);
	fclose(f);
	return (int)n;
}

static int write_file(const char *path, const void *buf, size_t size)
{
	FILE *f = fopen(path, "wb");
	size_t n;
	if (!f) return -1;
	n = fwrite(buf, 1, size, f);
	fclose(f);
	return n == size ? 0 : -1;
}

/* ------------------------------------------------------------------------ */
/* settings                                                                 */
/* ------------------------------------------------------------------------ */
/* WIIU_BTN bit numbers: 0 A 1 B 2 X 3 Y 4 Up 5 Down 6 Left 7 Right 8 L 9 R
 * 10 ZL 11 ZR 12 + 13 - 14 L3 15 R3 */
const int default_hotkeys[HK_COUNT] = {
	13,  /* modifier: - (Select) */
	9,   /* save state: R */
	8,   /* load state: L */
	7,   /* next slot: Right */
	6,   /* previous slot: Left */
	3,   /* screenshot: Y */
	11,  /* fast forward: ZR */
	2,   /* pause: X */
	-1,  /* reset: none */
};

const char *hotkey_name(int i)
{
	static const char *n[HK_COUNT] = { "Hotkey button (hold)", "Save state", "Load state",
		"Next slot", "Previous slot", "Screenshot", "Fast forward on/off", "Pause", "Reset" };
	return i >= 0 && i < HK_COUNT ? n[i] : "";
}

const char *button_name(int bit)
{
	static const char *n[16] = { "A", "B", "X", "Y", "Up", "Down", "Left", "Right",
		"L", "R", "ZL", "ZR", "+", "-", "L3", "R3" };
	return bit >= 0 && bit < 16 ? n[bit] : "None";
}

static struct { const char *key; int *val; int min, max; } OPTS[] = {
	{ "dynarec",    &cfg.dynarec,    0, 1 },
	{ "gpu_thread", &cfg.gpu_thread, 0, 1 },
	{ "spu_thread", &cfg.spu_thread, 0, 1 },
	{ "frameskip",  &cfg.frameskip,  0, 1 },
	{ "show_fps",   &cfg.show_fps,   0, 1 },
	{ "aspect",     &cfg.aspect,     0, 3 },
	{ "filter",     &cfg.filter,     0, 2 },
	{ "scanlines",  &cfg.scanlines,  0, 1 },
	{ "crt",        &cfg.crt,        0, 1 },
	{ "reverb",     &cfg.reverb,     0, 1 },
	{ "dithering",  &cfg.dithering,  0, 2 },
	{ "bios_hle",   &cfg.bios_hle,   0, 1 },
	{ "bootlogo",   &cfg.bootlogo,   0, 1 },
	{ "analog",     &cfg.analog,     0, 1 },
	{ "analog2",    &cfg.analog2,    0, 1 },
	{ "multitap",   &cfg.multitap,   0, 1 },
	{ "region",     &cfg.region,     0, 2 },
	{ "resume",     &cfg.resume,     0, 1 },
	{ "ffwd",       &cfg.ffwd,       0, 3 },
	{ "ffspeed",    &cfg.ffspeed,    2, 4 },
	{ "rumble",     &cfg.rumble,     0, 1 },
	{ "audio",      &cfg.audio,      0, 1 },
	{ "bezel",      &cfg.bezel,      0, BEZEL_COUNT - 1 },
	{ "view",       &cfg.view,       0, 4 },
	{ "cover3d",    &cfg.cover3d,    0, 1 },
	{ "lang",       &cfg.lang,       0, 1 },
	{ "hk0", &cfg.hk[0], -1, 15 }, { "hk1", &cfg.hk[1], -1, 15 }, { "hk2", &cfg.hk[2], -1, 15 },
	{ "hk3", &cfg.hk[3], -1, 15 }, { "hk4", &cfg.hk[4], -1, 15 }, { "hk5", &cfg.hk[5], -1, 15 },
	{ "hk6", &cfg.hk[6], -1, 15 }, { "hk7", &cfg.hk[7], -1, 15 }, { "hk8", &cfg.hk[8], -1, 15 },
};

/* ---- extra PCSX-ReARMed options (Advanced) ---- */
typedef struct {
	const char *key;        /* in settings.cfg: c_<key>; in the core: pcsx_rearmed_<key> */
	const char *label, *section, *help;
	int n, def;
	const char *vals[8], *names[8];
} coreopt_t;

#define OFFON  { "disabled", "enabled" }, { "Off", "On" }
static const coreopt_t COREOPT[] = {
	{ "psxclock", "PS1 CPU clock", "Speed",
	  "Higher removes slowdowns the real console had. Auto is safest.", 7, 0,
	  { "auto", "50", "57", "65", "75", "85", "100" },
	  { "Auto", "50%", "57%", "65%", "75%", "85%", "100%" } },
	{ "cd_turbo", "Fast CD loading", NULL,
	  "Shorter loading screens. Turn off if a game hangs while loading.", 2, 0, OFFON },
	{ "nostalls", "Skip CPU stalls", NULL,
	  "A little faster. May break timing in some games.", 2, 0, OFFON },
	{ "spu_interpolation", "Sound interpolation", "Sound",
	  "Gaussian sounds like the real console. Off is the fastest.", 4, 0,
	  { "simple", "gaussian", "cubic", "off" }, { "Simple", "Gaussian", "Cubic", "Off" } },
	{ "nocdaudio", "CD audio tracks", NULL,
	  "Music played from the disc's audio tracks.", 2, 0,
	  { "disabled", "enabled" }, { "On", "Off" } },
	{ "noxadecoding", "XA audio (music, voices)", NULL,
	  "Compressed music and voices. Off saves CPU but silences them.", 2, 0,
	  { "disabled", "enabled" }, { "On", "Off" } },
	{ "show_overscan", "Show overscan", "Picture",
	  "Shows the area at the screen edges that TVs used to hide.", 3, 0,
	  { "disabled", "auto", "hack" }, { "Off", "Auto", "Hack" } },
	{ "screen_centering", "Screen centering", NULL,
	  "Borderless removes side borders some games draw.", 3, 0,
	  { "auto", "game", "borderless" }, { "Auto", "Game", "Borderless" } },
	{ "gpu_slow_llists", "Slow GPU list walking", "Compatibility",
	  "Needed by a few games that hang or show broken graphics.", 3, 0,
	  { "auto", "disabled", "enabled" }, { "Auto", "Off", "On" } },
	{ "icache_emulation", "Instruction cache", NULL,
	  "Needed by a few games (e.g. Formula One 2001). Interpreter only.", 2, 0,
	  { "enabled", "disabled" }, { "On", "Off" } },
	{ "exception_emulation", "Exception emulation", NULL,
	  "Rarely needed. Interpreter only.", 2, 0, OFFON },
	{ "gpu_peops_odd_even_bit", "Odd/even bit hack", "GPU fixes",
	  "Fixes lock-ups in games such as Chrono Cross.", 2, 0, OFFON },
	{ "gpu_peops_expand_screen_width", "Expand screen width", NULL,
	  "For Capcom 2D fighting games: shows the whole background.", 2, 0, OFFON },
	{ "gpu_peops_ignore_brightness", "Ignore brightness colour", NULL,
	  "Fixes black screens in Lunar: Silver Star Story.", 2, 0, OFFON },
	{ "gpu_peops_disable_coord_check", "Disable coordinate check", NULL,
	  "Old compatibility mode. Only if a game draws wrongly.", 2, 0, OFFON },
	{ "gpu_peops_lazy_screen_update", "Lazy screen update", NULL,
	  "Fixes flickering text boxes in Dragon Warrior VII, Pandemonium 2.", 2, 0, OFFON },
	{ "gpu_peops_repeated_triangles", "Repeated flat triangles", NULL,
	  "Fixes graphics in Star Wars: Dark Forces.", 2, 0, OFFON },
	{ "gpu_peops_quads_with_triangles", "Draw quads as triangles", NULL,
	  "Fixes distorted shading, with slightly worse textures.", 2, 0, OFFON },
	{ "gpu_peops_fake_busy_state", "Fake GPU busy state", NULL,
	  "Emulates the GPU busy flag. Fixes some games, a bit slower.", 2, 0, OFFON },
};
#define NCOREOPT (int)(sizeof(COREOPT) / sizeof(COREOPT[0]))
_Static_assert(sizeof(COREOPT) / sizeof(COREOPT[0]) <= 24, "cfg.core too small");

int core_opt_count(void) { return NCOREOPT; }
const char *core_opt_label(int i) { return COREOPT[i].label; }
const char *core_opt_section(int i) { return COREOPT[i].section; }
const char *core_opt_help(int i) { return COREOPT[i].help; }
const char *core_opt_value(int i)
{
	int v = cfg.core[i];
	return COREOPT[i].names[v >= 0 && v < COREOPT[i].n ? v : 0];
}
void core_opt_cycle(int i, int dir)
{
	cfg.core[i] = (cfg.core[i] + COREOPT[i].n + dir) % COREOPT[i].n;
}

/* keys that only make sense globally (never in a per-game file) */
static int global_only(const char *k)
{
	return !strcmp(k, "view") || !strcmp(k, "cover3d") || !strcmp(k, "lang") || !strncmp(k, "hk", 2) || !strcmp(k, "games_dir") || !strcmp(k, "gpu_thread") ||
	       !strcmp(k, "spu_thread") || !strcmp(k, "resume") || !strcmp(k, "ffwd");
}

#define GAMECFG_DIR WIIU_APP_DIR "/gamesettings"
static char gameKey[64];        /* set while a game is loaded */
static int  gameScope;          /* editing the per-game file */

static void settings_defaults(void)
{
	cfg.dynarec = 1;
	cfg.gpu_thread = 1;   /* renderer on core 2 */
	cfg.spu_thread = 0;   /* off: sound was lost with Lightrec + SPU thread */
	cfg.frameskip = 0;
	cfg.show_fps = 0;
	cfg.aspect = WIIU_ASPECT_ZOOM;   /* 4:3 with the game's own black borders cropped */
	cfg.filter = WIIU_FILTER_LINEAR;
	cfg.scanlines = 0;
	cfg.crt = 0;
	cfg.reverb = 1;
	cfg.dithering = 1;
	cfg.bios_hle = 0;
	cfg.bootlogo = 0;
	cfg.analog = 0;       /* digital pad: works with every game */
	cfg.analog2 = 0;
	cfg.multitap = 0;
	cfg.region = 0;
	cfg.resume = 1;
	cfg.ffwd = 1;         /* hold R3 */
	cfg.ffspeed = 3;
	cfg.rumble = 1;
	cfg.audio = 1;
	cfg.bezel = BEZEL_AMBIENT;
	cfg.view = 4;         /* carousel */
	for (int i = 0; i < NCOREOPT; i++) cfg.core[i] = COREOPT[i].def;
	memcpy(cfg.hk, default_hotkeys, sizeof(cfg.hk));
	strcpy(cfg.games_dir, WIIU_ISO_DIR);
	memcpy(cfg.map, default_map, sizeof(cfg.map));
}

static int settings_parse(const char *path, int isGame)
{
	char line[300];
	FILE *f = fopen(path, "r");
	int version = 0;
	if (!f) return 0;
	/* files written before version 2 had the threaded GPU off and DualShock
	 * on; before version 3 the SPU thread on and a broken filter */
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, "version", 7)) version = atoi(strchr(line, '=') ? strchr(line, '=') + 1 : "0");
	rewind(f);
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#') continue;
		*eq = 0;
		char *k = line;
		while (*k == ' ') k++;
		for (char *e = eq - 1; e >= k && *e == ' '; e--) *e = 0;
		if (isGame && global_only(k)) continue;
		if (!strcmp(k, "games_dir")) {
			char *v = eq + 1;
			while (*v == ' ') v++;
			v[strcspn(v, "\r\n")] = 0;
			if (!strncmp(v, WIIU_SD_ROOT, strlen(WIIU_SD_ROOT)))
				strncpy(cfg.games_dir, v, sizeof(cfg.games_dir) - 1);
			continue;
		}
		if (!strncmp(k, "map", 3) && k[3] >= '0' && k[3] <= '9') {
			int id = atoi(k + 3), v = atoi(eq + 1);
			if (id >= 0 && id < 16 && v >= 0 && v < 16) cfg.map[id] = v;
			continue;
		}
		if (!strncmp(k, "c_", 2)) {
			int v = atoi(eq + 1);
			for (int i = 0; i < NCOREOPT; i++)
				if (!strcmp(COREOPT[i].key, k + 2) && v >= 0 && v < COREOPT[i].n) cfg.core[i] = v;
			continue;
		}
		int v = atoi(eq + 1);
		if (version < 2 && (!strcmp(k, "gpu_thread") || !strcmp(k, "analog")))
			continue;
		if (version < 3 && (!strcmp(k, "spu_thread") || !strcmp(k, "filter")))
			continue;
		if (version < 4 && !strcmp(k, "aspect"))     /* Auto zoom became the default */
			continue;
		if (version < 5 && !strcmp(k, "view"))       /* the carousel became the default */
			continue;
		for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
			if (!strcmp(OPTS[i].key, k) && v >= OPTS[i].min && v <= OPTS[i].max)
				*OPTS[i].val = v;
	}
	fclose(f);
	return 1;
}

static int settings_write(const char *path, int isGame)
{
	FILE *f = fopen(path, "w");
	if (!f) return 0;
	fprintf(f, "# WiiUStation %s\nversion = 5\n", isGame ? "per-game settings" : "settings");
	for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
		if (!isGame || !global_only(OPTS[i].key))
			fprintf(f, "%s = %d\n", OPTS[i].key, *OPTS[i].val);
	for (int i = 0; i < NCOREOPT; i++) fprintf(f, "c_%s = %d\n", COREOPT[i].key, cfg.core[i]);
	if (!isGame) fprintf(f, "games_dir = %s\n", cfg.games_dir);
	for (int i = 0; i < 16; i++) fprintf(f, "map%d = %d\n", i, cfg.map[i]);
	fclose(f);
	return 1;
}

static void game_cfg_path(char *out, size_t n)
{
	snprintf(out, n, "%s/%s.cfg", GAMECFG_DIR, gameKey);
}

void settings_load(void)
{
	settings_defaults();
	if (!settings_parse(WIIU_CONFIG_FILE, 0))
		settings_parse(WIIU_OLD_DIR "/wiiustation.cfg", 0);   /* older builds */
	if (gameKey[0]) {
		char p[256];
		game_cfg_path(p, sizeof(p));
		gameScope = settings_parse(p, 1);
	}
	if (!jit_available()) cfg.dynarec = 0;
}

int settings_save(void)
{
	char p[256];
	if (gameKey[0] && gameScope) {
		wiiu_mkdirs(GAMECFG_DIR);
		game_cfg_path(p, sizeof(p));
		/* globals edited while in game scope still go to the main file */
		return settings_write(p, 1);
	}
	wiiu_mkdirs(WIIU_APP_DIR);
	return settings_write(WIIU_CONFIG_FILE, 0);
}

void settings_enter_game(const char *key)
{
	snprintf(gameKey, sizeof(gameKey), "%s", key);
	for (char *c = gameKey; *c; c++)
		if (*c == '/' || *c == ':' || *c == '\\') *c = '_';
	settings_load();
}

void settings_leave_game(void)
{
	gameKey[0] = 0;
	gameScope = 0;
	settings_load();
}

int settings_game_scope(void) { return gameKey[0] && gameScope; }

int settings_set_game_scope(int on)
{
	char p[256];
	if (!gameKey[0]) return 0;
	game_cfg_path(p, sizeof(p));
	if (on) {
		gameScope = 1;
		return settings_save();          /* starts as a copy of the current values */
	}
	remove(p);
	gameScope = 0;
	settings_load();                     /* back to the global values */
	return 1;
}

/* ------------------------------------------------------------------------ */
/* libretro host                                                            */
/* ------------------------------------------------------------------------ */
static struct retro_system_av_info av;
static struct retro_disk_control_callback disk;
static struct retro_disk_control_ext_callback diskExt;
static int haveDisk, haveDiskExt;
static volatile int varsDirty = 1;
static char gamePath[512], gameName[256], gameId[16];
extern char CdromId[];   /* PCSX-ReARMed: e.g. "SCUS94244" */
static int loaded;

/* BIOS: the app folder, or the folder older builds used if it has one */
static const char *bios_dir(void)
{
	static const char *names[] = { "scph1001.bin", "SCPH1001.BIN", "scph5501.bin", "SCPH5501.BIN",
	                               "scph7001.bin", "SCPH7001.BIN", "scph5500.bin", "scph5502.bin", NULL };
	char p[256];
	struct stat st;
	for (int i = 0; names[i]; i++) {
		snprintf(p, sizeof(p), "%s/%s", WIIU_BIOS_DIR, names[i]);
		if (!stat(p, &st)) return WIIU_BIOS_DIR;
	}
	for (int i = 0; names[i]; i++) {
		snprintf(p, sizeof(p), "%s/bios/%s", WIIU_OLD_DIR, names[i]);
		if (!stat(p, &st)) return WIIU_OLD_DIR "/bios";
	}
	return WIIU_BIOS_DIR;
}

static const char *core_var(const char *key)
{
	if (!strcmp(key, "pcsx_rearmed_drc"))
		return cfg.dynarec && jit_available() ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_drc_thread")) return "disabled";
	if (!strcmp(key, "pcsx_rearmed_gpu_thread_rendering"))
		return cfg.gpu_thread ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_spu_thread"))
		return cfg.spu_thread ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_frameskip_type"))
		return cfg.frameskip ? "auto" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_dithering"))
		return cfg.dithering == 0 ? "disabled" : cfg.dithering == 1 ? "enabled" : "force";
	if (!strcmp(key, "pcsx_rearmed_bios")) return cfg.bios_hle ? "HLE" : "auto";
	if (!strcmp(key, "pcsx_rearmed_show_bios_bootlogo")) return cfg.bootlogo ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_spu_reverb")) return cfg.reverb ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_multitap")) return cfg.multitap ? "port 1" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_region"))
		return cfg.region == 1 ? "NTSC" : cfg.region == 2 ? "PAL" : "auto";
	if (!strcmp(key, "pcsx_rearmed_vibration")) return cfg.rumble ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_display_fps_v2")) return "disabled";
	if (!strcmp(key, "pcsx_rearmed_rgb32_output")) return "disabled"; /* RGB565: cheapest upload */
	if (!strncmp(key, "pcsx_rearmed_", 13))
		for (int i = 0; i < NCOREOPT; i++)
			if (!strcmp(key + 13, COREOPT[i].key)) {
				int v = cfg.core[i];
				return COREOPT[i].vals[v >= 0 && v < COREOPT[i].n ? v : 0];
			}
	return NULL;   /* core default */
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level; (void)fmt;
}

static bool rumble_cb(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
	static uint16_t s[2][2];
	if (port > 1) return false;
	s[port][effect == RETRO_RUMBLE_STRONG ? 0 : 1] = strength;
	/* port 0 -> GamePad if present, otherwise first Pro; port 1 -> next */
	extern int pad_for_port(int port);
	int pad = pad_for_port(port);
	if (pad >= 0) wiiu_input_rumble(pad, cfg.rumble && (s[port][0] || s[port][1]));
	return true;
}

static bool environment(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = bios_dir();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = WIIU_SAVE_DIR;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		enum retro_pixel_format f = *(enum retro_pixel_format *)data;
		if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_XRGB8888) return false;
		video_set_pixel_format(f);
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		v->value = core_var(v->key);
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = varsDirty;
		varsDirty = 0;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
		((struct retro_rumble_interface *)data)->set_rumble_state = rumble_cb;
		return true;
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
		disk = *(struct retro_disk_control_callback *)data;
		haveDisk = 1;
		return true;
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
		diskExt = *(struct retro_disk_control_ext_callback *)data;
		haveDiskExt = 1;
		return true;
	case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
		*(unsigned *)data = 1;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		av = *(struct retro_system_av_info *)data;
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
		av.geometry = *(struct retro_game_geometry *)data;
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE:
		video_osd(((struct retro_message *)data)->msg);
		return true;
	case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
		*(unsigned *)data = 0;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 0;   /* legacy variables: we answer GET_VARIABLE */
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		*(int *)data = 3;
		return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		return true;
	default:
		return false;
	}
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
	video_upload(data, w, h, pitch);
}

static void audio_sample_cb(int16_t l, int16_t r)
{
	int16_t s[2] = { l, r };
	audio_push(s, 1);
}

static int ffMuted;      /* fast forward: extra frames make no sound */

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
	if (!ffMuted) audio_push(d, frames);
	return frames;
}

/* ---- input: PSX pad index -> Wii U controller ----
 * Players are the connected controllers in order (GamePad first). Without a
 * multitap, player 1 = pad index 0 and player 2 = index 1. With a multitap
 * on port 1, players 1-4 = indices 0-3 and player 5 = index 4 (port 2). */
#define NPORTS 8
static int portPad[NPORTS];

int pad_for_port(int port)
{
	return port >= 0 && port < NPORTS ? portPad[port] : -1;
}

static void assign_pads(void)
{
	int players[WIIU_MAX_PADS], n = 0;
	for (int i = 0; i < NPORTS; i++) portPad[i] = -1;
	for (int i = 0; i < WIIU_MAX_PADS; i++)
		if (wiiu_input_get(i)->connected) players[n++] = i;
	if (!n) players[n++] = 0;
	for (int k = 0; k < n; k++) {
		int port = cfg.multitap ? (k < 4 ? k : 4) : (k == 0 ? 0 : k == 1 ? 1 : -1);
		if (port >= 0) portPad[port] = players[k];
	}
}

int emu_player_count(void)
{
	int n = 0;
	for (int i = 0; i < NPORTS; i++) if (portPad[i] >= 0) n++;
	return n;
}

/* pad type of a PSX pad index: player 1 uses cfg.analog, the others analog2 */
static int port_is_analog(int port) { return port == 0 ? cfg.analog : cfg.analog2; }

static void input_poll_cb(void)
{
	wiiu_input_update();
}

/* Default mapping, by position: A=Circle B=Cross X=Triangle Y=Square.
 * Index = RETRO_DEVICE_ID_JOYPAD_*, value = WIIU_BTN_* bit number. */
const int default_map[16] = {
	1,   /* B      (Cross)    <- B  */
	3,   /* Y      (Square)   <- Y  */
	13,  /* SELECT            <- -  */
	12,  /* START             <- +  */
	4, 5, 6, 7,                     /* d-pad */
	0,   /* A      (Circle)   <- A  */
	2,   /* X      (Triangle) <- X  */
	8, 9,                           /* L1 R1 <- L R */
	10, 11,                         /* L2 R2 <- ZL ZR */
	14, 15,                         /* L3 R3 */
};

static uint32_t hkHide;   /* WIIU buttons used by a hotkey right now */

static uint32_t psx_mask(const wiiu_pad_state *p, int analog)
{
	uint32_t b = p->held & ~hkHide, m = 0;
	/* Digital pad: the left stick also drives the d-pad */
	if (!analog) {
		if (p->ly > 0.5f)  b |= WIIU_BTN_UP;
		if (p->ly < -0.5f) b |= WIIU_BTN_DOWN;
		if (p->lx < -0.5f) b |= WIIU_BTN_LEFT;
		if (p->lx > 0.5f)  b |= WIIU_BTN_RIGHT;
	}
	for (int id = 0; id < 16; id++) {
		int bit = cfg.map[id];
		if (bit >= 0 && bit < 16 && (b & (1u << bit))) m |= 1u << id;
	}
	return m;
}

/* while fast forward is held on R3, the game must not see R3 */
static uint32_t ffwdMask = 0xffffffffu;

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	int pad = pad_for_port(port);
	const wiiu_pad_state *p;
	if (pad < 0) return 0;
	p = wiiu_input_get(pad);
	if (!p->connected) return 0;

	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: {
		uint32_t m = psx_mask(p, port_is_analog(port)) & ffwdMask;
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
		return (m >> id) & 1;
	}
	case RETRO_DEVICE_ANALOG: {
		float v;
		if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
			uint32_t m = psx_mask(p, port_is_analog(port)) & ffwdMask;
			return ((m >> id) & 1) ? 0x7fff : 0;
		}
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			v = id == RETRO_DEVICE_ID_ANALOG_X ? p->lx : -p->ly;
		else
			v = id == RETRO_DEVICE_ID_ANALOG_X ? p->rx : -p->ry;
		return (int16_t)(v * 32767.0f);
	}
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* game management                                                          */
/* ------------------------------------------------------------------------ */
static void srm_path(char *out, size_t sz)
{
	char base[256];
	strncpy(base, gameName, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
	snprintf(out, sz, "%s/%s.srm", WIIU_SAVE_DIR, base);
}

static uint8_t *srmShadow;

void emu_save_memcards(void)
{
	char path[512];
	size_t size;
	void *data;
	if (!loaded) return;
	size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (!size || !data) return;
	if (srmShadow && !memcmp(srmShadow, data, size)) return;   /* unchanged */
	srm_path(path, sizeof(path));
	wiiu_mkdirs(WIIU_SAVE_DIR);
	if (write_file(path, data, size) == 0) {
		if (!srmShadow) srmShadow = malloc(size);
		if (srmShadow) memcpy(srmShadow, data, size);
	}
}

static void load_memcard(void)
{
	char path[512];
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	free(srmShadow);
	srmShadow = NULL;
	if (!size || !data) return;
	srm_path(path, sizeof(path));
	if (read_file(path, data, size) <= 0) {
		/* memory card written by an older build */
		char old[512], base[256];
		strncpy(base, gameName, sizeof(base) - 1);
		base[sizeof(base) - 1] = 0;
		char *dot = strrchr(base, '.');
		if (dot) *dot = 0;
		snprintf(old, sizeof(old), "%s/saves/%s.srm", WIIU_OLD_DIR, base);
		/* if found, srmShadow stays NULL so the next save writes it to the
		 * new folder */
		read_file(old, data, size);
		return;
	}
	{
		srmShadow = malloc(size);
		if (srmShadow) memcpy(srmShadow, data, size);
	}
}

void emu_apply_settings(void)
{
	varsDirty = 1;
	if (loaded) {
		for (int port = 0; port < NPORTS; port++) {
			unsigned dev = port_is_analog(port) ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
			                                    : RETRO_DEVICE_JOYPAD;
			retro_set_controller_port_device(port, dev);
		}
	}
}

/* Multi-disc: start an .m3u at a given disc (disc_path as the core will
 * build it: <m3u folder>/<entry>). */
int emu_load_game_disc(const char *m3u, int index, const char *disc_path)
{
	if (haveDiskExt && diskExt.set_initial_image && index > 0)
		diskExt.set_initial_image(index, disc_path);
	return emu_load_game(m3u);
}

int emu_load_game(const char *path)
{
	struct retro_game_info gi;
	const char *slash = strrchr(path, '/');

	if (loaded) emu_unload_game();
	memset(&gi, 0, sizeof(gi));
	gi.path = path;
	strncpy(gamePath, path, sizeof(gamePath) - 1);
	strncpy(gameName, slash ? slash + 1 : path, sizeof(gameName) - 1);

	/* per-game settings are keyed by the file name; applied before the core
	 * reads its options (region, dynarec, BIOS...) */
	{
		char key[256];
		snprintf(key, sizeof(key), "%s", gameName);
		char *dot = strrchr(key, '.');
		if (dot) *dot = 0;
		settings_enter_game(key);
	}
	varsDirty = 1;
	if (!retro_load_game(&gi)) { settings_leave_game(); return -1; }
	loaded = 1;
	retro_get_system_av_info(&av);
	emu_apply_settings();
	load_memcard();
	audio_init(av.timing.sample_rate);
	covers_set_loaded_id(path, CdromId);
	if (!covers_detect_id(path, gameId)) gameId[0] = 0;
	video_set_game_id(gameId);
	recent_add(path);
	cheats_load();
	return 0;
}

void emu_unload_game(void)
{
	if (!loaded) return;
	emu_save_memcards();
	if (cfg.resume) emu_state_save(-1);     /* "continue where you left off" */
	retro_unload_game();
	audio_shutdown();
	loaded = 0;
	gameName[0] = 0;
	settings_leave_game();
}

/* ---- recently played (recent.txt, newest first) ---- */
#define RECENT_FILE WIIU_APP_DIR "/recent.txt"
#define RECENT_MAX  12
static char recent[RECENT_MAX][512];
static int nrecent = -1;

static void recent_load(void)
{
	FILE *f;
	char line[512];
	if (nrecent >= 0) return;
	nrecent = 0;
	f = fopen(RECENT_FILE, "r");
	if (!f) return;
	while (nrecent < RECENT_MAX && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		struct stat st;
		if (line[0] && !stat(line, &st)) strcpy(recent[nrecent++], line);
	}
	fclose(f);
}

static void recent_add(const char *path)
{
	recent_load();
	int i, n = nrecent;
	for (i = 0; i < n; i++) if (!strcmp(recent[i], path)) break;
	if (i == n) { if (n < RECENT_MAX) n++; i = n - 1; }
	for (; i > 0; i--) strcpy(recent[i], recent[i - 1]);
	strncpy(recent[0], path, sizeof(recent[0]) - 1);
	nrecent = n;
	FILE *f = fopen(RECENT_FILE, "w");
	if (!f) return;
	for (i = 0; i < nrecent; i++) fprintf(f, "%s\n", recent[i]);
	fclose(f);
}

int emu_recent_count(void) { recent_load(); return nrecent; }
const char *emu_recent(int i) { recent_load(); return i >= 0 && i < nrecent ? recent[i] : ""; }

int emu_game_loaded(void) { return loaded; }
const char *emu_game_path(void) { return gamePath; }
const char *emu_game_id(void) { return gameId; }
const char *emu_game_name(void) { return gameName; }
void emu_reset(void) { if (loaded) retro_reset(); }

static void state_path(int slot, char *out, size_t sz)
{
	char base[256];
	strncpy(base, gameName, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
	if (slot < 0) snprintf(out, sz, "%s/%s.auto", WIIU_STATE_DIR, base);
	else snprintf(out, sz, "%s/%s.state%d", WIIU_STATE_DIR, base, slot);
}

int emu_state_save(int slot)
{
	char path[512];
	size_t size;
	void *buf;
	int ok = 0;
	if (!loaded) return 0;
	size = retro_serialize_size();
	buf = malloc(size);
	if (!buf) return 0;
	if (retro_serialize(buf, size)) {
		wiiu_mkdirs(WIIU_STATE_DIR);
		state_path(slot, path, sizeof(path));
		ok = write_file(path, buf, size) == 0;
		if (ok && slot >= 0) {            /* thumbnail for the slot picker */
			char png[600];
			snprintf(png, sizeof(png), "%s.png", path);
			video_save_frame(png, 320);   /* 320x240: big enough for the preview */
		}
	}
	free(buf);
	return ok;
}

int emu_state_load(int slot)
{
	char path[512];
	size_t size;
	void *buf;
	int ok = 0;
	if (!loaded) return 0;
	size = retro_serialize_size();
	buf = malloc(size);
	if (!buf) return 0;
	state_path(slot, path, sizeof(path));
	if (read_file(path, buf, size) > 0)
		ok = retro_unserialize(buf, size);
	free(buf);
	return ok;
}

int emu_state_exists(int slot)
{
	char path[512];
	struct stat st;
	if (!loaded) return 0;
	state_path(slot, path, sizeof(path));
	return stat(path, &st) == 0;
}

/* when the slot was saved (0 = empty) */
long long emu_state_time(int slot)
{
	char path[512];
	struct stat st;
	if (!loaded) return 0;
	state_path(slot, path, sizeof(path));
	return stat(path, &st) == 0 ? (long long)st.st_mtime : 0;
}

/* path of a slot's thumbnail (may not exist) */
void emu_state_thumb(int slot, char *out, size_t n)
{
	char path[512];
	state_path(slot, path, sizeof(path));
	snprintf(out, n, "%s.png", path);
}

/* ---- screenshots ---- */
int emu_screenshot(void)
{
	char path[512], base[256];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (!loaded) return 0;
	snprintf(base, sizeof(base), "%s", gameName);
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
	wiiu_mkdirs(WIIU_APP_DIR "/screenshots");
	snprintf(path, sizeof(path), "%s/screenshots/%s-%04d%02d%02d-%02d%02d%02d.png", WIIU_APP_DIR, base,
	         tm ? tm->tm_year + 1900 : 0, tm ? tm->tm_mon + 1 : 0, tm ? tm->tm_mday : 0,
	         tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0);
	return video_save_frame(path, 0);
}

/* ---- cheats: libretro .cht files in WiiUStation/cheats ---- */
#define MAX_CHEATS 128
static struct { char desc[96]; char code[512]; int on; } cheats[MAX_CHEATS];
static int ncheats;
static char chtPath[512];

static char *cht_value(char *v)
{
	while (*v == ' ' || *v == '\t') v++;
	v[strcspn(v, "\r\n")] = 0;
	if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
	return v;
}

static void cheats_apply(void)
{
	retro_cheat_reset();
	for (int i = 0; i < ncheats; i++)
		if (cheats[i].on) retro_cheat_set(i, true, cheats[i].code);
}

static void cheats_load(void)
{
	char base[256], line[700];
	FILE *f = NULL;
	ncheats = 0;
	chtPath[0] = 0;
	snprintf(base, sizeof(base), "%s", gameName);
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
	/* <file name>.cht, else <GAME-ID>.cht */
	snprintf(chtPath, sizeof(chtPath), "%s/cheats/%s.cht", WIIU_APP_DIR, base);
	f = fopen(chtPath, "r");
	if (!f && gameId[0]) {
		snprintf(chtPath, sizeof(chtPath), "%s/cheats/%s.cht", WIIU_APP_DIR, gameId);
		f = fopen(chtPath, "r");
	}
	if (!f) { chtPath[0] = 0; return; }
	while (fgets(line, sizeof(line), f)) {
		int idx;
		char field[16];
		char *eq = strchr(line, '=');
		if (!eq || sscanf(line, "cheat%d_%15[a-z]", &idx, field) != 2) continue;
		if (idx < 0 || idx >= MAX_CHEATS) continue;
		char *v = cht_value(eq + 1);
		if (!strcmp(field, "desc")) snprintf(cheats[idx].desc, sizeof(cheats[idx].desc), "%s", v);
		else if (!strcmp(field, "code")) snprintf(cheats[idx].code, sizeof(cheats[idx].code), "%s", v);
		else if (!strcmp(field, "enable")) cheats[idx].on = !strcmp(v, "true");
		if (idx + 1 > ncheats) ncheats = idx + 1;
	}
	fclose(f);
	for (int i = 0; i < ncheats; i++)
		if (!cheats[i].desc[0]) snprintf(cheats[i].desc, sizeof(cheats[i].desc), "Cheat %d", i + 1);
	cheats_apply();
}

static void cheats_save(void)
{
	FILE *f;
	if (!chtPath[0]) return;
	f = fopen(chtPath, "w");
	if (!f) return;
	fprintf(f, "cheats = %d\n\n", ncheats);
	for (int i = 0; i < ncheats; i++)
		fprintf(f, "cheat%d_desc = \"%s\"\ncheat%d_code = \"%s\"\ncheat%d_enable = %s\n\n",
		        i, cheats[i].desc, i, cheats[i].code, i, cheats[i].on ? "true" : "false");
	fclose(f);
}

int emu_cheat_count(void) { return ncheats; }
const char *emu_cheat_name(int i) { return i >= 0 && i < ncheats ? cheats[i].desc : ""; }
int emu_cheat_enabled(int i) { return i >= 0 && i < ncheats && cheats[i].on; }
void emu_cheat_toggle(int i)
{
	if (i < 0 || i >= ncheats) return;
	cheats[i].on = !cheats[i].on;
	cheats_apply();
	cheats_save();
}

/* ---- favourites (file names, favorites.txt) ---- */
#define FAV_FILE WIIU_APP_DIR "/favorites.txt"
static char (*favs)[256];
static int nfavs = -1;

static void favs_load(void)
{
	FILE *f;
	char line[300];
	if (nfavs >= 0) return;
	nfavs = 0;
	f = fopen(FAV_FILE, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0]) continue;
		favs = realloc(favs, (nfavs + 1) * sizeof(*favs));
		snprintf(favs[nfavs++], sizeof(favs[0]), "%s", line);
	}
	fclose(f);
}

static const char *base_of(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

int emu_fav_is(const char *path)
{
	favs_load();
	for (int i = 0; i < nfavs; i++) if (!strcmp(favs[i], base_of(path))) return 1;
	return 0;
}

void emu_fav_toggle(const char *path)
{
	FILE *f;
	favs_load();
	int i;
	for (i = 0; i < nfavs; i++) if (!strcmp(favs[i], base_of(path))) break;
	if (i < nfavs) { memmove(favs[i], favs[i + 1], (nfavs - i - 1) * sizeof(*favs)); nfavs--; }
	else {
		favs = realloc(favs, (nfavs + 1) * sizeof(*favs));
		snprintf(favs[nfavs++], sizeof(favs[0]), "%s", base_of(path));
	}
	f = fopen(FAV_FILE, "w");
	if (!f) return;
	for (i = 0; i < nfavs; i++) fprintf(f, "%s\n", favs[i]);
	fclose(f);
}

int emu_disk_count(void) { return haveDisk ? (int)disk.get_num_images() : 0; }
int emu_disk_index(void) { return haveDisk ? (int)disk.get_image_index() : 0; }

int emu_disk_set(int index)
{
	if (!haveDisk) return 0;
	disk.set_eject_state(true);
	int ok = disk.set_image_index(index);
	disk.set_eject_state(false);
	return ok;
}

int emu_disk_append(const char *path)
{
	struct retro_game_info gi;
	if (!haveDisk) return 0;
	memset(&gi, 0, sizeof(gi));
	gi.path = path;
	disk.set_eject_state(true);
	if (!disk.add_image_index()) return 0;
	unsigned idx = disk.get_num_images() - 1;
	if (!disk.replace_image_index(idx, &gi)) return 0;
	disk.set_image_index(idx);
	disk.set_eject_state(false);
	return 1;
}

/* ------------------------------------------------------------------------ */
/* frame loop                                                               */
/* ------------------------------------------------------------------------ */
static int quickSlot;     /* slot used by the save / load hotkeys */

static int hotkey_pressed(void)
{
	static int latch = 1;
	uint32_t any = 0;
	int trigger = 0;
	for (int i = 0; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *p = wiiu_input_get(i);
		if (!p->connected) continue;
		any |= p->held;
		if (p->held & (WIIU_BTN_HOME | WIIU_BTN_TOUCH)) trigger = 1;
		if ((p->held & (WIIU_BTN_STICKL | WIIU_BTN_STICKR)) ==
		    (WIIU_BTN_STICKL | WIIU_BTN_STICKR)) trigger = 1;
	}
	if (latch) {   /* wait for the buttons that opened/closed the menu */
		if (!(any & (WIIU_BTN_HOME | WIIU_BTN_TOUCH | WIIU_BTN_STICKL | WIIU_BTN_STICKR)))
			latch = 0;
		return 0;
	}
	if (trigger) { latch = 1; return 1; }
	return 0;
}

static void run_game(void)
{
	OSTime last = OSGetSystemTime(), fpsStart = last;
	int frames = 0;
	const double fps = av.timing.fps > 1 ? av.timing.fps : 59.94;
	const OSTime frameTicks = (OSTime)(OSSecondsToTicks(1) / fps);

	assign_pads();
	wiiu_home_button_ingame(1);
	audio_pause(!cfg.audio);
	hotkey_pressed();   /* arm the latch */

	int ffToggle = 0, ffPrev = 0, paused = 0;
	uint32_t hkPrev = 0xffffffffu;   /* ignore buttons already held on entry */
	while (!wiiu_quit) {
		wiiu_input_update();
		if (hotkey_pressed()) break;

		/* ---- hotkeys: hold the hotkey button, press an action ---- */
		{
			uint32_t held = 0;
			for (int i = 0; i < WIIU_MAX_PADS; i++)
				if (wiiu_input_get(i)->connected) held |= wiiu_input_get(i)->held;
			uint32_t pressed = held & ~hkPrev;
			hkPrev = held;
			hkHide = 0;
			if (cfg.hk[HK_MOD] >= 0 && (held & (1u << cfg.hk[HK_MOD]))) {
				char m[64];
				for (int a = 1; a < HK_COUNT; a++) {
					int bit = cfg.hk[a];
					if (bit < 0 || bit == cfg.hk[HK_MOD]) continue;
					hkHide |= 1u << bit;                   /* the game doesn't see it */
					if (!(pressed & (1u << bit))) continue;
					switch (a) {
					case HK_SAVE:
						snprintf(m, sizeof(m), emu_state_save(quickSlot) ? tr("Saved to slot %d.")
						                                               : tr("Could not save to slot %d."), quickSlot);
						video_osd(m);
						break;
					case HK_LOAD:
						snprintf(m, sizeof(m), emu_state_load(quickSlot) ? tr("Loaded slot %d.")
						                                               : tr("Slot %d is empty."), quickSlot);
						video_osd(m);
						break;
					case HK_SLOTUP:
					case HK_SLOTDOWN:
						quickSlot = (quickSlot + (a == HK_SLOTUP ? 1 : 9)) % 10;
						snprintf(m, sizeof(m), tr(emu_state_exists(quickSlot) ? "Slot %d" : "Slot %d (empty)"), quickSlot);
						video_osd(m);
						break;
					case HK_SHOT:
						video_osd(emu_screenshot() ? tr("Screenshot saved to WiiUStation/screenshots.")
						                           : tr("Could not save the screenshot."));
						break;
					case HK_FFWD:  ffToggle = !ffToggle; break;
					case HK_PAUSE: paused = !paused; break;
					case HK_RESET: emu_reset(); video_osd(tr("Reset")); break;
					}
				}
			}
		}
		if (paused) {
			video_draw_game(1);
			video_osd(tr("Paused"));
			video_present();
			if (!pump_events()) break;
			OSSleepTicks(OSMillisecondsToTicks(16));
			continue;
		}

		/* ---- fast forward ---- */
		uint32_t any = 0;
		for (int i = 0; i < WIIU_MAX_PADS; i++)
			if (wiiu_input_get(i)->connected) any |= wiiu_input_get(i)->held;
		int r3 = (any & WIIU_BTN_STICKR) && !(any & WIIU_BTN_STICKL);   /* L3+R3 = menu */
		int zlzr = (any & (WIIU_BTN_ZL | WIIU_BTN_ZR)) == (WIIU_BTN_ZL | WIIU_BTN_ZR);
		if (hkHide) r3 = zlzr = 0;   /* a hotkey is being used */
		if (cfg.ffwd == 2 && r3 && !ffPrev) ffToggle = !ffToggle;
		ffPrev = r3;
		int ff = (cfg.ffwd == 1 && r3) || ffToggle || (cfg.ffwd == 3 && zlzr);
		ffwdMask = 0xffffffffu;
		if (cfg.ffwd == 1 || cfg.ffwd == 2) ffwdMask &= ~(1u << RETRO_DEVICE_ID_JOYPAD_R3);
		if (cfg.ffwd == 3 && zlzr)
			ffwdMask &= ~((1u << RETRO_DEVICE_ID_JOYPAD_L2) | (1u << RETRO_DEVICE_ID_JOYPAD_R2));

		if (ff) {
			ffMuted = 1;
			for (int i = 1; i < cfg.ffspeed; i++) retro_run();
			ffMuted = 0;
		}
		retro_run();
		video_draw_game(0);
		if (ff) video_osd(">> Fast forward");
		video_present();
		if (!pump_events()) break;

		/* pacing: audio sync when sound is on, timer otherwise */
		if (ff) {
			/* run as fast as the extra frames allow */
		} else if (cfg.audio) {
			size_t target = audio_target_frames();
			int guard = 0;
			while (audio_queued_frames() > target && guard++ < 40)
				OSSleepTicks(OSMillisecondsToTicks(1));
		} else {
			OSTime now = OSGetSystemTime();
			if (now - last < frameTicks)
				OSSleepTicks(frameTicks - (now - last));
			last = OSGetSystemTime();
		}

		if (++frames == 60) {
			OSTime now = OSGetSystemTime();
			double secs = (double)(now - fpsStart) / OSSecondsToTicks(1);
			video_set_fps(secs > 0 ? (int)(60.0 / secs + 0.5) : 0, (int)(fps + 0.5));
			fpsStart = now;
			frames = 0;
		}
	}

	ffwdMask = 0xffffffffu;
	hkHide = 0;
	audio_pause(1);
	for (int i = 0; i < WIIU_MAX_PADS; i++) wiiu_input_rumble(i, 0);
	wiiu_home_button_ingame(0);
	emu_save_memcards();
	wiiu_input_reset_hotkey();
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;

	if (video_init() < 0) return -1;
	wiiu_input_init();
	wiiu_mkdirs(WIIU_ISO_DIR);
	wiiu_mkdirs(WIIU_BIOS_DIR);
	wiiu_mkdirs(WIIU_SAVE_DIR);
	wiiu_mkdirs(WIIU_STATE_DIR);
	wiiu_mkdirs(WIIU_COVER_DIR);
	wiiu_mkdirs(WIIU_BEZEL_DIR);
	jit_available();          /* probe codegen on the main core */
	settings_load();

	retro_set_environment(environment);
	retro_set_video_refresh(video_cb);
	retro_set_audio_sample(audio_sample_cb);
	retro_set_audio_sample_batch(audio_batch_cb);
	retro_set_input_poll(input_poll_cb);
	retro_set_input_state(input_state_cb);
	retro_init();

	while (!wiiu_quit) {
		if (!menu_run()) break;
		if (wiiu_quit || !loaded) break;
		run_game();
	}

	emu_unload_game();
	retro_deinit();
	covers_shutdown();
	wiiu_input_shutdown();

	if (!wiiu_quit) {
		SYSLaunchMenu();
		while (pump_events()) SDL_Delay(10);
	}
	video_shutdown();
	return 0;
}
