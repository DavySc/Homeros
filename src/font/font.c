/* Fonts and shaping, as foot does with fcft: fontconfig sorts the fonts,
   FreeType draws a glyph, HarfBuzz shapes a run.  Fallback copies wezterm's
   rule (shaper.rs): shape the run with the first font, then reshape each
   cluster that came out with a missing glyph in the first later font, in
   fontconfig's order, that has all of its characters.  Rows are drawn left
   to right as Neovim draws them (no bidi), so the run is shaped LTR and its
   clusters come out in order. */
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <fontconfig/fontconfig.h>
#include "font.h"
#include "uni.h"

enum {
	RUN = 2048,            /* glyphs one run can shape to */
	SLOTS = 8192,          /* glyph cache entries, a power of two */
	ARENA = 8 << 20,       /* glyph bitmaps; pages count only once touched */
};

typedef struct {
	FT_Face ft;
	hb_font_t *hb;
	int state;             /* 0 not loaded, 1 loaded, -1 cannot be used */
	int color;
	double scale;          /* bitmap strike to the em asked for (emoji) */
} Face;

typedef struct { int font; unsigned gid; Glyph g; } Slot;          /* font -1: empty */
typedef struct { unsigned cluster; int font, done; unsigned gid; int x, y; } Shaped;   /* cluster: index in cp */

static FT_Library lib;
static FcFontSet *set;
static Face *face;
static hb_buffer_t *buf;
static Slot *slot;
static unsigned char *arena;
static size_t used, nslot;
static unsigned long hits, misses;
static int px, cw, ch, base;
static Shaped run[RUN];
static int nrun;
static hb_codepoint_t cp[RUN];   /* the run's characters as uni.h decodes them */
static size_t at[RUN];           /* and where each starts */
static unsigned ncp;

static Face *load(int i)
{
	Face *f = &face[i];
	if (f->state) return f->state > 0 ? f : NULL;
	f->state = -1;
	FcChar8 *file;
	int idx = 0;
	if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch) return NULL;
	FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &idx);
	if (FT_New_Face(lib, (const char *)file, idx, &f->ft)) return NULL;
	f->scale = 1;
	f->color = FT_HAS_COLOR(f->ft);
	if (FT_IS_SCALABLE(f->ft)) {
		if (FT_Set_Pixel_Sizes(f->ft, 0, (FT_UInt)px)) goto bad;
	} else if (FT_HAS_COLOR(f->ft) && f->ft->num_fixed_sizes > 0) {
		/* a colour bitmap font (Noto Color Emoji): take the smallest strike
		   at least as big as the em, else the biggest, and scale it */
		int best = 0;
		for (int k = 1; k < f->ft->num_fixed_sizes; k++) {
			FT_Pos a = f->ft->available_sizes[k].y_ppem, b = f->ft->available_sizes[best].y_ppem;
			if (b < px * 64 ? a > b : a >= px * 64 && a < b) best = k;
		}
		if (FT_Select_Size(f->ft, best)) goto bad;
		f->scale = px * 64.0 / (double)f->ft->available_sizes[best].y_ppem;
	} else goto bad;   /* a monochrome bitmap font: no size to draw it at */
	hb_blob_t *b = hb_blob_create_from_file((const char *)file);
	hb_face_t *hf = hb_face_create(b, (unsigned)idx);
	f->hb = hb_font_create(hf);   /* HarfBuzz's own OpenType functions */
	hb_face_destroy(hf);
	hb_blob_destroy(b);
	hb_font_set_scale(f->hb, px * 64, px * 64);   /* positions in 1/64 pixel */
	f->state = 1;
	return f;
bad:
	FT_Done_Face(f->ft);
	f->ft = NULL;
	return NULL;
}

static void flush(void)
{
	for (size_t i = 0; i < SLOTS; i++) slot[i].font = -1;
	used = nslot = 0;
}

void font_close(void)
{
	if (face)
		for (int i = 0; i < set->nfont; i++)
			if (face[i].state > 0) { hb_font_destroy(face[i].hb); FT_Done_Face(face[i].ft); }
	free(face);
	face = NULL;
	if (set) FcFontSetDestroy(set);
	set = NULL;
	if (buf) hb_buffer_destroy(buf);
	buf = NULL;
	free(slot);
	free(arena);
	slot = NULL;
	arena = NULL;
	if (lib) FT_Done_FreeType(lib);
	lib = NULL;
	FcFini();
}

