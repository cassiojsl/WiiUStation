/* Host (PC) harness for WiiUStation's menu.c: renders screens offscreen and
 * saves screenshots. Inputs are scripted. */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>
#define WIIU_SD_ROOT SDROOT
#include "platform.h"

static SDL_Renderer *ren;
static int frame, shotAt = 30, quitAt = 32;
static const char *shotName = "shot.png";
static uint32_t scriptBtn[400];
wiiu_settings cfg;
volatile int wiiu_quit;

SDL_Surface *host_IMG_Load(const char *p)
{
	char buf[512];
	if (!strncmp(p, "fs:/vol/content/", 16)) { snprintf(buf, sizeof(buf), "%s/%s", CONTENT, p + 16); p = buf; }
	return IMG_Load(p);
}
static void host_present(SDL_Renderer *r)
{
	frame++;
	if (getenv("SLOW")) SDL_Delay(16);
	if (frame == shotAt) {
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
		SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
		IMG_SavePNG(s, shotName);
		SDL_FreeSurface(s);
	}
	if (frame >= quitAt) wiiu_quit = 1;
	SDL_RenderPresent(r);
}
#define IMG_Load host_IMG_Load
#define SDL_RenderPresent host_present

/* platform stubs */
void *video_renderer(void) { return ren; }
void *video_open_font(int size) { return getenv("CJK") ? TTF_OpenFont("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", size * 0.82) : TTF_OpenFont("/usr/share/fonts/opentype/inter/Inter-SemiBold.otf", size * 0.82); }
void video_draw_game(int dim) { (void)dim; SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); }
void video_fade_in(void) {}
void video_set_game_id(const char *id) { (void)id; }
void emu_unload_game(void) {}
int pump_events(void) { return 1; }
static wiiu_pad_state pad;
void wiiu_input_update(void) {
	uint32_t b = frame < 400 ? scriptBtn[frame] : 0;
	pad.connected = 1; pad.held = b; pad.pressed = b;
}
const wiiu_pad_state *wiiu_input_get(int i) { (void)i; return &pad; }
uint32_t wiiu_input_menu_held(void) { return pad.held; }
uint32_t wiiu_input_menu_pressed(void) { return pad.pressed; }
int jit_available(void) { return 1; }
const int default_map[16] = { 1, 3, 13, 12, 4, 5, 6, 7, 0, 2, 8, 9, 10, 11, 14, 15 };
int settings_save(void) { return 1; }
void emu_apply_settings(void) {}
static int loaded;
int emu_game_loaded(void) { return loaded; }
const char *emu_game_name(void) { return "Crash Bandicoot 3 (SCUS-94244).chd"; }
const char *emu_game_id(void) { return "SCUS-94244"; }
const char *emu_game_path(void) { return "x"; }
static const char *rec[] = { SDROOT "/wiiu/apps/WiiUStation/games/Crash Bandicoot 3 (SCUS-94244).chd", SDROOT "/wiiu/apps/WiiUStation/games/Spyro the Dragon (SCUS-94228).chd", SDROOT "/wiiu/apps/WiiUStation/games/Final Fantasy VII (SCUS-94163).chd", SDROOT "/wiiu/apps/WiiUStation/games/Metal Gear Solid (SLUS-00594).chd", SDROOT "/wiiu/apps/WiiUStation/games/Tekken 3 (SCUS-94327).chd", SDROOT "/wiiu/apps/WiiUStation/games/Silent Hill (SLUS-00707).chd" };
int emu_recent_count(void) { return getenv("NOREC") ? 0 : 6; }
const char *emu_recent(int i) { return rec[i]; }
int emu_load_game(const char *p) { (void)p; return -1; }
int emu_load_game_disc(const char *m, int i, const char *d) { (void)m; (void)i; (void)d; return -1; }
void emu_reset(void) {}
int emu_state_save(int s) { (void)s; return 1; }
int emu_state_load(int s) { (void)s; return 0; }
int emu_disk_count(void) { return 1; }
int emu_state_exists(int s) { return s == 0 || s == 3 || s == -1; }
const char *bezel_name(int b) { (void)b; return "Tube TV"; }
int bezel_next(int b, int d) { return (b + d + BEZEL_COUNT) % BEZEL_COUNT; }
static const char *hoL[] = { "PS1 CPU clock", "Fast CD loading", "Skip CPU stalls", "Sound interpolation", "CD audio tracks", "Show overscan" };
static const char *hoS[] = { "Speed", NULL, NULL, "Sound", NULL, "Picture" };
static const char *hoV[] = { "Auto", "Off", "Off", "Simple", "On", "Off" };
int core_opt_count(void) { return 6; }
const char *core_opt_label(int i) { return hoL[i]; }
const char *core_opt_section(int i) { return hoS[i]; }
const char *core_opt_value(int i) { return hoV[i]; }
const char *core_opt_help(int i) { (void)i; return "Higher removes slowdowns the real console had. Auto is safest."; }
void core_opt_cycle(int i, int d) { (void)i; (void)d; }
int video_screen_modes_ok(void) { return 1; }
const char *pad_type_name(int t) { (void)t; return "Digital"; }
const char *hotkey_name(int i) { (void)i; return "Save state"; }
const char *button_name(int b) { (void)b; return "R"; }
const int default_hotkeys[HK_COUNT];

