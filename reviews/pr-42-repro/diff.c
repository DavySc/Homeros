/* Review of PR 42: are the faces a frame shows after edits the faces a full
   recompute over the same (edited, not yet parsed) trees gives?  That full
   recompute is what main did in every frame after a key (syntax_edit
   emptied the cache).  Includes syntax.c to reach the cache: each frame is
   drawn twice from the same state, once as is and once with the cache
   emptied, and the two must agree byte for byte.
   usage: diff FILE SEED STEPS [ROWS] [VERBOSE]   (DUMP=step DUMPTO=file writes the text before a step) */
#include "../../src/syntax/syntax.c"
#include <stdio.h>

static unsigned long long rng;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }

static const char *const tok[3][24] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", " ", "\xc3\xa9", "#define A(x) x\n", "int", "FOO", ";",
	  "//", "#if 0\n", "#endif\n", "\\", "NULL", "return", "printf(\"%d\", 1);", "char" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", " ", "e", "local", "function", "end", "--- @param", "vim.cmd",
	  "s:find(\"%d+\")", "--[[", "ffi.cdef[[int x;]]", "require", "self", "X", ".", ":" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "_", "`", "[", "](", ")", "- ", "| a |", "\n\n", "x", "> ",
	  "<!--", "-->", "    ", "~~~\n", "---\n", "[x]: http://a\n", "![", " " },
};

static int left;
static int stop(void) { return left > 0 && --left == 0; }

static Text *t;
static Syntax *s;
static size_t top;                 /* first visible line */
static unsigned rows = 50;
static unsigned char F[1 << 17], G[1 << 17];
static size_t wa, wz;

/* a frame as core_draw asks: line by line, at most 1,024 bytes of each */
static void frame(unsigned char *out)
{
	size_t nl = text_lines(t);
	wa = text_line_start(t, top);
	wz = wa;
	for (size_t l = top; l < top + rows && l < nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		if (z - a > 1024) z = a + 1024;
		if (z - wa > sizeof F) break;
		memset(out + (wz - wa), 0, a - wz);
		syntax_faces(s, a, z, out + (a - wa));
		wz = z;
	}
}

/* every capture over byte x, in the order a query over [a, z) gives them */
static void explain(LTree *L, uint32_t x, uint32_t a, uint32_t z)
{
	Q *Q = &queries[L->lang].hl;
	for (uint32_t ri = 0; Q->q && ri < L->nreg; ri++) {
		Region *g = &L->reg[ri];
		if (!g->tree) continue;
		ts_query_cursor_set_byte_range(s->qc, a, z);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(g->tree));
		Src src = { t, 0, 0, { 0 } };
		TSQueryMatch m;
		uint32_t ci, l;
		while (ts_query_cursor_next_capture(s->qc, &m, &ci)) {
			TSNode n = m.captures[ci].node;
			if (ts_node_start_byte(n) > x || ts_node_end_byte(n) <= x) continue;
			const char *nm = ts_query_capture_name_for_id(Q->q, m.captures[ci].index, &l);
			printf("      lang %d pattern %u match %u @%.*s %s [%u,%u) passes %d; match root captures:", L->lang, m.pattern_index, m.id, (int)l, nm,
			       ts_node_type(n), ts_node_start_byte(n), ts_node_end_byte(n), passes(&src, &m, &Q->pat[m.pattern_index]));
			for (uint16_t k = 0; k < m.capture_count; k++)
				printf(" %s[%u,%u)", ts_node_type(m.captures[k].node), ts_node_start_byte(m.captures[k].node), ts_node_end_byte(m.captures[k].node));
			printf("\n");
		}
	}
	for (uint32_t i = 0; i < L->nkid; i++) explain(L->kid[i], x, a, z);
}

/* is face f one that a capture over byte x gives, in a query over [a, z)?
   Then the two frames differ only in which of two captures of one priority
   came last (tree-sitter's order depends on where the cursor starts). */
