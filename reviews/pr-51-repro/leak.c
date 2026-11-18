/* Review of PR 51: parses argv[1] as SQL with tree-sitter alone and counts
   the scanner's allocations (malloc is wrapped), so the leak shows without
   the editor.  Built twice by leak.sh: with nvim-treesitter's scanner as
   fetched, and with the PR's patched one.  LeakSanitizer reports at exit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
const TSLanguage *tree_sitter_sql(void);
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	static char b[1 << 22];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	TSParser *p = ts_parser_new();
	ts_parser_set_language(p, tree_sitter_sql());
	TSTree *t = ts_parser_parse_string(p, NULL, b, (uint32_t)n);
	printf("%s: %zu bytes, error %d\n", argv[1], n, ts_node_has_error(ts_tree_root_node(t)));
	fflush(stdout);
	ts_tree_delete(t);
	ts_parser_delete(p);
	return 0;
}
