/* src/file/ on Windows (win.c), black box through file.h:
   1. Saves: a new file, then saves over it keep one identity (ReplaceFileW
      must leave the temp's), and an outside change of each kind (written in
      place, replaced by rename, removed, created where there was none) is
      refused and left as the outsider wrote it; :w! writes.
   2. Killed while saving, 200 times at a random moment: the file is always
      the old or the new text, never torn.
   3. Watch: one handle per directory; our own save reads as no change, an
      outside rename wakes the handle and reads as a change.
   4. Journal: killed while journalling, 100 times at a random moment: the
      reopen recovers the text and flags of the last edit that returned (or
      the one being written); a second editor is kept out (EWOULDBLOCK) while
      the first lives, and recovers once it is killed; cut at every byte, the
      whole records are kept; a clean close leaves nothing.
   5. Line ends: 'fileformats' is "dos,unix" here, so data with no LF is dos;
      a CR LF file loads as dos and saves byte for byte (several chunks of
      it), and its identity is the bytes written, so the next :w is allowed.
   Runs on Windows or under Wine (src/file/check.sh); elsewhere it says so. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "file.h"

static char dir[MAX_PATH], path[MAX_PATH + 16], jname[3 * MAX_PATH];
static wchar_t self[MAX_PATH];

static void put(const char *p, const char *s)
{
	FILE *f = fopen(p, "wb");
	CHECK(f && fwrite(s, 1, strlen(s), f) == strlen(s));
	fclose(f);
}

static size_t slurp(const char *p, char *b, size_t n)
{
	FILE *f = fopen(p, "rb");
	if (!f) return 0;
	size_t r = fread(b, 1, n, f);
	fclose(f);
	return r;
}

static int saved(const char *s, FileId *id, int force)
{
	Text *t = text_new(s, strlen(s));
	char kept[4];
	int r = file_save(path, t, strlen(s), 0, id, force, kept, sizeof kept);
	text_free(t);
	return r;
}

static void is(const char *s)
{
	char b[256];
	size_t n = slurp(path, b, sizeof b);
	CHECK(n == strlen(s) && !memcmp(b, s, n));
}

static int eol(const char *s, const char *want)
{
	char b[16];
	size_t n = strlen(s);
	memcpy(b, s, n);
	int dos = file_eol(b, &n);
	CHECK(n == strlen(want) && !memcmp(b, want, n));
	return dos;
}

static char big[100001];

static void line_ends(void)
{
	CHECK(eol("", "") == 1 && eol("a", "a") == 1 && eol("a\r", "a\r") == 1);
	CHECK(eol("a\nb", "a\nb") == 0 && eol("a\r\nb\n", "a\r\nb\n") == 0);
	CHECK(eol("a\r\nb", "a\nb") == 1 && eol("a\r\r\n", "a\r\n") == 1);
	static char d[sizeof big + sizeof big / 9];
	size_t n = 0;
	for (size_t i = 0; i < sizeof big - 1; i++) { if (big[i] == '\n') d[n++] = '\r'; d[n++] = big[i]; }
	FILE *f = fopen(path, "wb");
	CHECK(f && fwrite(d, 1, n, f) == n);
	fclose(f);
	char *data;
	size_t m;
	FileId id;
	CHECK(!file_load(path, &data, &m, &id) && file_eol(data, &m) == 1 && m == sizeof big - 1);
	Text *t = text_new(data, m);
	char kept[4];
	CHECK(t && file_save(path, t, m, 1, &id, 0, kept, sizeof kept) == SAVE_OK && id.size == n);
	CHECK(file_save(path, t, m, 1, &id, 0, kept, sizeof kept) == SAVE_OK);
	static char back[sizeof d];
	CHECK(slurp(path, back, sizeof back) == n && !memcmp(back, d, n));
	text_free(t);
	free(data);
	puts("  line ends: dos first; CR LF loaded as dos and saved byte for byte, then saved again");
}

static int same(const FileId *a, const FileId *b)
{
	return a->exists == b->exists && a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_ns == b->mtime_ns;
}

/* a later mtime than any save so far, whatever the clock's resolution */
static void touch_later(void)
{
	HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, 7, NULL, OPEN_EXISTING, 0, NULL);
	FILETIME ft;
	CHECK(h != INVALID_HANDLE_VALUE && GetFileTime(h, NULL, NULL, &ft));
	ULARGE_INTEGER u = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
	u.QuadPart += 20000000;   /* 2 s */
	ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
	CHECK(SetFileTime(h, NULL, NULL, &ft));
	CloseHandle(h);
}

