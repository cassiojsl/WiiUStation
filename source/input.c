/*
 * WiiUStation Wii U - input.
 *
 *  - Wii U GamePad (VPAD): buttons, both sticks, L3/R3, touch screen,
 *    rumble.  Touching the screen opens the emulator menu.
 *  - Wii U Pro Controller and Classic Controller (KPAD, up to 4).
 *
 * The libretro input callbacks in main.c read this state.
 */
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <string.h>
#include <math.h>

#include "platform.h"

/* ---------------------------------------------------------------------- */
/* Raw device layer                                                        */
/* ---------------------------------------------------------------------- */

static wiiu_pad_state pads[WIIU_MAX_PADS];
static OSTime lastUpdate;
static int rumbleState[WIIU_MAX_PADS];
static uint32_t hotkeyLatch;

static uint32_t map_vpad(uint32_t b)
{
	uint32_t o = 0;
	if (b & VPAD_BUTTON_A)       o |= WIIU_BTN_A;
	if (b & VPAD_BUTTON_B)       o |= WIIU_BTN_B;
	if (b & VPAD_BUTTON_X)       o |= WIIU_BTN_X;
	if (b & VPAD_BUTTON_Y)       o |= WIIU_BTN_Y;
	if (b & VPAD_BUTTON_UP)      o |= WIIU_BTN_UP;
	if (b & VPAD_BUTTON_DOWN)    o |= WIIU_BTN_DOWN;
	if (b & VPAD_BUTTON_LEFT)    o |= WIIU_BTN_LEFT;
	if (b & VPAD_BUTTON_RIGHT)   o |= WIIU_BTN_RIGHT;
	if (b & VPAD_BUTTON_L)       o |= WIIU_BTN_L;
	if (b & VPAD_BUTTON_R)       o |= WIIU_BTN_R;
	if (b & VPAD_BUTTON_ZL)      o |= WIIU_BTN_ZL;
	if (b & VPAD_BUTTON_ZR)      o |= WIIU_BTN_ZR;
	if (b & VPAD_BUTTON_PLUS)    o |= WIIU_BTN_PLUS;
	if (b & VPAD_BUTTON_MINUS)   o |= WIIU_BTN_MINUS;
	if (b & VPAD_BUTTON_STICK_L) o |= WIIU_BTN_STICKL;
	if (b & VPAD_BUTTON_STICK_R) o |= WIIU_BTN_STICKR;
	if (b & VPAD_BUTTON_HOME)    o |= WIIU_BTN_HOME;
	return o;
}

static uint32_t map_pro(uint32_t b)
{
	uint32_t o = 0;
	if (b & WPAD_PRO_BUTTON_A)       o |= WIIU_BTN_A;
	if (b & WPAD_PRO_BUTTON_B)       o |= WIIU_BTN_B;
	if (b & WPAD_PRO_BUTTON_X)       o |= WIIU_BTN_X;
	if (b & WPAD_PRO_BUTTON_Y)       o |= WIIU_BTN_Y;
	if (b & WPAD_PRO_BUTTON_UP)      o |= WIIU_BTN_UP;
	if (b & WPAD_PRO_BUTTON_DOWN)    o |= WIIU_BTN_DOWN;
	if (b & WPAD_PRO_BUTTON_LEFT)    o |= WIIU_BTN_LEFT;
	if (b & WPAD_PRO_BUTTON_RIGHT)   o |= WIIU_BTN_RIGHT;
	if (b & WPAD_PRO_TRIGGER_L)      o |= WIIU_BTN_L;
	if (b & WPAD_PRO_TRIGGER_R)      o |= WIIU_BTN_R;
	if (b & WPAD_PRO_TRIGGER_ZL)     o |= WIIU_BTN_ZL;
	if (b & WPAD_PRO_TRIGGER_ZR)     o |= WIIU_BTN_ZR;
	if (b & WPAD_PRO_BUTTON_PLUS)    o |= WIIU_BTN_PLUS;
	if (b & WPAD_PRO_BUTTON_MINUS)   o |= WIIU_BTN_MINUS;
	if (b & WPAD_PRO_BUTTON_STICK_L) o |= WIIU_BTN_STICKL;
	if (b & WPAD_PRO_BUTTON_STICK_R) o |= WIIU_BTN_STICKR;
	if (b & WPAD_PRO_BUTTON_HOME)    o |= WIIU_BTN_HOME;
	return o;
}

