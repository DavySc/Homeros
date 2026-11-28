/* Lean's colours against VS Code's (package 12): replays the cases that
   tests/test_syntax_lean.js recorded (slices of Lean's own sources and
   random text from its tokens, with the colour and font style VS Code
   gives each byte under Catppuccin Mocha) and compares each byte but the
   newlines, through the theme's faces[], Markdown in comments included:
   none may differ.  usage: test_syntax_lean [FILE] (default the .txt),
   -v prints each case's first difference.  Then a comment's line that
   Markdown's rules would read again from each position, which must stay
   within its budget. */
#include <string.h>
#include <time.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"

static char line[1 << 21], text[1 << 20];
static unsigned char got[1 << 20];

/* a face as the recorder writes VS Code's: fg in hex, then fontStyle's
   bits (1 italic, 2 bold, 4 underline, 8 strikethrough); no fg is the
   theme's text colour */
static void key(unsigned char f, char *k)
{
	const Face *c = &faces[f];
	int st = c->style, vs = (st & FACE_ITALIC ? 1 : 0) | (st & FACE_BOLD ? 2 : 0) | (st & FACE_UNDERLINE ? 4 : 0) | (st & FACE_STRIKE ? 8 : 0);
	if (st & FACE_FG) sprintf(k, "%02X%02X%02X/%d", c->fg[0], c->fg[1], c->fg[2], vs);
	else sprintf(k, "CDD6F4/%d", vs);
}

/* the faces of text[0, n) into got, from a fresh Syntax: the CPU time,
   least of 5 tries by the caller (a busy machine only adds time) */
static double read_ms(size_t n)
{
	clock_t c0 = clock();
	Text *t = text_new(text, n);
	Syntax *s = syntax_open("x.lean", t);
	size_t r1, r2;
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(s, 0, n, got);
	syntax_free(s), text_free(t);
	return (double)(clock() - c0) * 1e3 / CLOCKS_PER_SEC;
}

int main(int argc, char **argv)
{
	const char *path = "tests/test_syntax_lean.txt";
	int verbose = 0;
	for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1; else path = argv[i];
	FILE *in = fopen(path, "r");
	CHECK(in);
	static char pal[64][16];
	size_t n = 0, ncase = 0, bytes = 0, bad = 0, badcase = 0;
	while (fgets(line, sizeof line, in)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'P')
			for (char *s = strtok(line + 2, " "); s; s = strtok(NULL, " ")) snprintf(pal[s[0] - 'a'], sizeof *pal, "%s", s + 2);
		if (line[0] == 'T') {
			n = 0;
			for (const char *h = line + 2; h[0] && h[1]; h += 2) {
				unsigned b;
				CHECK(sscanf(h, "%2x", &b) == 1);
				text[n++] = (char)b;
			}
		}
		if (line[0] != 'C') continue;
		Text *t = text_new(text, n);
		Syntax *s = syntax_open("x.lean", t);
		CHECK(t && s);
		size_t r1, r2;
		while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
		memset(got, 0, n);
		/* line by line, as the core asks */
		for (size_t a = 0, e; a < n; a = e) {
			e = text_line_start(t, text_line_of(t, a) + 1);
			syntax_faces(s, a, e, got + a);
		}
		size_t i = 0, diff = 0;
		char k[16];
		for (char *r = strtok(line + 2, " "); r; r = strtok(NULL, " "))
			for (long m = strtol(r + 1, NULL, 10); m > 0; m--, i++) {
				while (i < n && text[i] == '\n') i++;
				CHECK(i < n);
				const char *w = pal[r[0] - 'a'];
				key(got[i], k);
				if (strcmp(k, w)) {
					if (verbose && !diff) fprintf(stderr, "case %zu byte %zu: %s, VS Code %s: %.40s\n", ncase, i, k, w, text + i);
					diff++;
				}
				bytes++;
			}
		bad += diff, badcase += diff > 0;
		ncase++;
		syntax_free(s);
		text_free(t);
	}
	fclose(in);
	/* A line past its budget passes on the state it found, as VS Code's
	   past its time limit, so the /- before its backticks does not deepen
	   the comment and x is code */
	n = (size_t)sprintf(text, "/--\n/- %1200s\n-/\nx\n", "");
	for (size_t i = 7; i < 1207; i++) text[i] = '`';
	CHECK(read_ms(n) >= 0 && got[n - 2] == FACE_NONE);
	/* Long lines that read again from each position: backticks in a
	   comment (the closer's search for each run's length), comments each
	   of 16 links' [ (each [ searches to the line's end), trace[ and def {
	   (each searches for its ] or }), short comments (each finds the
	   line's end) and a doc comment of blanks (each position's \s*).
	   Without the line's budget and the remembered runs, a second or more
	   at 19,900 bytes.  Each line 4 times as long must take less than 8
	   times as long, a ratio on one runner, whose speed cancels (a bound in
	   ms let a plant through on a fast runner, as Org's did, PR 127) */
	static const struct { const char *pre, *w; size_t len; } ln[] = {
		{ "/-- ", "`", 300 }, { "", "/-[a[a[a[a[a[a[a[a[a[a[a[a[a[a[a[a-/", 4975 }, { "", "trace[", 4975 },
		{ "", "def {", 4975 }, { "", "/-!-/", 4975 }, { "/--", " ", 4000 },
	};
	double worst = 0;
	for (size_t i = 0; i < sizeof ln / sizeof *ln; i++) {
		double t[2] = { 1e9, 1e9 };
		for (int k = 0; k < 10; k++) {
			n = (size_t)sprintf(text, "%s", ln[i].pre);
			for (size_t e = n + ln[i].len * (k % 2 ? 4 : 1); n < e; ) n += (size_t)sprintf(text + n, "%s", ln[i].w);
			text[n++] = '\n';
			double ms = read_ms(n);
			if (ms < t[k % 2]) t[k % 2] = ms;
		}
		if (t[1] / t[0] > worst) worst = t[1] / t[0];
		if (t[1] >= 8 * t[0]) fprintf(stderr, "a line of %s: %.2f ms, 4 times as long %.2f ms\n", ln[i].w, t[0], t[1]);
	}
	printf("test_syntax_lean: %zu of %zu bytes in %zu of %zu cases differ from VS Code; a long line 4 times as long at most %.1f times the time\n", bad, bytes, badcase, ncase, worst);
	CHECK(ncase && !bad && worst < 8);
	return 0;
}
