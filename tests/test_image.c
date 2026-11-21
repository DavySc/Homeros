/* Inline images (src/image/): random PNGs (written by stb_image_write)
   against a model of what a window must show, the cache's keys and reuse,
   a JPEG (tests/test_image.jpg), and the LaTeX jobs with stand-in latex
   and dvipng on PATH that log their arguments. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "image.h"
#include "loop.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ASan's hook sees every allocation in the process (as test_font) */
int __sanitizer_install_malloc_and_free_hooks(void (*)(const volatile void *, size_t), void (*)(const volatile void *));
static unsigned long allocs;
static void on_malloc(const volatile void *p, size_t n) { (void)p; (void)n; allocs++; }
static void on_free(const volatile void *p) { (void)p; }

static char dir[] = "/tmp/imageXXXXXX", path[600];
static unsigned char rgba[64 * 64 * 4];

/* A random w x h RGBA PNG at path, alpha 0, 255 or between */
static void png(const char *p, int w, int h)
{
	for (int i = 0; i < w * h * 4; i++) rgba[i] = (unsigned char)rnd(256);
	for (int i = 3; i < w * h * 4; i += 4) rgba[i] = rnd(3) == 0 ? 0 : rnd(2) ? 255 : rgba[i];
	CHECK(stbi_write_png(p, w, h, 4, rgba, w * 4));
}

/* What the window shows at pixel (x, y) of an unscaled w x h image over bg:
   each channel c * a + bg * (255 - a), over 255, rounded; bg past its edge */
static uint32_t model(int w, int h, uint32_t bg, int x, int y)
{
	if (x >= w || y >= h) return bg;
	const unsigned char *q = rgba + (y * w + x) * 4;
	uint32_t v = 0;
	for (int c = 0; c < 3; c++) v |= (uint32_t)((q[c] * q[3] + (bg >> (16 - 8 * c) & 0xff) * (255 - q[3]) + 127) / 255) << (16 - 8 * c);
	return v;
}

/* Puts image n on a grid of rows x cols at (r, c), skip rows off, and draws
   every cell into px (a cols * cw wide window) as a backend does: cells
   with ATTR_IMAGE through image_draw, the rest left as 1 */
static uint32_t win[200 * 200];
static Grid g;
static void show(int n, int rows, int cols, int r, int c, int skip, int cw, int ch)
{
	CHECK(grid_resize(&g, rows, cols) == 0);
	image_put(&g, r, c, n, skip);
	for (int i = 0; i < rows * cols * cw * ch; i++) win[i] = 1;
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < cols; x++)
			if (g.c[y * cols + x].attr == ATTR_IMAGE) image_draw(win + y * ch * cols * cw + x * cw, cols * cw, &g.c[y * cols + x]);
}

static void random_pngs(void)
{
	for (int t = 0; t < 400; t++) {
		int w = 1 + (int)rnd(40), h = 1 + (int)rnd(40), cw = 1 + (int)rnd(9), ch = 1 + (int)rnd(12);
		uint32_t bg = rnd(1u << 24);
		snprintf(path, sizeof path, "%s/r%d.png", dir, t);
		png(path, w, h);
		int tc = (w + cw - 1) / cw, tr = (h + ch - 1) / ch, n = image_load(path, tc + (int)rnd(3), tr + (int)rnd(3), cw, ch, bg);
		int cols, rows;
		CHECK(n >= 1 && n <= 255);
		image_size(n, &cols, &rows);
		CHECK(cols == tc && rows == tr);
		/* the whole image on a grid it fits in, then cut by the grid's edges and a scroll */
		int r = (int)rnd(3), c = (int)rnd(3), skip = (int)rnd((unsigned)tr + 1), gr = 1 + (int)rnd(12), gc = 1 + (int)rnd(12);
		if (gr * ch > 200 || gc * cw > 200) continue;
		show(n, gr, gc, r, c, skip, cw, ch);
		for (int y = 0; y < gr * ch; y++)
			for (int x = 0; x < gc * cw; x++) {
				int iy = y - r * ch + skip * ch, ix = x - c * cw, in = y >= r * ch && iy < tr * ch && ix >= 0 && ix < tc * cw;
				CHECK(win[y * gc * cw + x] == (in ? model(w, h, bg, ix, iy) : 1));
			}
	}
}

