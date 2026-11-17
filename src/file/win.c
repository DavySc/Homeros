/* Files on Windows: the twin of file.c and state.c.  Save follows Save.tla's
   rename mechanism, since Windows has no exchange call (DESIGN.md "Save,
   Windows"): temp file in the same directory, FlushFileBuffers, the identity
   check, then MoveFileExW over the file, as Git for Windows, libuv, Go and
   Rust replace files.  Not ReplaceFileW, which DESIGN.md named for keeping
   the original's attributes and ACLs: without a backup name Microsoft
   documents a failure (1176, ERROR_UNABLE_TO_MOVE_REPLACEMENT) that leaves no
   file at the path, and Wine runs it as three renames, so a save killed there
   left no file (measured under Wine, of Wine).  The rename gives up what
   ReplaceFileW keeps: creation time, DACL (the directory's inherited one
   instead), hidden and system attributes, EFS encryption, compression, named
   streams.  Paths are UTF-8 in the editor and UTF-16 here. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "state.h"

#define SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

static int fail(void)
{
	DWORD e = GetLastError();
	errno = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? ENOENT
	      : e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION ? EWOULDBLOCK
	      : e == ERROR_ACCESS_DENIED ? EACCES
	      : e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL ? ENOSPC : EIO;
	return -1;
}

static int wide(const char *s, wchar_t *w)
{
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, PATH_MAX)) return 0;
	errno = ENAMETOOLONG;
	return -1;
}

static int ident(HANDLE h, FileId *id)
{
	BY_HANDLE_FILE_INFORMATION i;
	if (!GetFileInformationByHandle(h, &i)) return fail();
	id->exists = 1;
	id->dev = i.dwVolumeSerialNumber;
	id->ino = (unsigned long long)i.nFileIndexHigh << 32 | i.nFileIndexLow;
	id->size = (unsigned long long)i.nFileSizeHigh << 32 | i.nFileSizeLow;
	/* 100 ns ticks since 1601 overflow as nanoseconds: count from 1970, as Linux */
	id->mtime_ns = ((long long)((unsigned long long)i.ftLastWriteTime.dwHighDateTime << 32
	                            | i.ftLastWriteTime.dwLowDateTime) - 116444736000000000LL) * 100;
	return 0;
}

static int same(const FileId *a, const FileId *b)
{
	return a->exists == b->exists && (!a->exists || (a->dev == b->dev && a->ino == b->ino
	       && a->size == b->size && a->mtime_ns == b->mtime_ns));
}

