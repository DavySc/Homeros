/* Package 12's gate, "frame budget held with tree-sitter on" (DESIGN.md: a
   frame ready under 2 ms p99 after a key).  Types into a file through the
   thin editor's keys, core and grid, as src/main.c's loop does: per key,
   keys_feed and core_draw (which asks syntax_faces for the visible lines);
   between keys, syntax_update, stopped by a key that arrives at a random
   moment.  What syntax adds to a key's latency is: the time the parse takes
   to return after the key arrived, syntax_edit inside the key, and
   syntax_faces inside the frame.  The last two are timed by wrapping them
   (ld --wrap), since the thin editor's own core is far slower than package
   4's will be and would hide them; so are text.h's line lookups made inside
   them (O(log n) in package 1's rope), so the cost is also given without
   them.  run.sh builds and runs it.
   usage: frame FILE KEYS SEED */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "grid.h"
#include "syntax.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double in_syntax, in_edit, in_text; /* ms in syntax_edit and syntax_faces; in syntax_edit;
                                            in text.h's line lookups called by them */
static int inside;

size_t __real_text_line_of(const Text *t, size_t pos);
size_t __wrap_text_line_of(const Text *t, size_t pos)
{
	double t0 = inside ? now() : 0;
	size_t r = __real_text_line_of(t, pos);
	if (inside) in_text += now() - t0;
	return r;
}

size_t __real_text_line_start(const Text *t, size_t l);
size_t __wrap_text_line_start(const Text *t, size_t l)
{
	double t0 = inside ? now() : 0;
	size_t r = __real_text_line_start(t, l);
	if (inside) in_text += now() - t0;
	return r;
}

void __real_syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins);
void __wrap_syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	double t0 = now();
	inside = 1;
	__real_syntax_edit(s, t, pos, ndel, ins, nins);
	inside = 0;
	in_syntax += now() - t0;
	in_edit += now() - t0;
}

void __real_syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face);
void __wrap_syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face)
{
	double t0 = now();
	inside = 1;
	__real_syntax_faces(s, a, z, face);
	inside = 0;
	in_syntax += now() - t0;
}

static double key_at;           /* when the next key "arrives" */
static int key_waiting(void) { return now() >= key_at; }

static unsigned long long rng = 88172645463325252ULL;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	if (argc != 4) { fprintf(stderr, "usage: frame FILE KEYS SEED\n"); return 2; }
	int nk = atoi(argv[2]);
	rng = 2 * strtoull(argv[3], NULL, 10) + 1;
	static Editor e;
	Grid g = { 0 };
	if (core_open(&e, argv[1]) || grid_resize(&g, 50, 200)) return 1;
	double *add = malloc((size_t)nk * sizeof *add), *ed = malloc((size_t)nk * sizeof *ed), stopmax = 0;
	double *own = malloc((size_t)nk * sizeof *own);
	if (!add || !ed || !own) return 1;
	/* start-up, not measured: the first frame, the parse, the frame with
	   colours */
	core_draw(&e, &g);
	while (syntax_update(e.buf->syn, e.buf->text, NULL) == 2) ;
	core_draw(&e, &g);
	/* go to a random line, then type a short statement, Esc, j, again */
	char go[32];
	snprintf(go, sizeof go, "%uG", 1 + rnd((unsigned)core_lines(&e)));
	for (char *k = go; *k; k++) keys_feed(&e, *k, 0);
	static const char *const ty = "ox = foo(a, b); /* c */\x1bj";
	int pending = 1, parses = 0, stopped = 0;
	for (int i = 0; i < nk; i++) {
		/* the next key comes 0 to 40 ms after the last frame */
		key_at = now() + rnd(40000) / 1000.0;
		double arrived = key_at, back = arrived;
		while (pending && now() < key_at) {
			int r = syntax_update(e.buf->syn, e.buf->text, key_waiting);
			back = now();
			pending = r == 2;
			stopped += pending;
			parses += r == 1;
		}
		double late = back > arrived ? back - arrived : 0;
		if (late > stopmax) stopmax = late;
		while (now() < key_at) ;   /* idle until the key */
		in_syntax = in_edit = in_text = 0;
		keys_feed(&e, (unsigned char)ty[i % (int)strlen(ty)], 0);
		core_draw(&e, &g);
		add[i] = late + in_syntax;
		ed[i] = in_edit;
		own[i] = add[i] - in_text;
		pending = e.buf->syn != NULL;
	}
	qsort(add, (size_t)nk, sizeof *add, cmp);
	qsort(ed, (size_t)nk, sizeof *ed, cmp);
	qsort(own, (size_t)nk, sizeof *own, cmp);
	printf("%s: %d keys; syntax adds to a key: median %.3f ms, p99 %.3f, max %.3f; without text.h's "
	       "line lookups: median %.3f, p99 %.3f, max %.3f; syntax_edit alone: median %.3f, p99 %.3f; "
	       "parse back at most %.3f ms after a key; %d parses finished, %d stopped by a key\n",
	       argv[1], nk, add[nk / 2], add[nk * 99 / 100], add[nk - 1], own[nk / 2], own[nk * 99 / 100],
	       own[nk - 1], ed[nk / 2], ed[nk * 99 / 100], stopmax, parses, stopped);
	core_free(&e);
	grid_free(&g);
	return 0;
}