/* A frame with an image allocates nothing: the lookup that finds it, putting
   it on the grid, drawing each of its cells */
static void frame(void)
{
	snprintf(path, sizeof path, "%s/f.png", dir);
	png(path, 30, 30);
	CHECK(image_load(path, 5, 5, 8, 8, 0) > 0 && grid_resize(&g, 8, 8) == 0);
	CHECK(__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free));
	int n = image_load(path, 5, 5, 8, 8, 0);
	image_put(&g, 1, 1, n, 0);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			if (g.c[y * 8 + x].attr == ATTR_IMAGE) image_draw(win + y * 8 * 64 + x * 8, 64, &g.c[y * 8 + x]);
	CHECK(n > 0 && allocs == 0 && win[8 * 64 + 8] == model(30, 30, 0, 0, 0));
}

/* The same file, size and bg: the same number, without reading the file:
   new bytes of the same size under the old mtime still show the old
   pixels.  Another mtime, size, box, cell or bg: read again. */
static void keys(void)
{
	snprintf(path, sizeof path, "%s/k.png", dir);
	png(path, 8, 8);
	int n = image_load(path, 4, 4, 2, 2, 0);
	CHECK(n > 0 && image_load(path, 4, 4, 2, 2, 0) == n);
	show(n, 4, 4, 0, 0, 0, 2, 2);
	uint32_t before = win[0];
	struct stat st;
	CHECK(stat(path, &st) == 0);
	unsigned char keep = rgba[0];
	FILE *f = fopen(path, "r+");
	CHECK(f && fseek(f, 0, SEEK_END) == 0);
	long size = ftell(f);
	rewind(f);
	for (long i = 0; i < size; i++) fputc('x', f);   /* no longer a PNG */
	fclose(f);
	struct timespec ts[2] = { st.st_atim, st.st_mtim };
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	CHECK(image_load(path, 4, 4, 2, 2, 0) == n);
	show(n, 4, 4, 0, 0, 0, 2, 2);
	CHECK(win[0] == before && rgba[0] == keep);
	CHECK(image_load(path, 5, 4, 2, 2, 0) == 0);     /* another box: read, no longer decodes */
	CHECK(image_load(path, 4, 4, 2, 2, 1) == 0);     /* another bg */
	CHECK(image_load(path, 4, 4, 2, 3, 0) == 0);     /* another cell height */
	CHECK(image_load(path, 4, 4, 3, 2, 0) == 0);     /* another cell width */
	CHECK(image_load(path, 4, 5, 2, 2, 0) == 0);     /* another box height */
	char twin[700];                                  /* another path, same bytes and mtime as before */
	snprintf(twin, sizeof twin, "%s/twin.png", dir);
	png(twin, 8, 8);
	CHECK(utimensat(AT_FDCWD, twin, ts, 0) == 0 && truncate(twin, size) == 0);
	CHECK(stat(twin, &st) == 0 && st.st_size == size);
	CHECK(utimensat(AT_FDCWD, twin, ts, 0) == 0);
	int t2 = image_load(twin, 4, 4, 2, 2, 0);
	CHECK(t2 != n);
	/* the same mtime, another size: read again (no longer decodes) */
	f = fopen(path, "a");
	fputc('x', f);
	fclose(f);
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	CHECK(image_load(path, 4, 4, 2, 2, 0) == 0);
	png(path, 8, 8);                                 /* a new PNG, new mtime */
	ts[1].tv_nsec = (ts[1].tv_nsec + 1) % 1000000000;
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	int m = image_load(path, 4, 4, 2, 2, 0);
	CHECK(m > 0);
	show(m, 4, 4, 0, 0, 0, 2, 2);
	CHECK(win[0] == model(8, 8, 0, 0, 0));
	snprintf(path, sizeof path, "%s/none.png", dir);
	CHECK(image_load(path, 4, 4, 2, 2, 0) == 0);     /* missing */
}

/* A file that does not decode is remembered with its mtime: a valid PNG
   put there under the same mtime and size is not read */