/* by path; a missing file is id->exists = 0 */
static int stat_id(const wchar_t *w, FileId *id)
{
	memset(id, 0, sizeof *id);
	HANDLE h = CreateFileW(w, FILE_READ_ATTRIBUTES, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return fail() && errno == ENOENT ? 0 : -1;
	int r = ident(h, id);
	CloseHandle(h);
	return r;
}

int file_load(const char *path, char **data, size_t *n, FileId *id)
{
	wchar_t w[PATH_MAX];
	*data = NULL; *n = 0;
	memset(id, 0, sizeof *id);
	if (wide(path, w)) return -1;
	HANDLE h = CreateFileW(w, GENERIC_READ, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return fail() && errno == ENOENT ? 0 : -1;
	/* the identity before the first read, as file.c */
	if (ident(h, id)) { CloseHandle(h); return -1; }
	size_t cap = (size_t)id->size + 1, len = 0;
	char *b = malloc(cap);
	for (DWORD r = 1; b && r > 0;) {   /* to EOF: the size may have changed */
		if (len == cap) { char *nb = realloc(b, cap *= 2); if (!nb) { free(b); b = NULL; break; } b = nb; }
		if (!ReadFile(h, b + len, cap - len > 1u << 30 ? 1u << 30 : (DWORD)(cap - len), &r, NULL)) { fail(); free(b); b = NULL; }
		else len += r;
	}
	CloseHandle(h);
	if (!b) { memset(id, 0, sizeof *id); return -1; }
	*data = b; *n = len;
	return 0;
}

static int write_text(HANDLE h, const Text *t, size_t n)
{
	char b[1 << 16];
	for (size_t pos = 0; pos < n;) {
		size_t k = text_read(t, pos, b, n - pos < sizeof b ? n - pos : sizeof b);
		DWORD w;
		for (size_t i = 0; i < k; i += w)
			if (!WriteFile(h, b + i, (DWORD)(k - i), &w, NULL)) return fail();
		pos += k;
	}
	return 0;
}

int file_save(const char *path, const Text *t, size_t n, FileId *id, int force,
              char *kept, size_t keptsz)
{
	static unsigned k;
	wchar_t w[PATH_MAX], tmp[PATH_MAX + 32];
	FileId now, mine;
	HANDLE h = INVALID_HANDLE_VALUE;
	(void)kept; (void)keptsz;   /* a rename hands nothing back: no SAVE_KEPT here */
	if (wide(path, w)) return SAVE_ERR;
	for (int i = 0; i < 100 && h == INVALID_HANDLE_VALUE; i++) {
		_snwprintf(tmp, sizeof tmp / sizeof *tmp, L"%ls.homeros-%lu-%u", w, GetCurrentProcessId(), k++);
		h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
	}
	if (h == INVALID_HANDLE_VALUE) { fail(); return SAVE_ERR; }
	/* WriteTmp: durable before the name points at it (Save2_nofsync's lesson
	   holds for a rename too) */
	if (write_text(h, t, n) || (!FlushFileBuffers(h) && fail())) {
		CloseHandle(h);
		DeleteFileW(tmp);
		return SAVE_ERR;
	}
	CloseHandle(h);
	/* its identity after the close, by its own name: the last write time is
	   final only once the writing handle is closed (Microsoft, "File
	   Times"), and the name is ours until the rename, which changes none of
	   volume, index, size or time */
	if (stat_id(tmp, &mine)) { DeleteFileW(tmp); return SAVE_ERR; }
	/* Check, as late as it can be: just before Rename */
	if (stat_id(w, &now)) { DeleteFileW(tmp); return SAVE_ERR; }
	if (!force && !same(id, &now)) { DeleteFileW(tmp); return SAVE_CHANGED; }
	/* Rename; a new file only where there is still none, unless :w! */
	if (!MoveFileExW(tmp, w, MOVEFILE_WRITE_THROUGH | (now.exists || force ? MOVEFILE_REPLACE_EXISTING : 0))) {
		DWORD e = GetLastError();
		fail();
		DeleteFileW(tmp);
		return e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS ? SAVE_CHANGED : SAVE_ERR;
	}
	*id = mine;
	return SAVE_OK;
}

/* The directory, as file.c, so a file replaced by rename is seen.  One
   handle per directory, kept for the session (file.h); the stat in
   file_watch_read decides, so a change notification is enough. */
int file_watch(const char *path)
{
	static struct { wchar_t dir[PATH_MAX]; HANDLE h; } d[64];
	static int nd;
	wchar_t p[PATH_MAX], full[PATH_MAX], *f;
	if (wide(path, p) || !GetFullPathNameW(p, PATH_MAX, full, &f) || !f) return -1;
	*f = 0;
	for (int i = 0; i < nd; i++) if (!wcscmp(d[i].dir, full)) return (int)(intptr_t)d[i].h;
	if (nd == 64) { errno = EMFILE; return -1; }
	HANDLE h = FindFirstChangeNotificationW(full, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE
	                                        | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES);
	if (h == INVALID_HANDLE_VALUE) return fail();
	wcscpy(d[nd].dir, full);
	d[nd++].h = h;
	return (int)(intptr_t)h;   /* loop.h: a HANDLE cut to int */
}

int file_watch_read(int fd, const char *path, const FileId *id)
{
	wchar_t w[PATH_MAX];
	FileId now;
	if (!FindNextChangeNotification((HANDLE)(intptr_t)fd)) return fail();
	if (wide(path, w) || stat_id(w, &now)) return -1;
	return !same(id, &now);
}

/* The journal's calls (state.h) */

int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags)
{
	(void)flags;   /* only the journal opens here: read, write, append, create */
	const char *x = getenv("XDG_STATE_HOME"), *base = x && *x ? x : getenv("LOCALAPPDATA");
	int k = snprintf(out, outsz, "%s\\homeros-data\\%s\\", base ? base : "", kind);
	if (k < 0 || (size_t)k + strlen(abs) >= outsz) { errno = ENAMETOOLONG; return -1; }
	for (size_t i = 0; abs[i]; i++) out[k + i] = abs[i] == '\\' || abs[i] == '/' || abs[i] == ':' ? '%' : abs[i];
	out[k + strlen(abs)] = 0;
	wchar_t w[PATH_MAX];
	if (wide(out, w)) return -1;
	/* full sharing: a scanner or a backup reading a journal left by a crash
	   must not look like a second editor (state_lock is the lock);
	   FILE_SHARE_DELETE also lets state_close remove the name while open */
	HANDLE h;
	for (int again = 1;; again = 0) {
		h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, SHARE_ALL, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PATH_NOT_FOUND || !again) break;
		/* only the first time: the directories (a failing one shows at the open) */
		for (wchar_t *p = wcschr(w, L'\\'); p; p = wcschr(p + 1, L'\\')) {
			*p = 0;
			CreateDirectoryW(w, NULL);
			*p = L'\\';
		}
	}
	if (h == INVALID_HANDLE_VALUE) return fail();
	/* without _O_TEXT the descriptor is binary: no CRLF in the records */
	int fd = _open_osfhandle((intptr_t)h, _O_APPEND);
	if (fd < 0) CloseHandle(h);
	return fd;
}