static void saves(void)
{
	FileId id, l;
	char *d;
	size_t n;
	DeleteFileA(path);
	CHECK(!file_load(path, &d, &n, &id) && !id.exists && n == 0);
	free(d);
	CHECK(saved("one\n", &id, 0) == SAVE_OK);
	is("one\n");
	CHECK(!file_load(path, &d, &n, &l) && same(&id, &l));
	free(d);
	for (int i = 0; i < 3; i++) CHECK(saved(i & 1 ? "one\n" : "two two\n", &id, 0) == SAVE_OK);
	CHECK(!file_load(path, &d, &n, &l) && same(&id, &l));   /* the identity a save leaves is the file's */
	free(d);
	/* written in place, same size */
	put(path, "ONE ONE\n");
	touch_later();
	CHECK(saved("mine\n", &id, 0) == SAVE_CHANGED);
	is("ONE ONE\n");
	CHECK(saved("mine\n", &id, 1) == SAVE_OK);
	is("mine\n");
	/* replaced by rename */
	char other[MAX_PATH + 16];
	snprintf(other, sizeof other, "%s\\other", dir);
	put(other, "outside\n");
	CHECK(MoveFileExA(other, path, MOVEFILE_REPLACE_EXISTING));
	CHECK(saved("mine\n", &id, 0) == SAVE_CHANGED);
	is("outside\n");
	CHECK(!file_load(path, &d, &n, &id));
	free(d);
	/* removed */
	CHECK(DeleteFileA(path));
	CHECK(saved("mine\n", &id, 0) == SAVE_CHANGED);
	CHECK(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES);
	/* created where we loaded none */
	memset(&id, 0, sizeof id);
	put(path, "theirs\n");
	CHECK(saved("mine\n", &id, 0) == SAVE_CHANGED);
	is("theirs\n");
	/* no temp left behind */
	WIN32_FIND_DATAA f;
	char pat[MAX_PATH + 16];
	snprintf(pat, sizeof pat, "%s\\*homeros*", dir);
	CHECK(FindFirstFileA(pat, &f) == INVALID_HANDLE_VALUE);
	puts("  saves: one identity across saves; in place, rename, remove and create refused; :w! writes");
}

static HANDLE child(const char *args, HANDLE *out)
{
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	HANDLE r = NULL, w = NULL;
	if (out) { CHECK(CreatePipe(&r, &w, &sa, 0)); SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0); }
	wchar_t cmd[1024];
	_snwprintf(cmd, 1024, L"\"%ls\" %hs", self, args);
	STARTUPINFOW si = { .cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES,
	                    .hStdInput = GetStdHandle(STD_INPUT_HANDLE), .hStdOutput = out ? w : GetStdHandle(STD_OUTPUT_HANDLE),
	                    .hStdError = GetStdHandle(STD_ERROR_HANDLE) };
	PROCESS_INFORMATION pi;
	CHECK(CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi));
	CloseHandle(pi.hThread);
	if (out) { CloseHandle(w); *out = r; }
	return pi.hProcess;
}

static void kill_child(HANDLE p)
{
	TerminateProcess(p, 9);
	CHECK(WaitForSingleObject(p, 10000) == WAIT_OBJECT_0);
	CloseHandle(p);
}

static void saver(void)
{
	FileId id;
	char *d;
	size_t n;
	CHECK(!file_load(path, &d, &n, &id));
	free(d);
	for (int i = 0;; i++) CHECK(saved(i & 1 ? "short\n" : big, &id, 0) == SAVE_OK);
}

