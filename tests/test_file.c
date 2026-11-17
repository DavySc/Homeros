/* file.h and the core's save path: bytes survive a load and save unchanged
   (no final newline, CRLF, a BOM, invalid UTF-8, an empty file); an outside
   change since load is never overwritten unasked (Save.tla Check); :w!
   overwrites; the temp file never stays behind; mode and symlinks kept; a new
   file follows the umask; the empty buffer writes nothing. */
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"

static char dir[] = "/tmp/homeros-test-XXXXXX", path[256];

static void put(const char *p, const char *s, size_t n)
{
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, s, n) == (ssize_t)n);
	close(fd);
}

static int is(const char *p, const char *s, size_t n)
{
	char b[256];
	int fd = open(p, O_RDONLY);
	if (fd < 0) return n == (size_t)-1;
	ssize_t r = read(fd, b, sizeof b);
	close(fd);
	return r == (ssize_t)n && !memcmp(b, s, n);
}

static int only_one_file(void)
{
	int k = 0;
	DIR *d = opendir(dir);
	for (struct dirent *x; (x = readdir(d));) k += x->d_name[0] != '.' || x->d_name[1] == 'h';
	closedir(d);
	return k == 1;
}

static void roundtrip(const char *s, size_t n)
{
	Editor e;
	put(path, s, n);
	CHECK(!core_open(&e, path));
	CHECK(!core_write(&e, e.buf, 0));
	CHECK(is(path, s, n));
	CHECK(only_one_file());
	core_free(&e);
}

int main(void)
{
	Editor e;
	CHECK(mkdtemp(dir));
	snprintf(path, sizeof path, "%s/f.txt", dir);

	roundtrip("a\nb\n", 4);
	roundtrip("a\nb", 3);                              /* no final newline */
	roundtrip("a\r\nb\r\n", 6);                        /* CRLF */
	roundtrip("\xef\xbb\xbfx\n", 5);                   /* BOM */
	roundtrip("\xff\xfe\x80 \xc3\n", 6);               /* invalid UTF-8 */
	roundtrip("", 0);                                  /* empty stays empty */

	/* an edit to a file without a final newline keeps it without one */
	put(path, "ab", 2);
	CHECK(!core_open(&e, path) && !core_edit(&e, 0, 0, "x", 1) && !core_write(&e, e.buf, 0));
	CHECK(is(path, "xab", 3));
	core_free(&e);

	/* a new file gets lines ending in newlines, as Vim writes them, and the
	   mode open(O_CREAT, 0666) would give */
	unlink(path);
	mode_t mask = umask(022);
	CHECK(!core_open(&e, path) && !e.buf->id.exists);
	CHECK(!core_edit(&e, 0, 0, "hi", 2) && !core_write(&e, e.buf, 0) && is(path, "hi\n", 3));
	struct stat st;
	CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0644);
	umask(mask);
	/* deleting every line leaves the empty buffer, which writes 0 bytes */
	CHECK(!core_delete_lines(&e, 0, 1) && !core_write(&e, e.buf, 0) && is(path, "", 0));
	core_free(&e);

	/* outside change after load: refused, file untouched; then :w! wins */
	put(path, "one\n", 4);
	CHECK(!core_open(&e, path));
	usleep(10000);
	put(path, "outside\n", 8);
	CHECK(!core_edit(&e, 0, 0, "X", 1));
	CHECK(core_write(&e, e.buf, 0) == -1 && is(path, "outside\n", 8) && core_modified(e.buf));
	CHECK(!core_write(&e, e.buf, 1) && is(path, "Xone\n", 5) && !core_modified(e.buf));
	/* an outside program replacing the file by rename is seen too */
	char other[300];
	snprintf(other, sizeof other, "%s/g", dir);
	put(other, "renamed in\n", 11);
	CHECK(!rename(other, path));
	CHECK(!core_edit(&e, 0, 1, "", 0));
	CHECK(core_write(&e, e.buf, 0) == -1 && is(path, "renamed in\n", 11));
	core_free(&e);
	CHECK(only_one_file());

	/* mode kept; a symlink keeps pointing at the file it names */
	chmod(path, 0600);
	CHECK(!core_open(&e, path) && !core_edit(&e, 0, 0, "m", 1) && !core_write(&e, e.buf, 0));
	CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0600);
	core_free(&e);
	char link[300];
	snprintf(link, sizeof link, "%s/link", dir);
	CHECK(!symlink(path, link));
	CHECK(!core_open(&e, link) && !core_edit(&e, 0, 0, "s", 1) && !core_write(&e, e.buf, 0));
	CHECK(!lstat(link, &st) && S_ISLNK(st.st_mode) && is(path, "smrenamed in\n", 13));
	core_free(&e);

	unlink(link);
	unlink(path);
	rmdir(dir);
	puts("test_file: byte-exact round trips, outside changes refused, :w! overwrites, no temp left");
	return 0;
}
