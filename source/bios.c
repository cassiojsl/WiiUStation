/*
 * WiiUStation - BIOS check: MD5 of each file in the bios folder, compared
 * with the known PlayStation BIOS dumps.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include "platform.h"

/* ---- MD5 (RFC 1321) ---- */
typedef struct { uint32_t a, b, c, d; uint64_t len; uint8_t buf[64]; int n; } md5_t;

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define ROT(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

static void md5_block(md5_t *m, const uint8_t *p)
{
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
	static const int R[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
	uint32_t w[16], a = m->a, b = m->b, c = m->c, d = m->d;
	for (int i = 0; i < 16; i++)
		w[i] = p[i * 4] | (p[i * 4 + 1] << 8) | (p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;
		if (i < 16)      { f = F(b, c, d); g = i; }
		else if (i < 32) { f = G(b, c, d); g = (5 * i + 1) & 15; }
		else if (i < 48) { f = H(b, c, d); g = (3 * i + 5) & 15; }
		else             { f = I(b, c, d); g = (7 * i) & 15; }
		uint32_t t = d;
		d = c;
		c = b;
		b = b + ROT(a + f + K[i] + w[g], R[i]);
		a = t;
	}
	m->a += a; m->b += b; m->c += c; m->d += d;
}

static void md5_init(md5_t *m)
{
	m->a = 0x67452301; m->b = 0xefcdab89; m->c = 0x98badcfe; m->d = 0x10325476;
	m->len = 0; m->n = 0;
}

static void md5_update(md5_t *m, const uint8_t *p, size_t n)
{
	m->len += n;
	while (n--) {
		m->buf[m->n++] = *p++;
		if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
	}
}

static void md5_final(md5_t *m, char hex[33])
{
	uint64_t bits = m->len * 8;
	uint8_t pad = 0x80, zero = 0, lenb[8];
	md5_update(m, &pad, 1);
	while (m->n != 56) md5_update(m, &zero, 1);
	for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (i * 8));
	md5_update(m, lenb, 8);
	uint32_t v[4] = { m->a, m->b, m->c, m->d };
	for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", (v[i / 4] >> ((i % 4) * 8)) & 0xff);
}

int bios_md5(const char *path, char hex[33])
{
	static uint8_t buf[16384];
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	md5_t m;
	md5_init(&m);
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) md5_update(&m, buf, n);
	fclose(f);
	md5_final(&m, hex);
	return 1;
}

/* ---- known dumps ---- */
static const struct { const char *md5, *model, *version; int region; } KNOWN[] = {
	{ "239665b1a3dade1b5a52c06338011044", "SCPH-1000",  "1.0", 3 },
	{ "849515939161e62f6b866f6853006780", "SCPH-3000",  "1.1", 3 },
	{ "cba733ceeff5aef5c32254f1d617fa62", "SCPH-3500",  "2.1", 3 },
	{ "8dd7d5296a650fac7319bce665a6a53c", "SCPH-5500",  "3.0", 3 },
	{ "dc2b9bf8da62ec93e868cfd29f0d067d", "SCPH-1001",  "2.0", 1 },
	{ "924e392ed05558ffdb115408c263dccf", "SCPH-1001",  "2.2", 1 },
	{ "490f666e1afb15b7362b406ed1cea246", "SCPH-5501",  "3.0", 1 },
	{ "1e68c231d0896b7eadcad1d7d8e76129", "SCPH-7001",  "4.1", 1 },
	{ "6e3735ff4c7dc899ee98981385f6f3d0", "SCPH-101",   "4.4", 1 },
	{ "32736f17079d0b2b7024407c39bd3050", "SCPH-5502",  "3.0", 2 },
	{ "b9d9a0286c33dc6b7237bb13cd46fdee", "SCPH-7502",  "4.1", 2 },
	{ "c53ca5908936d412331790f4426c6c33", "PSP (PSone)", "6.60", 0 },
	{ "81bbe60ba7a3d1cea1d48c14cbcc647b", "PS3 (PS1)",  "-", 0 },
};

/* fills info for a file; returns 1 if it is a known dump */
int bios_identify(const char *path, char *model, size_t mn, char *version, size_t vn, int *region)
{
	struct stat st;
	char hex[33];
	*region = -1;
	model[0] = version[0] = 0;
	if (stat(path, &st) != 0 || st.st_size != 512 * 1024) return -1;   /* not a BIOS size */
	if (!bios_md5(path, hex)) return -1;
	for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++)
		if (!strcmp(hex, KNOWN[i].md5)) {
			snprintf(model, mn, "%s", KNOWN[i].model);
			snprintf(version, vn, "%s", KNOWN[i].version);
			*region = KNOWN[i].region;
			return 1;
		}
	return 0;
}
