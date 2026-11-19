/* The Windows half of package 17, the twin of os.c: watching the
   repository (Git.tla's Outside), paths, the trash.  Paths are UTF-8 with
   '/' as git for Windows writes them. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop.h"
#include "gitint.h"

/* explorer opens a URL in the default browser */
const char *const git_opener = "explorer";

static int wide(const char *s, wchar_t *w) { return !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, PATH_MAX); }

static int narrow(const wchar_t *w, int wn, char *s)
{
	int k = WideCharToMultiByte(CP_UTF8, 0, w, wn, s, PATH_MAX - 1, NULL, NULL);
	if (k <= 0) return -1;
	s[k] = 0;
	for (char *p = s; *p; p++) if (*p == '\\') *p = '/';
	return 0;
}

/* realpath: the stored name of an existing file, as libuv's (Neovim's)
   uv_fs_realpath on Windows, drive letter upper case as git writes it */
int git_real(const char *path, char *out)
{
	wchar_t w[PATH_MAX], full[PATH_MAX], *p = full;
	HANDLE h;
	if (wide(path, w) || (h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
	                                      FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE) return -1;
	DWORD n = GetFullPathNameW(w, PATH_MAX, full, NULL);
	CloseHandle(h);
	if (!n || n >= PATH_MAX) return -1;
	/* \\?\C:\x is C:\x; \\?\UNC\host\x is \\host\x */
	if (!wcsncmp(p, L"\\\\?\\", 4)) { p += 4; n -= 4; }
	if (!wcsncmp(p, L"UNC\\", 4)) { p += 2; n -= 2; p[0] = L'\\'; }
	if (n > 3 && p[n - 1] == L'\\') p[--n] = 0;
	if (narrow(p, -1, out)) return -1;
	if (out[0] >= 'a' && out[0] <= 'z' && out[1] == ':') out[0] -= 'a' - 'A';
	return 0;
}

/* One directory change watch on the whole work tree, as VS Code's on
   Windows (ReadDirectoryChangesW with subtrees), and one on the common
   directory when it is elsewhere (a linked work tree).  Each completes into
   its own event, which the event loop waits on. */
struct Root { HANDLE dir; OVERLAPPED ov; char path[PATH_MAX]; DWORD buf[16384]; };
struct Watch { int n; struct Root r[2]; };

static int arm(struct Root *t)
{
	ResetEvent(t->ov.hEvent);
	return !ReadDirectoryChangesW(t->dir, t->buf, sizeof t->buf, TRUE,
	                              FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE
	                              | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES, NULL, &t->ov, NULL);
}

/* Takes every completed batch of changes, until a read is pending (as
   os.c reads until the queue is empty): changes that came while one batch
   was taken complete the next read at once.  A batch that counts is a
   change; one too large for the buffer comes back empty, or a read that
   failed: those are changes too. */
void git_drain(Repo *r)
{
	struct Watch *w = r->wt;
	for (int i = 0; w && i < w->n; i++) {
		struct Root *t = &w->r[i];
		DWORD n = 0;
		for (; GetOverlappedResult(t->dir, &t->ov, &n, FALSE) || GetLastError() != ERROR_IO_INCOMPLETE; n = 0) {
			int any = !n;
			/* copied out: Wine packs the entries without Windows's DWORD
			   alignment (measured, 1 run) */
			for (char *p = (char *)t->buf; n && !any; ) {
				FILE_NOTIFY_INFORMATION f;
				WCHAR w[PATH_MAX];
				char name[PATH_MAX], path[2 * PATH_MAX];
				size_t at = offsetof(FILE_NOTIFY_INFORMATION, FileName);
				memcpy(&f, p, at);
				if (f.FileNameLength >= sizeof w) any = 1;
				else {
					memcpy(w, p + at, f.FileNameLength);
					if (narrow(w, (int)(f.FileNameLength / sizeof(WCHAR)), name)) any = 1;
					else {
						snprintf(path, sizeof path, "%s/%s", t->path, name);
						any = git_counts(r, path);
					}
				}
				if (!f.NextEntryOffset) break;
				p += f.NextEntryOffset;
			}
			if (any) { r->ver++; r->dirty = 1; }
			if (arm(t)) break;
		}
	}
}

static int root(Repo *r, const char *path)
{
	struct Watch *w = r->wt;
	struct Root *t = &w->r[w->n];
	wchar_t p[PATH_MAX];
	if (wide(path, p)) return -1;
	t->dir = CreateFileW(p, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	if (t->dir == INVALID_HANDLE_VALUE) return -1;
	snprintf(t->path, sizeof t->path, "%s", path);
	if (!(t->ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)) || arm(t)
	    || loop_watch((int)(intptr_t)t->ov.hEvent, git_changed, r)) {
		if (t->ov.hEvent) CloseHandle(t->ov.hEvent);
		CloseHandle(t->dir);
		return -1;
	}
	w->n++;
	return 0;
}

void git_watch(Repo *r)
{
	if (!(r->wt = calloc(1, sizeof *r->wt))) return;
	size_t tl = strlen(r->top);
	root(r, r->top);
	if (strncmp(r->common, r->top, tl) || r->common[tl] != '/') root(r, r->common);
}

void git_unwatch(Repo *r)
{
	struct Watch *w = r->wt;
	if (!w) return;
	for (int i = 0; i < w->n; i++) {
		struct Root *t = &w->r[i];
		DWORD n;
		loop_unwatch((int)(intptr_t)t->ov.hEvent);
		/* the kernel writes into buf until the cancel completes */
		if (CancelIoEx(t->dir, &t->ov) || GetLastError() != ERROR_NOT_FOUND) GetOverlappedResult(t->dir, &t->ov, &n, TRUE);
		CloseHandle(t->ov.hEvent);
		CloseHandle(t->dir);
	}
	free(w);
	r->wt = NULL;
}

/* Emacs's move-file-to-trash on Windows: the Recycle Bin, through
   SHFileOperationW with FOF_ALLOWUNDO as Emacs's w32fns.c does */
int git_trash(const char *path)
{
	wchar_t w[PATH_MAX + 1];
	if (wide(path, w)) return -1;
	size_t n = wcslen(w);
	for (size_t i = 0; i < n; i++) if (w[i] == L'/') w[i] = L'\\';
	w[n + 1] = 0;   /* a list of names, ended by an empty one */
	SHFILEOPSTRUCTW op = { 0 };
	op.wFunc = FO_DELETE;
	op.pFrom = w;
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationW(&op) || op.fAnyOperationsAborted ? -1 : 0;
}
#endif