static void bad(void)
{
	snprintf(path, sizeof path, "%s/bad.png", dir);
	png(path, 3, 3);
	struct stat st;
	CHECK(stat(path, &st) == 0);
	FILE *f = fopen(path, "r+");
	fputc('x', f);
	fclose(f);
	struct timespec ts[2] = { st.st_atim, st.st_mtim };
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	CHECK(image_load(path, 4, 4, 2, 2, 0) == 0);
	png(path, 3, 3);
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	CHECK(image_load(path, 4, 4, 2, 2, 0) == 0);
	ts[1].tv_sec++;
	CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
	CHECK(image_load(path, 4, 4, 2, 2, 0) > 0);
}

/* Bigger than the box: scaled down to fit it, touching it, the aspect kept
   to a pixel; never scaled up.  The image is opaque red, so its drawn
   extent over black is its size.  A JPEG decodes. */
static void scale_and_jpeg(void)
{
	snprintf(path, sizeof path, "%s/big.png", dir);
	for (int t = 0; t < 200; t++) {
		int w = 1 + (int)rnd(64), h = 1 + (int)rnd(64), cw = 1 + (int)rnd(8), ch = 1 + (int)rnd(8);
		int bc = 1 + (int)rnd(8), br = 1 + (int)rnd(8), cols, rows, W = 0, H = 0;
		for (int i = 0; i < w * h; i++) memcpy(rgba + 4 * i, "\xff\0\0\xff", 4);
		CHECK(stbi_write_png(path, w, h, 4, rgba, w * 4));
		int n = image_load(path, bc, br, cw, ch, 0);
		CHECK(n > 0);
		image_size(n, &cols, &rows);
		CHECK(cols <= bc && rows <= br);
		show(n, br, bc, 0, 0, 0, cw, ch);
		while (W < bc * cw && win[W] != 0 && win[W] != 1) W++;
		while (H < br * ch && win[H * bc * cw] != 0 && win[H * bc * cw] != 1) H++;
		CHECK(cols == (W + cw - 1) / cw && rows == (H + ch - 1) / ch);
		if (w <= bc * cw && h <= br * ch) CHECK(W == w && H == h);
		else CHECK((W == bc * cw || H == br * ch) && abs(W * h - H * w) < (w > h ? w : h));
	}
	/* stb_image_write's JPEG writer is not clean under UBSan: a fixture,
	   16 x 16 of grey 200 (ImageMagick, quality 100) */
	int n = image_load("tests/test_image.jpg", 4, 4, 4, 4, 0), cols, rows;
	image_size(n, &cols, &rows);
	CHECK(n > 0 && cols == 4 && rows == 4);
	show(n, 4, 4, 0, 0, 0, 4, 4);
	for (int i = 0; i < 16 * 16; i++)
		for (int c = 0; c < 24; c += 8) CHECK((win[i] >> c & 0xff) >= 196 && (win[i] >> c & 0xff) <= 204);
}

/* Slots: past 255 images the least recently used is reused, and the cells
   that showed it no longer draw it (its generation changed) */
static int fresh(int t)
{
	snprintf(path, sizeof path, "%s/u%d.png", dir, t);
	png(path, 2, 2);
	int n = image_load(path, 1, 1, 2, 2, 0);
	CHECK(n >= 1 && n <= 255);
	return n;
}
static int draws(const Cell *x)
{
	uint32_t px[4] = { 1, 1, 1, 1 };
	image_draw(px, 2, x);
	return px[0] != 1;
}
static void reuse(void)
{
	char a[600];
	int first = fresh(0);
	strcpy(a, path);
	CHECK(grid_resize(&g, 1, 1) == 0);
	image_put(&g, 0, 0, first, 0);
	Cell old = g.c[0];
	for (int t = 1; t < 255; t++) fresh(t);
	CHECK(draws(&old) && image_load(a, 1, 1, 2, 2, 0) == first);   /* now not the oldest */
	CHECK(fresh(255) != first && draws(&old));
	int seen = 0;
	for (int t = 256; t < 600; t++) seen |= fresh(t) == first;
	CHECK(seen && !draws(&old));
}

