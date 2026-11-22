/* Review of PR 110: how long tree-sitter takes to compile each compiled-in
   query of language N (syntax.c's langs[] order): qcompile N */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tree_sitter/api.h>
extern const char *const syntax_queries[][3];
const TSLanguage *tree_sitter_c(void), *tree_sitter_cmake(void), *tree_sitter_systemverilog(void), *tree_sitter_rust(void);
int main(int argc, char **argv)
{
	int li = atoi(argv[argc - 1]);
	const TSLanguage *L = li == 0 ? tree_sitter_c() : li == 4 ? tree_sitter_rust() : li == 7 ? tree_sitter_cmake() : tree_sitter_systemverilog();
	for (int k = 0; k < 3; k++) {
		struct timespec a, b;
		uint32_t off;
		TSQueryError e;
		clock_gettime(CLOCK_MONOTONIC, &a);
		TSQuery *q = ts_query_new(L, syntax_queries[li][k], (uint32_t)strlen(syntax_queries[li][k]), &off, &e);
		clock_gettime(CLOCK_MONOTONIC, &b);
		printf("%s %.1f ms%s", k == 0 ? "highlights" : k == 1 ? ", injections" : ", folds", (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6, q ? "" : " (failed)");
	}
	puts("");
	return 0;
}
