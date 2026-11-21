/*
 * WiiUStation - PlayStation game ID detection (pure C, testable on a PC).
 * See covers.c for how the IDs are used.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/stat.h>
#include <libchdr/chd.h>

#include "platform.h"

#define COVER_DIR   WIIU_COVER_DIR
#define ID_FILE     COVER_DIR "/ids.txt"

/* ------------------------------------------------------------------------ */
/* ID helpers                                                               */
/* ------------------------------------------------------------------------ */
/* Normalises things like "SCUS_942.44", "SCUS94244", "scus-94244" to
 * "SCUS-94244". Returns 1 on success. */
static int normalise_id(const char *in, char *out)
{
	char letters[5] = { 0 }, digits[6] = { 0 };
	int nl = 0, nd = 0;
	for (const char *p = in; *p && nd < 5; p++) {
		if (nl < 4) {
			if (isalpha((unsigned char)*p)) letters[nl++] = toupper((unsigned char)*p);
			else if (nl) { nl = 0; }
		} else if (isdigit((unsigned char)*p)) {
			digits[nd++] = *p;
		} else if (*p != '_' && *p != '-' && *p != '.' && *p != ' ') {
			if (nd) break;
			nl = 0;
		}
	}
	if (nl != 4 || nd != 5) return 0;
	sprintf(out, "%s-%s", letters, digits);
	return 1;
}

static int id_from_name(const char *name, char *out)
{
	/* look for AAAA[-_. ]NNN[.]NN anywhere in the name */
	for (const char *p = name; *p; p++) {
		if (isalpha((unsigned char)p[0]) && isalpha((unsigned char)p[1]) &&
		    isalpha((unsigned char)p[2]) && isalpha((unsigned char)p[3]) &&
		    (p == name || !isalpha((unsigned char)p[-1]))) {
			char tmp[16];
			strncpy(tmp, p, 12);
			tmp[12] = 0;
			if (normalise_id(tmp, out)) {
				static const char *pfx[] = { "SCUS", "SLUS", "SCES", "SLES", "SCPS", "SLPS",
				                             "SLPM", "SCED", "SLED", "PAPX", "SIPS", "SCAJ",
				                             "SLKA", "SCKA", "ESPM", "PBPX", NULL };
				for (int i = 0; pfx[i]; i++)
					if (!strncmp(out, pfx[i], 4)) return 1;
			}
		}
	}
	return 0;
}

/* ---- ISO9660: the ID from SYSTEM.CNF, over any way of reading a sector ---- */
typedef int (*sector_fn)(void *ctx, uint32_t lba, uint8_t *buf);   /* 2048 bytes of user data */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int id_from_sectors(sector_fn rd, void *ctx, char *out)
{
	uint8_t buf[2048];
	if (!rd(ctx, 16, buf) || buf[0] != 1 || memcmp(buf + 1, "CD001", 5)) return 0;

	uint32_t rootLba = le32(buf + 156 + 2), rootSize = le32(buf + 156 + 10);
	uint32_t cnfLba = 0, cnfSize = 0;
	for (uint32_t s = 0; s < (rootSize + 2047) / 2048 && s < 16 && !cnfLba; s++) {
		if (!rd(ctx, rootLba + s, buf)) break;
		for (int off = 0; off < 2048;) {
			int len = buf[off];
			if (!len) break;
			int nlen = buf[off + 32];
			if (nlen >= 10 && !strncasecmp((char *)buf + off + 33, "SYSTEM.CNF", 10)) {
				cnfLba = le32(buf + off + 2);
				cnfSize = le32(buf + off + 10);
				break;
			}
			off += len;
		}
	}
	if (!cnfLba || !rd(ctx, cnfLba, buf)) return 0;
	char txt[2049];
	memcpy(txt, buf, 2048);
	txt[cnfSize < 2048 ? cnfSize : 2048] = 0;
	char *boot = strstr(txt, "BOOT");
	if (!boot) return 0;
	char *eol = strpbrk(boot, "\r\n");
	if (eol) *eol = 0;
	char *name = strrchr(boot, '\\');
	if (!name) name = strrchr(boot, ':');
	return name && normalise_id(name + 1, out);
}

/* a raw (2352) or cooked (2048) image file */
typedef struct { FILE *f; int ssize, doff; } disc_t;

static int disc_read(void *ctx, uint32_t lba, uint8_t *buf)
{
	disc_t *d = ctx;
	if (fseek(d->f, (long)lba * d->ssize + d->doff, SEEK_SET)) return 0;
	return fread(buf, 1, 2048, d->f) == 2048;
}

