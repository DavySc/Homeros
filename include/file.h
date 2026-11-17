/* Files: load into memory (never mmap, DESIGN.md) and save safely.
   Linux save follows Save2.tla "exchange": identity check, temp file in the
   same directory, fsync, swap atomically with renameat2(RENAME_EXCHANGE),
   then check what came out, fsync the directory.  Package 11 (src/file/)
   adds the crash journal, inotify warnings, the in-place fallback for hard
   links and read-only directories, and the Windows path (ReplaceFileW). */
#ifndef FILE_H
#define FILE_H
#include <stddef.h>
#include "text.h"

typedef struct {
	int exists;
	unsigned long long dev, ino, size;
	long long mtime_ns;
} FileId;

/* Reads the whole file into *data (malloc'd; free it).  A missing file gives
   0 bytes and id->exists = 0.  0 ok, -1 error (errno set). */
int file_load(const char *path, char **data, size_t *n, FileId *id);

enum {
	SAVE_OK,
	SAVE_CHANGED,  /* changed on disk since it was loaded or saved: nothing written */
	SAVE_KEPT,     /* written, but what it replaced was not what we loaded; that
	                  version is kept at the path written to kept */
	SAVE_ERR       /* nothing replaced; errno set */
};
/* Writes the first n bytes of t to path.  id is what we last loaded or saved
   there and is updated on success.  force skips the identity check (:w!). */
int file_save(const char *path, const Text *t, size_t n, FileId *id, int force,
              char *kept, size_t keptsz);
#endif
