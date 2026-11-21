/*
 * WiiUStation - standalone PlayStation emulator for the Wii U (Aroma),
 * built on the PCSX-ReARMed core.
 */
#ifndef WIIUSTATION_PLATFORM_H
#define WIIUSTATION_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SD card layout: everything lives next to the app ----
 *   sd:/wiiu/apps/WiiUStation.wuhb
 *   sd:/wiiu/apps/WiiUStation/{games,bios,saves,states,covers,bezels}
 */
#ifndef WIIU_SD_ROOT
#define WIIU_SD_ROOT     "fs:/vol/external01"
#endif
#define WIIU_APP_DIR     WIIU_SD_ROOT "/wiiu/apps/WiiUStation"
#define WIIU_ISO_DIR     WIIU_APP_DIR "/games"
#define WIIU_BIOS_DIR    WIIU_APP_DIR "/bios"
#define WIIU_SAVE_DIR    WIIU_APP_DIR "/saves"
#define WIIU_STATE_DIR   WIIU_APP_DIR "/states"
#define WIIU_COVER_DIR   WIIU_APP_DIR "/covers"
#define WIIU_BEZEL_DIR   WIIU_APP_DIR "/bezels"
#define WIIU_CONFIG_FILE WIIU_APP_DIR "/settings.cfg"
/* used by test builds before the folder move */
#define WIIU_OLD_DIR     WIIU_SD_ROOT "/wiisxrx"

int wiiu_mkdirs(const char *path);

/* ---- settings ---- */
/* ZOOM: 4:3 with the black borders the game itself draws cropped away */
enum { WIIU_ASPECT_4_3 = 0, WIIU_ASPECT_STRETCH, WIIU_ASPECT_INTEGER, WIIU_ASPECT_ZOOM };
/* values are stored in settings.cfg: keep the first three as they were */
enum { BEZEL_OFF = 0, BEZEL_AMBIENT, BEZEL_CUSTOM,
       BEZEL_TV, BEZEL_GREY, BEZEL_CARBON, BEZEL_SYNTHWAVE, BEZEL_WOOD,
       BEZEL_WHITE, BEZEL_SPACE,
       BEZEL_TUBETV, BEZEL_CLASSICTV, BEZEL_PIXELSKY, BEZEL_PIXELCITY, BEZEL_PIXELCASTLE,
       BEZEL_COUNT };
const char *bezel_name(int b);   /* "Retro TV", ... */
int bezel_next(int b, int dir);  /* menu order */
enum { WIIU_FILTER_LINEAR = 0, WIIU_FILTER_NEAREST, WIIU_FILTER_SHARP_BILINEAR };

typedef struct {
	int dynarec;        /* Lightrec */
	int gpu_thread;     /* render on core 2 */
	int spu_thread;     /* mix on core 0 */
	int frameskip;      /* 0 off, 1 auto */
	int show_fps;
	int aspect;
	int filter;
	int scanlines;
	int crt;            /* CRT aperture-grille mask */
	int reverb;         /* SPU reverb (costs CPU) */
	int dithering;      /* 0 off, 1 on, 2 force */
	int bios_hle;       /* 1 = always HLE */
	int bootlogo;       /* show BIOS boot logo */
	int analog;         /* player 1 pad type: 1 = DualShock */
	int analog2;        /* player 2 pad type */
	int multitap;       /* 4 players on port 1 */
	int region;         /* 0 auto, 1 NTSC, 2 PAL */
	int resume;         /* offer to continue where you left off */
	int ffwd;           /* fast forward: 0 off, 1 hold R3, 2 toggle R3, 3 hold ZL+ZR */
	int ffspeed;        /* 2..4x */
	int rumble;
	int audio;
	int bezel;          /* BEZEL_* */
	int cover3d;        /* covers: 0 = 2D front, 1 = 3D box */
	int view;           /* game browser: 0 grid, 1 list, 2 compact, 3 shelf, 4 carousel */
	int core[24];       /* extra PCSX-ReARMed options, see core_opt_* */
	int map[16];        /* PSX button (RETRO_DEVICE_ID_JOYPAD_*) -> WIIU_BTN bit index */
	char games_dir[256];  /* folder the game browser opens */
} wiiu_settings;

extern wiiu_settings cfg;
void settings_load(void);
int  settings_save(void);
/* per-game settings: game_key = game ID or file name */
void settings_enter_game(const char *game_key);  /* global + overrides */
void settings_leave_game(void);                  /* back to global */
int  settings_game_scope(void);                  /* 1 if editing this game's file */
int  settings_set_game_scope(int on);            /* create / delete the file */

/* extra core options shown in Advanced (main.c) */
int  core_opt_count(void);
const char *core_opt_label(int i);
const char *core_opt_value(int i);     /* current value, as shown */
const char *core_opt_section(int i);   /* first option of a group: its title */
const char *core_opt_help(int i);
void core_opt_cycle(int i, int dir);

