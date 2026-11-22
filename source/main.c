/*
 * WiiUStation - standalone PlayStation emulator for the Wii U (Aroma).
 *
 * The emulator is PCSX-ReARMed, linked in statically through its libretro
 * API.  This file is the "host": it answers the core's environment calls,
 * feeds it input, takes its video/audio, runs the frame loop on core 1 and
 * handles saves, states and settings.  No RetroArch involved.
 */
#include <coreinit/thread.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>
#include <sysapp/launch.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "platform.h"
#include "libretro.h"

wiiu_settings cfg;
extern const int default_map[16];
extern const int default_wmap[16];
static void recent_add(const char *path);
static void cheats_load(void);
static void cheats_apply(void);
static void cheats_clear(void);
static int wsActive;   /* widescreen on and the game has a code for it */
static int cheatsReady; /* the loaded game's cheats are read */
static volatile int chtState;   /* cheat download: 0 idle, 1 running, 2 got a file (reload), 3 none */

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
	{ "fx",         &cfg.fx,         0, FX_COUNT - 1 },
	{ "vsync",      &cfg.vsync,      0, 1 },
	{ "drc",        &cfg.drc,        0, DRC_COUNT - 1 },
	{ "widescreen", &cfg.widescreen, 0, 1 },
	{ "gyroaim",    &cfg.gyroaim,    0, 1 },
	{ "scanlines",  &cfg.scanlines,  0, 1 },
	{ "crt",        &cfg.crt,        0, 1 },
	{ "reverb",     &cfg.reverb,     0, 1 },
	{ "dithering",  &cfg.dithering,  0, 2 },
	{ "bios_hle",   &cfg.bios_hle,   0, 1 },
	{ "bootlogo",   &cfg.bootlogo,   0, 1 },
	{ "analog",     &cfg.analog,     -1, PAD_COUNT - 1 },
	{ "analog2",    &cfg.analog2,    -1, PAD_NEGCON },
	{ "deadzone",   &cfg.deadzone,   0, 6 },
	{ "multitap",   &cfg.multitap,   0, 1 },
	{ "hires_fast", &cfg.hires_fast, 0, 2 },
	{ "menumusic",  &cfg.menumusic,  0, 1 },
	{ "tex565",     &cfg.tex565,     0, 1 },
	{ "p1first",    &cfg.p1first,    0, 1 },
	{ "pointer",    &cfg.pointer,    0, 2 },
	{ "region",     &cfg.region,     0, 2 },
	{ "resume",     &cfg.resume,     0, 1 },
	{ "ffwd",       &cfg.ffwd,       0, 3 },
	{ "ffspeed",    &cfg.ffspeed,    2, 4 },
	{ "rumble",     &cfg.rumble,     0, 1 },
	{ "audio",      &cfg.audio,      0, 1 },
	{ "bezel",      &cfg.bezel,      0, BEZEL_COUNT - 1 },
	{ "view",       &cfg.view,       0, 4 },
	{ "cover3d",    &cfg.cover3d,    0, 1 },
	{ "sort",       &cfg.sort,       0, 2 },
	{ "regionf",    &cfg.regionf,    0, 4 + 64 },
	{ "menusfx",    &cfg.menusfx,    0, 6 },
	{ "theme",      &cfg.theme,      0, THEME_COUNT - 1 },
	{ "autostart",  &cfg.autostart,  0, 1 },
	{ "autofix",    &cfg.autofix,    0, 1 },
	{ "lang",       &cfg.lang,       -1, LANG_COUNT - 1 },
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
	/* careful: despite their names, "enabled" turns these sounds ON */
	{ "nocdaudio", "CD audio tracks", NULL,
	  "Music played from the disc's audio tracks.", 2, 0,
	  { "enabled", "disabled" }, { "On", "Off" } },
	{ "noxadecoding", "XA audio (music, voices)", NULL,
	  "Compressed music and voices. Off saves CPU but silences them.", 2, 0,
	  { "enabled", "disabled" }, { "On", "Off" } },
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
	{ "analog_axis_modifier", "Analog range", "Controller",
	  "Square lets the stick reach the corners, like some games expect.", 2, 0,
	  { "circle", "square" }, { "Circle", "Square" } },
	{ "analog_combo", "Digital/analog switch combo", NULL,
	  "Buttons that toggle the DualShock's analog mode, like the ANALOG button.", 5, 0,
	  { "disabled", "l1+r1+select", "l1+r1+start", "l1+r1+l3", "l1+r1+r3" },
	  { "Off", "L1+R1+Select", "L1+R1+Start", "L1+R1+L3", "L1+R1+R3" } },
	{ "input_sensitivity", "Mouse speed", NULL,
	  "For the PlayStation Mouse: how fast the pointer moves.", 6, 2,
	  { "0.50", "0.75", "1.00", "1.25", "1.50", "2.00" }, { "50%", "75%", "100%", "125%", "150%", "200%" } },
	{ "negcon_deadzone", "neGcon twist dead zone", NULL,
	  "For the neGcon: how far the stick moves before steering starts.", 5, 0,
	  { "0", "5", "10", "15", "20" }, { "0%", "5%", "10%", "15%", "20%" } },
	{ "negcon_response", "neGcon steering response", NULL,
	  "Quadratic and Cubic give finer control near the centre.", 3, 0,
	  { "linear", "quadratic", "cubic" }, { "Linear", "Quadratic", "Cubic" } },
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
_Static_assert(sizeof(COREOPT) / sizeof(COREOPT[0]) <= 32, "cfg.core too small");

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

/* ---- built-in fixes for known games ----
 * The core has its own list (libpcsxcore/database.c); these are the
 * P.E.Op.S. GPU fixes its options name, matched on the game's name or ID.
 * They are only given to the core while the game runs and only when the
 * option is left at its default, so the player's choice always wins. */
static const struct { const char *match; const char *opt; } AUTOFIX[] = {
	{ "chrono cross",         "gpu_peops_odd_even_bit" },
	{ "SCUS-94536",           "gpu_peops_odd_even_bit" },
	{ "SCUS-94537",           "gpu_peops_odd_even_bit" },
	{ "lunar silver star",    "gpu_peops_ignore_brightness" },
	{ "lunar - silver star",  "gpu_peops_ignore_brightness" },
	{ "dark forces",          "gpu_peops_repeated_triangles" },
	{ "dragon warrior vii",   "gpu_peops_lazy_screen_update" },
	{ "dragon warrior 7",     "gpu_peops_lazy_screen_update" },
	{ "pandemonium 2",        "gpu_peops_lazy_screen_update" },
	{ "street fighter alpha", "gpu_peops_expand_screen_width" },
	{ "street fighter zero",  "gpu_peops_expand_screen_width" },
	{ "street fighter collection", "gpu_peops_expand_screen_width" },
	{ "marvel super heroes",  "gpu_peops_expand_screen_width" },
	{ "marvel vs. capcom",    "gpu_peops_expand_screen_width" },
	{ "marvel vs capcom",     "gpu_peops_expand_screen_width" },
	{ "x-men vs. street",     "gpu_peops_expand_screen_width" },
	{ "x-men vs street",      "gpu_peops_expand_screen_width" },
	{ "children of the atom", "gpu_peops_expand_screen_width" },
	{ "darkstalkers",         "gpu_peops_expand_screen_width" },
	{ "vampire savior",       "gpu_peops_expand_screen_width" },
};
static uint32_t autofixMask;     /* COREOPT indices on for the loaded game */

static int ci_contains(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	for (; *hay; hay++)
		if (!strncasecmp(hay, needle, n)) return 1;
	return 0;
}

/* Um Jammer Lammy plays the guitar through the SPU's decode buffers and its
 * reverb unit: the core must keep the buffers in SPU (little endian) byte
 * order and the reverb must stay on. Only this game gets that. */
extern int wiiustation_decode_le;      /* patches/0005 */
static int ujlFix;

static int is_ujl(const char *name, const char *id)
{
	static const char *const ids[] = { "SCUS-94408", "SCES-01753", "SCPS-45391", "SCPS-91226" };
	if (ci_contains(name, "jammer lammy")) return 1;
	for (size_t i = 0; id && id[0] && i < sizeof(ids) / sizeof(ids[0]); i++)
		if (!strcmp(id, ids[i])) return 1;
	return 0;
}