static void killed_saves(void)
{
	static char b[sizeof big];
	int shorts = 0;
	for (int s = 0; s < 200; s++) {
		put(path, "short\n");
		HANDLE p = child("saver", NULL);
		Sleep(30 + rnd(200));   /* past the child's start, into its saves */
		kill_child(p);
		size_t n = slurp(path, b, sizeof b);
		int sh = n == 6 && !memcmp(b, "short\n", 6);
		CHECK(sh || (n == strlen(big) && !memcmp(b, big, n)));
		shorts += sh;
		/* a temp of the killed save may be left, as Neovim's backup files */
		WIN32_FIND_DATAA f;
		char pat[MAX_PATH + 16], t[2 * MAX_PATH];
		snprintf(pat, sizeof pat, "%s\\*homeros*", dir);
		HANDLE h = FindFirstFileA(pat, &f);
		if (h != INVALID_HANDLE_VALUE) {
			do { snprintf(t, sizeof t, "%s\\%s", dir, f.cFileName); DeleteFileA(t); } while (FindNextFileA(h, &f));
			FindClose(h);
		}
	}
	printf("  200 saves killed at random: the old or the new text every time (%d old)\n", shorts);
}

static void watch(void)
{
	FileId id;
	char *d;
	size_t n;
	put(path, "w\n");
	CHECK(!file_load(path, &d, &n, &id));
	free(d);
	int fd = file_watch(path);
	char other[MAX_PATH + 16];
	snprintf(other, sizeof other, "%s\\other", dir);
	CHECK(fd != -1 && file_watch(other) == fd);
	HANDLE h = (HANDLE)(intptr_t)fd;
	CHECK(saved("ours\n", &id, 0) == SAVE_OK);
	CHECK(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
	CHECK(file_watch_read(fd, path, &id) == 0);
	Sleep(100);   /* let the notifications of our save arrive, then read them */
	CHECK(file_watch_read(fd, path, &id) == 0);
	CHECK(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
	put(other, "outside\n");
	CHECK(MoveFileExA(other, path, MOVEFILE_REPLACE_EXISTING));
	CHECK(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
	CHECK(file_watch_read(fd, path, &id) == 1);
	puts("  watch: one handle per directory; our save is no change, an outside rename is");
}

/* the journal: a model in an array, edits from a seed */
enum { CAP = 4096 };
typedef struct { char b[CAP]; size_t n; unsigned state; } Model;

static void edit(Model *m, size_t *pos, size_t *del, char *ins, size_t *nins)
{
	*pos = rnd((unsigned)m->n + 1);
	*del = rnd((unsigned)(m->n - *pos) + 1) % 8;
	*nins = rnd(6);
	if (m->n - *del + *nins > CAP) *nins = 0;
	for (size_t k = 0; k < *nins; k++) ins[k] = "ab\n\xc3\xa9x"[rnd(6)];
	memmove(m->b + *pos + *nins, m->b + *pos + *del, m->n - *pos - *del);
	memcpy(m->b + *pos, ins, *nins);
	m->n = m->n - *del + *nins;
	m->state = rnd(4);
}

static void journaller(unsigned long long seed)
{
	Model m = { "first line\n", 11, 0 };
	Journal *j;
	Text *t = text_new(m.b, m.n), *x;
	unsigned st;
	FileId id = { 1, 2, 3, 4, 5 };
	rng = seed;
	CHECK(journal_open(path, &j, &x, &st, &id) == 0);
	CHECK(!journal_base(j, t, 0, &id));
	printf("0\n");
	fflush(stdout);
	for (int i = 1;; i++) {
		size_t pos, del, nins;
		char ins[8];
		edit(&m, &pos, &del, ins, &nins);
		CHECK(!journal_edit(j, pos, del, ins, nins, m.state));
		printf("%d\n", i);
		fflush(stdout);
		if (i % 16 == 0) journal_sync(j);
	}
}

static int equal(const Text *t, const Model *m)
{
	static char b[CAP];
	return t && text_len(t) == m->n && text_read(t, 0, b, CAP) == m->n && !memcmp(b, m->b, m->n);
}

static void killed_journal(void)
{
	int mid = 0;
	for (int s = 0; s < 100; s++) {
		unsigned long long seed = 0x9E3779B97F4A7C15ULL * (unsigned long long)(s + 1);
		char args[64];
		snprintf(args, sizeof args, "journaller %llu", seed);
		HANDLE out, p = child(args, &out);
		static char b[1 << 20];
		DWORD got = 0, r;
		/* wait for the base, then kill at a random moment */
		while (got == 0 || !memchr(b, '\n', got)) { CHECK(ReadFile(out, b + got, sizeof b - got - 1, &r, NULL) && r); got += r; }
		Sleep(rnd(40));
		kill_child(p);
		while (ReadFile(out, b + got, sizeof b - got - 1, &r, NULL) && r) got += r;
		CloseHandle(out);
		b[got] = 0;
		int last = 0;
		for (char *c = b; *c; c = strchr(c, '\n') + 1) last = atoi(c);
		Journal *j;
		Text *t;
		unsigned st;
		FileId id;
		CHECK(journal_open(path, &j, &t, &st, &id) == 1 && id.ino == 3);
		Model m = { "first line\n", 11, 0 }, next;
		rng = seed;
		for (int i = 0; i < last; i++) { size_t a, b2, c2; char ins[8]; edit(&m, &a, &b2, ins, &c2); }
		next = m;
		{ size_t a, b2, c2; char ins[8]; edit(&next, &a, &b2, ins, &c2); }
		int now = equal(t, &m) && st == m.state, then = equal(t, &next) && st == next.state;
		CHECK(now || then);
		mid += !now;
		text_free(t);
		journal_close(j);
		CHECK(GetFileAttributesA(jname) == INVALID_FILE_ATTRIBUTES);   /* a clean close leaves nothing */
	}
	printf("  100 journals killed at random: each recovered the last edit (%d the one being written)\n", mid);
}

static void holder(void)
{
	Journal *j;
	Text *t = text_new("held\n", 5), *x;
	unsigned st;
	FileId id = { 1, 2, 3, 4, 5 };
	CHECK(journal_open(path, &j, &x, &st, &id) == 0 && !journal_base(j, t, 0, &id));
	printf("ready\n");
	fflush(stdout);
	Sleep(INFINITE);
}

static void locks(void)
{
	HANDLE out, p = child("holder", &out);
	char b[16];
	DWORD r;
	CHECK(ReadFile(out, b, sizeof b, &r, NULL) && r >= 5 && !memcmp(b, "ready", 5));
	Journal *j;
	Text *t;
	unsigned st;
	FileId id;
	errno = 0;
	CHECK(journal_open(path, &j, &t, &st, &id) == -1 && errno == EWOULDBLOCK);
	kill_child(p);
	CloseHandle(out);
	/* a reader of the leftover journal (a scanner, a backup) is not an
	   editor: recovery goes on (review of PR 25, 2) */
	HANDLE rd = CreateFileA(jname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	CHECK(rd != INVALID_HANDLE_VALUE);
	CHECK(journal_open(path, &j, &t, &st, &id) == 1 && text_len(t) == 5);
	text_free(t);
	journal_close(j);
	CloseHandle(rd);
	p = child("holder", &out);
	CHECK(ReadFile(out, b, sizeof b, &r, NULL) && r >= 5 && !memcmp(b, "ready", 5));
	kill_child(p);
	CloseHandle(out);
	/* NTFS names ignore case: the same file spelt in capitals finds it */
	char up[sizeof path];
	snprintf(up, sizeof up, "%s", path);
	CharUpperA(up);
	CHECK(journal_open(up, &j, &t, &st, &id) == 1 && text_len(t) == 5);
	text_free(t);
	journal_close(j);
	puts("  locks: a second editor is kept out while the first lives, and recovers once it is killed, with a reader holding the journal, by any case of the name");
}

/* base "abc\n" and k edits inserting "xy", then waits to be killed: the
   journal is locked while it lives, so its bytes are read after */
static void writer(int k)
{
	Journal *j;
	Text *t = text_new("abc\n", 4), *x;
	unsigned st;
	FileId id = { 1, 2, 3, 4, 5 };
	CHECK(journal_open(path, &j, &x, &st, &id) == 0 && !journal_base(j, t, 0, &id));
	for (int i = 1; i <= k; i++) CHECK(!journal_edit(j, 0, 0, "xy", 2, (unsigned)i));
	printf("ready\n");
	fflush(stdout);
	Sleep(INFINITE);
}

static size_t written(int k, char *b, size_t n)
{
	char args[32], r[16];
	DWORD got;
	snprintf(args, sizeof args, "writer %d", k);
	DeleteFileA(jname);   /* the last writer's, left as a crash leaves it */
	HANDLE out, p = child(args, &out);
	CHECK(ReadFile(out, r, sizeof r, &got, NULL) && got >= 5 && !memcmp(r, "ready", 5));
	kill_child(p);
	CloseHandle(out);
	return slurp(jname, b, n);
}

/* cut at every byte: whole records kept, the rest dropped */
static void torn(void)
{
	Journal *j;
	Text *t;
	unsigned st;
	FileId id;
	size_t ends[4];
	char full[512];
	for (int i = 0; i < 4; i++) ends[i] = written(i, full, sizeof full);
	size_t len = ends[3];
	for (size_t cut = 0; cut <= len; cut++) {
		FILE *f = fopen(jname, "wb");
		CHECK(f && fwrite(full, 1, cut, f) == cut);
		fclose(f);
		int w = -1;
		while (w < 3 && ends[w + 1] <= cut) w++;
		int r = journal_open(path, &j, &t, &st, &id);
		CHECK(r == (w >= 0));
		if (r) { CHECK(text_len(t) == 4 + 2 * (size_t)w && st == (unsigned)w); text_free(t); }
		journal_close(j);
	}
	printf("  cut at each of %zu bytes: the whole records kept\n", len + 1);
}

int main(int argc, char **argv)
{
	GetModuleFileNameW(NULL, self, MAX_PATH);
	for (size_t i = 0; i < sizeof big - 1; i++) big[i] = "big text\n"[i % 9];
	if (argc == 1) {
		char tmp[MAX_PATH];
		CHECK(GetTempPathA(MAX_PATH, tmp));
		snprintf(dir, sizeof dir, "%shomeros-file-%lu-%llu", tmp, GetCurrentProcessId(), (unsigned long long)GetTickCount64());
		CHECK(CreateDirectoryA(dir, NULL));
		/* the children find the directory in the environment */
		CHECK(SetEnvironmentVariableA("HOMEROS_TEST_DIR", dir));
	} else snprintf(dir, sizeof dir, "%s", getenv("HOMEROS_TEST_DIR"));
	snprintf(path, sizeof path, "%s\\f", dir);
	char st[MAX_PATH + 16];
	snprintf(st, sizeof st, "%s\\state", dir);
	CHECK(!_putenv_s("XDG_STATE_HOME", st));
	/* the name win.c gives it: the path lowercased, separators and ':' as '%' */
	char full[MAX_PATH];
	CHECK(GetFullPathNameA(path, MAX_PATH, full, NULL));
	CharLowerA(full);
	int k = snprintf(jname, sizeof jname, "%s\\homeros-data\\journal\\", st);
	for (char *c = full; *c; c++) jname[k++] = *c == '\\' || *c == ':' ? '%' : *c;
	jname[k] = 0;
	if (argc > 1 && !strcmp(argv[1], "saver")) saver();
	if (argc > 1 && !strcmp(argv[1], "holder")) holder();
	if (argc > 2 && !strcmp(argv[1], "writer")) writer(atoi(argv[2]));
	if (argc > 2 && !strcmp(argv[1], "journaller")) journaller(strtoull(argv[2], NULL, 10));
	if (argc > 1) return 2;
	saves();
	killed_saves();
	watch();
	DeleteFileA(path);
	killed_journal();
	locks();
	torn();
	line_ends();
	puts("test_file_win: saves refuse outside changes and are never torn; the watch and the journal work");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_file_win: Windows only (src/file/check.sh runs it under Wine)"); return 0; }
#endif