int state_abs(const char *path, char *out)
{
	wchar_t p[PATH_MAX], full[PATH_MAX];
	DWORD n;
	if (wide(path, p)) return -1;
	if (!(n = GetFullPathNameW(p, PATH_MAX, full, NULL)) || n >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
	/* NTFS names ignore case: one journal per file however it is spelled */
	CharLowerBuffW(full, n);
	if (!WideCharToMultiByte(CP_UTF8, 0, full, -1, out, PATH_MAX, NULL, NULL)) { errno = ENAMETOOLONG; return -1; }
	return 0;
}

int state_lock(int fd, const char *name, long long *size)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	FILE_STANDARD_INFO i;
	/* one byte far past any data, so the lock never blocks our own reads
	   and writes; it goes with a crashed process, as flock */
	OVERLAPPED o = { .Offset = 0, .OffsetHigh = 0x40000000 };
	(void)name;
	if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &o)) return fail();
	if (!GetFileInformationByHandleEx(h, FileStandardInfo, &i, sizeof i)) return fail();
	/* the holder before us removed it and has not closed it yet */
	if (i.DeletePending) return 1;
	*size = i.EndOfFile.QuadPart;
	return 0;
}

int fdatasync(int fd)
{
	return FlushFileBuffers((HANDLE)_get_osfhandle(fd)) ? 0 : fail();
}

/* NTFS logs a new name with the file's metadata, which FlushFileBuffers
   flushes too: no directory to flush (hypothesis until phase 1 measures it) */
int state_sync_dir(const char *name)
{
	(void)name;
	return 0;
}

void state_close(int fd, const char *name)
{
	wchar_t w[PATH_MAX];
	if (!wide(name, w)) DeleteFileW(w);
	close(fd);
}

ssize_t writev(int fd, const struct iovec *v, int k)
{
	ssize_t all = 0;
	for (int i = 0; i < k; i++) {
		int w = _write(fd, v[i].iov_base, (unsigned)v[i].iov_len);
		if (w < 0) return all ? all : -1;
		all += w;
		if ((size_t)w < v[i].iov_len) break;
	}
	return all;
}
#endif