static void autofix_detect(const char *name, const char *id)
{
	autofixMask = 0;
	ujlFix = cfg.autofix && is_ujl(name, id);
	wiiustation_decode_le = ujlFix;
	if (ujlFix) wlog("game fix: Um Jammer Lammy sound (decode buffers byte order, reverb on)");
	if (!cfg.autofix) return;
	for (size_t k = 0; k < sizeof(AUTOFIX) / sizeof(AUTOFIX[0]); k++) {
		if (!ci_contains(name, AUTOFIX[k].match) && !(id && id[0] && !strcmp(id, AUTOFIX[k].match))) continue;
		for (int i = 0; i < NCOREOPT; i++)
			if (!strcmp(COREOPT[i].key, AUTOFIX[k].opt)) autofixMask |= 1u << i;
	}
}

/* the first automatic fix's name, for a message */
const char *emu_autofix_name(void)
{
	for (int i = 0; i < NCOREOPT; i++)
		if (autofixMask & (1u << i)) return COREOPT[i].label;
	return NULL;
}

/* keys that only make sense globally (never in a per-game file) */
static int global_only(const char *k)
{
	return !strcmp(k, "view") || !strcmp(k, "cover3d") || !strcmp(k, "menusfx") || !strcmp(k, "theme") || !strcmp(k, "autostart") || !strcmp(k, "autofix") || !strcmp(k, "sort") || !strcmp(k, "regionf") || !strcmp(k, "lang") || !strncmp(k, "hk", 2) || !strcmp(k, "games_dir") || !strcmp(k, "gpu_thread") ||
	       !strcmp(k, "spu_thread") || !strcmp(k, "resume") || !strcmp(k, "ffwd") || !strcmp(k, "pointer") || !strcmp(k, "tex565") || !strcmp(k, "menumusic");
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
	cfg.aspect = WIIU_ASPECT_4_3;
	cfg.filter = WIIU_FILTER_SHARP_BILINEAR;   /* Sharp smooth */
	cfg.scanlines = 0;
	cfg.crt = 0;
	cfg.reverb = 1;
	cfg.dithering = 1;
	cfg.bios_hle = 0;
	cfg.bootlogo = 0;
	cfg.analog = PAD_AUTO;  /* DualShock for games that support it, else Digital */
	cfg.analog2 = PAD_AUTO;
	cfg.deadzone = 1;     /* 5% */
	cfg.multitap = 0;
	cfg.pointer = 1;      /* Wii Remote pointer: automatic */
	cfg.hires_fast = 1;   /* 640x480: half the lines when the game is slow */
	cfg.tex565 = 1;       /* the 16-bit picture goes to the screen as it is (60 fps in heavy menus) */
	cfg.region = 0;
	cfg.resume = 1;
	cfg.ffwd = 1;         /* hold R3 */
	cfg.ffspeed = 3;
	cfg.rumble = 1;
	cfg.audio = 1;
	cfg.bezel = BEZEL_AMBIENT;
	cfg.view = 4;         /* carousel */
	cfg.menusfx = 2;                 /* Soft */
	cfg.lang = LANG_AUTO;  /* the Wii U's language */
	cfg.vsync = 1;
	cfg.autofix = 1;
	for (int i = 0; i < NCOREOPT; i++) cfg.core[i] = COREOPT[i].def;
	memcpy(cfg.hk, default_hotkeys, sizeof(cfg.hk));
	strcpy(cfg.games_dir, WIIU_ISO_DIR);
	memcpy(cfg.map, default_map, sizeof(cfg.map));
	memcpy(cfg.wmap, default_wmap, sizeof(cfg.wmap));
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
		if (!strncmp(k, "wmap", 4) && k[4] >= '0' && k[4] <= '9') {
			int id = atoi(k + 4), v = atoi(eq + 1);
			if (id >= 0 && id < 16 && v >= -1 && v < WR_COUNT) cfg.wmap[id] = v;
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
		if (version < 6 && !strcmp(k, "lang"))       /* following the system became the default */
			continue;
		if (version < 7 && (!strcmp(k, "analog") || !strcmp(k, "analog2")))   /* Auto became the default */
			continue;
		if (version < 8 && !strcmp(k, "menusfx") && v == 1)   /* Soft replaced Classic as the default */
			continue;
		if (version < 9 && !strcmp(k, "aspect"))     /* 4:3 became the default */
			continue;
		if (version < 10 && !strcmp(k, "filter"))    /* Sharp smooth became the default */
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
	fprintf(f, "# WiiUStation %s\nversion = 10\n", isGame ? "per-game settings" : "settings");
	for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
		if (!isGame || !global_only(OPTS[i].key))
			fprintf(f, "%s = %d\n", OPTS[i].key, *OPTS[i].val);
	for (int i = 0; i < NCOREOPT; i++) fprintf(f, "c_%s = %d\n", COREOPT[i].key, cfg.core[i]);
	if (!isGame) fprintf(f, "games_dir = %s\n", cfg.games_dir);
	for (int i = 0; i < 16; i++) fprintf(f, "map%d = %d\n", i, cfg.map[i]);
	for (int i = 0; i < 16; i++) fprintf(f, "wmap%d = %d\n", i, cfg.wmap[i]);
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
	settings_parse(WIIU_CONFIG_FILE, 0);
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

/* BIOS: the app's bios folder */
static const char *bios_dir(void)
{
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
	if (!strcmp(key, "pcsx_rearmed_spu_reverb")) return cfg.reverb || ujlFix ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_multitap")) return cfg.multitap ? "port 1" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_region"))
		return cfg.region == 1 ? "NTSC" : cfg.region == 2 ? "PAL" : "auto";
	if (!strcmp(key, "pcsx_rearmed_vibration")) return cfg.rumble ? "enabled" : "disabled";
	/* slot 2 off: the core's default ("shared") would create pcsx-card2.mcd */
	if (!strcmp(key, "pcsx_rearmed_memcard2")) return "none";
	if (!strcmp(key, "pcsx_rearmed_display_fps_v2")) return "disabled";
	if (!strcmp(key, "pcsx_rearmed_rgb32_output")) return "disabled"; /* RGB565: cheapest upload */
	if (!strcmp(key, "pcsx_rearmed_show_overscan") && emu_widescreen_active()) return "hack";
	if (!strncmp(key, "pcsx_rearmed_", 13))
		for (int i = 0; i < NCOREOPT; i++)
			if (!strcmp(key + 13, COREOPT[i].key)) {
				int v = cfg.core[i];
				if ((autofixMask & (1u << i)) && v == COREOPT[i].def && COREOPT[i].n == 2)
					v = 1;                      /* automatic fix: "enabled" */
				return COREOPT[i].vals[v >= 0 && v < COREOPT[i].n ? v : 0];
			}
	return NULL;   /* core default */
}

/* The core's messages are only kept while a game is being loaded: if it
 * fails, they say why (see load_failed). */
static char coreLog[48][200];
static int coreLogN, coreLogOn;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	if (!coreLogOn) {
		if (level >= RETRO_LOG_WARN) {                 /* during the game: only problems */
			char w[200];
			va_list ap2;
			va_start(ap2, fmt);
			vsnprintf(w, sizeof(w), fmt, ap2);
			va_end(ap2);
			wlog("core: %s", w);
		}
		return;
	}
	char b[184];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	b[strcspn(b, "\r\n")] = 0;
	if (!b[0]) return;
	snprintf(coreLog[coreLogN++ % 48], sizeof(coreLog[0]), "%s%s",
	         level >= RETRO_LOG_ERROR ? "[error] " : level == RETRO_LOG_WARN ? "[warn] " : "", b);
}

static char loadError[160];
const char *emu_load_error(void) { return loadError; }

/* why the game did not load, from the core's own messages, and a report
 * with everything needed to understand it: sd:/wiiu/apps/WiiUStation/last_error.txt */
