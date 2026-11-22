/* Inline images (image.h): a cache of decoded images, each kept as the
   pixels its cells show, so drawing a cell is a copy.  Slots are reused
   least recently used first; a slot's generation, in every cell that shows
   it, makes a reused slot's cells differ from what a backend last drew. */
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "image.h"
#include "stb_image.h"
#include "stb_image_resize.h"

/* 255: the number is a cell byte.  BUDGET: the bytes of pixels kept, a
   window-sized image is 8 MB at 1920 x 1080 */
enum { SLOTS = 255, BUDGET = 256 << 20 };

static struct slot {
	char *path;           /* NULL: free */
	FileId id;
	int mcols, mrows;     /* the space asked for */
	int cols, rows, cw, ch;
	uint32_t bg, *px;     /* cols * cw by rows * ch */
	unsigned char gen;
	unsigned long long used;
} slot[SLOTS + 1];
static size_t held;
static unsigned long long tick;

static void drop(struct slot *s)
{
	held -= (size_t)s->cols * (size_t)s->cw * (size_t)s->rows * (size_t)s->ch * 4;
	free(s->path);
	free(s->px);
	s->path = NULL;
	s->px = NULL;
	s->used = 0;
}

/* The least recently used slot, a free one first; only held ones if held_only */
static struct slot *oldest(int held_only)
{
	struct slot *o = NULL;
	for (struct slot *s = slot + 1; s <= slot + SLOTS; s++)
		if ((s->path || !held_only) && (!o || s->used < o->used)) o = s;
	return o;
}

int image_load(const char *path, int cols, int rows, int cw, int ch, uint32_t bg)
{
	FileId id;
	cols = cols < 255 ? cols : 255;   /* a tile's column and row are cell bytes */
	rows = rows < 255 ? rows : 255;
	if (cols < 1 || rows < 1 || cw < 1 || ch < 1 || file_id(path, &id) || !id.exists) return 0;
	for (struct slot *s = slot + 1; s <= slot + SLOTS; s++)
		if (s->path && s->id.mtime_ns == id.mtime_ns && s->id.size == id.size && s->mcols == cols
		    && s->mrows == rows && s->cw == cw && s->ch == ch && s->bg == bg && !strcmp(s->path, path)) {
			s->used = ++tick;
			return s->px ? (int)(s - slot) : 0;
		}
	/* a file that does not decode is remembered too, so it is not read
	   again at every lookup */
	char *data;
	size_t n;
	int w = 0, h = 0, k;
	FileId now;
	unsigned char *rgba = file_load(path, &data, &n, &now) || n > 0x7fffffff ? NULL
		: stbi_load_from_memory((unsigned char *)data, (int)n, &w, &h, &k, 4);
	free(data);
	/* fit the box, keeping the aspect (Org's and snacks.image's max size) */
	long long mw = (long long)cols * cw, mh = (long long)rows * ch, W = rgba ? w : 0, H = rgba ? h : 0;
	if (W > mw || H > mh) {
		if (W * mh > H * mw) { H = H * mw / W; W = mw; }
		else { W = W * mh / H; H = mh; }
		W += !W;
		H += !H;
	}
	unsigned char *fit = W == w && H == h ? rgba : malloc((size_t)W * (size_t)H * 4);
	if (fit != rgba && fit && !stbir_resize_uint8_srgb(rgba, w, h, 0, fit, (int)W, (int)H, 0, 4, 3, 0)) {
		free(fit);
		fit = NULL;
	}
	int tc = fit ? (int)((W + cw - 1) / cw) : 0, tr = fit ? (int)((H + ch - 1) / ch) : 0;
	size_t bytes = (size_t)tc * (size_t)cw * (size_t)tr * (size_t)ch * 4;
	for (struct slot *o; held + bytes > BUDGET && (o = oldest(1));) drop(o);
	struct slot *s = oldest(0);
	if (s->path) drop(s);
	uint32_t *px = fit ? malloc(bytes) : NULL;
	char *p = strdup(path);
	if (px) {   /* over bg, as the window shows it: drawing then only copies */
		int pw = tc * cw;
		for (long long y = 0; y < (long long)tr * ch; y++)
			for (long long x = 0; x < pw; x++) {
				uint32_t v = bg;
				if (x < W && y < H) {
					const unsigned char *q = fit + (y * W + x) * 4;
					v = 0;
					for (int c = 0; c < 3; c++)
						v |= (uint32_t)((q[c] * q[3] + (bg >> (16 - 8 * c) & 0xff) * (255 - q[3]) + 127) / 255) << (16 - 8 * c);
				}
				px[y * pw + x] = v;
			}
	}
	if (fit != rgba) free(fit);
	stbi_image_free(rgba);
	if (!p || (rgba && !px)) {   /* out of memory: not remembered */
		free(p);
		free(px);
		return 0;
	}
	*s = (struct slot){ p, id, cols, rows, px ? tc : 0, px ? tr : 0, cw, ch, bg, px, (unsigned char)(s->gen + 1), ++tick };
	held += bytes;
	return px ? (int)(s - slot) : 0;
}

unsigned char image_gen(int n) { return n > 0 && n <= SLOTS ? slot[n].gen : 0; }

void image_size(int n, int *cols, int *rows)
{
	int ok = n > 0 && n <= SLOTS && slot[n].px;
	*cols = ok ? slot[n].cols : 0;
	*rows = ok ? slot[n].rows : 0;
}

void image_put(Grid *g, int r, int c, int n, int skip)
{
	int cols, rows;
	image_size(n, &cols, &rows);
	for (int y = skip > 0 ? skip : 0; y < rows && r + y - skip < g->rows; y++)
		for (int x = 0; x < cols && c + x < g->cols; x++)
			if (r + y - skip >= 0 && c + x >= 0)
				g->c[(r + y - skip) * g->cols + c + x] = (Cell){ { (unsigned char)n, (unsigned char)x,
					(unsigned char)y, slot[n].gen }, 4, ATTR_IMAGE, FACE_NONE, 0 };
}

void image_draw(uint32_t *px, int stride, const Cell *x)
{
	const struct slot *s = &slot[x->ch[0]];
	if (!s->px || s->gen != x->ch[3] || x->ch[1] >= s->cols || x->ch[2] >= s->rows) return;
	int w = s->cols * s->cw;
	const uint32_t *p = s->px + (size_t)x->ch[2] * (size_t)s->ch * (size_t)w + (size_t)x->ch[1] * (size_t)s->cw;
	for (int y = 0; y < s->ch; y++) memcpy(px + (size_t)y * (size_t)stride, p + (size_t)y * (size_t)w, (size_t)s->cw * sizeof *px);
}