static int id_from_iso(const char *path, char *out)
{
	static const int layouts[][2] = { { 2352, 24 }, { 2352, 16 }, { 2048, 0 }, { 2336, 8 } };
	disc_t d;
	int ok = 0;

	d.f = fopen(path, "rb");
	if (!d.f) return 0;
	for (unsigned i = 0; i < sizeof(layouts) / sizeof(layouts[0]) && !ok; i++) {
		d.ssize = layouts[i][0];
		d.doff = layouts[i][1];
		ok = id_from_sectors(disc_read, &d, out);
	}
	fclose(d.f);
	return ok;
}

/* a .chd: its frames are 2352 bytes of sector + 96 of subcode, packed in
 * hunks; read through libchdr (already in the core) */
typedef struct { chd_file *chd; const chd_header *h; uint8_t *hunk; uint32_t cur; int doff; } chd_ctx_t;

static int chd_sector_read(void *ctx, uint32_t lba, uint8_t *buf)
{
	chd_ctx_t *c = ctx;
	const uint32_t frame = 2352 + 96, per = c->h->hunkbytes / frame;
	if (!per) return 0;
	uint32_t hunk = lba / per;
	if (hunk >= c->h->totalhunks) return 0;
	if (hunk != c->cur) {
		if (chd_read(c->chd, hunk, c->hunk) != CHDERR_NONE) { c->cur = (uint32_t)-1; return 0; }
		c->cur = hunk;
	}
	memcpy(buf, c->hunk + (lba % per) * frame + c->doff, 2048);
	return 1;
}

static int id_from_chd(const char *path, char *out)
{
	chd_file *chd = NULL;
	if (chd_open(path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE) return 0;
	chd_ctx_t c = { chd, chd_get_header(chd), NULL, (uint32_t)-1, 0 };
	int ok = 0;
	if (c.h && c.h->hunkbytes >= 2352 + 96 && c.h->hunkbytes <= 1024 * 1024 && (c.hunk = malloc(c.h->hunkbytes))) {
		static const int offs[] = { 24, 16, 0 };       /* MODE2 form 1, MODE1, cooked */
		for (unsigned i = 0; i < 3 && !ok; i++) {
			c.doff = offs[i];
			c.cur = (uint32_t)-1;
			ok = id_from_sectors(chd_sector_read, &c, out);
		}
		free(c.hunk);
	}
	chd_close(chd);
	return ok;
}

static void dir_of(const char *path, char *out, size_t n)
{
	strncpy(out, path, n - 1);
	out[n - 1] = 0;
	char *s = strrchr(out, '/');
	if (s) *s = 0;
	else strcpy(out, ".");
}

static int id_from_cue(const char *path, char *out)
{
	FILE *f = fopen(path, "r");
	char line[512], dir[512], bin[1024];
	if (!f) return 0;
	dir_of(path, dir, sizeof(dir));
	while (fgets(line, sizeof(line), f)) {
		char *q = strchr(line, '"');
		if (strncasecmp(line, "FILE", 4) || !q) continue;
		char *e = strchr(q + 1, '"');
		if (!e) continue;
		*e = 0;
		snprintf(bin, sizeof(bin), "%s/%s", dir, q + 1);
		fclose(f);
		return id_from_iso(bin, out);
	}
	fclose(f);
	return 0;
}

static int id_from_pbp(const char *path, char *out)
{
	uint8_t hdr[40], sfo[4096];
	FILE *f = fopen(path, "rb");
	int ok = 0;
	if (!f) return 0;
	if (fread(hdr, 1, 40, f) == 40 && !memcmp(hdr, "\0PBP", 4)) {
		uint32_t sfoOff = le32(hdr + 8), sfoEnd = le32(hdr + 12);
		uint32_t n = sfoEnd > sfoOff ? sfoEnd - sfoOff : 0;
		if (n > sizeof(sfo)) n = sizeof(sfo);
		if (n > 20 && !fseek(f, sfoOff, SEEK_SET) && fread(sfo, 1, n, f) == n &&
		    !memcmp(sfo, "\0PSF", 4)) {
			uint32_t keyOff = le32(sfo + 8), dataOff = le32(sfo + 12), cnt = le32(sfo + 16);
			for (uint32_t i = 0; i < cnt && 20 + i * 16 + 16 <= n; i++) {
				const uint8_t *e = sfo + 20 + i * 16;
				uint32_t k = keyOff + (e[0] | e[1] << 8), dOff = dataOff + le32(e + 12);
				if (k < n && dOff < n && !strcmp((char *)sfo + k, "DISC_ID")) {
					char tmp[16] = { 0 };
					memcpy(tmp, sfo + dOff, n - dOff < 15 ? n - dOff : 15);
					ok = normalise_id(tmp, out);
					break;
				}
			}
		}
	}
	fclose(f);
	return ok;
}

static int id_from_m3u(const char *path, char *out)
{
	FILE *f = fopen(path, "r");
	char line[512], dir[512], sub[1024];
	if (!f) return 0;
	dir_of(path, dir, sizeof(dir));
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#') continue;
		snprintf(sub, sizeof(sub), "%s/%s", dir, line);
		fclose(f);
		return covers_detect_id(sub, out);
	}
	fclose(f);
	return 0;
}