/* ---- video (video.c) ---- */
int  video_init(void);
void video_shutdown(void);
void *video_renderer(void);
void video_set_pixel_format(int fmt);  /* RETRO_PIXEL_FORMAT_* */
void video_upload(const void *data, unsigned w, unsigned h, size_t pitch);
void video_draw_game(int dim);
void video_present(void);
void video_osd(const char *msg);
void video_set_fps(int fps, int target);  /* shown when Show FPS is on */
void video_set_game_id(const char *id);
void video_fade_in(void);
/* save the last game frame as PNG; width 0 = full size, else scaled */
int  video_save_frame(const char *path, int width);
void *video_open_font(int size);       /* TTF_Font* (system font) */

/* events: 0 when the app must quit (HOME Menu -> Close) */
int  pump_events(void);
/* system keyboard: begin, poll (1 = confirmed, -1 = cancelled, 0 = typing) */
void video_text_begin(void);
int  video_text_poll(char *buf, size_t n);
int  video_text_active(void);
void video_text_end(void);
extern volatile int wiiu_quit;

/* ---- audio (audio.c) ---- */
int  audio_init(double rate);
void audio_shutdown(void);
void audio_pause(int pause);
void audio_push(const int16_t *frames, size_t count);
size_t audio_queued_frames(void);
size_t audio_target_frames(void);

/* ---- input (input.c) ---- */
enum {
	WIIU_BTN_A      = 1 << 0,
	WIIU_BTN_B      = 1 << 1,
	WIIU_BTN_X      = 1 << 2,
	WIIU_BTN_Y      = 1 << 3,
	WIIU_BTN_UP     = 1 << 4,
	WIIU_BTN_DOWN   = 1 << 5,
	WIIU_BTN_LEFT   = 1 << 6,
	WIIU_BTN_RIGHT  = 1 << 7,
	WIIU_BTN_L      = 1 << 8,
	WIIU_BTN_R      = 1 << 9,
	WIIU_BTN_ZL     = 1 << 10,
	WIIU_BTN_ZR     = 1 << 11,
	WIIU_BTN_PLUS   = 1 << 12,
	WIIU_BTN_MINUS  = 1 << 13,
	WIIU_BTN_STICKL = 1 << 14,
	WIIU_BTN_STICKR = 1 << 15,
	WIIU_BTN_TOUCH  = 1 << 16,
	WIIU_BTN_HOME   = 1 << 17,
};

typedef struct {
	int connected;
	uint32_t held;
	uint32_t pressed;
	float lx, ly, rx, ry;    /* -1..1, +y = up */
	int touch_x, touch_y;
} wiiu_pad_state;

#define WIIU_MAX_PADS 5          /* 0 = GamePad, 1..4 = Pro/Classic/Remote */
void wiiu_input_init(void);
void wiiu_input_shutdown(void);
void wiiu_input_update(void);
const wiiu_pad_state *wiiu_input_get(int idx);
uint32_t wiiu_input_menu_pressed(void);
uint32_t wiiu_input_menu_held(void);
void wiiu_input_rumble(int idx, int on);
void wiiu_input_reset_hotkey(void);
void wiiu_home_button_ingame(int ingame);

/* ---- JIT (jit.c) ---- */
int  jit_available(void);

/* ---- emulator (main.c) ---- */
int  emu_load_game(const char *path);
int  emu_load_game_disc(const char *m3u, int index, const char *disc_path);
void emu_unload_game(void);
int  emu_game_loaded(void);
const char *emu_game_path(void);
int  emu_recent_count(void);
const char *emu_recent(int i);   /* full path, newest first */
const char *emu_game_name(void);
void emu_reset(void);
int  emu_state_save(int slot);
int  emu_state_load(int slot);      /* slot -1 = automatic resume state */
int  emu_state_exists(int slot);
void emu_state_thumb(int slot, char *out, size_t n);
long long emu_state_time(int slot);   /* mtime, 0 = empty */
int  emu_screenshot(void);
int  emu_player_count(void);
int  emu_cheat_count(void);
const char *emu_cheat_name(int i);
int  emu_cheat_enabled(int i);
void emu_cheat_toggle(int i);
int  emu_fav_is(const char *path);
void emu_fav_toggle(const char *path);
int  emu_disk_count(void);
int  emu_disk_index(void);
int  emu_disk_set(int index);
int  emu_disk_append(const char *path);
void emu_apply_settings(void);
void emu_save_memcards(void);

/* ---- covers (covers.c) ---- */
int   covers_detect_id(const char *path, char *out);   /* out: "SCUS-94244" */
void  covers_set_loaded_id(const char *path, const char *cdromId);
void  covers_request(const char *id);
int   covers_status(const char *id);   /* 2 have, 1 downloading, 0 none */
void *covers_texture(void *renderer, const char *id, int *w, int *h);
void  covers_shutdown(void);
void  covers_flush(void);              /* after changing 2D / 3D */
const char *emu_game_id(void);

/* ---- menu (menu.c) ---- */
int  menu_run(void);   /* 1 = play, 0 = quit */

#ifdef __cplusplus
}
#endif
#endif