static void load_failed(const char *path)
{
	struct stat st;
	const char *reason = NULL;
	int chdErr = -1, n = coreLogN < 48 ? coreLogN : 48;
	loadError[0] = 0;
	for (int i = 0; i < n; i++) {
		const char *l = coreLog[(coreLogN - n + i + 48 * 4) % 48];
		if (!strncmp(l, "chd_open: ", 10)) chdErr = atoi(l + 10);
		if (!strncmp(l, "[error] ", 8)) reason = l + 8;
	}
	if (chdErr >= 0) {
		static const char *why[] = { "", "no interface", "out of memory", "invalid file", "invalid parameter", "invalid data",
		                             "file not found", "needs a parent CHD", "not writeable", "read error", "write error",
		                             "codec error", "invalid parent", "hunk out of range", "decompression error" };
		snprintf(loadError, sizeof(loadError), "CHD error %d (%s)", chdErr,
		         chdErr < (int)(sizeof(why) / sizeof(why[0])) ? why[chdErr] : "unsupported");
	} else if (reason) {
		if (!strncmp(reason, "unsupported/invalid CD image", 28)) snprintf(loadError, sizeof(loadError), "not a PlayStation disc image");
		else snprintf(loadError, sizeof(loadError), "%.100s", reason);
	} else snprintf(loadError, sizeof(loadError), "the core gave no reason");

	wiiu_mkdirs(WIIU_APP_DIR);
	FILE *f = fopen(WIIU_APP_DIR "/last_error.txt", "w");
	if (!f) return;
	fprintf(f, "WiiUStation %s\n", app_version());
	fprintf(f, "game: %s\n", path);
	fprintf(f, "size: %lld bytes\n", stat(path, &st) == 0 ? (long long)st.st_size : -1LL);
	fprintf(f, "reason: %s\n", loadError);
	fprintf(f, "dynarec: %s (jit %s)\n", cfg.dynarec ? "on" : "off", jit_available() ? "available" : "unavailable");
	fprintf(f, "bios folder: %s\n", bios_dir());
	DIR *d = opendir(bios_dir());
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL) {
			char p[400];
			if (e->d_name[0] == '.') continue;
			snprintf(p, sizeof(p), "%s/%s", bios_dir(), e->d_name);
			fprintf(f, "  %s (%lld bytes)\n", e->d_name, stat(p, &st) == 0 ? (long long)st.st_size : -1LL);
		}
		closedir(d);
	} else fprintf(f, "  (folder not found)\n");
	fprintf(f, "--- core messages ---\n");
	for (int i = 0; i < n; i++) fprintf(f, "%s\n", coreLog[(coreLogN - n + i + 48 * 4) % 48]);
	fclose(f);
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

/* The core's options and their defaults, as it declares them (legacy
 * variables: "Description; default|other|..."). Options WiiUStation sets
 * itself are answered by core_var; every other one gets its default, so the
 * core never runs with an option missing (the memory card was "none"). */
#define MAXDEF 160
static struct { char key[64]; char def[48]; } coreDefs[MAXDEF];
static int nCoreDefs;

static void core_defaults_set(const struct retro_variable *v)
{
	nCoreDefs = 0;
	for (; v && v->key && nCoreDefs < MAXDEF; v++) {
		const char *semi = v->value ? strchr(v->value, ';') : NULL;
		if (!semi) continue;
		semi++;
		while (*semi == ' ') semi++;
		size_t n = strcspn(semi, "|");
		snprintf(coreDefs[nCoreDefs].key, sizeof(coreDefs[0].key), "%s", v->key);
		snprintf(coreDefs[nCoreDefs].def, sizeof(coreDefs[0].def), "%.*s", (int)n, semi);
		nCoreDefs++;
	}
}