/* ---- remembered IDs (file name -> ID) ---- */
typedef struct { char name[200]; char id[12]; } idmap_t;
static idmap_t *ids;
static int nids, idsLoaded;

static void ids_load(void)
{
	FILE *f;
	char line[256];
	if (idsLoaded) return;
	idsLoaded = 1;
	f = fopen(ID_FILE, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		tab[1 + strcspn(tab + 1, "\r\n")] = 0;
		ids = realloc(ids, (nids + 1) * sizeof(idmap_t));
		strncpy(ids[nids].name, line, sizeof(ids[nids].name) - 1);
		ids[nids].name[sizeof(ids[nids].name) - 1] = 0;
		strncpy(ids[nids].id, tab + 1, sizeof(ids[nids].id) - 1);
		ids[nids].id[sizeof(ids[nids].id) - 1] = 0;
		/* a .chd with no ID remembered was only given up on because CHDs
		 * could not be read before: look at it again */
		const char *dot = strrchr(ids[nids].name, '.');
		if (!strcmp(ids[nids].id, "-") && dot && !strcasecmp(dot, ".chd")) continue;
		nids++;
	}
	fclose(f);
}

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static void ids_remember(const char *path, const char *id)
{
	const char *n = base_name(path);
	ids_load();
	for (int i = 0; i < nids; i++)
		if (!strcmp(ids[i].name, n)) {
			if (strcmp(ids[i].id, id)) strcpy(ids[i].id, id);
			else return;
			goto save;
		}
	ids = realloc(ids, (nids + 1) * sizeof(idmap_t));
	strncpy(ids[nids].name, n, sizeof(ids[nids].name) - 1);
	ids[nids].name[sizeof(ids[nids].name) - 1] = 0;
	strcpy(ids[nids].id, id);
	nids++;
save:
	wiiu_mkdirs(COVER_DIR);
	{
		FILE *f = fopen(ID_FILE, "w");
		if (!f) return;
		for (int i = 0; i < nids; i++)
			if (ids[i].id[0]) fprintf(f, "%s\t%s\n", ids[i].name, ids[i].id);
		fclose(f);
	}
}

int covers_detect_id(const char *path, char *out)
{
	const char *ext = strrchr(path, '.');
	const char *n = base_name(path);
	ids_load();
	for (int i = 0; i < nids; i++)
		if (!strcmp(ids[i].name, n)) {
			/* "-" = analysed before and no ID found: don't read the file again */
			if (!strcmp(ids[i].id, "-")) { out[0] = 0; return 0; }
			strcpy(out, ids[i].id);
			return out[0] != 0;
		}

	int ok = 0;
	if (id_from_name(n, out)) ok = 1;
	else if (ext && !strcasecmp(ext, ".cue")) ok = id_from_cue(path, out);
	else if (ext && !strcasecmp(ext, ".chd")) ok = id_from_chd(path, out);
	else if (ext && !strcasecmp(ext, ".pbp")) ok = id_from_pbp(path, out);
	else if (ext && !strcasecmp(ext, ".m3u")) ok = id_from_m3u(path, out);
	else if (ext && (!strcasecmp(ext, ".bin") || !strcasecmp(ext, ".iso") ||
	                 !strcasecmp(ext, ".img") || !strcasecmp(ext, ".mdf")))
		ok = id_from_iso(path, out);
	if (ok) ids_remember(path, out);
	else { ids_remember(path, "-"); out[0] = 0; }
	return ok;
}

int covers_normalise_id(const char *in, char *out) { return normalise_id(in, out); }

/* remembered-ID store is shared with covers.c */
void covers_remember_id(const char *path, const char *id) { ids_remember(path, id); }
