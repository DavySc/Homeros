/* Lean's colours against VS Code's (package 12): replays the cases that
   tests/test_syntax_lean.js recorded (slices of Lean's own sources and
   random text from its tokens, with the colour and font style VS Code
   gives each byte under Catppuccin Mocha) and compares each byte but the
   newlines, through the theme's faces[], Markdown in comments included:
   none may differ.  usage: test_syntax_lean [FILE] (default the .txt),
   -v prints each case's first difference. */
#include <string.h>
#include "check.h"
#include "grid.h"
#include "tsql.h"
#include "mk.h"
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
		Tsql *s = mk(t, 3);
		CHECK(t && s);
		size_t r1, r2;
		(void)r1, (void)r2; while (tsql_update(s, NULL) == 2) ;
		memset(got, 0, n);
		/* line by line, as the core asks */
		for (size_t a = 0, e; a < n; a = e) {
			e = text_line_start(t, text_line_of(t, a) + 1);
			tsql_faces(s, a, e, got + a);
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
		tsql_free(s);
		text_free(t);
	}
	fclose(in);
	printf("test_syntax_lean: %zu of %zu bytes in %zu of %zu cases differ from VS Code\n", bad, bytes, badcase, ncase);
	CHECK(ncase && !bad);
	return 0;
}