static const char *core_default(const char *key)
{
	for (int i = 0; i < nCoreDefs; i++)
		if (!strcmp(coreDefs[i].key, key)) return coreDefs[i].def;
	return NULL;
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
		if (!v->value) v->value = core_default(v->key);   /* the core's own default */
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
		core_defaults_set((const struct retro_variable *)data);
		return true;
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
	/* the GamePad first, or (player 1 option) the Wii controllers first */
	if (cfg.p1first)
		for (int i = 1; i < WIIU_MAX_PADS; i++)
			if (wiiu_input_get(i)->connected) players[n++] = i;
	for (int i = 0; i < WIIU_MAX_PADS; i++)
		if (wiiu_input_get(i)->connected && (i == 0 || !cfg.p1first)) players[n++] = i;
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

/* controller type of a PSX pad index: player 1 uses cfg.analog, the others analog2 */
/* Auto: DualShock when the game supports the analog sticks
 * (content/analog_ids.txt, from the libretro database), else Digital */
static int gameAnalog;

static int analog_game(const char *id)
{
	char line[32];
	int found = 0;
	if (!id || !id[0]) return 0;
	FILE *f = fopen("fs:/vol/content/analog_ids.txt", "r");
	if (!f) return 0;
	while (!found && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		found = !strcmp(line, id);
	}
	fclose(f);
	return found;
}

static int port_type(int port)
{
	int t = port == 0 ? cfg.analog : cfg.analog2;
	if (t == PAD_AUTO) t = gameAnalog ? PAD_DUALSHOCK : PAD_DIGITAL;
	return t;
}
/* sticks are read as sticks (not as the d-pad) */
static int port_is_analog(int port) { return port_type(port) != PAD_DIGITAL; }

const char *pad_type_name(int t)
{
	static const char *n[PAD_COUNT] = { "Digital", "DualShock", "neGcon", "Mouse", "GunCon", "Justifier" };
	if (t == PAD_AUTO) {
		if (!loaded) return "Auto";
		return gameAnalog ? "Auto (DualShock)" : "Auto (Digital)";
	}
	return t >= 0 && t < PAD_COUNT ? n[t] : "";
}

/* the GamePad screen is the light gun / mouse for player 1 */
int emu_touch_is_input(void)
{
	if (cfg.drc == DRC_PANEL || cfg.drc == DRC_TV_ONLY) return 0;   /* the GamePad shows no game */
	return cfg.analog == PAD_MOUSE || cfg.analog == PAD_GUNCON || cfg.analog == PAD_JUSTIFIER;
}

#define DEV_DUALSHOCK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define DEV_NEGCON    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define DEV_GUNCON    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define DEV_JUSTIFIER RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1)
#define DEV_MOUSE     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)

static unsigned port_device(int port)
{
	switch (port_type(port)) {
	case PAD_DUALSHOCK: return DEV_DUALSHOCK;
	case PAD_NEGCON:    return DEV_NEGCON;
	case PAD_MOUSE:     return DEV_MOUSE;
	case PAD_GUNCON:    return DEV_GUNCON;
	case PAD_JUSTIFIER: return DEV_JUSTIFIER;
	default:            return RETRO_DEVICE_JOYPAD;
	}
}

/* touch state for the gun and the mouse, updated once per frame */
static struct { int down, inside, gx, gy, lastX, lastY, dx, dy; } touch;

static void touch_update(void)
{
	const wiiu_pad_state *gp = wiiu_input_get(0);
	int down = gp->connected && (gp->held & WIIU_BTN_TOUCH);
	touch.dx = touch.dy = 0;
	if (down) {
		int gx, gy;
		if (touch.down) { touch.dx = gp->touch_x - touch.lastX; touch.dy = gp->touch_y - touch.lastY; }
		touch.lastX = gp->touch_x;
		touch.lastY = gp->touch_y;
		touch.inside = video_screen_to_game(gp->touch_x, gp->touch_y, &gx, &gy);
		if (touch.inside) { touch.gx = gx; touch.gy = gy; }   /* the aim stays when released */
	}
	touch.down = down;

	/* aim by turning the GamePad: integrate the turn speed; L re-centres */
	if (cfg.gyroaim && (cfg.analog == PAD_GUNCON || cfg.analog == PAD_JUSTIFIER) && gp->connected) {
		static float ax, ay;
		const float span = 30.0f / 360.0f;        /* 30 degrees = the whole picture */
		ax -= gp->gyro_y / 60.0f / span * 2.0f;
		ay -= gp->gyro_x / 60.0f / span * 2.0f * 4.0f / 3.0f;
		if (gp->held & WIIU_BTN_L) ax = ay = 0;
		if (ax < -1) ax = -1;
		if (ax > 1) ax = 1;
		if (ay < -1) ay = -1;
		if (ay > 1) ay = 1;
		if (!down) {           /* touching still wins */
			touch.gx = (int)(ax * 32767);
			touch.gy = (int)(ay * 32767);
		}
		video_set_crosshair(1, touch.gx, touch.gy);
	} else {
		video_set_crosshair(0, 0, 0);
	}
}

/* radial dead zone, then rescaled so full tilt still reaches 1 */
static float dead(float v, float other)
{
	float dz = cfg.deadzone * 0.05f;
	float mag = sqrtf(v * v + other * other);
	if (dz <= 0) return v;
	if (mag <= dz) return 0;
	float k = (mag - dz) / (1 - dz) / mag;
	v *= k;
	return v > 1 ? 1 : v < -1 ? -1 : v;
}

static void input_poll_cb(void)
{
	wiiu_input_update();
	touch_update();
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

/* Default for the Wii Remote (held sideways) and Nunchuk: same order as default_map.
 * Index = RETRO_DEVICE_ID_JOYPAD_*, value = WR_* (-1 none). */
const int default_wmap[16] = {
	WR_1,      /* B      (Cross)    */
	WR_B,      /* Y      (Square)   */
	WR_MINUS,  /* SELECT            */
	WR_PLUS,   /* START             */
	WR_UP, WR_DOWN, WR_LEFT, WR_RIGHT,
	WR_2,      /* A      (Circle)   */
	WR_A,      /* X      (Triangle) */
	WR_C, WR_Z,/* L1 R1 (Nunchuk)   */
	-1, -1,    /* L2 R2             */
	-1, -1,    /* L3 R3             */
};

static uint32_t hkHide;   /* WIIU buttons used by a hotkey right now */

static uint32_t psx_mask(const wiiu_pad_state *p, int analog)
{
	uint32_t b = p->held & ~hkHide, m = 0;
	if (p->kind >= PADKIND_REMOTE) {            /* Wii Remote / Nunchuk: its own layout */
		for (int id = 0; id < 16; id++) {
			int w = cfg.wmap[id];
			if (w >= 0 && w < WR_COUNT && (p->wraw & (1u << w))) m |= 1u << id;
		}
		if (!analog) {                          /* the Nunchuk stick also drives the d-pad */
			if (p->ly > 0.5f)  m |= 1u << 4;
			if (p->ly < -0.5f) m |= 1u << 5;
			if (p->lx < -0.5f) m |= 1u << 6;
			if (p->lx > 0.5f)  m |= 1u << 7;
		}
		return m;
	}
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
	case RETRO_DEVICE_LIGHTGUN: {
		/* player 1 aims by touching the GamePad screen; touching outside
		 * the picture reloads. A / B / + are the gun's other buttons. */
		uint32_t h = wiiu_input_get(0)->held | p->held;
		switch (id) {
		case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:     return (int16_t)touch.gx;
		case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:     return (int16_t)touch.gy;
		case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN: return touch.down && !touch.inside;
		case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:      return (touch.down && touch.inside) || (h & WIIU_BTN_ZR) != 0;
		case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:       return (touch.down && !touch.inside) || (h & WIIU_BTN_ZL) != 0;
		case RETRO_DEVICE_ID_LIGHTGUN_AUX_A:        return (h & WIIU_BTN_A) != 0;
		case RETRO_DEVICE_ID_LIGHTGUN_AUX_B:        return (h & WIIU_BTN_B) != 0;
		case RETRO_DEVICE_ID_LIGHTGUN_START:        return (h & WIIU_BTN_PLUS) != 0;
		}
		return 0;
	}
	case RETRO_DEVICE_MOUSE: {
		/* drag on the GamePad screen, or the right stick; ZR / A = left
		 * button, ZL / B = right button */
		uint32_t h = p->held;
		switch (id) {
		case RETRO_DEVICE_ID_MOUSE_X: return (int16_t)(touch.dx / 2 + (int)(dead(p->rx, p->ry) * 8));
		case RETRO_DEVICE_ID_MOUSE_Y: return (int16_t)(touch.dy / 2 - (int)(dead(p->ry, p->rx) * 8));
		case RETRO_DEVICE_ID_MOUSE_LEFT:  return (h & (WIIU_BTN_ZR | WIIU_BTN_A)) != 0;
		case RETRO_DEVICE_ID_MOUSE_RIGHT: return (h & (WIIU_BTN_ZL | WIIU_BTN_B)) != 0;
		}
		return 0;
	}
	case RETRO_DEVICE_ANALOG: {
		float v;
		if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
			uint32_t m = psx_mask(p, port_is_analog(port)) & ffwdMask;
			return ((m >> id) & 1) ? 0x7fff : 0;
		}
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			v = id == RETRO_DEVICE_ID_ANALOG_X ? dead(p->lx, p->ly) : -dead(p->ly, p->lx);
		else
			v = id == RETRO_DEVICE_ID_ANALOG_X ? dead(p->rx, p->ry) : -dead(p->ry, p->rx);
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

int emu_memcard_path(char *out, size_t n)
{
	if (!loaded) return 0;
	srm_path(out, n);
	return 1;
}

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
	} else wlog("could not write the memory card: %s", path);
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
	if (read_file(path, data, size) <= 0) return;   /* a new card */
	{
		srmShadow = malloc(size);
		if (srmShadow) memcpy(srmShadow, data, size);
	}
}

void emu_apply_settings(void)
{
	varsDirty = 1;
	if (loaded) {
		static int lastWs = -1;
		if (cheatsReady) {
			cheats_apply();
			if (cfg.widescreen && !wsActive && lastWs != 0)
				video_osd(tr("No widescreen code for this game: add one to its .cht file."));
			lastWs = cfg.widescreen ? wsActive : -1;
		}
		for (int port = 0; port < NPORTS; port++) {
			retro_set_controller_port_device(port, port_device(port));
		}
	}
}

/* Multi-disc: start an .m3u at a given disc (disc_path as the core will
 * build it: <m3u folder>/<entry>). */
/* Translation patches: <game>.ppf in WiiUStation/patches or next to the
 * game, or <GAME-ID>.ppf in WiiUStation/patches. The core applies it to
 * every sector it reads (fan translations, fixes). */
extern char wiiustation_ppf_path[];
extern void wiiustation_spu_diag(char *out, int n);
extern void wiiustation_cd_info(char *out, int n);   /* patches/0007 */
extern unsigned wiiustation_chd_reads;           /* patches/0006 */
extern unsigned long long wiiustation_chd_us;
static int ppfActive, ppfFromDisc;

static void find_ppf(const char *gamefile)
{
	char base[256], dir[512], p[768], id[16];
	struct stat st;
	const char *slash = strrchr(gamefile, '/');
	snprintf(base, sizeof(base), "%s", slash ? slash + 1 : gamefile);
	char *dot = strrchr(base, '.');
	if (dot) *dot = 0;
	snprintf(dir, sizeof(dir), "%.*s", slash ? (int)(slash - gamefile) : 0, gamefile);
	wiiustation_ppf_path[0] = 0;
	ppfActive = 0;
	const char *cand[3] = { WIIU_APP_DIR "/patches/%s.ppf", "%s/%s.ppf", NULL };
	snprintf(p, sizeof(p), cand[0], base);
	if (stat(p, &st) != 0) snprintf(p, sizeof(p), cand[1], dir, base);
	if (stat(p, &st) != 0 && covers_detect_id(gamefile, id)) snprintf(p, sizeof(p), WIIU_APP_DIR "/patches/%s.ppf", id);
	if (stat(p, &st) == 0 && strlen(p) < 255) {
		strcpy(wiiustation_ppf_path, p);
		ppfActive = 1;
	}
}

int emu_ppf_active(void) { return ppfActive; }

/* the HOME Menu may close the app: save the memory cards and the resume
 * state before Cafe OS takes over */
void emu_background_save(void)
{
	if (!loaded) return;
	emu_save_memcards();
	if (cfg.resume) emu_state_save(-1);
}

int emu_load_game_disc(const char *m3u, int index, const char *disc_path)
{
	find_ppf(disc_path && disc_path[0] ? disc_path : m3u);
	ppfFromDisc = 1;
	if (haveDiskExt && diskExt.set_initial_image && index > 0)
		diskExt.set_initial_image(index, disc_path);
	return emu_load_game(m3u);
}

/* ---- what the log records about a game session ---- */
static uint32_t sessStartMs, sessWindows, sessSlow;
static double sessFpsSum, sessFpsMin;
static unsigned padSig;                 /* which controllers are connected, to log changes */

static const char *pad_kind_name(const wiiu_pad_state *p)
{
	switch (p->kind) {
	case PADKIND_GAMEPAD: return "GamePad";
	case PADKIND_PRO:     return "Pro Controller";
	case PADKIND_CLASSIC: return "Classic Controller";
	case PADKIND_NUNCHUK: return "Wii Remote + Nunchuk";
	default:              return "Wii Remote";
	}
}

static void log_controllers(int force)
{
	unsigned sig = 0;
	for (int i = 0; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *p = wiiu_input_get(i);
		if (p->connected) sig |= (unsigned)(1 + p->kind) << (i * 4);
	}
	if (sig == padSig && !force) return;
	padSig = sig;
	char b[200] = "";
	for (int i = 0; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *p = wiiu_input_get(i);
		if (!p->connected) continue;
		size_t l = strlen(b);
		snprintf(b + l, sizeof(b) - l, "%s%s", l ? ", " : "", pad_kind_name(p));
	}
	wlog("controllers: %s", b[0] ? b : "none");
}

static void log_session_summary(void)
{
	unsigned under = 0, drops = 0;
	audio_take_stats(&under, &drops);
	uint32_t secs = (SDL_GetTicks() - sessStartMs) / 1000;
	if (sessWindows)
		wlog("session: %u min %u s, average %.1f fps, lowest %.1f fps, %u%% of the time under 55 fps, sound ran dry %u times, dropped %u",
		     secs / 60, secs % 60, sessFpsSum / sessWindows, sessFpsMin, sessSlow * 100 / sessWindows, under, drops);
	else
		wlog("session: %u s", secs);
}

/* the setup, once per start: what a bug report needs to know */
static void log_system(void)
{
	const OSSystemInfo *si = OSGetSystemInfo();
	{
		char env[160];
		wiiu_env_info(env, sizeof(env));
		wlog("%s", env);
	}
	if (si)
		wlog("system: Espresso %u MHz, bus %u MHz, language setting %d, games folder %s",
		     (unsigned)(si->coreClockSpeed / 1000000), (unsigned)(si->busClockSpeed / 1000000), cfg.lang, cfg.games_dir);
	/* every setting that is not at its default, on a few lines */
	char b[360] = "settings:";
	for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++) {
		char one[48];
		snprintf(one, sizeof(one), " %s=%d", OPTS[i].key, *OPTS[i].val);
		if (strlen(b) + strlen(one) >= sizeof(b) - 1) { wlog("%s", b); snprintf(b, sizeof(b), "settings:"); }
		strcat(b, one);
	}
	wlog("%s", b);
	snprintf(b, sizeof(b), "core options changed:");
	int any = 0;
	for (int i = 0; i < NCOREOPT; i++) {
		if (cfg.core[i] == COREOPT[i].def) continue;
		char one[64];
		snprintf(one, sizeof(one), " %s=%s", COREOPT[i].key, COREOPT[i].vals[cfg.core[i]]);
		if (strlen(b) + strlen(one) >= sizeof(b) - 1) { wlog("%s", b); snprintf(b, sizeof(b), "core options changed:"); }
		strcat(b, one);
		any = 1;
	}
	if (any) wlog("%s", b);
	/* the BIOS files */
	{
		char list[360] = "bios files:";
		int n = 0;
		DIR *d = opendir(WIIU_BIOS_DIR);
		struct dirent *e;
		while (d && (e = readdir(d))) {
			char full[600];
			struct stat st;
			if (e->d_name[0] == '.') continue;
			snprintf(full, sizeof(full), "%s/%s", WIIU_BIOS_DIR, e->d_name);
			if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) continue;
			size_t l = strlen(list);
			if (l + 60 < sizeof(list)) snprintf(list + l, sizeof(list) - l, " %s (%ld)", e->d_name, (long)st.st_size);
			n++;
		}
		if (d) closedir(d);
		wlog("%s", n ? list : "bios files: none (the built-in BIOS is used)");
	}
}

