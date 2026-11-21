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
#include "platform.h"

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

static struct { const tr_t *t; size_t n; const tr_t **sorted; } LANGS[LANG_COUNT] = {
	[LANG_PT] = { PT, sizeof(PT) / sizeof(PT[0]), NULL },
	[LANG_ES] = { ES, sizeof(ES) / sizeof(ES[0]), NULL },
	[LANG_FR] = { FR, sizeof(FR) / sizeof(FR[0]), NULL },
	[LANG_DE] = { DE, sizeof(DE) / sizeof(DE[0]), NULL },
	[LANG_IT] = { IT, sizeof(IT) / sizeof(IT[0]), NULL },
};

/* each language's name, written in that language */
const char *lang_name(int l)
{
	static const char *n[LANG_COUNT] = { "English", "Portugu\xc3\xaas", "Espa\xc3\xb1ol",
		"Fran\xc3\xa7" "ais", "Deutsch", "Italiano" };
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
	int l = cfg.lang;
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
