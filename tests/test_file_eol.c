/* 'fileformat' against Neovim 0.12 (nvim --headless on PATH): random files
   of LF, CR LF, lone CR, with and without a final line end, some over 64 KiB
   so a save takes several chunks, are opened and written with :w by the core
   and by nvim; the format, the buffer's lines and the bytes written must
   match, and the file must come back byte for byte (DESIGN.md: CR LF and a
   missing final line end survive a save; so nvim runs with nofixeol, the
   one setting where the design leaves Neovim's defaults).  A third of ours are hard-linked, so the in-place save (and its cut
   to the bytes written) is tested too.  Also: the message names [dos], :e
   takes the format the file has now, a
   crashed dos buffer comes back dos, and a journal is not taken for the file
   when only the format differs. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"

enum { CASES = 400, BIG = 3, MAXF = 1 << 18 };
static char dir[] = "/tmp/homeros-eol-XXXXXX";

static void put(const char *p, const char *s, size_t n)
{
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, s, n) == (ssize_t)n);
	close(fd);
}

static size_t slurp(const char *p, char *b, size_t n)
{
	int fd = open(p, O_RDONLY);
	CHECK(fd >= 0);
	ssize_t r = read(fd, b, n);
	close(fd);
	CHECK(r >= 0 && (size_t)r < n);
	return (size_t)r;
}

/* a file whose line ends are all LF (mode 0), all CR LF (1), or mixed, with lone CRs
   inside lines; the last line end left off at random */
static size_t gen(char *b, int big, int mode)
{
	int lines = big ? 9000 : (int)rnd(8);
	size_t n = 0;
	for (int l = 0; l < lines; l++) {
		for (int k = (int)rnd(big ? 12 : 5); k > 0; k--) {
			unsigned c = rnd(10);
			b[n++] = c < 6 ? "ab c x"[c] : c < 9 ? 'a' + (char)c : '\r';
		}
		int crlf = mode == 0 ? 0 : mode == 1 ? 1 : (int)rnd(2);
		if (l == lines - 1 && rnd(3) == 0) break;
		if (crlf) b[n++] = '\r';
		b[n++] = '\n';
	}
	if (!big && rnd(6) == 0) b[n++] = '\r';   /* a CR as the last byte */
	return n;
}

static void file_name(char *b, size_t n, const char *kind, int i) { snprintf(b, n, "%s/%s%d", dir, kind, i); }

/* our lines in nvim's .got form: the format, then the lines joined by LF */
static size_t ours(Editor *e, char *b)
{
	size_t n = (size_t)sprintf(b, "%s\n", e->buf->dos ? "dos" : "unix");
	for (size_t l = 0; l < core_lines(e); l++) {
		size_t len;
		const unsigned char *s = core_line(e, l, &len);
		if (l) b[n++] = '\n';
		memcpy(b + n, s, len);
		n += len;
	}
	return n;
}