int font_open(const char *family, int size)
{
	font_close();
	px = size;
	hits = misses = 0;
	if (size <= 0 || !FcInit() || FT_Init_FreeType(&lib)) goto fail;
	FcPattern *p = FcNameParse((const FcChar8 *)family);
	if (!p) goto fail;
	FcPatternAddDouble(p, FC_PIXEL_SIZE, size);
	FcConfigSubstitute(NULL, p, FcMatchPattern);
	FcDefaultSubstitute(p);
	FcResult r;
	set = FcFontSort(NULL, p, FcTrue, NULL, &r);   /* trimmed: each font adds characters */
	FcPatternDestroy(p);
	if (!set || set->nfont == 0) goto fail;
	face = calloc((size_t)set->nfont, sizeof *face);
	slot = malloc(SLOTS * sizeof *slot);
	arena = malloc(ARENA);
	buf = hb_buffer_create();
	/* the longest run now, so a longer row later allocates nothing */
	if (!face || !slot || !arena || !hb_buffer_pre_allocate(buf, RUN) || !load(0)) goto fail;
	flush();
	FT_Face f = face[0].ft;
	base = (int)((f->size->metrics.ascender + 63) >> 6);
	ch = base + (int)((-f->size->metrics.descender + 63) >> 6);
	if (FT_Load_Char(f, 'M', FT_LOAD_TARGET_LIGHT)) goto fail;
	cw = (int)((f->glyph->advance.x + 32) >> 6);
	return 0;
fail:
	font_close();
	return -1;
}

void font_cell(int *w, int *h, int *baseline) { *w = cw; *h = ch; *baseline = base; }

void font_stats(unsigned long *hit, unsigned long *miss) { *hit = hits; *miss = misses; }

/* Characters a font need not have: joiners, variation selectors and tags
   are default-ignorable, and HarfBuzz hides them when the font lacks them. */
static int ignorable(unsigned c)
{
	return c == 0x200C || c == 0x200D || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0000 && c <= 0xE0FFF);
}

/* Emoji_Presentation, Unicode 16 (emoji_table.py). */
static const unsigned pres[][2] = {
	{ 0x231A, 0x231B }, { 0x23E9, 0x23EC }, { 0x23F0, 0x23F0 }, { 0x23F3, 0x23F3 },
	{ 0x25FD, 0x25FE }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 }, { 0x267F, 0x267F },
	{ 0x2693, 0x2693 }, { 0x26A1, 0x26A1 }, { 0x26AA, 0x26AB }, { 0x26BD, 0x26BE },
	{ 0x26C4, 0x26C5 }, { 0x26CE, 0x26CE }, { 0x26D4, 0x26D4 }, { 0x26EA, 0x26EA },
	{ 0x26F2, 0x26F3 }, { 0x26F5, 0x26F5 }, { 0x26FA, 0x26FA }, { 0x26FD, 0x26FD },
	{ 0x2705, 0x2705 }, { 0x270A, 0x270B }, { 0x2728, 0x2728 }, { 0x274C, 0x274C },
	{ 0x274E, 0x274E }, { 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
	{ 0x27B0, 0x27B0 }, { 0x27BF, 0x27BF }, { 0x2B1B, 0x2B1C }, { 0x2B50, 0x2B50 },
	{ 0x2B55, 0x2B55 }, { 0x1F004, 0x1F004 }, { 0x1F0CF, 0x1F0CF }, { 0x1F18E, 0x1F18E },
	{ 0x1F191, 0x1F19A }, { 0x1F1E6, 0x1F1FF }, { 0x1F201, 0x1F201 }, { 0x1F21A, 0x1F21A },
	{ 0x1F22F, 0x1F22F }, { 0x1F232, 0x1F236 }, { 0x1F238, 0x1F23A }, { 0x1F250, 0x1F251 },
	{ 0x1F300, 0x1F320 }, { 0x1F32D, 0x1F335 }, { 0x1F337, 0x1F37C }, { 0x1F37E, 0x1F393 },
	{ 0x1F3A0, 0x1F3CA }, { 0x1F3CF, 0x1F3D3 }, { 0x1F3E0, 0x1F3F0 }, { 0x1F3F4, 0x1F3F4 },
	{ 0x1F3F8, 0x1F43E }, { 0x1F440, 0x1F440 }, { 0x1F442, 0x1F4FC }, { 0x1F4FF, 0x1F53D },
	{ 0x1F54B, 0x1F54E }, { 0x1F550, 0x1F567 }, { 0x1F57A, 0x1F57A }, { 0x1F595, 0x1F596 },
	{ 0x1F5A4, 0x1F5A4 }, { 0x1F5FB, 0x1F64F }, { 0x1F680, 0x1F6C5 }, { 0x1F6CC, 0x1F6CC },
	{ 0x1F6D0, 0x1F6D2 }, { 0x1F6D5, 0x1F6D7 }, { 0x1F6DC, 0x1F6DF }, { 0x1F6EB, 0x1F6EC },
	{ 0x1F6F4, 0x1F6FC }, { 0x1F7E0, 0x1F7EB }, { 0x1F7F0, 0x1F7F0 }, { 0x1F90C, 0x1F93A },
	{ 0x1F93C, 0x1F945 }, { 0x1F947, 0x1F9FF }, { 0x1FA70, 0x1FA7C }, { 0x1FA80, 0x1FA89 },
	{ 0x1FA8F, 0x1FAC6 }, { 0x1FACE, 0x1FADC }, { 0x1FADF, 0x1FAE9 }, { 0x1FAF0, 0x1FAF8 },

};

