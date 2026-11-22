/*
 * WiiUStation - USB drive (FAT32) support.
 *
 * The Wii U only mounts its own format (WFS) for USB drives, so a FAT32 drive shows up as a "raw" storage.  The
 * Aroma environment gives homebrew raw access to it (libmocha), and libfat turns the sectors into files:
 * the drive becomes the device "usb:", used with the normal file functions (fopen, opendir...).  Read only
 * in practice: the app never writes to it.
 */
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <mocha/mocha.h>
#include <mocha/disc_interface.h>
#include "libfat/fat.h"
#include "platform.h"

static int mochaOk, mounted, noneLogged;

int usbfs_mount(void)
{
	if (mounted) return 1;
	if (!mochaOk) {
		MochaUtilsStatus st = Mocha_InitLibrary();
		if (st != MOCHA_RESULT_SUCCESS) { if (!noneLogged) { noneLogged = 1; wlog("usb: libmocha not available (%s)", Mocha_GetStatusStr(st)); } return 0; }
		mochaOk = 1;
	}
	const DISC_INTERFACE *d = &Mocha_usb_disc_interface;
	if (!d->startup() || !d->isInserted()) { if (!noneLogged) { noneLogged = 1; wlog("usb: no USB drive found"); } d->shutdown(); return 0; }
	if (!fatMount("usb", d, 0, 32, 64)) { wlog("usb: the drive is not FAT32 (or has no FAT partition)"); d->shutdown(); return 0; }
	mounted = 1;
	char label[16] = "";
	fatGetVolumeLabel("usb", label);
	int n = 0;
	DIR *dir = opendir("usb:/");
	if (dir) { while (readdir(dir)) n++; closedir(dir); }
	wlog("usb: drive mounted as usb:/ (volume '%s', %d entries in the root)", label, n);
	return 1;
}

int usbfs_mounted(void) { return mounted; }

void usbfs_unmount(void)
{
	if (mounted) {
		fatUnmount("usb");
		Mocha_usb_disc_interface.shutdown();
		mounted = 0;
	}
	if (mochaOk) { Mocha_DeInitLibrary(); mochaOk = 0; }
}