/* The budget: four 4000 x 4000 images (64 MB of pixels each, one file
   under four backgrounds) are kept; a fifth makes room by dropping the
   least recently used, though slots are left */
static void budget(void)
{
	static unsigned char red[4000 * 4 * 4000];
	for (size_t i = 0; i < sizeof red; i += 4) memcpy(red + i, "\xff\0\0\xff", 4);
	snprintf(path, sizeof path, "%s/huge.png", dir);
	CHECK(stbi_write_png(path, 4000, 4000, 4, red, 4000 * 4));
	Cell first = { 0 };
	for (unsigned bg = 1; bg <= 5; bg++) {
		int n = image_load(path, 250, 250, 16, 16, bg);
		CHECK(n > 0);
		if (bg == 1) {
			CHECK(grid_resize(&g, 1, 1) == 0);
			image_put(&g, 0, 0, n, 0);
			first = g.c[0];
		}
		uint32_t px[16 * 16] = { 1 };
		image_draw(px, 16, &first);
		CHECK((px[0] == 0xff0000) == (bg < 5));
	}
}

/* latex and dvipng stand-ins: each logs its arguments; latex reads its
   input to the end (TeX asks there on an error), keeps the .tex as
   last.tex, fails on a fragment with "FAIL", never ends on one with
   "SPIN" (its pid in spin.pid) and makes the .dvi; dvipng writes the PNG
   at -o */
static void stand_ins(void)
{
	char f[700];
	snprintf(f, sizeof f, "%s/bin/latex", dir);
	mkdir(strcat(strcpy(path, dir), "/bin"), 0700);
	FILE *o = fopen(f, "w");
	fprintf(o, "#!/bin/sh\ncat >/dev/null\necho latex \"$@\" >> %s/log\n"
		"for a; do t=$a; done\ncp \"$t\" %s/last.tex\ngrep -q FAIL \"$t\" && exit 1\n"
		"grep -q SPIN \"$t\" && echo $$ > %s/spin.pid && exec sleep 1000\n"
		"cp \"$t\" \"${t%%.tex}.dvi\"\ntouch \"${t%%.tex}.aux\" \"${t%%.tex}.log\"\n", dir, dir, dir);
	fclose(o);
	chmod(f, 0700);
	snprintf(f, sizeof f, "%s/bin/dvipng", dir);
	o = fopen(f, "w");
	fprintf(o, "#!/bin/sh\necho dvipng \"$@\" >> %s/log\n"
		"while [ \"$1\" != -o ]; do shift; done\ncp %s/k.png \"$2\"\n", dir, dir);
	fclose(o);
	chmod(f, 0700);
	char p[1200];
	snprintf(p, sizeof p, "%s/bin:%s", dir, getenv("PATH"));
	setenv("PATH", p, 1);
}

static long long ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* Runs the loop until core_changed */
static void wait_done(void)
{
	long long end = ms() + 15000;   /* past latex.c's TIMEOUT */
	for (core_changed = 0; !core_changed;) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
}

static int lines(void)
{
	char f[700];
	snprintf(f, sizeof f, "%s/log", dir);
	FILE *o = fopen(f, "r");
	int n = 0;
	for (int c; o && (c = fgetc(o)) != EOF;) n += c == '\n';
	if (o) fclose(o);
	return n;
}

