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

typedef struct { const char *en, *pt; } tr_t;

static const tr_t PT[] = {
#include "lang_pt.h"
};
#define NPT (sizeof(PT) / sizeof(PT[0]))

static const tr_t **sorted;   /* PT sorted by the English text, for bsearch */

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
	if (!en || !*en || cfg.lang != LANG_PT) return en;
	if (!sorted) {
		sorted = malloc(NPT * sizeof(*sorted));
		if (!sorted) return en;
		for (size_t i = 0; i < NPT; i++) sorted[i] = &PT[i];
		qsort(sorted, NPT, sizeof(*sorted), cmp_tr);
	}
	const tr_t **hit = bsearch(en, sorted, NPT, sizeof(*sorted), cmp_key);
	return hit ? (*hit)->pt : en;
}