/* A cluster with an Emoji_Presentation character or U+FE0F is drawn from a
   colour font if one has it, even when an earlier font has a plain glyph,
   as fcft (foot's font library) and kitty do. */
static int emoji(unsigned a, unsigned b)
{
	for (; a < b; a++) {
		unsigned c = cp[a];
		size_t lo = 0, hi = sizeof pres / sizeof *pres;
		while (lo < hi) {
			size_t m = lo + (hi - lo) / 2;
			if (c > pres[m][1]) lo = m + 1; else hi = m;
		}
		if (c == 0xFE0F || (lo < sizeof pres / sizeof *pres && c >= pres[lo][0])) return 1;
	}
	return 0;
}

/* The first font after `after` in fontconfig's order that has every
   character of cp[a, b), or -1: colour fonts only (pass 0), any font (pass
   1), or colour fonts first (both). */
static int pick(unsigned a, unsigned b, int after, int from, int to)
{
	for (int pass = from; pass <= to; pass++)
		for (int i = after + 1; i < set->nfont; i++) {
			FcBool color;
			FcCharSet *cs;
			if (pass == 0 && (FcPatternGetBool(set->fonts[i], FC_COLOR, 0, &color) != FcResultMatch || !color)) continue;
			if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs) != FcResultMatch) continue;
			unsigned j = a;
			while (j < b && (ignorable(cp[j]) || FcCharSetHasChar(cs, cp[j]))) j++;
			if (j >= b && load(i)) return i;
		}
	return -1;
}

/* Shapes cp[a, b) with font fi and puts the glyphs at run[to], replacing
   `gone` entries there.  Positions are made relative to each cluster's
   start.  The characters are uni.h's, not HarfBuzz's own UTF-8 decoding,
   so clusters start where the rest of the editor says characters start
   (they differ on overlong forms such as C0 B2). */
static void shape(unsigned a, unsigned b, int fi, int to, int gone)
{
	hb_buffer_clear_contents(buf);
	hb_buffer_add_codepoints(buf, cp, (int)ncp, a, (int)(b - a));
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(face[fi].hb, buf, NULL, 0);
	unsigned k;
	hb_glyph_info_t *gi = hb_buffer_get_glyph_infos(buf, &k);
	hb_glyph_position_t *gp = hb_buffer_get_glyph_positions(buf, NULL);
	if (k > (unsigned)(RUN - (nrun - gone))) k = (unsigned)(RUN - (nrun - gone));
	memmove(&run[to + (int)k], &run[to + gone], (size_t)(nrun - to - gone) * sizeof *run);
	nrun += (int)k - gone;
	int pen = 0;
	for (unsigned i = 0; i < k; i++) {
		if (i > 0 && gi[i].cluster != gi[i - 1].cluster) pen = 0;
		run[to + (int)i] = (Shaped){ gi[i].cluster, fi, 0, gi[i].codepoint,
			pen + gp[i].x_offset, gp[i].y_offset };
		pen += gp[i].x_advance;
	}
}

/* Area average of a premultiplied BGRA bitmap into w by h pixels. */
static void scale(const unsigned char *src, int sw, int sh, int pitch, unsigned char *dst, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int x0 = x * sw / w, x1 = (x + 1) * sw / w, y0 = y * sh / h, y1 = (y + 1) * sh / h;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			for (int c = 0; c < 4; c++) {
				unsigned sum = 0;
				for (int v = y0; v < y1; v++)
					for (int u = x0; u < x1; u++) sum += src[v * pitch + u * 4 + c];
				dst[(y * w + x) * 4 + c] = (unsigned char)(sum / (unsigned)((x1 - x0) * (y1 - y0)));
			}
		}
}

