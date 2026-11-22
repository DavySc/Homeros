/* Files: load into memory (never mmap, DESIGN.md) and save safely.
   Linux save follows Save2.tla "exchange": identity check, temp file in the
   same directory, fsync, swap atomically with renameat2(RENAME_EXCHANGE),
   then check what came out, fsync the directory.  Package 11 (src/file/)
   adds the in-place fallback for hard links and read-only directories and
   the Windows path (MoveFileExW, DESIGN.md "Save, Windows"). */
#ifndef FILE_H
#define FILE_H
#include <stddef.h>
#include "text.h"
#ifdef _WIN32
#include <limits.h>
#undef PATH_MAX   /* mingw's is MAX_PATH, in UTF-16 units: as UTF-8, 3 bytes each */
#define PATH_MAX (3 * 260)
#endif

typedef struct {
	int exists;
	unsigned long long dev, ino, size;
	long long mtime_ns;
} FileId;

/* Reads the whole file into *data (malloc'd; free it).  A missing file gives
   0 bytes and id->exists = 0.  0 ok, -1 error (errno set). */
int file_load(const char *path, char **data, size_t *n, FileId *id);

/* Neovim's 'fileformats' "unix,dos" ("dos,unix" on Windows), as readfile()
   decides: dos when the data has a LF and every LF follows a CR; data with
   no LF takes the first entry (dos on Windows, unix elsewhere).  For dos it
   drops the CR before each LF in place and shrinks *n.  1 dos, 0 unix. */
int file_eol(char *data, size_t *n);

enum {
	SAVE_OK,
	SAVE_CHANGED,  /* changed on disk since it was loaded or saved: nothing written */
	SAVE_KEPT,     /* written, but what it replaced was not what we loaded; that
	                  version is kept at the path written to kept */
	SAVE_ERR       /* nothing replaced; errno set */
};
/* Writes the first n bytes of t to path, each LF as CR LF if dos.  id is
   what we last loaded or saved there and is updated on success (its size is
   the bytes written).  force skips the identity check (:w!). */
int file_save(const char *path, const Text *t, size_t n, int dos, FileId *id,
              int force, char *kept, size_t keptsz);

/* Outside changes: inotify on the file's directory (FindFirstChangeNotificationW
   on Windows), so a file replaced by rename is still seen.  Returns an fd for
   loop_watch, -1 on error: the same fd for every file, never closed (closing
   one costs milliseconds), so a directory stays watched for the session. */
int  file_watch(const char *path);
/* Reads the fd's pending events.  1 if path no longer matches id, 0 if it
   does, -1 on error.  A stat decides, not the events, so our own saves never
   count (they update id).  The core warns at the first edit after a 1 and a
   plain save over it is refused as before (DESIGN.md "External change"). */
int  file_watch_read(int fd, const char *path, const FileId *id);

/* Neovim's stdpath() for homeros, which 0 "data", 1 "state", 2 "config",
   followed by name (which starts with the separator), into f, n bytes,
   truncated: $XDG_DATA_HOME/homeros (default ~/.local/share/homeros),
   $XDG_STATE_HOME (~/.local/state), $XDG_CONFIG_HOME (~/.config); on
   Windows %LOCALAPPDATA%\homeros-data for data and state and
   %LOCALAPPDATA%\homeros for config (an XDG variable set there wins, as in
   Neovim). */
void file_stdpath(char *f, size_t n, int which, const char *name);
/* mkdir -p of the directories in f (a file's path; '\\' also on Windows), mode
   0700; f is unchanged after.  A directory that cannot be made shows as the
   file's open failing. */
void file_mkdirs(char *f);
/* Neovim's full name of a buffer (fix_fname()) into out, PATH_MAX bytes: the
   directory resolved (symlinks too), the last part as typed, so a symlink or
   a file not made yet keeps its name; GetFullPathNameW on Windows.  The
   directory must exist.  0 ok, -1 error. */
int  file_full(const char *path, char *out);

/* Crash journal (Vim's swap file; Neovim's place for it): the buffer as it
   was when journalling started (its base: the bytes, the core's state flags
   and the FileId they belong to), then every change since, appended as it
   happens and made durable at most a second later.  One per edited file in
   $XDG_STATE_HOME/homeros/journal/ (default ~/.local/state), named by the
   file's full path with '/' as '%'; a lock keeps a second editor out.
   Removed on a clean close; what a crash leaves is recovered at the next open,
   even if the file changed meanwhile, because the base is in the journal. */
typedef struct Journal Journal;
/* Opens path's journal.  0: nothing left behind.  1: recovered: *t (the
   caller frees it), *state and *id are the crashed session's text, flags and
   the file identity it was based on (keep that id, so a plain save refuses to
   overwrite a file changed since); the journal continues from there.
   -1: error; errno EWOULDBLOCK when another editor journals this file. */
int  journal_open(const char *path, Journal **j, Text **t,
                  unsigned *state, FileId *id);
/* Before each change of the buffer (an edit, an undo or redo step): the first
   after journal_open or journal_saved records t, state and id as the base;
   later calls do nothing.  0 ok, -1 write error. */
int  journal_base(Journal *j, const Text *t, unsigned state, const FileId *id);
/* Each change (just before or after it is made): bytes [pos, pos + ndel)
   of the old text become ins; state is the core's flags.  0 ok, -1 write
   error. */
int  journal_edit(Journal *j, size_t pos, size_t ndel, const char *ins,
                  size_t nins, unsigned state);
void journal_saved(Journal *j);       /* after a save: empty, base again at the next change */
int  journal_due(const Journal *j);   /* ms until journal_sync is due, -1 if nothing is pending */
void journal_sync(Journal *j);        /* fdatasync what was appended */
void journal_close(Journal *j);       /* clean exit: removes the journal */
#endif
