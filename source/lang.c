/*
 * WiiUStation - interface language.
 *
 * Texts are written in English in the code. tr() returns the translation for
 * the chosen language, or the English text itself. Menu labels are translated
 * when drawn, so most strings never need tr() in the code; format strings
 * ("Slot %d") must go through tr() before snprintf.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "platform.h"
#ifdef __WIIU__
#include <coreinit/userconfig.h>
#endif

typedef struct { const char *en, *tx; } tr_t;

static const tr_t PT[] = {
#include "lang_pt.h"
};
static const tr_t ES[] = {
#include "lang_es.h"
};
static const tr_t FR[] = {
#include "lang_fr.h"
};
static const tr_t DE[] = {
#include "lang_de.h"
};
static const tr_t IT[] = {
#include "lang_it.h"
};
static const tr_t NL[] = {
#include "lang_nl.h"
};
static const tr_t RU[] = {
#include "lang_ru.h"
};
static const tr_t JA[] = {
#include "lang_ja.h"
};

static struct { const tr_t *t; size_t n; const tr_t **sorted; } LANGS[LANG_COUNT] = {
	[LANG_PT] = { PT, sizeof(PT) / sizeof(PT[0]), NULL },
	[LANG_ES] = { ES, sizeof(ES) / sizeof(ES[0]), NULL },
	[LANG_FR] = { FR, sizeof(FR) / sizeof(FR[0]), NULL },
	[LANG_DE] = { DE, sizeof(DE) / sizeof(DE[0]), NULL },
	[LANG_IT] = { IT, sizeof(IT) / sizeof(IT[0]), NULL },
	[LANG_NL] = { NL, sizeof(NL) / sizeof(NL[0]), NULL },
	[LANG_RU] = { RU, sizeof(RU) / sizeof(RU[0]), NULL },
	[LANG_JA] = { JA, sizeof(JA) / sizeof(JA[0]), NULL },
};

/* the Wii U's own language (System Settings), as one of ours; read once */
int lang_system(void)
{
	static int sys = -2;
	if (sys != -2) return sys;
	sys = LANG_EN;
#ifdef __WIIU__
	uint32_t v = 1;
	UCHandle h = UCOpen();
	if (h >= 0) {
		UCSysConfig c;
		memset(&c, 0, sizeof(c));
		strcpy(c.name, "cafe.language");
		c.dataType = UC_DATATYPE_UNSIGNED_INT;
		c.dataSize = sizeof(v);
		c.data = &v;
		if (UCReadSysConfig(h, 1, &c) == UC_ERROR_OK) {
			/* 0 ja 1 en 2 fr 3 de 4 it 5 es 6 zh 7 ko 8 nl 9 pt 10 ru 11 zh-TW */
			static const int map[12] = { LANG_JA, LANG_EN, LANG_FR, LANG_DE, LANG_IT, LANG_ES,
			                             LANG_EN, LANG_EN, LANG_NL, LANG_PT, LANG_RU, LANG_EN };
			if (v < 12) sys = map[v];
		}
		UCClose(h);
	}
#endif
	return sys;
}

/* the language in use: the chosen one, or the system's */
int lang_current(void) { return cfg.lang < 0 ? lang_system() : cfg.lang; }

/* each language's name, written in that language; LANG_AUTO = "System (...)" */
const char *lang_name(int l)
{
	if (l < 0) {
		static char buf[96];
		snprintf(buf, sizeof(buf), tr("System (%s)"), lang_name(lang_system()));
		return buf;
	}
	static const char *n[LANG_COUNT] = { "English", "Portugu\xc3\xaas", "Espa\xc3\xb1ol",
		"Fran\xc3\xa7" "ais", "Deutsch", "Italiano", "Nederlands",
		"\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9",      /* Russkiy */
		"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" };                        /* Nihongo */
	return l >= 0 && l < LANG_COUNT ? n[l] : n[0];
}

static int cmp_tr(const void *a, const void *b)
{
	return strcmp((*(const tr_t * const *)a)->en, (*(const tr_t * const *)b)->en);
}

static int cmp_key(const void *k, const void *e)
{
	return strcmp((const char *)k, (*(const tr_t * const *)e)->en);
}

const char *tr(const char *en)
{
	int l = lang_current();
	if (!en || !*en || l <= LANG_EN || l >= LANG_COUNT || !LANGS[l].t) return en;
	if (!LANGS[l].sorted) {
		const tr_t **v = malloc(LANGS[l].n * sizeof(*v));
		if (!v) return en;
		for (size_t i = 0; i < LANGS[l].n; i++) v[i] = &LANGS[l].t[i];
		qsort(v, LANGS[l].n, sizeof(*v), cmp_tr);
		LANGS[l].sorted = v;
	}
	const tr_t **hit = bsearch(en, LANGS[l].sorted, LANGS[l].n, sizeof(*hit), cmp_key);
	return hit ? (*hit)->tx : en;
}