static uint32_t map_classic(uint32_t b)
{
	uint32_t o = 0;
	if (b & WPAD_CLASSIC_BUTTON_A)     o |= WIIU_BTN_A;
	if (b & WPAD_CLASSIC_BUTTON_B)     o |= WIIU_BTN_B;
	if (b & WPAD_CLASSIC_BUTTON_X)     o |= WIIU_BTN_X;
	if (b & WPAD_CLASSIC_BUTTON_Y)     o |= WIIU_BTN_Y;
	if (b & WPAD_CLASSIC_BUTTON_UP)    o |= WIIU_BTN_UP;
	if (b & WPAD_CLASSIC_BUTTON_DOWN)  o |= WIIU_BTN_DOWN;
	if (b & WPAD_CLASSIC_BUTTON_LEFT)  o |= WIIU_BTN_LEFT;
	if (b & WPAD_CLASSIC_BUTTON_RIGHT) o |= WIIU_BTN_RIGHT;
	if (b & WPAD_CLASSIC_BUTTON_L)     o |= WIIU_BTN_L;
	if (b & WPAD_CLASSIC_BUTTON_R)     o |= WIIU_BTN_R;
	if (b & WPAD_CLASSIC_BUTTON_ZL)    o |= WIIU_BTN_ZL;
	if (b & WPAD_CLASSIC_BUTTON_ZR)    o |= WIIU_BTN_ZR;
	if (b & WPAD_CLASSIC_BUTTON_PLUS)  o |= WIIU_BTN_PLUS;
	if (b & WPAD_CLASSIC_BUTTON_MINUS) o |= WIIU_BTN_MINUS;
	if (b & WPAD_CLASSIC_BUTTON_HOME)  o |= WIIU_BTN_HOME;
	return o;
}

