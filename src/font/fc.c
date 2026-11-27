/* find.h with fontconfig, as foot does with fcft: FcFontSort sorts every
   font for the family asked, trimmed to fonts that add characters; the
   first is the main font.  Not on Windows (dwrite.c). */
#ifndef _WIN32
#include <stddef.h>
#include <fontconfig/fontconfig.h>
#include "find.h"

static FcFontSet *set;

int find_open(const char *family, int px)
{
	find_close();
	if (!FcInit()) return -1;
	FcPattern *p = FcNameParse((const FcChar8 *)family);
	if (!p) return -1;
	FcPatternAddDouble(p, FC_PIXEL_SIZE, px);
	FcConfigSubstitute(NULL, p, FcMatchPattern);
	FcDefaultSubstitute(p);
	FcResult r;
	set = FcFontSort(NULL, p, FcTrue, NULL, &r);
	FcPatternDestroy(p);
	return set && set->nfont > 0 ? set->nfont : -1;
}

void find_close(void)
{
	if (set) FcFontSetDestroy(set);
	set = NULL;
	FcFini();
}

const char *find_file(int i, int *index)
{
	FcChar8 *file;
	*index = 0;
	if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch) return NULL;
	FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, index);
	return (const char *)file;
}

int find_font(const unsigned *cp, unsigned n, int after, int color)
{
	for (int i = after + 1; i < set->nfont; i++) {
		FcBool c;
		FcCharSet *cs;
		if (color && (FcPatternGetBool(set->fonts[i], FC_COLOR, 0, &c) != FcResultMatch || !c)) continue;
		if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs) != FcResultMatch) continue;
		unsigned j = 0;
		while (j < n && FcCharSetHasChar(cs, cp[j])) j++;
		if (j == n) return i;
	}
	return -1;
}
#endif
