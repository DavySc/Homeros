/* Fonts and shaping, as foot does with fcft and WezTerm on every system:
   the system's finder (find.h) gives the fonts, FreeType draws a glyph,
   HarfBuzz shapes a run.  Fallback copies wezterm's rule (shaper.rs): shape
   the run with the first font, then reshape each cluster that came out with
   a missing glyph in the first later font, in the finder's order, that has
   all of its characters.  Rows are drawn left
   to right as Neovim draws them (no bidi), so the run is shaped LTR and its
   clusters come out in order. */
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include "find.h"
#include "font.h"
#include "uni.h"

enum {
	RUN = 2048,            /* glyphs one run can shape to */
	SLOTS = 8192,          /* glyph cache entries, a power of two */
	ARENA = 8 << 20,       /* glyph bitmaps; pages count only once touched */
	PICKS = 1024,          /* fallback answers remembered, a power of two */
	SPRITE = 1 << 30,      /* the font number of sprite.c's glyphs; their glyph is the character */
	SIZES = 4,             /* sizes open at once (frames on monitors of 4 scales) */
};

typedef struct {
	FT_Face ft;
	hb_blob_t *blob;       /* the file, which FreeType reads from too */
	hb_font_t *hb;
	int state;             /* 0 not loaded, 1 loaded, -1 cannot be used */
	int color;
	double scale;          /* bitmap strike to the em asked for (emoji) */
} Face;

typedef struct { int font; unsigned gid; Glyph g; } Slot;          /* font -1: empty */
typedef struct { unsigned cluster; int font, done; unsigned gid; int x, y; } Shaped;   /* cluster: index in cp */
typedef struct { unsigned long long key; int font; } Pick;          /* key 0: empty */

/* The finder and the shaping buffer serve every size: the fonts a family
   falls back to do not depend on the size (fontconfig sorts for the first
   size asked; DirectWrite ignores it). */
static FT_Library lib;
static int nfont;
static hb_buffer_t *buf;

/* An open size.  A window per tab page (frames) may sit on monitors of
   different scales, so a few sizes stay open: drawing them in turn would
   otherwise reopen the font each time (5.2 to 6.2 ms, 12 runs, measured)
   and empty the glyph cache.  Fallback answers are per size, as a font
   is loaded per size. */
typedef struct {
	Face *face;
	Slot *slot;
	Pick *picks;
	unsigned char *arena;
	size_t used, nslot;
	unsigned npick;
	unsigned long hits, misses;
	int px, cw, ch, base, thin;   /* thin: the underline's thickness, sprite.c's light line */
} Size;
static Size cur, old[SIZES - 1];   /* the size in use; the others open, last used first */
static Shaped run[RUN];
static int nrun;
static hb_codepoint_t cp[RUN];   /* the run's characters as uni.h decodes them */
static size_t at[RUN];           /* and where each starts */
static unsigned ncp;

static Face *load(int i)
{
	Face *f = &cur.face[i];
	if (f->state) return f->state > 0 ? f : NULL;
	f->state = -1;
	int idx;
	const char *file = find_file(i, &idx);
	/* HarfBuzz maps the file (by its wide name on Windows), FreeType reads the map */
	if (!file || !(f->blob = hb_blob_create_from_file_or_fail(file))) return NULL;
	unsigned len;
	const char *data = hb_blob_get_data(f->blob, &len);
	if (FT_New_Memory_Face(lib, (const FT_Byte *)data, (FT_Long)len, idx, &f->ft)) { f->ft = NULL; goto bad; }
	f->scale = 1;
	f->color = FT_HAS_COLOR(f->ft);
	if (FT_IS_SCALABLE(f->ft)) {
		if (FT_Set_Pixel_Sizes(f->ft, 0, (FT_UInt)cur.px)) goto bad;
	} else if (FT_HAS_COLOR(f->ft) && f->ft->num_fixed_sizes > 0) {
		/* a colour bitmap font (Noto Color Emoji): take the smallest strike
		   at least as big as the em, else the biggest, and scale it */
		int best = 0;
		for (int k = 1; k < f->ft->num_fixed_sizes; k++) {
			FT_Pos a = f->ft->available_sizes[k].y_ppem, b = f->ft->available_sizes[best].y_ppem;
			if (b < cur.px * 64 ? a > b : a >= cur.px * 64 && a < b) best = k;
		}
		if (FT_Select_Size(f->ft, best)) goto bad;
		f->scale = cur.px * 64.0 / (double)f->ft->available_sizes[best].y_ppem;
	} else goto bad;   /* a monochrome bitmap font: no size to draw it at */
	hb_face_t *hf = hb_face_create(f->blob, (unsigned)idx);
	f->hb = hb_font_create(hf);   /* HarfBuzz's own OpenType functions */
	hb_face_destroy(hf);
	hb_font_set_scale(f->hb, cur.px * 64, cur.px * 64);   /* positions in 1/64 pixel */
	f->state = 1;
	return f;
bad:
	if (f->ft) FT_Done_Face(f->ft);
	hb_blob_destroy(f->blob);
	f->ft = NULL;
	f->blob = NULL;
	return NULL;
}

