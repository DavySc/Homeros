#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "grid.h"
#include "keyname.h"
#include "snip.h"
#include "tools.h"
extern int rank_calls;
int main(void)
{
	static char dir[] = "/tmp/homeros-cnt-XXXXXX"; CHECK(mkdtemp(dir));
	char cfg[96], md[160], cmd[512], path[96];
	snprintf(cfg, sizeof cfg, "%s/cfg", dir); snprintf(md, sizeof md, "%s/homeros/snippets/sql.md", cfg);
	setenv("XDG_CONFIG_HOME", cfg, 1); file_mkdirs(md);
	snprintf(cmd, sizeof cmd, "cp %s %s", getenv("SNIPCAT"), md); CHECK(!system(cmd));
	snprintf(path, sizeof path, "%s/x_default_a.sql", dir);
	static Editor e; CHECK(!core_open(&e, path));
	CHECK(!tools_pick(&e, PICK_SNIPPETS));
	int r0 = rank_calls;
	static Grid g; CHECK(!grid_resize(&g, 40, 120));
	for (int i = 0; i < 10; i++) tools_draw(&e, &g);
	printf("rank() calls: %d to open the picker, %d more for 10 redraws\n", r0, rank_calls - r0);
	return 0;
}