static int among(LTree *L, uint32_t x, uint32_t a, uint32_t z, unsigned char f)
{
	Q *Q = &queries[L->lang].hl;
	for (uint32_t ri = 0; Q->q && ri < L->nreg; ri++) {
		Region *g = &L->reg[ri];
		if (!g->tree) continue;
		ts_query_cursor_set_byte_range(s->qc, a, z);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(g->tree));
		Src src = { t, 0, 0, { 0 } };
		TSQueryMatch m;
		uint32_t ci;
		while (ts_query_cursor_next_capture(s->qc, &m, &ci)) {
			TSNode n = m.captures[ci].node;
			if (ts_node_start_byte(n) > x || ts_node_end_byte(n) <= x) continue;
			if (Q->face[m.captures[ci].index] == f && passes(&src, &m, &Q->pat[m.pattern_index])) return 1;
		}
	}
	for (uint32_t i = 0; i < L->nkid; i++) if (among(L->kid[i], x, a, z, f)) return 1;
	return 0;
}

static char hist[16][96];
static unsigned nh;
static void note(const char *fmt, size_t a, size_t b, size_t c)
{
	snprintf(hist[nh++ % 16], sizeof hist[0], fmt, a, b, c);
}

static void edit(size_t pos, size_t del, const char *ins, size_t ni)
{
	size_t len = text_len(t);
	if (pos > len - 1) pos = len - 1;
	if (del > len - 1 - pos) del = len - 1 - pos;
	note("edit pos=%zu del=%zu ins=%zu", pos, del, ni);
	syntax_edit(s, t, pos, del, ins, ni);
	if (text_delete(t, pos, del) || text_insert(t, pos, ins, ni)) abort();
}

