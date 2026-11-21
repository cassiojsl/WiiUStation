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

/* ---- SD card layout (same folders as WiiStation on the Wii) ---- */
#define WIIU_SD_ROOT     "fs:/vol/external01"
#define WIIU_APP_DIR     WIIU_SD_ROOT "/wiisxrx"
#define WIIU_ISO_DIR     WIIU_APP_DIR "/isos"
#define WIIU_BIOS_DIR    WIIU_APP_DIR "/bios"
#define WIIU_SAVE_DIR    WIIU_APP_DIR "/saves"
#define WIIU_STATE_DIR   WIIU_APP_DIR "/savestates"
#define WIIU_CONFIG_FILE WIIU_APP_DIR "/wiiustation.cfg"

int wiiu_mkdirs(const char *path);

/* ---- settings ---- */
enum { WIIU_ASPECT_4_3 = 0, WIIU_ASPECT_STRETCH, WIIU_ASPECT_INTEGER };
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
	int analog;         /* pad type: 1 = DualShock */
	int rumble;
	int audio;
} wiiu_settings;

extern wiiu_settings cfg;
void settings_load(void);
int  settings_save(void);

/* ---- video (video.c) ---- */
int  video_init(void);
void video_shutdown(void);
void *video_renderer(void);
void video_set_pixel_format(int fmt);  /* RETRO_PIXEL_FORMAT_* */
void video_upload(const void *data, unsigned w, unsigned h, size_t pitch);
void video_draw_game(int dim);
void video_present(void);
void video_osd(const char *msg);
void video_set_fps_text(const char *txt);
void *video_open_font(int size);       /* TTF_Font* (system font) */

/* events: 0 when the app must quit (HOME Menu -> Close) */
int  pump_events(void);
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
void emu_unload_game(void);
int  emu_game_loaded(void);
const char *emu_game_name(void);
void emu_reset(void);
int  emu_state_save(int slot);
int  emu_state_load(int slot);
int  emu_disk_count(void);
int  emu_disk_index(void);
int  emu_disk_set(int index);
int  emu_disk_append(const char *path);
void emu_apply_settings(void);
void emu_save_memcards(void);

/* ---- menu (menu.c) ---- */
int  menu_run(void);   /* 1 = play, 0 = quit */

#ifdef __cplusplus
}
#endif
#endif
