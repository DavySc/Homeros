/* syntax_md (src/syntax/syntax.c) against Neovim (nvim --headless on PATH,
   running tests/test_syntax_md.lua on its bundled Markdown grammar, the
   one build/syntax compiles): random Markdown of headings, nested and
   ordered lists, checkboxes ([ ], [x], the custom [-]), fenced code with
   info strings, inside block quotes and indents, a heading after a list
   marker; every line's facts must
   equal those of a walk of Neovim's whole tree. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "syntax.h"
#include "text.h"
static size_t r1, r2;   /* syntax_update's fold rows, read where a test needs them */

enum { DOCS = 300, MAXT = 1 << 14 };

static char b[MAXT];
static size_t n;
static void put(const char *s)
{
	size_t l = strlen(s);
	if (n + l < MAXT) memcpy(b + n, s, l), n += l;
}

int main(void)
{
	static const char *const quote[] = { "", "", "", "> ", "> > " }, *const ind[] = { "", "", "", "  ", "    ", "      ", "\t" },
		*const body[] = { "# h", "### h", "###### h", "####### x", "#", "- a", "* a", "+ a", "1. a", "2) a", "10. a",
		"- [ ] a", "- [x] a", "- [X] a", "- [-] a", "- [-]", "1. [ ] a", "* [-]x", "- ", "-", "```lua", "```", "~~~",
		"``` c x", "````", "text", "", "", "- - a", "- # h", "- ```", "- - # h", "3. # h", "```rust x", "``` {.py}", "2.  [-] a", "- [-](u) a", "- [-] " };
	char dir[] = "/tmp/test_syntax_md_XXXXXX", path[256], cmd[512];
	CHECK(mkdtemp(dir));
	for (int i = 0; i < DOCS; i++) {
		n = 0;
		for (unsigned k = 2 + rnd(40); k--; ) {
			put(quote[rnd(5)]), put(ind[rnd(7)]), put(body[rnd(sizeof body / sizeof *body)]);
			if (k) put("\n");
		}
		if (rnd(2)) put("\n");
		snprintf(path, sizeof path, "%s/%d.md", dir, i);
		FILE *o = fopen(path, "wb");
		CHECK(o && fwrite(b, 1, n, o) == n && !fclose(o));
	}
	snprintf(cmd, sizeof cmd, "nvim --headless --clean -l tests/test_syntax_md.lua %s %d", dir, DOCS);
	CHECK(!system(cmd));
	size_t lines = 0, facts = 0;
	for (int i = 0; i < DOCS; i++) {
		snprintf(path, sizeof path, "%s/%d.md", dir, i);
		FILE *o = fopen(path, "rb");
		CHECK(o);
		n = fread(b, 1, MAXT, o);
		fclose(o);
		Text *t = text_new(b, n);
		Syntax *s = t ? syntax_open("x.md", t) : NULL;
		CHECK(s);
		while (syntax_update(s, t, NULL, &r1, &r2)) ;
		snprintf(path, sizeof path, "%s/%d.want", dir, i);
		CHECK(o = fopen(path, "rb"));
		char want[128], got[128];
		for (size_t l = 0; l <= text_lines(t); l++, lines++) {
			CHECK(fgets(want, sizeof want, o));
			SyntaxMd m;
			facts += syntax_md(s, t, l, &m);
			snprintf(got, sizeof got, "%d %d %d %d %d %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu\n", m.head, m.list, m.ordered, m.task, m.code,
				m.mark, m.mend, m.item, m.iend, m.index, m.task_col, m.info, m.ninfo, m.lang, m.nlang);
			if (strcmp(got, want)) {
				size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
				fprintf(stderr, "%s (and .md): line %zu: %.*s\nours   %sNeovim %s", path, l, (int)(z - a), b + a, got, want);
			}
			CHECK(!strcmp(got, want));
		}
		fclose(o);
		unlink(path);
		snprintf(path, sizeof path, "%s/%d.md", dir, i);
		unlink(path);
		syntax_free(s);
		text_free(t);
	}
	rmdir(dir);
	/* not Markdown: nothing */
	Text *t = text_new("# a\n", 4);
	Syntax *s = syntax_open("x.c", t);
	SyntaxMd m;
	while (syntax_update(s, t, NULL, &r1, &r2)) ;
	CHECK(!syntax_md(s, t, 0, &m) && !m.head && !syntax_md(NULL, t, 0, &m));
	syntax_free(s);
	text_free(t);
	printf("test_syntax_md: %d random documents, %zu lines (%zu with facts) as Neovim's tree gives them\n", DOCS, lines, facts);
	return 0;
}
