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
/* screen effects drawn on top of the filter */
enum { FX_OFF = 0, FX_CURVE, FX_LCD, FX_GLOW, FX_COUNT };

/* hotkeys: hold the modifier (hk[HK_MOD]) and press the action's button */
enum { HK_MOD = 0, HK_SAVE, HK_LOAD, HK_SLOTUP, HK_SLOTDOWN, HK_SHOT, HK_FFWD, HK_PAUSE, HK_RESET, HK_COUNT };
const char *hotkey_name(int i);
const char *button_name(int bit);       /* "A", "ZL", "None" for -1 ... */

/* what the GamePad screen shows during a game */
enum { DRC_MIRROR = 0, DRC_ONLY, DRC_TV_ONLY, DRC_PANEL, DRC_COUNT };
/* control panel buttons (DRC_PANEL): hotkey actions, plus the menu */
#define PANEL_MENU HK_COUNT

/* controller types */
enum { PAD_AUTO = -1, PAD_DIGITAL = 0, PAD_DUALSHOCK, PAD_NEGCON, PAD_MOUSE, PAD_GUNCON, PAD_JUSTIFIER, PAD_COUNT };
const char *pad_type_name(int t);        /* PAD_AUTO shows what it picked */
int  emu_touch_is_input(void);           /* 1: GamePad touch is the gun / mouse */

/* interface language */
enum { LANG_AUTO = -1, LANG_EN = 0, LANG_PT, LANG_ES, LANG_FR, LANG_DE, LANG_IT, LANG_NL, LANG_RU, LANG_JA, LANG_COUNT };
const char *lang_name(int lang);        /* in its own language */
int  lang_system(void);                  /* the Wii U's language */
int  lang_current(void);                 /* the one in use */
const char *tr(const char *en);          /* translated text (lang.c) */

enum { THEME_BLUE, THEME_PURPLE, THEME_PINK, THEME_RED, THEME_ORANGE, THEME_GOLD, THEME_GREEN, THEME_TEAL, THEME_GRAPHITE, THEME_COUNT };

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
	int fx;             /* FX_* */
	int vsync;          /* wait for the screen refresh (no tearing) */
	int widescreen;     /* 16:9 with the game's widescreen code (per game) */
	int gyroaim;        /* light gun aimed by turning the GamePad */
	int drc;            /* DRC_*: GamePad screen during a game */
	int reverb;         /* SPU reverb (costs CPU) */
	int dithering;      /* 0 off, 1 on, 2 force */
	int bios_hle;       /* 1 = always HLE */
	int bootlogo;       /* show BIOS boot logo */
	int analog;         /* player 1 controller: PAD_* */
	int analog2;        /* players 2+: PAD_DIGITAL, PAD_DUALSHOCK or PAD_NEGCON */
	int deadzone;       /* analog stick dead zone, in 5% steps */
	int multitap;       /* 4 players on port 1 */
	int region;         /* 0 auto, 1 NTSC, 2 PAL */
	int resume;         /* offer to continue where you left off */
	int ffwd;           /* fast forward: 0 off, 1 hold R3, 2 toggle R3, 3 hold ZL+ZR */
	int ffspeed;        /* 2..4x */
	int rumble;
	int audio;
	int bezel;          /* BEZEL_* */
	int lang;           /* LANG_*, LANG_AUTO = follow the Wii U */
	int sort;           /* game list: 0 name, 1 last played, 2 most played */
	int regionf;        /* game list: 0 all, 1 USA, 2 Europe, 3 Japan, 4 favourites, 5+ a collection */
	int theme;          /* menu colours (THEME_*) */
	int menusfx;        /* menu sounds: 0 off, 1 classic, 2 soft, 3 retro, 4 crystal, 5 marimba, 6 click */
	int autostart;      /* open the last game when the app starts */
	int autofix;        /* apply the built-in fixes for known games */
	int cover3d;        /* covers: 0 = 2D front, 1 = 3D box */
	int view;           /* game browser: 0 grid, 1 list, 2 compact, 3 shelf, 4 carousel */
	int core[32];       /* extra PCSX-ReARMed options, see core_opt_* */
	int hk[HK_COUNT];   /* hotkeys: WIIU_BTN bit index, -1 = none; hk[0] = modifier */
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
/* screen point (1280x720) -> game frame, -32767..32767; 0 when outside the picture */
int  video_screen_to_game(int sx, int sy, int *gx, int *gy);
void video_set_widescreen(int on);
void video_set_crosshair(int on, int gx, int gy);   /* game coords -32767..32767 */
void video_fade_in(void);
void video_set_ingame(int ingame);
void video_set_vsync(int on);
void *video_preview(void);              /* SDL_Texture* of the game with the picture settings */
void video_lang_changed(void);           /* drop cached translated labels */        /* screens follow cfg.drc only in a game */
int  video_screen_modes_ok(void);         /* 0 if TV / GamePad can't be split */
int  video_panel_hit(int x, int y);       /* control panel button at a touch, or -1 */
void video_panel_state(int slot, int ff, int paused, const char *game);
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
void audio_route(int tv, int drc);       /* which screens play the sound */
void audio_push(const int16_t *frames, size_t count);
size_t audio_queued_frames(void);
size_t audio_target_frames(void);
/* menu sounds */
enum { SFX_MOVE, SFX_OK, SFX_BACK, SFX_ERROR, SFX_INTRO, SFX_COUNT };
void sfx_prepare(void);
void sfx_play(int id);

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
	float gyro_x, gyro_y;     /* GamePad turn speed, 1.0 = 360 degrees per second */
	int battery;              /* GamePad battery as reported by VPAD (0..6) */
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
void emu_background_save(void);          /* before the HOME Menu */
int  emu_ppf_active(void);
/* play time, by game key (file name without folder, extension, disc tag) */
long emu_playtime(const char *key, long long *lastPlayed);
void emu_playtime_reset(const char *key);
int  emu_widescreen_active(void);
int  wiiu_battery(void);                 /* GamePad battery 0..6, -1 unknown */
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
int  emu_cheats_downloading(void);        /* 1 while the cheat file is being fetched */
void emu_cheats_poll(void);              /* load them once downloaded */
const char *emu_cheat_name(int i);
int  emu_cheat_enabled(int i);
void emu_cheat_toggle(int i);
int  emu_fav_is(const char *path);
void emu_fav_toggle(const char *path);
/* collections: named lists of games (by file name) */
int  emu_coll_count(void);
const char *emu_coll_name(int i);
int  emu_coll_has(const char *coll, const char *path);
void emu_coll_toggle(const char *coll, const char *path);
int  emu_disk_count(void);
int  emu_disk_index(void);
int  emu_disk_set(int index);
int  emu_disk_append(const char *path);
void emu_apply_settings(void);
void emu_save_memcards(void);

