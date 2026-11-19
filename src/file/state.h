/* Private to src/file/: the files kept in the state directory (Neovim's
   stdpath('state'): $XDG_STATE_HOME/homeros/<kind>/ on Linux,
   %LOCALAPPDATA%\\homeros-data\\<kind>\\ on Windows; Neovim keeps swap and
   backup files there, so read-only directories and git trees get none), each
   named by the edited file's absolute path with the separators (and ':' on
   Windows) as '%', as Neovim's "//" names them; and the few calls the journal
   makes that differ between the two systems (state.c, win.c). */
#ifndef FILE_STATE_H
#define FILE_STATE_H
#include <stddef.h>
#include "file.h"   /* PATH_MAX on Windows */
#ifdef _WIN32
#include <sys/types.h>
struct iovec { void *iov_base; size_t iov_len; };
ssize_t writev(int fd, const struct iovec *v, int k);
int fdatasync(int fd);
#else
#include <sys/uio.h>
#endif
/* Opens (with flags, mode 0600) the kind file for the absolute path abs,
   creating the directories; its name goes to out.  fd, or -1 (errno set). */
int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags);
/* The absolute path of path (symlinks resolved on Linux), which may not exist
   yet: its directory must.  0 ok, -1 error. */
int state_abs(const char *path, char *out);
/* Takes the exclusive lock on the file just opened as name: 0 held (*size
   its length), -1 another editor holds it (EWOULDBLOCK) or error, 1 the name
   was removed meanwhile by the holder before us: open it again. */
int state_lock(int fd, const char *name, long long *size);
/* Removes the name, then closes. */
void state_close(int fd, const char *name);
/* fsyncs the directory holding name: its entry durable.  0 ok, -1 error. */
int state_sync_dir(const char *name);
int file_write_all(int fd, const char *b, size_t n);   /* 0 ok, -1 error */
/* The next bytes of t from *pos (below n) into b, at most cap, each LF as
   CR LF if dos; *pos moves past what was read.  Returns the bytes in b. */
size_t file_text_read(const Text *t, size_t *pos, size_t n, int dos, char *b, size_t cap);
/* The same file, unchanged since: both missing, or equal ids. */
int file_same(const FileId *a, const FileId *b);
#endif
