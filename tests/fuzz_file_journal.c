/* libFuzzer: journal recovery against an array model.  The input drives
   edits and saves, then damages the journal the way a crash or a bad disk
   would (cut it, change a byte, add garbage); recovery must give exactly the
   model after the last whole record before the damage, or nothing if no
   whole base is left. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file.h"

enum { CAP = 1 << 12, MAXREC = 64 };
static char path[64], jname[512];

static void setup(void)
{
	static char dir[] = "/tmp/homeros-fuzz-XXXXXX";
	char st[64];
	if (!mkdtemp(dir)) abort();
	snprintf(path, sizeof path, "%s/f", dir);
	snprintf(st, sizeof st, "%s/state", dir);
	if (mkdir(st, 0700) || setenv("XDG_STATE_HOME", st, 1)) abort();
	snprintf(jname, sizeof jname, "%s/homeros/journal/%s", st, path);
	for (char *c = jname + strlen(st) + 17; *c; c++) if (*c == '/') *c = '%';
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char m[MAXREC + 1][CAP], b[CAP], j[1 << 16];
	static size_t mn[MAXREC + 1];
	static off_t end[MAXREC + 1];
	if (!path[0]) setup();
	if (n < 3) return 0;
	Journal *J;
	Text *t = text_new("seed\n", 5), *x;
	unsigned st, sts[MAXREC + 1];
	FileId id = { 1, 2, 3, 4, 5 }, rid;
	struct stat sb;
	if (journal_open(path, &J, &x, &st, &id) != 0) abort();
	/* records written since the last base: nr; m[i] the model after i */
	int nr = -1;
	size_t cur = 5;
	char now[CAP];
	memcpy(now, "seed\n", 5);
	unsigned state = 0;
	size_t i = 3;
	for (; i + 3 <= n && nr < MAXREC; i += 3) {
		if (d[i] == 0xff) {   /* a save: the next change writes a new base */
			journal_saved(J);
			nr = -1;
			continue;
		}
		if (nr < 0) {
			if (journal_base(J, t, state, &id)) abort();
			nr = 0;
			memcpy(m[0], now, cur); mn[0] = cur; sts[0] = state;
			if (stat(jname, &sb)) abort();
			end[0] = sb.st_size;
		}
		size_t pos = d[i + 1] % (cur + 1), del = (d[i] >> 4) % (cur - pos + 1), ins = d[i + 2] % 5;
		char s[5];
		for (size_t k = 0; k < ins; k++) s[k] = "ab\n\xc3\xa9"[(d[i] + k) % 5];
		if (cur - del + ins > CAP) continue;
		if (text_delete(t, pos, del) || text_insert(t, pos, s, ins)) abort();
		memmove(now + pos + ins, now + pos + del, cur - pos - del);
		memcpy(now + pos, s, ins);
		cur = cur - del + ins;
		state = d[i] & 3;
		if (journal_edit(J, pos, del, s, ins, state)) abort();
		nr++;
		memcpy(m[nr], now, cur); mn[nr] = cur; sts[nr] = state;
		if (stat(jname, &sb)) abort();
		end[nr] = sb.st_size;
	}
	/* "crash": take the journal's bytes, then damage them as d[0] says */
	int fd = open(jname, O_RDONLY);
	ssize_t len = fd < 0 ? -1 : read(fd, j, sizeof j);
	if (len < 0) abort();
	close(fd);
	journal_close(J);
	text_free(t);
	size_t at = len ? ((size_t)d[1] << 8 | d[2]) % (size_t)len : 0, keep = (size_t)len;
	switch (d[0] % 3) {
	case 0: keep = at; break;                          /* cut */
	case 1: if (len) j[at] ^= (char)(1 + d[0] / 3 % 255); break;   /* a changed byte */
	case 2: for (size_t k = 0; k < 16; k++) j[len + (ssize_t)k] = (char)d[(k * 7) % n]; keep += 16; break;   /* garbage */
	}
	fd = open(jname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || write(fd, j, keep) != (ssize_t)keep) abort();
	close(fd);
	/* the whole records before the damage; all of them before garbage */
	int w = -1;
	while (w < nr && (size_t)end[w + 1] <= at) w++;
	if (d[0] % 3 == 2) w = nr;
	int r = journal_open(path, &J, &t, &st, &rid);
	if (r != (w >= 0)) abort();
	if (r && (text_len(t) != mn[w] || text_read(t, 0, b, CAP) != mn[w] || memcmp(b, m[w], mn[w])
	          || st != sts[w] || rid.ino != 3)) abort();
	text_free(t);
	journal_close(J);
	return 0;
}