static void flush(void)
{
	for (size_t i = 0; i < SLOTS; i++) cur.slot[i].font = -1;
	cur.used = cur.nslot = 0;
}

static void close_size(void)
{
	if (cur.face)
		for (int i = 0; i < nfont; i++)
			if (cur.face[i].state > 0) { hb_font_destroy(cur.face[i].hb); FT_Done_Face(cur.face[i].ft); hb_blob_destroy(cur.face[i].blob); }
	free(cur.face);
	free(cur.slot);
	free(cur.picks);
	free(cur.arena);
	cur = (Size){ 0 };
}

void font_close(void)
{
	close_size();
	for (int i = 0; i < SIZES - 1; i++) {
		cur = old[i];
		close_size();
		old[i] = cur;
	}
	nfont = 0;
	if (buf) hb_buffer_destroy(buf);
	buf = NULL;
	if (lib) FT_Done_FreeType(lib);
	lib = NULL;
	find_close();
}

int font_open(const char *family, int size)
{
	if (cur.face && cur.px == size) return 0;
	/* the size in use becomes the most recently used; the one asked for
	   comes back with its caches, else the least recently used closes */
	int i = 0;
	while (i < SIZES - 2 && !(old[i].face && old[i].px == size)) i++;
	Size t = old[i];
	memmove(old + 1, old, (size_t)i * sizeof *old);
	old[0] = cur;
	cur = t;
	if (cur.face && cur.px == size) return 0;
	close_size();
	cur.px = size;
	if (size <= 0) goto fail;
	if (!lib) {
		buf = hb_buffer_create();
		/* the longest run now, so a longer row later allocates nothing */
		if (FT_Init_FreeType(&lib) || (nfont = find_open(family, size)) <= 0 || !hb_buffer_pre_allocate(buf, RUN)) {
			font_close();
			return -1;
		}
	}
	cur.face = calloc((size_t)nfont, sizeof *cur.face);
	cur.slot = malloc(SLOTS * sizeof *cur.slot);
	cur.picks = calloc(PICKS, sizeof *cur.picks);
	cur.arena = malloc(ARENA);
	if (!cur.face || !cur.slot || !cur.picks || !cur.arena || !load(0)) goto fail;
	flush();
	FT_Face f = cur.face[0].ft;
	cur.base = (int)((f->size->metrics.ascender + 63) >> 6);
	cur.ch = cur.base + (int)((-f->size->metrics.descender + 63) >> 6);
	if (FT_Load_Char(f, 'M', FT_LOAD_TARGET_LIGHT)) goto fail;
	cur.cw = (int)((f->glyph->advance.x + 32) >> 6);
	cur.thin = (int)((FT_MulFix(f->underline_thickness, f->size->metrics.y_scale) + 32) >> 6);
	if (cur.thin < 1) cur.thin = 1;
	return 0;
fail:
	close_size();
	return -1;
}

void font_cell(int *w, int *h, int *baseline) { *w = cur.cw; *h = cur.ch; *baseline = cur.base; }

void font_stats(unsigned long *hit, unsigned long *miss) { *hit = cur.hits; *miss = cur.misses; }

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

/* The first font after `after` in the finder's order that has every
   character of cp[a, b) and loads, or -1: colour fonts only (pass 0), any
   font (pass 1), or colour fonts first (both).  Remembered, so a character
   costs the finder (DirectWrite on Windows) one look per size opened, not one
   per frame: by a 64-bit hash of the question, whose collisions (about
   n * n / 2^65 for n questions) are not checked. */
