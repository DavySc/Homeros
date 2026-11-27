/* PR 169: pass= edge cases against live servers. argv[1]: "lf", "crlf", "bom", "644" */
#include "h.h"
static const char *lines[] = {
	"sp mssql 127.0.0.1 h_sp master trust pass=Ho meros 1 x",
	"trail mssql 127.0.0.1 h_trail master trust pass=trail  ",
	"tab mssql 127.0.0.1 h_tab master trust\tpass=a\tb c",
	"hash mssql 127.0.0.1 h_hash master trust pass=a #b c",
	"pt mssql 127.0.0.1 h_pt master trust pass=x trust",
	"passin mssql 127.0.0.1 h_pass master trust pass=a pass=b",
	"lead mssql 127.0.0.1 h_lead master trust pass= lead",
	"old mssql 127.0.0.1 sa master pass=Homeros_Test1 trust",
	"empty mssql 127.0.0.1 sa master trust pass=",
	"pgsp pg 127.0.0.1 h_pgsp postgres trust pass=pg pw 1",
	"pgempty pg 127.0.0.1 postgres postgres trust pass=",
	"winlogin mssql 127.0.0.1 - master trust",
	"dir %s/ms sp",
};
int main(int argc, char **argv)
{
	setup();
	const char *m = argv[1], *eol = !strcmp(m, "crlf") ? "\r\n" : "\n";
	char conf[4096], f[8192] = "", l[512];
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	if (!strcmp(m, "bom")) strcat(f, "\xEF\xBB\xBF");
	for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) snprintf(l, sizeof l, lines[i], dir), strcat(f, l), strcat(f, eol);
	put(conf, f, !strcmp(m, "644") ? 0644 : 0600);
	put("ms/a.sql", "select suser_name() as who\n", 0600);
	core_open(&e, "ms/a.sql");
	for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
		char name[32], p[64]; sscanf(lines[i], "%31s", name);
		int pg = !strncmp(name, "pg", 2);
		snprintf(p, sizeof p, "%s/%s.sql", pg ? "pg" : "ms", name);
		put(p, pg ? "select current_user as who\n" : "select suser_name() as who\n", 0600);
		core_edit_file(&e, p, 0); e.msg[0] = 0;
		int r = strcmp(name, "dir") ? sql_connect(&e, name) : 0;
		if (!r) { e.msg[0] = 0; sql_key(&e, KEY_F5); for (int k = 0; k < 500 && !strstr(results(), "Completion") && !*e.msg; k++) loop_once(10); }
		char out[200] = ""; const char *w = rwin() ? res : "";
		const char *q = strstr(w, "who\n"); if (q) snprintf(out, sizeof out, "%.*s", (int)strcspn(q + 4, "\n"), q + 4);
		else snprintf(out, sizeof out, "%.120s", w);
		for (char *c = out; *c; c++) if (*c == '\n') *c = '|';
		printf("%-8s connect=%d msg=[%s] who=[%s]\n", name, r, e.msg, out);
		if (rwin()) sql_results(&e);
	}
	return 0;
}
