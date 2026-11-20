/* Recent files on Windows (src/shada/check.sh runs it under Wine): the full
   name of a buffer with a relative, a non-ASCII and a /tmp/-looking name, the
   file under %XDG_STATE_HOME%\homeros-data\, its directory made, and the
   read back.  Runs on Windows or under Wine; elsewhere it says so. */
#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "check.h"
#include "core.h"
#include "shada.h"

int main(void)
{
	char dir[MAX_PATH], state[MAX_PATH + 64], cwd[MAX_PATH];
	CHECK(GetTempPathA(sizeof dir, dir));
	snprintf(state, sizeof state, "%shomeros-shada-%lu", dir, (unsigned long)GetCurrentProcessId());
	CHECK(SetEnvironmentVariableA("XDG_STATE_HOME", state) && !_putenv_s("XDG_STATE_HOME", state));
	CHECK(GetCurrentDirectoryA(sizeof cwd, cwd));
	/* two windows: a relative name and a non-ASCII one, both left at exit */
	static Buf b1, b2;
	static Win w1, w2;
	b1.path = "notes.txt";
	b2.path = "C:\\d\xc3\xbcr\\\xc3\xa9t\xc3\xa9.org";   /* C:\dür\été.org */
	w1.buf = &b1; w1.next = &w2; w2.buf = &b2;
	Editor e = { .buf = &b1, .win = &w1, .wins = &w1, .bufs = &b1 };
	shada_track(&e);
	CHECK(!shada_write(&e));
	char f[MAX_PATH + 128];
	snprintf(f, sizeof f, "%s\\homeros-data\\oldfiles", state);
	FILE *fp = fopen(f, "rb");
	CHECK(fp);
	char got[2048];
	size_t n = fread(got, 1, sizeof got - 1, fp);
	fclose(fp);
	got[n] = 0;
	/* both at the same second, in window order; no CR in the file */
	char want[2048];
	long long t = strtoll(got, NULL, 10);
	snprintf(want, sizeof want, "%lld %s\\notes.txt\n%lld C:\\d\xc3\xbcr\\\xc3\xa9t\xc3\xa9.org\n", t, cwd, t);
	if (strcmp(got, want)) { fprintf(stderr, "got:\n%swant:\n%s", got, want); CHECK(0); }
	CHECK(!shada_read());
	char first[MAX_PATH + 16];
	snprintf(first, sizeof first, "%s\\notes.txt", cwd);
	CHECK(shada_oldfile(0) && !strcmp(shada_oldfile(0), first) && shada_oldfile(1) && !shada_oldfile(2));
	puts("test_shada_win: oldfiles written and read back with Windows paths");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_shada_win: Windows only (src/shada/check.sh runs it under Wine)"); return 0; }
#endif
