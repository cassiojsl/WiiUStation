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
#include <sys/stat.h>

#include "platform.h"
#include "libretro.h"

wiiu_settings cfg;

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
static struct { const char *key; int *val; int min, max; } OPTS[] = {
	{ "dynarec",    &cfg.dynarec,    0, 1 },
	{ "gpu_thread", &cfg.gpu_thread, 0, 1 },
	{ "spu_thread", &cfg.spu_thread, 0, 1 },
	{ "frameskip",  &cfg.frameskip,  0, 1 },
	{ "show_fps",   &cfg.show_fps,   0, 1 },
	{ "aspect",     &cfg.aspect,     0, 2 },
	{ "filter",     &cfg.filter,     0, 2 },
	{ "scanlines",  &cfg.scanlines,  0, 1 },
	{ "crt",        &cfg.crt,        0, 1 },
	{ "reverb",     &cfg.reverb,     0, 1 },
	{ "dithering",  &cfg.dithering,  0, 2 },
	{ "bios_hle",   &cfg.bios_hle,   0, 1 },
	{ "bootlogo",   &cfg.bootlogo,   0, 1 },
	{ "analog",     &cfg.analog,     0, 1 },
	{ "rumble",     &cfg.rumble,     0, 1 },
	{ "audio",      &cfg.audio,      0, 1 },
	{ "bezel",      &cfg.bezel,      0, 2 },
	{ "view",       &cfg.view,       0, 1 },
};

void settings_load(void)
{
	char line[128];
	FILE *f;
	cfg.dynarec = 1;
	cfg.gpu_thread = 1;   /* renderer on core 2 */
	cfg.spu_thread = 0;   /* off: sound was lost with Lightrec + SPU thread */
	cfg.frameskip = 0;
	cfg.show_fps = 0;
	cfg.aspect = WIIU_ASPECT_4_3;
	cfg.filter = WIIU_FILTER_LINEAR;
	cfg.scanlines = 0;
	cfg.crt = 0;
	cfg.reverb = 1;
	cfg.dithering = 1;
	cfg.bios_hle = 0;
	cfg.bootlogo = 0;
	cfg.analog = 0;       /* digital pad: works with every game */
	cfg.rumble = 1;
	cfg.audio = 1;
	cfg.bezel = BEZEL_AMBIENT;
	cfg.view = 0;
	strcpy(cfg.games_dir, WIIU_ISO_DIR);
	f = fopen(WIIU_CONFIG_FILE, "r");
	if (!f) f = fopen(WIIU_OLD_DIR "/wiiustation.cfg", "r");   /* older builds */
	if (f) {
		int version = 0;
		long start;
		/* settings files written before version 2 had the threaded GPU off
		 * and DualShock on: ignore those two keys from old files. */
		while (fgets(line, sizeof(line), f))
			if (!strncmp(line, "version", 7)) version = atoi(strchr(line, '=') ? strchr(line, '=') + 1 : "0");
		rewind(f);
		(void)start;
		while (fgets(line, sizeof(line), f)) {
			char *eq = strchr(line, '=');
			if (!eq || line[0] == '#') continue;
			*eq = 0;
			char *k = line;
			while (*k == ' ') k++;
			for (char *e = eq - 1; e >= k && *e == ' '; e--) *e = 0;
			if (!strcmp(k, "games_dir")) {
				char *v = eq + 1;
				while (*v == ' ') v++;
				v[strcspn(v, "\r\n")] = 0;
				if (!strncmp(v, WIIU_SD_ROOT, strlen(WIIU_SD_ROOT)))
					strncpy(cfg.games_dir, v, sizeof(cfg.games_dir) - 1);
				continue;
			}
			int v = atoi(eq + 1);
			if (version < 2 && (!strcmp(k, "gpu_thread") || !strcmp(k, "analog")))
				continue;
			if (version < 3 && (!strcmp(k, "spu_thread") || !strcmp(k, "filter")))
				continue;
			for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
				if (!strcmp(OPTS[i].key, k) && v >= OPTS[i].min && v <= OPTS[i].max)
					*OPTS[i].val = v;
		}
		fclose(f);
	}
	if (!jit_available()) cfg.dynarec = 0;
}