static int pick(unsigned a, unsigned b, int after, int from, int to)
{
	static unsigned c[RUN];
	unsigned n = 0;
	unsigned long long key = 14695981039346656037ULL;
	for (unsigned j = a; j < b; j++)
		if (!ignorable(cp[j])) key = (key ^ (c[n++] = cp[j])) * 1099511628211ULL;
	key = (((key ^ (unsigned)after) * 1099511628211ULL ^ (unsigned)(from * 2 + to)) * 1099511628211ULL) | 1;
	size_t h = key & (PICKS - 1);
	while (cur.picks[h].key && cur.picks[h].key != key) h = (h + 1) & (PICKS - 1);
	if (cur.picks[h].key) return cur.picks[h].font;
	int fi = -1;
	for (int pass = from; pass <= to && fi < 0; pass++)
		for (int i = after; (i = find_font(c, n, i, pass == 0)) >= 0;)
			if (load(i)) { fi = i; break; }
	if (cur.npick >= PICKS * 3 / 4) { memset(cur.picks, 0, PICKS * sizeof *cur.picks); cur.npick = 0; h = key & (PICKS - 1); }
	cur.npick++;
	cur.picks[h] = (Pick){ key, fi };
	return fi;
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
	hb_shape(cur.face[fi].hb, buf, NULL, 0);
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
	while (cur.slot[h].font >= 0) {
		if (cur.slot[h].font == fi && cur.slot[h].gid == gid) { cur.hits++; return &cur.slot[h].g; }
		h = (h + 1) & (SLOTS - 1);
	}
	if (cur.nslot >= SLOTS * 3 / 4) return NULL;
	Glyph g = { cur.arena + cur.used, 0, 0, 0, 0, 0 };
	if (fi == SPRITE) {
		if ((size_t)cur.cw * (size_t)cur.ch > ARENA - cur.used) return NULL;
		sprite_draw(gid, cur.cw, cur.ch, cur.thin, cur.arena + cur.used);
		g = (Glyph){ cur.arena + cur.used, cur.cw, cur.ch, 0, cur.base, 0 };
		cur.used += (size_t)cur.cw * (size_t)cur.ch;
	} else if (!FT_Load_Glyph(cur.face[fi].ft, gid, cur.face[fi].color ? FT_LOAD_COLOR | FT_LOAD_RENDER
	                                     : FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP | FT_LOAD_RENDER)) {
		Face *f = &cur.face[fi];
		FT_GlyphSlot gs = f->ft->glyph;
		FT_Bitmap *bm = &gs->bitmap;
		int w = (int)bm->width, hh = (int)bm->rows;
		if (bm->pixel_mode == FT_PIXEL_MODE_GRAY && bm->pitch >= w) {
			if ((size_t)w * (size_t)hh > ARENA - cur.used) return NULL;
			for (int y = 0; y < hh; y++) memcpy(cur.arena + cur.used + (size_t)(y * w), bm->buffer + y * bm->pitch, (size_t)w);
			g = (Glyph){ cur.arena + cur.used, w, hh, gs->bitmap_left, gs->bitmap_top, 0 };
		} else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA && w > 0 && hh > 0 && bm->pitch >= 4 * w) {
			int dw = nearest(w * f->scale), dh = nearest(hh * f->scale);
			if (dw < 1) dw = 1;
			if (dh < 1) dh = 1;
			if ((size_t)dw * (size_t)dh * 4 > ARENA - cur.used) return NULL;
			scale(bm->buffer, w, hh, bm->pitch, cur.arena + cur.used, dw, dh);
			g = (Glyph){ cur.arena + cur.used, dw, dh, nearest(gs->bitmap_left * f->scale), nearest(gs->bitmap_top * f->scale), 1 };
		}
		cur.used += (size_t)g.w * (size_t)g.h * (g.color ? 4 : 1);
	}
	cur.misses++;
	cur.nslot++;
	cur.slot[h] = (Slot){ fi, gid, g };
	return &cur.slot[h].g;
}

/* only the regular face is open yet: every style draws in it (font.h) */
void font_style(int style) { (void)style; }

int font_shape(const unsigned char *s, size_t n, Placed *out, int max)
{
	if (!cur.face || n == 0 || max <= 0) return 0;
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
		if (run[i].done) { i = j; continue; }
		if (j == i + 1 && end == run[i].cluster + 1 && sprite(cp[run[i].cluster])) {   /* drawn, not from a font */
			run[i] = (Shaped){ run[i].cluster, SPRITE, 1, cp[run[i].cluster], 0, 0 };
			i = j;
			continue;
		}
		int want = !cur.face[run[i].font].color && emoji(run[i].cluster, end);
		if (!(miss || want)) { i = j; continue; }
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
