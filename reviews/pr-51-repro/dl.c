/* Review of PR 51: is the parser Neovim loads in the test built from the
   patched scanner?  Loads a parser .so (argv[1]), parses argv[2] with it and
   prints how many blocks it allocated and did not free (malloc and free of
   this program are wrappers that count; the .so's calls bind to them). */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <tree_sitter/api.h>
void *__libc_malloc(size_t), __libc_free(void *);
static long live;
void *malloc(size_t n) { void *p = __libc_malloc(n); live += p != NULL; return p; }
void free(void *p) { live -= p != NULL; __libc_free(p); }
int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	void *h = dlopen(argv[1], RTLD_NOW);
	if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
	const TSLanguage *(*fn)(void) = (const TSLanguage *(*)(void))dlsym(h, "tree_sitter_sql");
	FILE *f = fopen(argv[2], "rb");
	if (!fn || !f) return 2;
	static char b[1 << 22];
	size_t n = fread(b, 1, sizeof b, f);
	TSParser *p = ts_parser_new();
	ts_parser_set_language(p, fn());
	long before = live;
	TSTree *t = ts_parser_parse_string(p, NULL, b, (uint32_t)n);
	ts_tree_delete(t);
	ts_parser_reset(p);
	printf("%s: %ld blocks from malloc still live after the parse and its tree are gone\n", argv[1], live - before);
	return 0;
}