int settings_save(void)
{
	FILE *f;
	wiiu_mkdirs(WIIU_APP_DIR);
	f = fopen(WIIU_CONFIG_FILE, "w");
	if (!f) return 0;
	fprintf(f, "# WiiUStation settings\nversion = 3\n");
	for (unsigned i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
		fprintf(f, "%s = %d\n", OPTS[i].key, *OPTS[i].val);
	fprintf(f, "games_dir = %s\n", cfg.games_dir);
	fclose(f);
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
	if (!strcmp(key, "pcsx_rearmed_vibration")) return cfg.rumble ? "enabled" : "disabled";
	if (!strcmp(key, "pcsx_rearmed_display_fps_v2")) return "disabled";
	if (!strcmp(key, "pcsx_rearmed_rgb32_output")) return "disabled"; /* RGB565: cheapest upload */
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

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
	audio_push(d, frames);
	return frames;
}

/* ---- input: PSX port -> Wii U controller ---- */
static int portPad[2] = { -1, -1 };

int pad_for_port(int port)
{
	return port >= 0 && port < 2 ? portPad[port] : -1;
}

static void assign_pads(void)
{
	int n = 0;
	portPad[0] = portPad[1] = -1;
	for (int i = 0; i < WIIU_MAX_PADS && n < 2; i++)
		if (wiiu_input_get(i)->connected) portPad[n++] = i;
	if (portPad[0] < 0) portPad[0] = 0;
}

static void input_poll_cb(void)
{
	wiiu_input_update();
}

static uint32_t psx_mask(const wiiu_pad_state *p)
{
	/* Nintendo face buttons by position: A=Circle B=Cross X=Triangle Y=Square */
	uint32_t b = p->held, m = 0;
	if (b & WIIU_BTN_B)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_B;      /* Cross    */
	if (b & WIIU_BTN_A)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_A;      /* Circle   */
	if (b & WIIU_BTN_Y)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_Y;      /* Square   */
	if (b & WIIU_BTN_X)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_X;      /* Triangle */
	if (b & WIIU_BTN_UP)     m |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
	if (b & WIIU_BTN_DOWN)   m |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (b & WIIU_BTN_LEFT)   m |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (b & WIIU_BTN_RIGHT)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	if (b & WIIU_BTN_L)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
	if (b & WIIU_BTN_R)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_R;
	if (b & WIIU_BTN_ZL)     m |= 1 << RETRO_DEVICE_ID_JOYPAD_L2;
	if (b & WIIU_BTN_ZR)     m |= 1 << RETRO_DEVICE_ID_JOYPAD_R2;
	if (b & WIIU_BTN_STICKL) m |= 1 << RETRO_DEVICE_ID_JOYPAD_L3;
	if (b & WIIU_BTN_STICKR) m |= 1 << RETRO_DEVICE_ID_JOYPAD_R3;
	if (b & WIIU_BTN_PLUS)   m |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
	if (b & WIIU_BTN_MINUS)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;
	/* Digital-only pads: left stick also drives the d-pad */
	if (!cfg.analog) {
		if (p->ly > 0.5f)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
		if (p->ly < -0.5f) m |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
		if (p->lx < -0.5f) m |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
		if (p->lx > 0.5f)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	}
	return m;
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	int pad = pad_for_port(port);
	const wiiu_pad_state *p;
	if (pad < 0) return 0;
	p = wiiu_input_get(pad);
	if (!p->connected) return 0;

	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: {
		uint32_t m = psx_mask(p);
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
		return (m >> id) & 1;
	}
	case RETRO_DEVICE_ANALOG: {
		float v;
		if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
			uint32_t m = psx_mask(p);
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
		unsigned dev = cfg.analog ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
		                          : RETRO_DEVICE_JOYPAD;
		retro_set_controller_port_device(0, dev);
		retro_set_controller_port_device(1, dev);
	}
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

	varsDirty = 1;
	if (!retro_load_game(&gi)) return -1;
	loaded = 1;
	retro_get_system_av_info(&av);
	emu_apply_settings();
	load_memcard();
	audio_init(av.timing.sample_rate);
	covers_set_loaded_id(path, CdromId);
	if (!covers_detect_id(path, gameId)) gameId[0] = 0;
	video_set_game_id(gameId);
	return 0;
}

void emu_unload_game(void)
{
	if (!loaded) return;
	emu_save_memcards();
	retro_unload_game();
	audio_shutdown();
	loaded = 0;
	gameName[0] = 0;
}

int emu_game_loaded(void) { return loaded; }
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
	snprintf(out, sz, "%s/%s.state%d", WIIU_STATE_DIR, base, slot);
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

	while (!wiiu_quit) {
		wiiu_input_update();
		if (hotkey_pressed()) break;

		retro_run();
		video_draw_game(0);
		video_present();
		if (!pump_events()) break;

		/* pacing: audio sync when sound is on, timer otherwise */
		if (cfg.audio) {
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
			char t[32];
			double secs = (double)(now - fpsStart) / OSSecondsToTicks(1);
			snprintf(t, sizeof(t), "%.1f FPS", secs > 0 ? 60.0 / secs : 0.0);
			video_set_fps_text(t);
			fpsStart = now;
			frames = 0;
		}
	}

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