static void latex(void)
{
	char out[64], d[400], *s;
	stand_ins();
	snprintf(out, sizeof out, "%s/ltx/sub", dir);   /* made on first use */
	const char *tex = "$a \\le b$";
	CHECK(image_latex(tex, strlen(tex), 0xcdd6f4, 140, out) == NULL);
	CHECK(image_latex(tex, strlen(tex), 0xcdd6f4, 140, out) == NULL);   /* rendering: not started twice */
	wait_done();
	CHECK(lines() == 2);
	s = (char *)image_latex(tex, strlen(tex), 0xcdd6f4, 140, out);
	CHECK(s && strstr(s, out) == s && strstr(s, "/ltximg-") && image_load(s, 9, 9, 2, 2, 0) > 0);
	CHECK(lines() == 2);                       /* done: never again */
	snprintf(d, sizeof d, "%s/log", dir);
	FILE *o = fopen(d, "r");
	char l[1000], m[1000];
	CHECK(fgets(l, sizeof l, o) && fgets(m, sizeof m, o));
	fclose(o);
	CHECK(strstr(l, "latex -interaction=nonstopmode -halt-on-error ltximg-") == l);
	CHECK(strstr(m, "dvipng -q -D 140 -T tight -bg Transparent -fg rgb 0.804 0.839 0.957 -o "));
	/* nothing but the PNG left */
	snprintf(d, sizeof d, "ls %s | wc -l | grep -qx 1", out);
	CHECK(system(d) == 0);
	/* another colour or dpi is another render */
	CHECK(image_latex(tex, strlen(tex), 0xcdd6f5, 140, out) == NULL);
	wait_done();
	CHECK(image_latex(tex, strlen(tex), 0xcdd6f4, 141, out) == NULL);
	wait_done();
	CHECK(lines() == 6);
	/* a fragment that fails is not tried again, and leaves nothing */
	const char *bad = "FAIL";
	CHECK(image_latex(bad, 4, 0, 140, out) == NULL);
	wait_done();
	CHECK(image_latex(bad, 4, 0, 140, out) == NULL);
	CHECK(lines() == 7);
	snprintf(d, sizeof d, "ls %s | wc -l | grep -qx 3", out);
	CHECK(system(d) == 0);
	/* a dir relative to the working directory (Org's ltximg/ next to the note) */
	char cwd[600];
	CHECK(getcwd(cwd, sizeof cwd) && chdir(dir) == 0);
	CHECK(image_latex(tex, strlen(tex), 1, 140, "ltx/rel") == NULL);
	wait_done();
	CHECK((s = (char *)image_latex(tex, strlen(tex), 1, 140, "ltx/rel")) && image_load(s, 9, 9, 2, 2, 0) > 0);
	CHECK(chdir(cwd) == 0);
	/* the fragment's own bytes: n, not up to a NUL */
	CHECK(image_latex("$x$ and more", 3, 0, 140, out) == NULL);
	wait_done();
	snprintf(d, sizeof d, "grep -q '^\\$x\\$$' %s/last.tex", dir);
	CHECK(system(d) == 0);
	/* a screen of failing fragments (no latex): each tried once, however
	   many, then no more jobs however often they are asked for */
	int before = lines();
	char frag[100][24];
	for (int i = 0; i < 100; i++) snprintf(frag[i], sizeof frag[i], "FAIL %d", i);
	for (int round = 0; round < 1000 && lines() < before + 100; round++) {
		for (int i = 0; i < 100; i++) CHECK(image_latex(frag[i], strlen(frag[i]), 0, 140, out) == NULL);
		CHECK(loop_once(100) == 0);
	}
	CHECK(lines() == before + 100);
	for (int round = 0; round < 20; round++) {
		for (int i = 0; i < 100; i++) CHECK(image_latex(frag[i], strlen(frag[i]), 0, 140, out) == NULL);
		CHECK(loop_once(10) == 0);
	}
	CHECK(lines() == before + 100);
	/* a fragment TeX never finishes is killed and not tried again */
	CHECK(image_latex("SPIN", 4, 0, 140, out) == NULL);
	long long t0 = ms();
	wait_done();
	CHECK(ms() - t0 >= 9000);
	snprintf(d, sizeof d, "%s/spin.pid", dir);
	FILE *sp = fopen(d, "r");
	int pid = 0;
	CHECK(sp && fscanf(sp, "%d", &pid) == 1 && pid > 0);
	fclose(sp);
	for (long long end = ms() + 3000; kill(pid, 0) == 0;) { CHECK(ms() < end); CHECK(loop_once(100) == 0); }
	CHECK(image_latex("SPIN", 4, 0, 140, out) == NULL && lines() == before + 101);
}

int main(void)
{
	CHECK(mkdtemp(dir));
	random_pngs();
	frame();
	keys();
	bad();
	scale_and_jpeg();
	reuse();
	budget();
	latex();
	char rm[700];
	snprintf(rm, sizeof rm, "rm -rf %s", dir);
	CHECK(system(rm) == 0);
	grid_free(&g);
	printf("test_image: ok\n");
	return 0;
}