static float clampf(float v)
{
	return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

void wiiu_input_init(void)
{
	KPADInit();
	WPADEnableURCC(1);       /* Pro Controller */
	WPADEnableWiiRemote(1);  /* Wii Remote + Classic Controller */
	memset(pads, 0, sizeof(pads));
}

static int pointerMenu = 1;

/* the pointer is for the menu: in a game it must not click, and the
 * GamePad's sensor bar is switched off */
void wiiu_input_pointer_menu(int on)
{
	pointerMenu = on;
	if (!on) wiiu_input_sensor_bar(0);
	else {
		int remote = 0;
		for (int i = 1; i < WIIU_MAX_PADS; i++)
			if (pads[i].connected && pads[i].kind >= PADKIND_REMOTE) remote = 1;
		/* the GamePad's bar only while a Wii Remote is there to use it */
		wiiu_input_sensor_bar(cfg.pointer == 1 && !remote ? 0 : cfg.pointer);
	}
}

/* where the Wii Remote's pointer finds its sensor bar: 0 none (the GamePad's
 * bar is switched off to save its battery), 1 the GamePad's, 2 the TV's */
void wiiu_input_sensor_bar(int mode)
{
	static int cur = -1;
	if (mode == cur) return;
	cur = mode;
	VPADSetSensorBar(VPAD_CHAN_0, mode == 1);
	if (mode == 2) WPADSetSensorBarPosition(WPAD_SENSOR_BAR_POS_BELOW);
}

void wiiu_input_shutdown(void)
{
	wiiu_input_sensor_bar(0);
	for (int i = 0; i < WIIU_MAX_PADS; i++) wiiu_input_rumble(i, 0);
	KPADShutdown();
}

static void update_now(void)
{
	VPADStatus vs;
	VPADReadError verr;
	uint32_t prev;

	/* --- GamePad --- */
	prev = pads[0].held;
	if (VPADRead(VPAD_CHAN_0, &vs, 1, &verr) > 0 && verr == VPAD_READ_SUCCESS) {
		pads[0].connected = 1;
		pads[0].kind = PADKIND_GAMEPAD;
		pads[0].held = map_vpad(vs.hold);
		pads[0].lx = clampf(vs.leftStick.x);
		pads[0].ly = clampf(vs.leftStick.y);
		pads[0].rx = clampf(vs.rightStick.x);
		pads[0].ry = clampf(vs.rightStick.y);
		pads[0].gyro_x = vs.gyro.x;
		pads[0].gyro_y = vs.gyro.y;
		pads[0].battery = vs.battery;
		VPADTouchData tp;
		VPADGetTPCalibratedPoint(VPAD_CHAN_0, &tp, &vs.tpNormal);
		if (tp.touched && tp.validity == VPAD_VALID) {
			pads[0].held |= WIIU_BTN_TOUCH;
			pads[0].touch_x = tp.x;
			pads[0].touch_y = tp.y;
		}
	} else if (verr == VPAD_READ_INVALID_CONTROLLER) {
		pads[0].connected = 0;
		pads[0].held = 0;
	}
	/* VPAD_READ_NO_SAMPLES keeps the previous state */
	pads[0].pressed = pads[0].held & ~prev;

	/* --- Pro / Classic controllers --- */
	for (int ch = 0; ch < 4; ch++) {
		wiiu_pad_state *p = &pads[1 + ch];
		KPADStatus ks;
		KPADError kerr;
		uint32_t ext;
		prev = p->held;
		memset(&ks, 0, sizeof(ks));
		if (WPADProbe((WPADChan)ch, &ext) != 0) {
			p->connected = 0;
			p->held = 0;
			p->pressed = 0;
			continue;
		}
		if (KPADReadEx((KPADChan)ch, &ks, 1, &kerr) > 0 && kerr == KPAD_ERROR_OK) {
			if (ks.extensionType == WPAD_EXT_PRO_CONTROLLER) {
				p->connected = 1;
				p->kind = PADKIND_PRO;
				p->ptr_valid = 0;
				p->held = map_pro(ks.pro.hold);
				p->lx = clampf(ks.pro.leftStick.x);
				p->ly = clampf(ks.pro.leftStick.y);
				p->rx = clampf(ks.pro.rightStick.x);
				p->ry = clampf(ks.pro.rightStick.y);
			} else if (ks.extensionType == WPAD_EXT_CLASSIC ||
			           ks.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
				p->connected = 1;
				p->kind = PADKIND_CLASSIC;
				p->ptr_valid = 0;
				p->held = map_classic(ks.classic.hold);
				p->lx = clampf(ks.classic.leftStick.x);
				p->ly = clampf(ks.classic.leftStick.y);
				p->rx = clampf(ks.classic.rightStick.x);
				p->ry = clampf(ks.classic.rightStick.y);
			} else {
				/* Wii Remote: held sideways (a digital pad), or upright with a
				 * Nunchuk (stick + C and Z). */
				uint32_t b = ks.hold, o = 0, raw = 0;
				int nun = ks.extensionType == WPAD_EXT_NUNCHUK || ks.extensionType == WPAD_EXT_MPLUS_NUNCHUK;
				p->connected = 1;
				p->kind = nun ? PADKIND_NUNCHUK : PADKIND_REMOTE;
				if (b & WPAD_BUTTON_1)     raw |= 1u << WR_1;
				if (b & WPAD_BUTTON_2)     raw |= 1u << WR_2;
				if (b & WPAD_BUTTON_A)     raw |= 1u << WR_A;
				if (b & WPAD_BUTTON_B)     raw |= 1u << WR_B;
				if (b & WPAD_BUTTON_PLUS)  raw |= 1u << WR_PLUS;
				if (b & WPAD_BUTTON_MINUS) raw |= 1u << WR_MINUS;
				p->lx = p->ly = p->rx = p->ry = 0;
				if (nun) {
					if (b & WPAD_BUTTON_UP)    raw |= 1u << WR_UP;
					if (b & WPAD_BUTTON_DOWN)  raw |= 1u << WR_DOWN;
					if (b & WPAD_BUTTON_LEFT)  raw |= 1u << WR_LEFT;
					if (b & WPAD_BUTTON_RIGHT) raw |= 1u << WR_RIGHT;
					if (ks.nunchuk.hold & WPAD_NUNCHUK_BUTTON_C) raw |= 1u << WR_C;
					if (ks.nunchuk.hold & WPAD_NUNCHUK_BUTTON_Z) raw |= 1u << WR_Z;
					p->lx = clampf(ks.nunchuk.stick.x);
					p->ly = clampf(ks.nunchuk.stick.y);
					if (p->ly > 0.5f)  raw |= 1u << WR_SUP;
					if (p->ly < -0.5f) raw |= 1u << WR_SDOWN;
					if (p->lx < -0.5f) raw |= 1u << WR_SLEFT;
					if (p->lx > 0.5f)  raw |= 1u << WR_SRIGHT;
				} else {
					/* sideways: the physical d-pad is turned a quarter */
					if (b & WPAD_BUTTON_RIGHT) raw |= 1u << WR_UP;
					if (b & WPAD_BUTTON_LEFT)  raw |= 1u << WR_DOWN;
					if (b & WPAD_BUTTON_UP)    raw |= 1u << WR_LEFT;
					if (b & WPAD_BUTTON_DOWN)  raw |= 1u << WR_RIGHT;
				}
				p->wraw = raw;
				/* the menu: a fixed layout */
				if (raw & (1u << WR_UP))    o |= WIIU_BTN_UP;
				if (raw & (1u << WR_DOWN))  o |= WIIU_BTN_DOWN;
				if (raw & (1u << WR_LEFT))  o |= WIIU_BTN_LEFT;
				if (raw & (1u << WR_RIGHT)) o |= WIIU_BTN_RIGHT;
				if (nun) {
					if (raw & (1u << WR_A))  o |= WIIU_BTN_A;
					if (raw & (1u << WR_B))  o |= WIIU_BTN_B;
					if (raw & (1u << WR_1))  o |= WIIU_BTN_X;
					if (raw & (1u << WR_2))  o |= WIIU_BTN_Y;
				} else {
					if (raw & (1u << WR_2))  o |= WIIU_BTN_A;
					if (raw & (1u << WR_1))  o |= WIIU_BTN_B;
					if (raw & (1u << WR_B))  o |= WIIU_BTN_Y;
					if (raw & (1u << WR_A))  o |= WIIU_BTN_X;
				}
				if (b & WPAD_BUTTON_PLUS)  o |= WIIU_BTN_PLUS;
				if (b & WPAD_BUTTON_MINUS) o |= WIIU_BTN_MINUS;
				if (b & WPAD_BUTTON_HOME)  o |= WIIU_BTN_HOME;
				/* the pointer (when the sensor bar sees it): A clicks instead */
				p->ptr_valid = pointerMenu && ks.posValid > 0 && cfg.pointer;
				if (p->ptr_valid) {
					float px = (ks.pos.x + 1.0f) * 0.5f * 1280.0f;
					float py = (ks.pos.y + 1.0f) * 0.5f * 720.0f;
					p->ptr_x = px < 0 ? 0 : px > 1279 ? 1279 : px;
					p->ptr_y = py < 0 ? 0 : py > 719 ? 719 : py;
					if (b & WPAD_BUTTON_A) { o |= WIIU_BTN_TOUCH; o &= ~(WIIU_BTN_A | WIIU_BTN_X); }
					p->touch_x = (int)p->ptr_x;
					p->touch_y = (int)p->ptr_y;
				}
				p->held = o;
			}
		}
		p->pressed = p->held & ~prev;
	}
}

void wiiu_input_update(void)
{
	/* Several callers (two PSX ports, menu) may ask for an update in the
	 * same frame: sample at most every ~4 ms. */
	OSTime now = OSGetSystemTime();
	if (now - lastUpdate < OSMillisecondsToTicks(4)) return;
	lastUpdate = now;
	update_now();
}

const wiiu_pad_state *wiiu_input_get(int idx)
{
	if (idx < 0 || idx >= WIIU_MAX_PADS) return &pads[0];
	return &pads[idx];
}

uint32_t wiiu_input_menu_pressed(void)
{
	uint32_t r = 0;
	for (int i = 0; i < WIIU_MAX_PADS; i++)
		if (pads[i].connected) r |= pads[i].pressed;
	return r;
}

uint32_t wiiu_input_menu_held(void)
{
	uint32_t r = 0;
	for (int i = 0; i < WIIU_MAX_PADS; i++)
		if (pads[i].connected) {
			r |= pads[i].held;
			/* sticks act as a d-pad in the menu */
			if (pads[i].ly > 0.6f)  r |= WIIU_BTN_UP;
			if (pads[i].ly < -0.6f) r |= WIIU_BTN_DOWN;
			if (pads[i].lx < -0.6f) r |= WIIU_BTN_LEFT;
			if (pads[i].lx > 0.6f)  r |= WIIU_BTN_RIGHT;
		}
	return r;
}

void wiiu_input_rumble(int idx, int on)
{
	if (idx < 0 || idx >= WIIU_MAX_PADS) return;
	if (rumbleState[idx] == on) return;
	rumbleState[idx] = on;
	if (idx == 0) {
		static uint8_t pattern[15] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
		if (on) VPADControlMotor(VPAD_CHAN_0, pattern, 120);
		else    VPADStopMotor(VPAD_CHAN_0);
	} else {
		WPADControlMotor((WPADChan)(idx - 1), on ? 1 : 0);
	}
}

int wiiu_battery(void)
{
	return pads[0].connected ? pads[0].battery : -1;
}

void wiiu_home_button_ingame(int ingame)
{
	if (!ingame) {
		/* Wait until HOME is released, otherwise the same press would also
		 * open the system HOME Menu as soon as we re-enable it. */
		for (int i = 0; i < 200; i++) {
			update_now();
			int held = 0;
			for (int p = 0; p < WIIU_MAX_PADS; p++)
				if (pads[p].held & WIIU_BTN_HOME) held = 1;
			if (!held) break;
			OSSleepTicks(OSMillisecondsToTicks(5));
		}
	}
	OSEnableHomeButtonMenu(ingame ? 0 : 1);
}

void wiiu_input_reset_hotkey(void)
{
	/* Swallow the buttons that triggered the menu so they don't leak into
	 * the game or the menu. */
	hotkeyLatch = 1;
	for (int i = 0; i < WIIU_MAX_PADS; i++) pads[i].pressed = 0;
}