/* ---- updates from GitHub releases (update.c) ---- */
const char *app_version(void);
int  net_download(const char *url, const char *path);   /* 1 ok, -1 404, 0 failed */
void update_check_background(void);
const char *update_available(void);
int  update_check(char *tag, size_t tn, char *err, size_t en);   /* 1 newer, 0 current, -1 error */
int  update_start(void);
int  update_progress(float *frac);       /* 1 running, 2 installed, -1 failed */

/* ---- BIOS check (bios.c) ---- */
/* 1 known dump (region 1 USA, 2 Europe, 3 Japan, 0 other), 0 unknown, -1 not a BIOS */
int bios_identify(const char *path, char *model, size_t mn, char *version, size_t vn, int *region);

/* ---- memory cards (memcard.c) ---- */
#define MC_SIZE 131072
typedef struct {
	int first, blocks;           /* first block (1..15), blocks used */
	char fname[24];              /* name in the directory, e.g. BASLUS-00594... */
	char title[96];              /* title from the save, as ASCII */
	uint32_t icon[256];          /* 16x16 ARGB, 0 = transparent */
} mc_save_t;
int  mc_load(const char *path, uint8_t *card);
int  mc_save(const char *path, const uint8_t *card);
int  mc_list(const uint8_t *card, mc_save_t *out, int max);
int  mc_free_blocks(const uint8_t *card);
void mc_delete(uint8_t *card, int first);
int  mc_copy(const uint8_t *src, int first, uint8_t *dst);   /* 0, -1 full, -2 exists */
void mc_format(uint8_t *card);
int  emu_memcard_path(char *out, size_t n);   /* 1 while a game is loaded */

/* ---- covers (covers.c) ---- */
int   covers_detect_id(const char *path, char *out);   /* out: "SCUS-94244" */
void  covers_set_loaded_id(const char *path, const char *cdromId);
void  covers_request(const char *id);
int   covers_status(const char *id);   /* 2 have, 1 downloading, 0 none */
void *covers_texture(void *renderer, const char *id, int *w, int *h);
void  covers_shutdown(void);
int   covers_have(const char *id);
int   covers_missing(const char *id);
int   net_status(void);                 /* 1 online, 0 offline, -1 not known yet */
void  covers_flush(void);              /* after changing 2D / 3D */
/* custom covers, by game key (file name without extension / disc tag) */
int   covers_custom_id(const char *gameKey, char *out);   /* 1 if there is one; out = "@..." */
int   covers_set_custom(const char *gameKey, const char *image);
void  covers_remove_custom(const char *gameKey);
int   covers_normalise_id(const char *in, char *out);
void  covers_remember_id(const char *path, const char *id);
const char *emu_game_id(void);

/* ---- menu (menu.c) ---- */
int  menu_run(void);   /* 1 = play, 0 = quit */

#ifdef __cplusplus
}
#endif
#endif
