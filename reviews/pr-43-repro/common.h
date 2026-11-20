/* Shared by the repros: the unit test's helpers and its set-up (a project
   with A.lean, the fake Lean server, the page's files, the test binary as
   the opener). */
#include "iv_gen.h"
#include <signal.h>
static Editor e;
static char path[256], root[256], redirect_path[256];
static const char *const roots[] = { "lean-toolchain", NULL };
static char exe[4096];
static LspServer table[2];
static char *fargv[4];

/* Returns 1 if this process is the fake server or the opener (and ran). */
static int helper(int argc, char **argv, int *rc)
{
	if (argc == 3 && !strcmp(argv[1], "--fake")) { *rc = fake(argv[2]); return 1; }
	if (argc == 2 && !strncmp(argv[1], "file://", 7)) {
		char opened[256];
		snprintf(opened, sizeof opened, "%s/opened", getenv("IV_TEST_DIR"));
		int fd = open(opened, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (write(fd, argv[1], strlen(argv[1])) < 0 || write(fd, "\n", 1) < 0) return 1;
		close(fd);
		if (getenv("R43_CHATTER")) { fputs(getenv("R43_CHATTER"), stdout); fflush(stdout); sleep(3); }
		*rc = 0;
		return 1;
	}
	return 0;
}

static void setup(void)
{
	char s[1024];
	CHECK(realpath("/proc/self/exe", exe));
	strcpy(dir, "/tmp/homeros-iv-XXXXXX");
	CHECK(mkdtemp(dir));
	setenv("IV_TEST_DIR", dir, 1);
	snprintf(logpath, sizeof logpath, "%s/lean.log", dir);
	fargv[0] = exe; fargv[1] = "--fake"; fargv[2] = logpath;
	table[0] = (LspServer){ ".lean", "lean4", (const char *const *)fargv, roots, 1 };
	lsp_servers = table;
	snprintf(root, sizeof root, "%s/root", dir);
	mkdir(root, 0755);
	snprintf(s, sizeof s, "%s/dist", root);
	mkdir(s, 0755);
	const char *page_files[] = { "index.html", "host.js", "theme.css" };
	for (int i = 0; i < 3; i++) {
		char from[256], to[512];
		snprintf(from, sizeof from, "src/infoview/page/%s", page_files[i]);
		snprintf(to, sizeof to, "%s/%s", root, page_files[i]);
		copy_file(from, to);
	}
	snprintf(s, sizeof s, "%s/dist/index.css", root); put_file(s, "body{}");
	snprintf(s, sizeof s, "%s/dist/index.production.min.js", root); put_file(s, "export {}");
	snprintf(s, sizeof s, "%s/lean-toolchain", dir); put_file(s, "leanprover/lean4:v4.35.0\n");
	snprintf(path, sizeof path, "%s/A.lean", dir);
	put_file(path, "theorem t : True := by\n  trivial\n");
	setenv("HOMEROS_INFOVIEW", root, 1);
	setenv("HOMEROS_OPEN", exe, 1);
}

/* \w, then the address from the file the opener was given; 0 if the
   opener never ran within 5 s. */
static int open_bridge(void)
{
	char opened[256];
	CHECK(!bridge_open(&e));
	snprintf(opened, sizeof opened, "%s/opened", dir);
	long long end = ms() + 5000;
	char *o;
	while ((o = slurp(opened)), count(o, "\n") < 1 && ms() < end) { free(o); loop_once(10); }
	if (count(o, "\n") < 1) { free(o); return 0; }
	*strchr(o, '\n') = 0;
	snprintf(redirect_path, sizeof redirect_path, "%s", o + 7);
	char *html = slurp(o + 7), *u = strstr(html, "url=http://127.0.0.1:");
	CHECK(u);
	port = atoi(u + 21);
	char *slash = strchr(u + 21, '/');
	snprintf(url, sizeof url, "%.*s", (int)(strchr(slash, '"') - slash - 1), slash + 1);
	free(html); free(o);
	unlink(opened);
	return 1;
}

static void turn(int t) { for (long long end = ms() + t; ms() < end; ) loop_once(10); }
static void cleanup(void) { char s[300]; snprintf(s, sizeof s, "rm -rf %s", dir); if (system(s)) {} }
