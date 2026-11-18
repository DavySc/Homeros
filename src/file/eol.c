/* Line ends, both systems: Neovim's 'fileformat' for unix and dos (the
   default 'fileformats' never gives mac). */
#include "file.h"
#include "state.h"

#ifdef _WIN32
enum { FIRST = 1 };   /* 'fileformats' "dos,unix" */
#else
enum { FIRST = 0 };   /* "unix,dos" */
#endif

/* readfile() takes dos from the first LF that follows a CR and goes back to
   unix at any LF that does not; so dos means every LF follows a CR.  Only a
   CR right before a LF goes: a lone CR stays in the line, as in Neovim. */
int file_eol(char *data, size_t *n)
{
	size_t lf = 0, crlf = 0, j = 0;
	for (size_t i = 0; i < *n; i++)
		if (data[i] == '\n') { lf++; if (i > 0 && data[i - 1] == '\r') crlf++; }
	if (lf == 0) return FIRST;
	if (crlf < lf) return 0;
	for (size_t i = 0; i < *n; i++)
		if (!(data[i] == '\r' && i + 1 < *n && data[i + 1] == '\n')) data[j++] = data[i];
	*n = j;
	return 1;
}

size_t file_text_read(const Text *t, size_t *pos, size_t n, int dos, char *b, size_t cap)
{
	size_t want = n - *pos, room = dos ? cap / 2 : cap;
	size_t k = text_read(t, *pos, b, want < room ? want : room), lf = 0;
	*pos += k;
	if (!dos) return k;
	for (size_t i = 0; i < k; i++) if (b[i] == '\n') lf++;
	/* from the end, so each byte moves once and the CRs fit in place */
	for (size_t i = k, j = k + lf; i > 0;) {
		b[--j] = b[--i];
		if (b[i] == '\n') b[--j] = '\r';
	}
	return k + lf;
}