int main(int argc, char **argv)
{
	if (argc < 4) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	static char buf[1 << 22];
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	if (!n || buf[n - 1] != '\n') buf[n++] = '\n';
	rng = 88172645463325252ULL ^ ((unsigned long long)atoi(argv[2]) * 0x9E3779B97F4A7C15ULL);
	int steps = atoi(argv[3]), verbose = argc > 5;
	if (argc > 4) rows = (unsigned)atoi(argv[4]);
	size_t pl = strlen(argv[1]);
	int lang = argv[1][pl - 1] == 'd' ? 2 : argv[1][pl - 1] == 'a' ? 1 : 0;
	t = text_new(buf, n);
	s = syntax_open(argv[1], t);
	if (!t || !s) return 2;
	top = rnd((unsigned)text_lines(t));
	frame(F);
	while (syntax_update(s, t, NULL) == 2) ;
	long frames = 0, bad = 0, partial = 0, order = 0, badhit = 0;
	size_t cur = 0;
	static Syntax S, S2;
	for (int i = 0; i < steps; i++) {
		if (getenv("DUMP") && atoi(getenv("DUMP")) == i) {   /* the text before step i, for directed.c */
			static char d[1 << 22];
			size_t m = text_read(t, 0, d, sizeof d);
			FILE *o = fopen(getenv("DUMPTO"), "wb");
			fwrite(d, 1, m, o);
			fclose(o);
			fprintf(stderr, "dumped before step %d: top line %zu\n", i, top);
		}
		unsigned op = rnd(100);
		size_t len = text_len(t), nl = text_lines(t);
		wa = text_line_start(t, top);
		wz = text_line_start(t, top + rows < nl ? top + rows : nl);
		if (op < 30) {                    /* typing at a cursor in the window */
			if (cur < wa || cur >= wz || !rnd(8)) cur = wa + rnd((unsigned)(wz - wa ? wz - wa : 1));
			unsigned k = rnd(6);
			if (k == 0 && cur > wa) edit(--cur, 1, "", 0);
			else if (k == 1) edit(cur, 0, "\n", 1), cur++;
			else {
				char c = (char)(' ' + rnd(95));
				edit(cur, 0, &c, 1), cur++;
			}
		} else if (op < 60) {             /* a token, or a deletion, in the window */
			size_t pos = wa + rnd((unsigned)(wz - wa ? wz - wa : 1));
			if (rnd(3)) {
				const char *x = tok[lang][rnd(24)];
				edit(pos, rnd(4) ? 0 : rnd(8), x, strlen(x));
			} else edit(pos, rnd(4) ? 1 + rnd(8) : rnd(400), "", 0);
		} else if (op < 65) {             /* anywhere in the file */
			const char *x = tok[lang][rnd(24)];
			edit(rnd((unsigned)len), rnd(8), x, strlen(x));
		} else if (op < 72) {             /* several edits before one frame */
			for (unsigned k = 2 + rnd(4); k--; ) {
				const char *x = tok[lang][rnd(24)];
				size_t a = text_line_start(t, top), z = text_line_start(t, top + rows < text_lines(t) ? top + rows : text_lines(t));
				edit(a + rnd((unsigned)(z - a ? z - a : 1)), rnd(3), x, strlen(x));
			}
		} else if (op < 87) {             /* the parse, stopped or whole */
			left = rnd(3) ? (int)rnd(6) : 0;
			int r = syntax_update(s, t, left ? stop : NULL);
			note("update left=%zu r=%zu", (size_t)left, (size_t)r, 0);
		} else {                          /* scroll */
			long d = (long)rnd(2 * rows) - (long)rows;
			if (!rnd(4)) d = (long)rnd(7) - 3;
			long v = (long)top + d;
			top = v < 0 ? 0 : (size_t)v >= text_lines(t) ? text_lines(t) - 1 : (size_t)v;
			note("scroll top=%zu", top, 0, 0);
		}
		if (top >= text_lines(t)) top = text_lines(t) - 1;
		if (!started(s)) continue;
		memcpy(&S, s, sizeof S);
		int hit = s->cz && s->sa < s->sz;
		frame(F);
		memcpy(&S2, s, sizeof S);
		memcpy(s, &S, sizeof S);
		s->cz = 0;
		frame(G);
		memcpy(s, &S2, sizeof S);
		frames++;
		partial += hit;
		if (memcmp(F, G, wz - wa)) {
			bad++;
			badhit += hit;
			{
				size_t a = 0;
				while (F[a] == G[a]) a++;
				order += F[a] && among(s->root, (uint32_t)(wa + a), (uint32_t)wa, (uint32_t)wz, F[a]);
			}
			if (bad <= 3 || verbose) {
				size_t a = 0, z = wz - wa;
				while (F[a] == G[a]) a++;
				while (F[z - 1] == G[z - 1]) z--;
				printf("MISMATCH step %d: window [%zu,%zu) bytes [%zu,%zu) differ; cache [%u,%u) seeds were [%u,%u)\n",
				       i, wa, wz, wa + a, wa + z, S.ca, S.cz, S.sa, S.sz);
				for (unsigned k = 0; k < 16 && k < nh; k++) printf("   %s\n", hist[(nh - 1 - k) % 16]);
				char x[81];
				size_t b = wa + a > 30 ? wa + a - 30 : 0, m = text_read(t, b, x, 80);
				for (size_t k = 0; k < m; k++) if (x[k] == '\n') x[k] = '$';
				x[m] = 0;
				printf("   text from %zu: %s\n   shown/full:", b, x);
				for (size_t k = a; k < z && k < a + 24; k++) printf(" %d/%d", F[k], G[k]);
				printf("\n");
				if (verbose) {
					printf("   captures over byte %zu, query over the window:\n", wa + a);
					explain(s->root, (uint32_t)(wa + a), (uint32_t)wa, (uint32_t)wz);
					printf("   query over the seeds:\n");
					explain(s->root, (uint32_t)(wa + a), S.sa, S.sz);
					TSNode nd = ts_node_descendant_for_byte_range(ts_tree_root_node(s->root->reg[0].tree), (uint32_t)(wa + a), (uint32_t)(wa + a + 1));
					printf("   ancestors:");
					for (; !ts_node_is_null(nd); nd = ts_node_parent(nd)) printf(" %s[%u,%u)", ts_node_type(nd), ts_node_start_byte(nd), ts_node_end_byte(nd));
					printf("\n");
				}
			}
		}
	}
	printf("%s seed %s: %ld frames, %ld drawn from an edited cache, %ld differ from a full recompute (%ld of them by the order of two captures; %ld in a frame drawn from an edited cache)\n",
	       argv[1], argv[2], frames, partial, bad, order, badhit);
	syntax_free(s);
	text_free(t);
	return bad != 0;
}