static void against_nvim(void)
{
	static char all[1 << 20], a[MAXF], b[MAXF];
	char *data[CASES + 1] = { all };
	char p[300], q[300], h[300], cmd[1024];
	size_t len[CASES];
	for (int i = 0; i < CASES; i++) {
		len[i] = gen(data[i], i < BIG, i < BIG ? i != BIG - 1 : (int)rnd(3));   /* the big ones dos but one */
		data[i + 1] = data[i] + len[i];
		CHECK(data[i + 1] + MAXF / 2 < all + sizeof all);
		file_name(p, sizeof p, "n", i); put(p, data[i], len[i]);
		file_name(p, sizeof p, "o", i); put(p, data[i], len[i]);
		file_name(h, sizeof h, "h", i);
		if (i % 3 == 0) CHECK(!link(p, h));
	}
	/* one nvim for all: :edit, the format and lines, :write */
	snprintf(cmd, sizeof cmd, "nvim --headless --clean -n -l /dev/stdin %s %d <<'E'\n"
	         "local d, n = arg[1], tonumber(arg[2])\n"
	         "vim.o.fixendofline = false\n"
	         "for i = 0, n - 1 do\n"
	         "  local f = d .. '/n' .. i\n"
	         "  vim.cmd('silent edit! ' .. f)\n"
	         "  local o = io.open(f .. '.got', 'wb')\n"
	         "  o:write(vim.bo.fileformat, '\\n', table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, true), '\\n'))\n"
	         "  o:close()\n"
	         "  vim.cmd('silent write')\n"
	         "  vim.cmd('bwipeout!')\n"
	         "end\nE", dir, CASES);
	CHECK(!system(cmd));
	int dos = 0;
	for (int i = 0; i < CASES; i++) {
		Editor e;
		file_name(p, sizeof p, "o", i);
		CHECK(!core_open(&e, p));
		dos += e.buf->dos;
		CHECK(!strstr(e.msg, "[dos]") == !e.buf->dos);
		file_name(q, sizeof q, "n", i);
		strcat(q, ".got");
		size_t na = ours(&e, a), nb = slurp(q, b, sizeof b);
		if (na != nb || memcmp(a, b, na)) fprintf(stderr, "case %d (%zu bytes): lines or format differ\n", i, len[i]);
		CHECK(na == nb && !memcmp(a, b, na));
		CHECK(!core_write(&e, e.buf, 0));
		struct stat st;
		CHECK(!stat(p, &st) && !strstr(e.msg, "[dos]") == !e.buf->dos);
		snprintf(cmd, sizeof cmd, " %lluB written", (unsigned long long)st.st_size);
		CHECK(strstr(e.msg, cmd));
		core_free(&e);
		na = slurp(p, a, sizeof a);
		file_name(q, sizeof q, "n", i);
		nb = slurp(q, b, sizeof b);
		if (na != nb || memcmp(a, b, na)) fprintf(stderr, "case %d: bytes after :w differ\n", i);
		CHECK(na == nb && !memcmp(a, b, na) && na == len[i] && !memcmp(a, data[i], na));
		if (i % 3 == 0) { file_name(h, sizeof h, "h", i); CHECK(!stat(h, &st) && st.st_nlink == 2); }
	}
	CHECK(dos > CASES / 5 && dos < CASES - CASES / 5);
	printf("  %d files (%d dos, %d over 64 KiB, a third hard-linked): format, lines and bytes after :w as nvim's, the file's own bytes\n", CASES, dos, BIG);
}

static char path[300], jname[700];

/* drops the editor as a crash would: the clean close removes the journal,
   so its bytes are put back */
static void crash(Editor *e)
{
	static char j[1 << 16];
	size_t n = slurp(jname, j, sizeof j);
	core_free(e);
	put(jname, j, n);
}

static void journal(void)
{
	static char b[64];
	Editor e;
	put(path, "a\r\nb\r\n", 6);
	CHECK(!core_open(&e, path) && e.buf->dos);
	CHECK(!core_edit(&e, 0, 0, "x", 1));
	crash(&e);
	CHECK(!core_open(&e, path) && core_modified(e.buf) && e.buf->dos);
	CHECK(!core_write(&e, e.buf, 0));
	core_free(&e);
	CHECK(slurp(path, b, sizeof b) == 7 && !memcmp(b, "xa\r\nb\r\n", 7));
	puts("  a crashed dos buffer comes back dos and saves CR LF");

	/* the journal's text equals the file's lines, but not its format */
	CHECK(!core_open(&e, path) && e.buf->dos);
	CHECK(!core_edit(&e, 0, 1, "", 0));
	crash(&e);
	put(path, "a\nb\n", 4);
	CHECK(!core_open(&e, path) && core_modified(e.buf) && e.buf->dos && strstr(e.msg, "recovered"));
	core_free(&e);
	puts("  a journal that saves other bytes than the file's is recovered, though its lines are the file's");

	/* :e takes the format the file has now */
	put(path, "a\nb\n", 4);
	CHECK(!core_open(&e, path) && !e.buf->dos);
	put(path, "a\r\nb\r\n", 6);
	CHECK(!core_edit_file(&e, NULL, 0) && e.buf->dos && strstr(e.msg, " [dos] 2L, 6B"));
	CHECK(!core_write(&e, e.buf, 0) && strstr(e.msg, " [dos] 2L, 6B written"));
	core_free(&e);
	CHECK(slurp(path, b, sizeof b) == 6 && !memcmp(b, "a\r\nb\r\n", 6));
	puts("  :e takes the file's format as it is now");
}

int main(void)
{
	char st[300], cmd[400];
	if (getenv("SEED")) rng = 2 * strtoull(getenv("SEED"), NULL, 10) + 1;   /* distinct seeds stay distinct */
	CHECK(mkdtemp(dir));
	snprintf(st, sizeof st, "%s/state", dir);
	CHECK(!setenv("XDG_STATE_HOME", st, 1));
	snprintf(path, sizeof path, "%s/f", dir);
	snprintf(jname, sizeof jname, "%s/homeros/journal/%s", st, path);
	for (char *c = jname + strlen(st) + 17; *c; c++) if (*c == '/') *c = '%';
	against_nvim();
	journal();
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
	puts("test_file_eol: CR LF files read, shown and written as Neovim's 'fileformat' does");
	return 0;
}