/* 1/64 pixel to the nearest pixel (a shift of a negative value is
   implementation-defined, so divide), and a scaled pixel to the nearest. */
static int pixels(int v) { return v >= 0 ? (v + 32) / 64 : -((-v + 31) / 64); }
static int nearest(double v) { return (int)(v < 0 ? v - 0.5 : v + 0.5); }

/* The cached glyph, drawn on a miss; NULL when the cache is full. */
static const Glyph *glyph(int fi, unsigned gid)
{
	size_t h = ((size_t)fi * 2654435761u + gid * 40503u) & (SLOTS - 1);
	while (slot[h].font >= 0) {
		if (slot[h].font == fi && slot[h].gid == gid) { hits++; return &slot[h].g; }
		h = (h + 1) & (SLOTS - 1);
	}
	if (nslot >= SLOTS * 3 / 4) return NULL;
	Face *f = &face[fi];
	Glyph g = { arena + used, 0, 0, 0, 0, 0 };
	int color = f->color;
	if (!FT_Load_Glyph(f->ft, gid, color ? FT_LOAD_COLOR | FT_LOAD_RENDER
	                                     : FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP | FT_LOAD_RENDER)) {
		FT_GlyphSlot gs = f->ft->glyph;
		FT_Bitmap *bm = &gs->bitmap;
		int w = (int)bm->width, hh = (int)bm->rows;
		if (bm->pixel_mode == FT_PIXEL_MODE_GRAY && bm->pitch >= w) {
			if ((size_t)w * (size_t)hh > ARENA - used) return NULL;
			for (int y = 0; y < hh; y++) memcpy(arena + used + (size_t)(y * w), bm->buffer + y * bm->pitch, (size_t)w);
			g = (Glyph){ arena + used, w, hh, gs->bitmap_left, gs->bitmap_top, 0 };
		} else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA && w > 0 && hh > 0 && bm->pitch >= 4 * w) {
			int dw = nearest(w * f->scale), dh = nearest(hh * f->scale);
			if (dw < 1) dw = 1;
			if (dh < 1) dh = 1;
			if ((size_t)dw * (size_t)dh * 4 > ARENA - used) return NULL;
			scale(bm->buffer, w, hh, bm->pitch, arena + used, dw, dh);
			g = (Glyph){ arena + used, dw, dh, nearest(gs->bitmap_left * f->scale), nearest(gs->bitmap_top * f->scale), 1 };
		}
		used += (size_t)g.w * (size_t)g.h * (g.color ? 4 : 1);
	}
	misses++;
	nslot++;
	slot[h] = (Slot){ fi, gid, g };
	return &slot[h].g;
}

int font_shape(const unsigned char *s, size_t n, Placed *out, int max)
{
	if (!face || n == 0 || max <= 0) return 0;
	ncp = 0;
	for (size_t i = 0; i < n && ncp < RUN; i += uni_len(s, n, i)) {
		at[ncp] = i;
		cp[ncp++] = uni_cp(s, n, i);
	}
	nrun = 0;
	shape(0, ncp, 0, 0, 0);
	/* each cluster with a missing glyph goes to the next font that has all
	   its characters, until none is missing or no font is left; an emoji
	   not yet in colour goes to a colour font */
	for (int i = 0; i < nrun;) {
		int j = i, miss = 0;
		while (j < nrun && run[j].cluster == run[i].cluster) miss |= run[j++].gid == 0;
		unsigned end = j < nrun ? run[j].cluster : ncp;
		int want = !face[run[i].font].color && emoji(run[i].cluster, end);
		if (run[i].done || !(miss || want)) { i = j; continue; }
		int fi = pick(run[i].cluster, end, run[i].font, want ? 0 : 1, miss);
		if (fi < 0) { while (i < j) run[i++].done = 1; continue; }
		shape(run[i].cluster, end, fi, i, j - i);
	}
	static const Glyph none;
	int k = 0;
	for (int pass = 0; k < nrun && k < max; k++) {
		const Glyph *g = glyph(run[k].font, run[k].gid);
		if (!g && !pass++) { flush(); k = -1; continue; }   /* full: once more, from empty */
		out[k] = (Placed){ g ? g : &none, at[run[k].cluster], pixels(run[k].x), pixels(run[k].y) };
	}
	return k;
}