int emu_load_game(const char *pathIn)
{
	struct retro_game_info gi;
	char cue[512];
	const char *path = pathIn;
	/* a .bin / .img picked directly: use its .cue when there is one, or the
	 * CD audio tracks (music, many sounds) are missing */
	{
		const char *dot = strrchr(pathIn, '.');
		struct stat st;
		if (dot && (!strcasecmp(dot, ".bin") || !strcasecmp(dot, ".img"))) {
			snprintf(cue, sizeof(cue), "%.*s.cue", (int)(dot - pathIn), pathIn);
			if (stat(cue, &st) == 0) path = cue;
		}
	}
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
	{
		char aid[16] = "";
		covers_detect_id(path, aid);
		autofix_detect(gameName, aid);
		gameAnalog = analog_game(aid);
	}
	if (!ppfFromDisc) find_ppf(path);
	ppfFromDisc = 0;
	coreLogN = 0;
	coreLogOn = 1;
	{
		struct stat gst;
		wlog("loading %s (%lld bytes)", path, stat(path, &gst) == 0 ? (long long)gst.st_size : -1LL);
	}
	const uint32_t loadStart = SDL_GetTicks();
	bool loadedOk = retro_load_game(&gi);
	coreLogOn = 0;
	if (!loadedOk) {
		load_failed(path);
		wlog("could not load the game: %s", loadError);
		settings_leave_game();
		return -1;
	}
	wlog("loaded in %u ms", (unsigned)(SDL_GetTicks() - loadStart));
	{   /* what the core said while loading (BIOS, game id, region...) */
		int n = coreLogN < 12 ? coreLogN : 12;
		for (int i = 0; i < n; i++)
			wlog("core: %s", coreLog[(coreLogN - n + i + 48 * 4) % 48]);
	}
	video_set_halve(cfg.hires_fast == 2);
	sessStartMs = SDL_GetTicks();
	sessWindows = sessSlow = 0;
	sessFpsSum = 0;
	sessFpsMin = 1e9;
	{
		char act[300];
		snprintf(act, sizeof(act), "playing %s", gameName);
		wlog_activity(act);
	}
	loaded = 1;
	retro_get_system_av_info(&av);
	emu_apply_settings();
	load_memcard();
	audio_music(0);                      /* the menu music stops while a game plays */
	audio_init(av.timing.sample_rate);
	covers_set_loaded_id(path, CdromId);
	if (!covers_detect_id(path, gameId)) gameId[0] = 0;
	video_set_game_id(gameId);
	recent_add(path);
	cheats_load();
	if (ppfActive) video_osd(tr("Translation patch applied."));
	else if (emu_autofix_name()) {
		char m[160];
		snprintf(m, sizeof(m), tr("Automatic fix for this game: %s"), tr(emu_autofix_name()));
		video_osd(m);
	}
	return 0;
}

void emu_unload_game(void)
{
	if (!loaded) return;
	wlog("closing %s", gameName);
	log_session_summary();
	wlog_activity("");
	emu_save_memcards();
	if (cfg.resume) emu_state_save(-1);     /* "continue where you left off" */
	retro_unload_game();
	audio_shutdown();
	loaded = 0;
	autofixMask = 0;
	if (chtState != 1) chtState = 0;
	cheats_clear();
	cheatsReady = 0;
	wsActive = 0;
	video_set_widescreen(0);
	gameName[0] = 0;
	settings_leave_game();
}

/* ---- play time (playtime.txt: "seconds|last played|key" per line) ---- */
#define PLAYTIME_FILE WIIU_APP_DIR "/playtime.txt"
#define PT_MAX 1024
static struct { char key[200]; long secs; long long last; } ptime[PT_MAX];
static int nptime = -1;

static void playtime_load(void)
{
	char line[300];
	if (nptime >= 0) return;
	nptime = 0;
	FILE *f = fopen(PLAYTIME_FILE, "r");
	if (!f) return;
	while (nptime < PT_MAX && fgets(line, sizeof(line), f)) {
		long secs; long long last; int off = 0;
		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, "%ld|%lld|%n", &secs, &last, &off) < 2 || !off || !line[off]) continue;
		snprintf(ptime[nptime].key, sizeof(ptime[nptime].key), "%s", line + off);
		ptime[nptime].secs = secs;
		ptime[nptime].last = last;
		nptime++;
	}
	fclose(f);
}

static void playtime_save(void)
{
	FILE *f = fopen(PLAYTIME_FILE, "w");
	if (!f) return;
	for (int i = 0; i < nptime; i++)
		if (ptime[i].secs > 0) fprintf(f, "%ld|%lld|%s\n", ptime[i].secs, ptime[i].last, ptime[i].key);
	fclose(f);
}

static int playtime_find(const char *key, int create)
{
	playtime_load();
	for (int i = 0; i < nptime; i++)
		if (!strcmp(ptime[i].key, key)) return i;
	if (!create || nptime >= PT_MAX) return -1;
	snprintf(ptime[nptime].key, sizeof(ptime[nptime].key), "%s", key);
	ptime[nptime].secs = 0;
	ptime[nptime].last = 0;
	return nptime++;
}

