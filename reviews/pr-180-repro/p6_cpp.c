/* K with the real clangd on a .cpp file (and a .c one): the float */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "keys.h"
#include "loop.h"
static Editor e;
static Grid g;
int main(int argc, char **argv)
{
	char dir[] = "/tmp/pr180cppXXXXXX";
	CHECK(argc > 1 && mkdtemp(dir) && !chdir(dir));
	FILE *f = fopen(argv[1], "w");
	CHECK(f && fputs("#include <vector>\n/// Adds one to x.\nint foo(int x) { return x + 1; }\nint main() { std::vector<int> v; v.push_back(foo(1)); return v.size(); }\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, argv[1]) && !grid_resize(&g, 24, 80));
	printf("%s: server %s\n", argv[1], e.buf->lsp ? "yes" : "NO");
	core_draw(&e, NULL, &g);
	for (int i = 0; i < 300; i++) loop_once(10);
	core_goto(&e, 3, strstr("int main() { std::vector<int> v; v.push_back(foo(1));", "foo") - "int main() { std::vector<int> v; v.push_back(foo(1));");
	e.msg[0] = 0;
	keys_input(&e, 'K', 0);
	for (int i = 0; i < 500 && !e.pop && !e.msg[0]; i++) loop_once(10);
	core_draw(&e, NULL, &g);
	printf("pop %s msg [%s]\n", e.pop ? "yes" : "none", e.msg);
	for (int r = 0; r < 16; r++) {
		printf("  |");
		for (int c = 0; c < g.cols; c++) { const Cell *x = &g.c[r * g.cols + c]; if (x->len) fwrite(grid_text(x), 1, x->len, stdout); }
		printf("|\n");
	}
	core_free(&e);
	return 0;
}
