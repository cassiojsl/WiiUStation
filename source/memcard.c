/*
 * WiiUStation - PlayStation memory card files (128 KB raw images: the
 * core's .srm, and .mcr / .mcd / .mc from other emulators).
 *
 * Layout: 16 blocks of 8 KB. Block 0 is the directory: frame 0 is the "MC"
 * header, frames 1-15 describe blocks 1-15 (state, size, next block, file
 * name, XOR checksum). A save's first block starts with its title frame
 * ("SC", icon frames, Shift-JIS title, icon palette) and up to 3 icon frames.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "platform.h"

#define BLOCK 8192
#define FRAME 128

static uint8_t *dir_frame(uint8_t *card, int block) { return card + block * FRAME; }

static void fix_checksum(uint8_t *f)
{
	uint8_t x = 0;
	for (int i = 0; i < FRAME - 1; i++) x ^= f[i];
	f[FRAME - 1] = x;
}

int mc_load(const char *path, uint8_t *card)
{
	struct stat st;
	if (stat(path, &st) != 0 || st.st_size != MC_SIZE) return 0;
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	int ok = fread(card, 1, MC_SIZE, f) == MC_SIZE;
	fclose(f);
	return ok && card[0] == 'M' && card[1] == 'C';
}

int mc_save(const char *path, const uint8_t *card)
{
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return 0;
	int ok = fwrite(card, 1, MC_SIZE, f) == MC_SIZE;
	fclose(f);
	if (!ok) { remove(tmp); return 0; }
	remove(path);
	return rename(tmp, path) == 0;
}

/* Shift-JIS title -> UTF-8: full-width letters, digits and the usual
 * punctuation become ASCII; anything else (kana, kanji) is skipped. */
