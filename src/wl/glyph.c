#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>
#include "glyph.h"

/* A full table or arena is emptied and refilled: the redraw path never allocates. */
enum { SLOTS = 4096, ARENA = 1 << 22 };
static struct { unsigned key; Glyph g; } slot[SLOTS];   /* key cp + 1, 0 empty */
static unsigned char arena[ARENA];
static size_t used, nslot;
static FT_Library ft;
static FT_Face face;
int glyph_cw, glyph_ch, glyph_asc;

static void clear(void) { memset(slot, 0, sizeof slot); used = nslot = 0; }

int glyph_open(int px)
{
	if (!face) {
		FcChar8 *file;
		int index = 0;
		FcResult r;
		FcPattern *p = FcNameParse((const FcChar8 *)"JetBrainsMono Nerd Font,JetBrains Mono,monospace");
		FcConfigSubstitute(NULL, p, FcMatchPattern);
		FcDefaultSubstitute(p);
		FcPattern *m = FcFontMatch(NULL, p, &r);
		FcPatternDestroy(p);
		int bad = !m || FcPatternGetString(m, FC_FILE, 0, &file) != FcResultMatch;
		if (!bad) FcPatternGetInteger(m, FC_INDEX, 0, &index);
		bad = bad || FT_Init_FreeType(&ft) || FT_New_Face(ft, (const char *)file, index, &face);
		if (m) FcPatternDestroy(m);
		if (bad) return -1;
	}
	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) || FT_Load_Char(face, 'M', FT_LOAD_DEFAULT)) return -1;
	glyph_cw = (int)((face->glyph->advance.x + 63) >> 6);
	glyph_ch = (int)((face->size->metrics.height + 63) >> 6);
	glyph_asc = (int)((face->size->metrics.ascender + 63) >> 6);
	clear();
	return 0;
}

const Glyph *glyph_get(unsigned cp)
{
	static const Glyph none = { 0, 0, 0, 0, arena };
	unsigned i = cp % SLOTS;
	while (slot[i].key && slot[i].key != cp + 1) i = (i + 1) % SLOTS;
	if (slot[i].key) return &slot[i].g;
	if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) return &none;
	FT_Bitmap *b = &face->glyph->bitmap;
	size_t n = (size_t)b->width * b->rows;
	if (n > ARENA) return &none;
	if (used + n > ARENA || nslot >= SLOTS * 3 / 4) {
		clear();
		i = cp % SLOTS;
	}
	unsigned char *a = arena + used;
	for (unsigned y = 0; y < b->rows; y++) memcpy(a + y * b->width, b->buffer + (int)y * b->pitch, b->width);
	used += n;
	nslot++;
	slot[i].key = cp + 1;
	slot[i].g = (Glyph){ (int)b->width, (int)b->rows, face->glyph->bitmap_left, face->glyph->bitmap_top, a };
	return &slot[i].g;
}

void glyph_close(void)
{
	if (face) FT_Done_Face(face);
	if (ft) FT_Done_FreeType(ft);
	face = NULL;
	ft = NULL;
	FcFini();
}