long emu_playtime(const char *key, long long *lastPlayed)
{
	int i = playtime_find(key, 0);
	if (lastPlayed) *lastPlayed = i >= 0 ? ptime[i].last : 0;
	return i >= 0 ? ptime[i].secs : 0;
}

void emu_playtime_reset(const char *key)
{
	int i = playtime_find(key, 0);
	if (i < 0) return;
	ptime[i].secs = 0;
	playtime_save();
}

static void game_key(char *out, size_t n)
{
	snprintf(out, n, "%s", gameName);
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

static void playtime_add(long secs)
{
	char key[200];
	if (!gameName[0] || secs <= 0) return;
	game_key(key, sizeof(key));
	int i = playtime_find(key, 1);
	if (i < 0) return;
	ptime[i].secs += secs;
	ptime[i].last = (long long)time(NULL);
	playtime_save();
}

/* ---- recently played (recent.txt, newest first) ---- */
#define RECENT_FILE WIIU_APP_DIR "/recent.txt"
#define RECENT_MAX  12
static char recent[RECENT_MAX][512];
static int nrecent = -1;

/* "Crash (USA).cue" and ".../other/Crash (USA).bin" are the same game */
static void recent_key(const char *path, char *out, size_t n)
{
	const char *sl = strrchr(path, '/');
	snprintf(out, n, "%s", sl ? sl + 1 : path);
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

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
		if (!line[0] || stat(line, &st)) continue;
		char k[256], k2[256];
		int dup = 0;
		recent_key(line, k, sizeof(k));
		for (int j = 0; j < nrecent && !dup; j++) { recent_key(recent[j], k2, sizeof(k2)); dup = !strcmp(k, k2); }
		if (!dup) strcpy(recent[nrecent++], line);
	}
	fclose(f);
}

static void recent_add(const char *path)
{
	recent_load();
	int i, n = nrecent;
	char k[256], k2[256];
	recent_key(path, k, sizeof(k));
	for (i = 0; i < n; i++) { recent_key(recent[i], k2, sizeof(k2)); if (!strcmp(k, k2)) break; }
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
	if (!ok) wlog("could not save the state (slot %d)", slot);
	else if (slot >= 0) wlog("state saved (slot %d)", slot);
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
	if (slot >= 0) wlog(ok ? "state loaded (slot %d)" : "could not load the state (slot %d)", slot);
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

/* ---- cheats from the libretro database ----
 * When a game has no .cht file, it is fetched in the background from
 * github.com/libretro/libretro-database (cht/Sony - PlayStation): by its game
 * ID through content/cheats_index.txt, else by the game's file name. */
#define CHT_URL "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/Sony%20-%20PlayStation/"
static char chtNames[4][256], chtDest[512];
static int nChtNames;

static int cheat_index_lookup(const char *id, char *out, size_t n)
{
	char line[400];
	size_t l = strlen(id);
	FILE *f = id[0] ? fopen("fs:/vol/content/cheats_index.txt", "r") : NULL;
	if (!f) return 0;
	int found = 0;
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, id, l) && line[l] == '|') {
			line[strcspn(line, "\r\n")] = 0;
			snprintf(out, n, "%s", line + l + 1);
			found = 1;
			break;
		}
	fclose(f);
	return found;
}

static void url_encode(const char *in, char *out, size_t n)
{
	size_t o = 0;
	for (; *in && o + 4 < n; in++) {
		unsigned char c = (unsigned char)*in;
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
		else o += snprintf(out + o, n - o, "%%%02X", c);
	}
	out[o] = 0;
}

static int cheat_thread(void *ud)
{
	(void)ud;
	for (int i = 0; i < nChtNames; i++) {
		char url[900], enc[700];
		url_encode(chtNames[i], enc, sizeof(enc));
		snprintf(url, sizeof(url), "%s%s.cht", CHT_URL, enc);
		int r = net_download(url, chtDest);
		if (r == 1) { chtState = 2; return 0; }
		if (r == 0) break;                     /* no network: don't try the others */
	}
	chtState = 3;
	return 0;
}

static void cheats_fetch(const char *base)
{
	char idx[256];
	if (chtState == 1 || !net_status()) return;
	nChtNames = 0;
	if (cheat_index_lookup(gameId, idx, sizeof(idx))) snprintf(chtNames[nChtNames++], 256, "%s", idx);
	/* guesses from a No-Intro style file name */
	snprintf(chtNames[nChtNames++], 256, "%s (GameShark)", base);
	snprintf(chtNames[nChtNames++], 256, "%s (Game Buster)", base);
	snprintf(chtNames[nChtNames++], 256, "%s", base);
	wiiu_mkdirs(WIIU_APP_DIR "/cheats");
	snprintf(chtDest, sizeof(chtDest), "%s/cheats/%s.cht", WIIU_APP_DIR, gameId[0] ? gameId : base);
	chtState = 1;
	SDL_Thread *t = SDL_CreateThread(cheat_thread, "cheats", NULL);
	if (t) SDL_DetachThread(t); else chtState = 0;
}

int emu_cheats_downloading(void) { return chtState == 1; }

/* read the cheats once their download has finished */
void emu_cheats_poll(void)
{
	if (chtState == 2 && loaded) { chtState = 0; cheats_load(); }
}

/* ---- cheats: libretro .cht files in WiiUStation/cheats ---- */
#define MAX_CHEATS 128
static struct { char desc[96]; char code[512]; int on; } cheats[MAX_CHEATS];
static int ncheats;
static void cheats_clear(void) { ncheats = 0; }
static char chtPath[512];

static char *cht_value(char *v)
{
	while (*v == ' ' || *v == '\t') v++;
	v[strcspn(v, "\r\n")] = 0;
	if (*v == '"') { v++; char *e = strrchr(v, '"'); if (e) *e = 0; }
	return v;
}


static int is_widescreen_cheat(const char *d)
{
	char l[96];
	int i;
	for (i = 0; d[i] && i < 95; i++) l[i] = (char)tolower((unsigned char)d[i]);
	l[i] = 0;
	return strstr(l, "widescreen") || strstr(l, "wide screen") || strstr(l, "16:9") || strstr(l, "16x9");
}

int emu_widescreen_active(void) { return wsActive; }

static void cheats_apply(void)
{
	int ws = 0;
	retro_cheat_reset();
	for (int i = 0; i < ncheats; i++) {
		int on = cheats[i].on;
		if (cfg.widescreen && is_widescreen_cheat(cheats[i].desc)) { on = 1; ws = 1; }
		if (on) retro_cheat_set(i, true, cheats[i].code);
	}
	if (wsActive != ws) varsDirty = 1;   /* overscan "hack" follows it */
	wsActive = ws;
	video_set_widescreen(ws);
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
	if (!f) {
		/* none on the SD card: fetch it from the libretro database */
		chtPath[0] = 0;
		cheats_apply();
		cheatsReady = 1;
		if (chtState != 3) cheats_fetch(base);
		return;
	}
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
	cheatsReady = 1;
	if (cfg.widescreen && !wsActive)
		video_osd(tr("No widescreen code for this game: add one to its .cht file."));
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

/* ---- collections (collections.txt: "collection|file name" per line) ---- */
#define COLL_FILE WIIU_APP_DIR "/collections.txt"
static struct { char coll[64]; char file[256]; } *cent;
static int ncent = -1;
static char collNames[64][64];
static int ncoll;

static void coll_names(void)
{
	ncoll = 0;
	for (int i = 0; i < ncent && ncoll < 64; i++) {
		int k;
		for (k = 0; k < ncoll; k++) if (!strcmp(collNames[k], cent[i].coll)) break;
		if (k == ncoll) snprintf(collNames[ncoll++], 64, "%s", cent[i].coll);
	}
}

static void coll_load(void)
{
	char line[400];
	if (ncent >= 0) return;
	ncent = 0;
	FILE *f = fopen(COLL_FILE, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			char *bar = strchr(line, '|');
			if (!bar || bar == line || !bar[1]) continue;
			*bar = 0;
			cent = realloc(cent, (ncent + 1) * sizeof(*cent));
			snprintf(cent[ncent].coll, 64, "%s", line);
			snprintf(cent[ncent].file, 256, "%s", bar + 1);
			ncent++;
		}
		fclose(f);
	}
	coll_names();
}

static void coll_write(void)
{
	FILE *f = fopen(COLL_FILE, "w");
	if (!f) return;
	for (int i = 0; i < ncent; i++) fprintf(f, "%s|%s\n", cent[i].coll, cent[i].file);
	fclose(f);
	coll_names();
}