void video_lang_changed(void) {}
int covers_custom_id(const char *k, char *o) { (void)k; o[0] = 0; return 0; }
int covers_set_custom(const char *k, const char *i) { (void)k; (void)i; return 1; }
void covers_remove_custom(const char *k) { (void)k; }
int covers_normalise_id(const char *i, char *o) { snprintf(o, 16, "%s", i); return 1; }
void covers_remember_id(const char *p, const char *i) { (void)p; (void)i; }
long emu_playtime(const char *k, long long *l) { if (l) *l = 0; return strlen(k) * 977; }
void emu_playtime_reset(const char *k) { (void)k; }
int wiiu_battery(void) { return 4; }
void video_set_vsync(int on) { (void)on; }
void *video_preview(void) { static SDL_Texture *t; if (!t) { SDL_Surface *s = IMG_Load(SDROOT "/wiiu/apps/WiiUStation/screenshots/Game 3-20261123-120000.png"); if (s) { t = SDL_CreateTextureFromSurface(ren, s); SDL_FreeSurface(s); } } return t; }
void emu_cheats_poll(void) {}
int emu_cheats_downloading(void) { return 0; }
int covers_have(const char *i) { return i[3] == 'S'; }
int covers_missing(const char *i) { return i[5] == 9 && i[6] == 4 && i[7] == 3; }
int emu_coll_count(void) { return 2; }
const char *emu_coll_name(int i) { return i ? "Para jogar" : "RPGs"; }
int emu_coll_has(const char *c, const char *p) { return strstr(p, "Metal") && !strcmp(c, "RPGs"); }
void emu_coll_toggle(const char *c, const char *p) { (void)c; (void)p; }
int net_status(void) { return getenv("NET") ? atoi(getenv("NET")) : 1; }
void sfx_play(int id) { (void)id; }
void sfx_prepare(void) {}
void update_check_background(void) {}
const char *update_available(void) { return getenv("UPD"); }
const char *app_version(void) { return "v1.0.0"; }
int update_check(char *t, size_t tn, char *e, size_t en) { snprintf(t, tn, "v1.1.0"); (void)e; (void)en; return 1; }
int update_start(void) { return 1; }
int update_progress(float *f) { *f = 0.4f; return 1; }
int emu_memcard_path(char *o, size_t n) { (void)o; (void)n; return 0; }
int wiiu_mkdirs(const char *p) { (void)p; return 0; }
void covers_request(const char *i) { (void)i; }
long long emu_state_time(int s) { return emu_state_exists(s) ? 1795000000LL + s * 86400LL : 0; }
void emu_state_thumb(int s, char *o, size_t n) { snprintf(o, n, "%s/states/thumb%d.png", WIIU_APP_DIR, s); }
int emu_screenshot(void) { return 1; }
int emu_player_count(void) { return 2; }
static int chon[3];
int emu_cheat_count(void) { return 3; }
const char *emu_cheat_name(int i) { static const char *n[] = { "Infinite lives", "All gems", "Max time trial" }; return n[i]; }
int emu_cheat_enabled(int i) { return chon[i]; }
void emu_cheat_toggle(int i) { chon[i] = !chon[i]; }
int emu_fav_is(const char *p) { return strstr(p, "Crash") || strstr(p, "Tekken"); }
void emu_fav_toggle(const char *p) { (void)p; }
int settings_game_scope(void) { return 0; }
int settings_set_game_scope(int on) { (void)on; return 1; }
void video_text_begin(void) {}
int video_text_poll(char *b, size_t n) { (void)b; (void)n; return 0; }
int video_text_active(void) { return 1; }
void video_text_end(void) {}
int emu_disk_index(void) { return 0; }
int emu_disk_set(int i) { (void)i; return 1; }
int emu_disk_append(const char *p) { (void)p; return 1; }
int covers_detect_id(const char *path, char *out) {
	const char *o = strchr(path, '('); if (!o || strlen(o) < 11) { out[0] = 0; return 0; }
	memcpy(out, o + 1, 10); out[10] = 0; return 1;
}
int covers_status(const char *id) { (void)id; return 0; }
void *covers_texture(void *r, const char *id, int *w, int *h) {
	static char last[64][16]; static SDL_Texture *tex[64];
	for (int i = 0; i < 64; i++) if (tex[i] && !strcmp(last[i], id)) { SDL_QueryTexture(tex[i], 0, 0, w, h); return tex[i]; }
	char p[512]; if (cfg.cover3d) snprintf(p, sizeof(p), "%s/covers/3d/%s.png", WIIU_APP_DIR, id); else snprintf(p, sizeof(p), "%s/covers/%s.jpg", WIIU_APP_DIR, id);
	SDL_Surface *s = IMG_Load(p); if (!s) return NULL; SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
	for (int i = 0; i < 64; i++) if (!tex[i]) { tex[i] = SDL_CreateTextureFromSurface(r, s); strcpy(last[i], id); *w = s->w; *h = s->h; SDL_FreeSurface(s); return tex[i]; }
	SDL_FreeSurface(s); return NULL;
}