static void sjis_title(const uint8_t *s, int n, char *out, size_t on)
{
	size_t o = 0;
	for (int i = 0; i < n && s[i] && o + 1 < on; i++) {
		uint8_t c = s[i];
		if (c < 0x80) { out[o++] = (char)c; continue; }
		if (i + 1 >= n) break;
		unsigned w = (c << 8) | s[i + 1];
		i++;
		char a = 0;
		if (w >= 0x824F && w <= 0x8258) a = (char)('0' + w - 0x824F);
		else if (w >= 0x8260 && w <= 0x8279) a = (char)('A' + w - 0x8260);
		else if (w >= 0x8281 && w <= 0x829A) a = (char)('a' + w - 0x8281);
		else switch (w) {
			case 0x8140: a = ' '; break;  case 0x8143: a = ','; break;  case 0x8144: a = '.'; break;
			case 0x8146: a = ':'; break;  case 0x8147: a = ';'; break;  case 0x8148: a = '?'; break;
			case 0x8149: a = '!'; break;  case 0x815B: a = '-'; break;  case 0x815E: a = '/'; break;
			case 0x8166: a = '\''; break; case 0x8168: a = '"'; break;  case 0x8169: a = '('; break;
			case 0x816A: a = ')'; break;  case 0x816D: a = '['; break;  case 0x816E: a = ']'; break;
			case 0x817B: a = '+'; break;  case 0x817C: a = '-'; break;  case 0x8181: a = '='; break;
			case 0x8193: a = '%'; break;  case 0x8194: a = '#'; break;  case 0x8195: a = '&'; break;
			case 0x8196: a = '*'; break;  case 0x8197: a = '@'; break;  case 0x8160: a = '~'; break;
		}
		if (a) out[o++] = a;
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = 0;
	/* collapse runs of spaces */
	char *r = out, *w2 = out;
	for (; *r; r++) if (!(*r == ' ' && w2 > out && w2[-1] == ' ')) *w2++ = *r;
	*w2 = 0;
}

int mc_list(const uint8_t *card, mc_save_t *out, int max)
{
	int n = 0;
	for (int b = 1; b <= 15 && n < max; b++) {
		const uint8_t *d = card + b * FRAME;
		if (d[0] != 0x51) continue;                /* first block of a save */
		mc_save_t *s = &out[n];
		memset(s, 0, sizeof(*s));
		s->first = b;
		memcpy(s->fname, d + 0x0A, 20);
		s->fname[20] = 0;
		/* count the chain */
		int blk = b, guard = 0;
		while (guard++ < 15) {
			s->blocks++;
			const uint8_t *e = card + blk * FRAME;
			unsigned next = e[8] | (e[9] << 8);
			if (next == 0xFFFF || next > 14) break;
			blk = next + 1;
		}
		const uint8_t *t = card + b * BLOCK;     /* title frame */
		if (t[0] == 'S' && t[1] == 'C') {
			sjis_title(t + 4, 64, s->title, sizeof(s->title));
			/* icon: palette at 0x60, first icon frame after the title frame */
			uint32_t pal[16];
			for (int i = 0; i < 16; i++) {
				unsigned c = t[0x60 + i * 2] | (t[0x61 + i * 2] << 8);
				uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, bl = ((c >> 10) & 31) << 3;
				pal[i] = c == 0 ? 0 : (0xFFu << 24) | (r << 16) | (g << 8) | bl;
			}
			const uint8_t *ic = t + FRAME;
			for (int i = 0; i < 128; i++) {
				s->icon[i * 2]     = pal[ic[i] & 15];
				s->icon[i * 2 + 1] = pal[ic[i] >> 4];
			}
		}
		if (!s->title[0]) snprintf(s->title, sizeof(s->title), "%s", s->fname);
		n++;
	}
	return n;
}

int mc_free_blocks(const uint8_t *card)
{
	int n = 0;
	for (int b = 1; b <= 15; b++) {
		uint8_t st = card[b * FRAME];
		if (st == 0xA0 || st == 0xA1 || st == 0xA2 || st == 0xA3) n++;
	}
	return n;
}

static void free_entry(uint8_t *card, int b)
{
	uint8_t *d = dir_frame(card, b);
	memset(d, 0, FRAME);
	d[0] = 0xA0;
	d[8] = d[9] = 0xFF;
	fix_checksum(d);
}

void mc_delete(uint8_t *card, int first)
{
	int blk = first, guard = 0;
	while (blk >= 1 && blk <= 15 && guard++ < 15) {
		uint8_t *d = dir_frame(card, blk);
		unsigned next = d[8] | (d[9] << 8);
		free_entry(card, blk);
		if (next == 0xFFFF || next > 14) break;
		blk = next + 1;
	}
}

/* 0 ok, -1 not enough free blocks, -2 a save with that name is there */
int mc_copy(const uint8_t *src, int first, uint8_t *dst)
{
	int chain[15], n = 0, blk = first;
	while (blk >= 1 && blk <= 15 && n < 15) {
		chain[n++] = blk;
		const uint8_t *d = src + blk * FRAME;
		unsigned next = d[8] | (d[9] << 8);
		if (next == 0xFFFF || next > 14) break;
		blk = next + 1;
	}
	for (int b = 1; b <= 15; b++)
		if (dst[b * FRAME] == 0x51 && !memcmp(dst + b * FRAME + 0x0A, src + first * FRAME + 0x0A, 20)) return -2;
	int freeb[15], nf = 0;
	for (int b = 1; b <= 15 && nf < n; b++) {
		uint8_t st = dst[b * FRAME];
		if (st == 0xA0 || st == 0xA1 || st == 0xA2 || st == 0xA3) freeb[nf++] = b;
	}
	if (nf < n) return -1;
	for (int i = 0; i < n; i++) {
		uint8_t *d = dir_frame(dst, freeb[i]);
		memcpy(d, src + chain[i] * FRAME, FRAME);
		unsigned next = i + 1 < n ? (unsigned)(freeb[i + 1] - 1) : 0xFFFF;
		d[8] = next & 0xFF;
		d[9] = next >> 8;
		fix_checksum(d);
		memcpy(dst + freeb[i] * BLOCK, src + chain[i] * BLOCK, BLOCK);
	}
	return 0;
}

/* a blank, formatted card */
void mc_format(uint8_t *card)
{
	memset(card, 0, MC_SIZE);
	card[0] = 'M';
	card[1] = 'C';
	fix_checksum(card);
	for (int b = 1; b <= 15; b++) free_entry(card, b);
	for (int b = 16; b < 64; b++) {            /* unused frames: "broken sector" list */
		uint8_t *d = card + b * FRAME;
		d[0] = d[1] = d[2] = d[3] = 0xFF;
		d[8] = d[9] = 0xFF;
		fix_checksum(d);
	}
}