int emu_coll_count(void) { coll_load(); return ncoll; }
const char *emu_coll_name(int i) { coll_load(); return i >= 0 && i < ncoll ? collNames[i] : ""; }

int emu_coll_has(const char *coll, const char *path)
{
	coll_load();
	for (int i = 0; i < ncent; i++)
		if (!strcmp(cent[i].coll, coll) && !strcmp(cent[i].file, base_of(path))) return 1;
	return 0;
}

void emu_coll_toggle(const char *coll, const char *path)
{
	coll_load();
	for (int i = 0; i < ncent; i++)
		if (!strcmp(cent[i].coll, coll) && !strcmp(cent[i].file, base_of(path))) {
			memmove(&cent[i], &cent[i + 1], (ncent - i - 1) * sizeof(*cent));
			ncent--;
			coll_write();
			return;
		}
	cent = realloc(cent, (ncent + 1) * sizeof(*cent));
	snprintf(cent[ncent].coll, 64, "%s", coll);
	snprintf(cent[ncent].file, 256, "%s", base_of(path));
	ncent++;
	coll_write();
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

/* a hotkey or control-panel action */
static void do_action(int a, int *ffToggle, int *paused)
{
	char m[64];
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
	case HK_FFWD:  *ffToggle = !*ffToggle; break;
	case HK_PAUSE: *paused = !*paused; break;
	case HK_RESET: emu_reset(); video_osd(tr("Reset")); break;
	}
}

static int hotkey_pressed(void)
{
	static int latch = 1;
	uint32_t any = 0;
	int trigger = 0;
	for (int i = 0; i < WIIU_MAX_PADS; i++) {
		const wiiu_pad_state *p = wiiu_input_get(i);
		if (!p->connected) continue;
		any |= p->held;
		if (p->held & WIIU_BTN_HOME) trigger = 1;
		if ((p->held & WIIU_BTN_TOUCH) && !emu_touch_is_input() && cfg.drc != DRC_PANEL) trigger = 1;
		if ((p->held & (WIIU_BTN_STICKL | WIIU_BTN_STICKR)) ==
		    (WIIU_BTN_STICKL | WIIU_BTN_STICKR)) trigger = 1;
	}
	if (latch) {   /* wait for the buttons that opened/closed the menu */
		if (emu_touch_is_input() || cfg.drc == DRC_PANEL) any &= ~WIIU_BTN_TOUCH;
		if (!(any & (WIIU_BTN_HOME | WIIU_BTN_TOUCH | WIIU_BTN_STICKL | WIIU_BTN_STICKR)))
			latch = 0;
		return 0;
	}
	if (trigger) { latch = 1; return 1; }
	return 0;
}


/* ---- the watchdog: if the game loop stops producing frames, say where (in the log) ----
 * A freeze that takes the whole console leaves nothing behind, but a loop stuck in one
 * place leaves its marks here: the stage of the loop and where the emulated CPU is. */
extern unsigned wiiustation_ps1_state(unsigned *cycle);     /* patches/0009 */
static volatile unsigned wdFrames;
static volatile int wdActive, wdQuit;
static const char *volatile wdStage = "";

static int watchdog_thread(void *ud)
{
	(void)ud;
	unsigned lastFrames = 0;
	uint32_t lastChange = SDL_GetTicks();
	int reports = 0;
	while (!wdQuit) {
		SDL_Delay(1000);
		uint32_t now = SDL_GetTicks();
		if (!wdActive || wdFrames != lastFrames) {
			lastFrames = wdFrames;
			lastChange = now;
			reports = 0;
			continue;
		}
		uint32_t idle = now - lastChange;
		if ((reports == 0 && idle >= 4000) || (reports == 1 && idle >= 12000) || (reports == 2 && idle >= 40000)) {
			unsigned cyc = 0, pc = wiiustation_ps1_state(&cyc);
			wlog("STUCK: no frame for %u s; stage: %s; PS1 pc=%08x cycle=%u", (unsigned)(idle / 1000), wdStage, pc, cyc);
			reports++;
		}
	}
	return 0;
}