void covers_flush(void) {}
#include "lang.c"
#include "memcard.c"
#include "bios.c"
#include "menu.c"

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "main";
	if (argc > 2) shotName = argv[2];
	SDL_Init(SDL_INIT_VIDEO); TTF_Init(); IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
	SDL_Window *w = SDL_CreateWindow("m", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
	ren = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
	if (getenv("PT")) cfg.lang = atoi(getenv("PT"));
	if (getenv("THEME")) cfg.theme = atoi(getenv("THEME"));
	strcpy(cfg.games_dir, WIIU_ISO_DIR); cfg.dynarec = 1; cfg.gpu_thread = 1; cfg.bezel = 1; memcpy(cfg.map, default_map, sizeof(cfg.map));
	if (getenv("GDIR")) strcpy(cfg.games_dir, getenv("GDIR"));
	if (strcmp(mode, "intro")) introShown = 1;
	if (getenv("SHOT")) shotAt = atoi(getenv("SHOT")), quitAt = shotAt + 2;
	if (!strcmp(mode, "intro")) { R = ren; menu_fonts(); intro(); }
	else if (!strcmp(mode, "main")) { menu_run(); }
	else if (!strcmp(mode, "ingame")) { loaded = 1; menu_run(); }
	else if (!strcmp(mode, "grid")) { char o[512]; cfg.view = 0; R = ren; menu_fonts(); scriptBtn[10] = WIIU_BTN_RIGHT; scriptBtn[14] = WIIU_BTN_DOWN; browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "compact")) { char o[512]; cfg.view = 2; R = ren; menu_fonts(); scriptBtn[10] = WIIU_BTN_RIGHT; browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "shelf")) { char o[512]; cfg.view = 0; cfg.cover3d = 0; R = ren; menu_fonts(); scriptBtn[10] = WIIU_BTN_RIGHT; browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "carousel")) { char o[512]; cfg.view = 4; cfg.cover3d = 1; R = ren; menu_fonts(); scriptBtn[10] = WIIU_BTN_RIGHT; scriptBtn[20] = WIIU_BTN_RIGHT; shotAt = 90; quitAt = 92; browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "pickdir")) { char o[512]; R = ren; menu_fonts(); browse("Games folder", o, sizeof(o), 1); }
	else if (!strcmp(mode, "list")) { char o[512]; cfg.view = 1; R = ren; menu_fonts(); browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "list2")) { char o[512]; cfg.view = 1; R = ren; menu_fonts(); browse("Load game", o, sizeof(o), 0); }
	else if (!strcmp(mode, "disc")) { R = ren; menu_fonts(); choose_disc("Final Fantasy VII", 3, 0); }
	else if (!strcmp(mode, "slots")) { loaded = 1; R = ren; menu_fonts(); slot_picker(0); }
	else if (!strcmp(mode, "cheats")) { loaded = 1; R = ren; menu_fonts(); scriptBtn[8] = WIIU_BTN_A; cheats_screen(); }
	else if (!strcmp(mode, "resume")) { loaded = 1; R = ren; menu_fonts(); ask_resume(); }
	else if (!strcmp(mode, "gamesettings")) { loaded = 1; R = ren; menu_fonts(); settings(); }
	else if (!strcmp(mode, "controls")) { R = ren; menu_fonts(); controls(); }
	else if (!strcmp(mode, "advanced")) { R = ren; menu_fonts(); scriptBtn[8] = WIIU_BTN_DOWN; scriptBtn[12] = WIIU_BTN_DOWN; scriptBtn[16] = WIIU_BTN_DOWN; scriptBtn[20] = WIIU_BTN_DOWN; advanced(); }
	else if (!strcmp(mode, "cards")) { R = ren; menu_fonts(); scriptBtn[8] = WIIU_BTN_DOWN; scriptBtn[12] = WIIU_BTN_A; memcards(); }
	else if (!strcmp(mode, "update")) { R = ren; menu_fonts(); update_screen(); }
	else if (!strcmp(mode, "info")) { R = ren; menu_fonts(); entry_t e; memset(&e, 0, sizeof(e)); strcpy(e.name, "Metal Gear Solid (SLUS-00594).chd"); game_info(&e, WIIU_ISO_DIR); }
	else if (!strcmp(mode, "colls")) { R = ren; menu_fonts(); entry_t e; memset(&e, 0, sizeof(e)); strcpy(e.name, "Metal Gear Solid (SLUS-00594).chd"); game_collections(&e, WIIU_ISO_DIR); }
	else if (!strcmp(mode, "allcovers")) { R = ren; menu_fonts(); strcpy(cfg.games_dir, WIIU_ISO_DIR); all_covers(); }
	else if (!strcmp(mode, "bios")) { R = ren; menu_fonts(); bios_screen(); }
	else if (!strcmp(mode, "resume")) { loaded = 1; R = ren; menu_fonts(); ask_resume(); }
	else if (!strcmp(mode, "gsettings")) { loaded = 1; R = ren; menu_fonts(); for (int i = 0; i < 9; i++) scriptBtn[8 + i * 2] = WIIU_BTN_DOWN; if (getenv("PICA")) scriptBtn[28] = WIIU_BTN_A; if (getenv("PICB")) scriptBtn[34] = WIIU_BTN_B; settings(); }
	else if (!strcmp(mode, "gallery")) { R = ren; menu_fonts(); scriptBtn[8] = WIIU_BTN_RIGHT; gallery(); }
	else if (!strcmp(mode, "options")) { R = ren; menu_fonts(); entry_t e; memset(&e, 0, sizeof(e)); strcpy(e.name, "Metal Gear Solid (SLUS-00594).chd"); strcpy(e.id, "SLUS-00594"); game_options(&e, WIIU_ISO_DIR); }
	else if (!strcmp(mode, "settings")) { R = ren; menu_fonts(); settings(); }
	else if (!strcmp(mode, "credits")) { R = ren; menu_fonts(); shotAt = 200; quitAt = 202; credits(); }
	else if (!strcmp(mode, "loading")) { R = ren; menu_fonts(); loading_screen(WIIU_ISO_DIR "/Crash Bandicoot 3 (SCUS-94244).chd");
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
		SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch); IMG_SavePNG(s, shotName); }
	return 0;
}