static void run_game(void)
{
	OSTime last = OSGetSystemTime(), fpsStart = last;
	int frames = 0;
	const double fps = av.timing.fps > 1 ? av.timing.fps : 59.94;
	const OSTime frameTicks = (OSTime)(OSSecondsToTicks(1) / fps);

	assign_pads();
	wdActive = 1;
	wiiu_input_pointer_menu(0);          /* the pointer is for the menu only */
	log_controllers(1);
	wiiu_home_button_ingame(1);
	video_set_ingame(1);
	video_set_vsync(cfg.vsync);
	audio_pause(!cfg.audio);
	hotkey_pressed();   /* arm the latch */

	int ffToggle = 0, ffPrev = 0, paused = 0;
	/* performance readout and adaptive VSync: busy = emulation + drawing
	 * (without the wait for the refresh). While it fits the frame, VSync
	 * is on (no tearing); when the game is heavy it is turned off, as a
	 * frame that misses the refresh would otherwise wait for the next
	 * one: 60 fps would drop to 30. */
	OSTime tEmu = 0, tDraw = 0, tWait = 0;
	int spuDiagT = 0, spuDiagN = 0, slowLogN = 0;
	double lastFps = 0, lastUpdMs = 0, lastEmuMs = 0, lastConvMs = 0, lastDrawMs = 0, lastWaitMs = 0;
	double busyAvg = 0;
	int vsOn = cfg.vsync;
	const double frameMs = 1000.0 / fps;
	OSTime playStart = OSGetSystemTime(), playPaused = 0, pauseAt = 0;
	uint32_t hkPrev = 0xffffffffu;   /* ignore buttons already held on entry */
	while (!wiiu_quit) {
		wiiu_input_update();
		if (hotkey_pressed()) break;

		/* cheats downloaded in the background: read them now */
		emu_cheats_poll();

		/* ---- hotkeys: hold the hotkey button, press an action ---- */
		{
			uint32_t held = 0;
			for (int i = 0; i < WIIU_MAX_PADS; i++)
				if (wiiu_input_get(i)->connected) held |= wiiu_input_get(i)->held;
			uint32_t pressed = held & ~hkPrev;
			hkPrev = held;
			hkHide = 0;
			if (cfg.hk[HK_MOD] >= 0 && (held & (1u << cfg.hk[HK_MOD]))) {
				for (int a = 1; a < HK_COUNT; a++) {
					int bit = cfg.hk[a];
					if (bit < 0 || bit == cfg.hk[HK_MOD]) continue;
					hkHide |= 1u << bit;                   /* the game doesn't see it */
					if (!(pressed & (1u << bit))) continue;
					do_action(a, &ffToggle, &paused);
				}
			}
		}
		/* control panel on the GamePad: buttons by touch */
		if (cfg.drc == DRC_PANEL) {
			static int wasDown;
			const wiiu_pad_state *gp = wiiu_input_get(0);
			int down = gp->connected && (gp->held & WIIU_BTN_TOUCH);
			if (down && !wasDown) {
				int a = video_panel_hit(gp->touch_x, gp->touch_y);
				if (a == PANEL_MENU) { wasDown = 1; break; }
				if (a > 0) do_action(a, &ffToggle, &paused);
			}
			wasDown = down;
			video_panel_state(quickSlot, ffToggle, paused, gameName);
		}
		if (paused && !pauseAt) pauseAt = OSGetSystemTime();
		if (!paused && pauseAt) { playPaused += OSGetSystemTime() - pauseAt; pauseAt = 0; }
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
		OSTime t0 = OSGetSystemTime();
		wdStage = "emulating a frame (retro_run)";
		retro_run();
		OSTime t1 = OSGetSystemTime();
		wdStage = "drawing the frame";
		video_draw_game(0);
		OSTime t2 = OSGetSystemTime();
		if (ff) video_osd(">> Fast forward");
		wdStage = "presenting the frame";
		video_present();
		OSTime t3 = OSGetSystemTime();
		wdStage = "system events";
		if (!pump_events()) break;
		wdFrames++;
		tEmu += t1 - t0; tDraw += t2 - t1; tWait += t3 - t2;
		if (cfg.vsync && !ff) {
			double busy = (double)(t2 - t0) * 1000.0 / OSSecondsToTicks(1);
			busyAvg = busyAvg ? busyAvg * 0.9 + busy * 0.1 : busy;
			if (vsOn && busyAvg > frameMs * 0.94) vsOn = 0;
			else if (!vsOn && busyAvg < frameMs * 0.80) vsOn = 1;
		} else if (!cfg.vsync) vsOn = 0;
		video_set_vsync(vsOn);

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
			{
				const double k = 1000.0 / OSSecondsToTicks(1) / 60.0;      /* ticks -> ms per frame */
				double convMs = video_take_conv_ms() / 60.0;
				lastUpdMs = video_take_upd_ms() / 60.0;
				if (cfg.show_fps) video_set_timing(tEmu * k, convMs, tDraw * k, tWait * k, vsOn);
				if (!ff && secs > 0) {                 /* the session summary */
					double f = 60.0 / secs;
					sessFpsSum += f;
					if (f < sessFpsMin) sessFpsMin = f;
					if (f < 55.0) sessSlow++;
					sessWindows++;
				}
				log_controllers(0);
				/* a heartbeat every ~5 s kept for the next start (if the console freezes the
				 * last one says what the game was doing), and one line in the log every minute */
				{
					static uint32_t hbAt, hbLog;
					uint32_t nowMs = SDL_GetTicks();
					if (nowMs - hbAt >= 5000) {
						char cd[220], hb[400], act[480];
						hbAt = nowMs;
						wiiustation_cd_info(cd, sizeof(cd));
						snprintf(hb, sizeof(hb), "%.1f fps, screen %dx%d, emulation %.1f ms; %s",
						         secs > 0 ? 60.0 / secs : 0.0, video_game_width(), video_game_height(), tEmu * 1000.0 / OSSecondsToTicks(1) / 60.0, cd);
						snprintf(act, sizeof(act), "playing %.100s | %s", gameName, hb);
						wlog_activity(act);
						if (nowMs - hbLog >= 60000) {
							hbLog = nowMs;
							wlog("alive: %s", hb);
						}
					}
				}
				lastFps = secs > 0 ? 60.0 / secs : 0;
				if (cfg.hires_fast == 1) {             /* auto: a hi-res stretch under 50 fps twice in a row */
					static int hiSlow;
					if (lastFps > 0 && lastFps < 50 && video_game_height() >= 400) {
						if (++hiSlow == 2) { video_set_halve(1); wlog("hi-res speed-up on: half of the lines of 640x480 frames"); }
					} else hiSlow = 0;
				} lastEmuMs = tEmu * k; lastConvMs = convMs; lastDrawMs = tDraw * k; lastWaitMs = tWait * k;
			}
			tEmu = tDraw = tWait = 0;
			fpsStart = now;
			frames = 0;
			/* a slow stretch (under 45 fps): where the time goes, up to 40 times */
			if (lastFps > 0 && lastFps < 45 && slowLogN < 40) {
				static unsigned prevReads;
				static unsigned long long prevUs;
				wlog("slow: %.1f fps; per frame: emulation %.1f ms, picture conversion %.1f ms (texture upload %.1f ms), drawing %.1f ms, waiting %.1f ms; screen %dx%d; CHD: %u hunk reads, %.1f ms per frame",
				     lastFps, lastEmuMs, lastConvMs, lastUpdMs, lastDrawMs, lastWaitMs, video_game_width(), video_game_height(),
				     wiiustation_chd_reads - prevReads, (double)(wiiustation_chd_us - prevUs) / 1000.0 / 60.0);
				prevReads = wiiustation_chd_reads;
				prevUs = wiiustation_chd_us;
				slowLogN++;
			}
			/* sound state for the log: every 3 s, the first 25 times */
			if (spuDiagN < 25 && ++spuDiagT >= 3) {
				char d[400];
				spuDiagT = 0;
				wiiustation_spu_diag(d, sizeof(d));
wlog("spu %s", d);
				spuDiagN++;
			}
		}
	}

	wdActive = 0;
	ffwdMask = 0xffffffffu;
	hkHide = 0;
	if (pauseAt) playPaused += OSGetSystemTime() - pauseAt;
	playtime_add((long)((OSGetSystemTime() - playStart - playPaused) / OSSecondsToTicks(1)));
	audio_pause(1);
	for (int i = 0; i < WIIU_MAX_PADS; i++) wiiu_input_rumble(i, 0);
	wiiu_home_button_ingame(0);
	video_set_ingame(0);
	video_set_vsync(1);         /* the menu always waits for the refresh */
	emu_save_memcards();
	wiiu_input_reset_hotkey();
}

/* Versions 1.0.0 and 1.0.1 kept the data folder at sd:/WiiUStation; it is
 * back in sd:/wiiu/apps/WiiUStation. On the first start of this version the
 * folder is moved as a whole (same card, so it is only renamed) and the
 * paths saved in its lists are rewritten. */
static void replace_in_file(const char *path, const char *from, const char *to)
{
	FILE *f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 4 * 1024 * 1024) { fclose(f); return; }
	char *buf = malloc(n + 1);
	if (!buf) { fclose(f); return; }
	n = (long)fread(buf, 1, n, f);
	buf[n] = 0;
	fclose(f);
	if (!strstr(buf, from)) { free(buf); return; }
	size_t fl = strlen(from), tl = strlen(to);
	FILE *o = fopen(path, "wb");
	if (o) {
		for (char *p = buf, *q; *p; p = q + fl) {
			q = strstr(p, from);
			if (!q) { fputs(p, o); break; }
			fwrite(p, 1, q - p, o);
			fwrite(to, 1, tl, o);
		}
		fclose(o);
	}
	free(buf);
}

static void app_folder_move(void)
{
	struct stat st;
	if (stat(WIIU_APP_DIR, &st) == 0) return;                          /* already there */
	wiiu_mkdirs(WIIU_SD_ROOT "/wiiu/apps");
	if (stat(WIIU_PREV_APP_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return;   /* new install */
	if (rename(WIIU_PREV_APP_DIR, WIIU_APP_DIR) != 0) return;            /* keep using nothing old */
	static const char *files[] = { "settings.cfg", "recent.txt", "favorites.txt", "collections.txt",
	                               "playtime.txt", "covers/ids.txt", NULL };
	for (int i = 0; files[i]; i++) {
		char p[256];
		snprintf(p, sizeof(p), "%s/%s", WIIU_APP_DIR, files[i]);
		replace_in_file(p, WIIU_PREV_APP_DIR "/", WIIU_APP_DIR "/");
	}
	/* per-game settings files */
	DIR *d = opendir(WIIU_APP_DIR "/gamesettings");
	if (d) {
		struct dirent *e;
		while ((e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.') continue;
			char p[512];
			snprintf(p, sizeof(p), "%s/gamesettings/%s", WIIU_APP_DIR, e->d_name);
			replace_in_file(p, WIIU_PREV_APP_DIR "/", WIIU_APP_DIR "/");
		}
		closedir(d);
	}
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;

	if (video_init() < 0) return -1;
	app_folder_move();
	update_cleanup();
	wlog_init();
	{
		SDL_Thread *wd = SDL_CreateThreadWithStackSize(watchdog_thread, "watchdog", 64 * 1024, NULL);
		if (wd) SDL_DetachThread(wd);
	}
	wiiu_input_init();
	wiiu_mkdirs(WIIU_ISO_DIR);
	wiiu_mkdirs(WIIU_BIOS_DIR);
	wiiu_mkdirs(WIIU_SAVE_DIR);
	wiiu_mkdirs(WIIU_STATE_DIR);
	wiiu_mkdirs(WIIU_COVER_DIR);
	wiiu_mkdirs(WIIU_BEZEL_DIR);
	wiiu_mkdirs(WIIU_APP_DIR "/patches");
	jit_available();          /* probe codegen on the main core */
	settings_load();
	video_set_tex565(cfg.tex565);        /* experimental 16-bit picture upload */
	wlog("start: jit %s, dynarec %s, gpu thread %s, vsync %s, aspect %d, filter %d, region %d",
	     jit_available() ? "available" : "unavailable", cfg.dynarec ? "on" : "off", cfg.gpu_thread ? "on" : "off",
	     cfg.vsync ? "on" : "off", cfg.aspect, cfg.filter, cfg.region);
	log_system();

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
		/* a downloaded update is put in place now that nothing is open,
		 * and the app starts again in the new version */
		if (!update_finish()) SYSLaunchMenu();
		while (pump_events()) SDL_Delay(10);
	}
	wlog_exit();
	video_shutdown();
	return 0;
}
