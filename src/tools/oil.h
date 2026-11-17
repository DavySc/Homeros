/* The file manager's model: oil.nvim's (stevearc/oil.nvim at my-nvim's
   d3733d9), without its UI.  A directory is shown as lines
   "/<id> <icon>  <name>" (directories end in '/', links "name -> target");
   editing the lines and saving turns into create, delete, move and copy
   actions (mutator/parser.lua, mutator/init.lua). */
#ifndef TOOLS_OIL_H
#define TOOLS_OIL_H
#include <stddef.h>

enum { OIL_DIR = 'd', OIL_FILE = 'f', OIL_LINK = 'l' };
enum { OIL_CREATE = 'c', OIL_DELETE = 'd', OIL_MOVE = 'm', OIL_COPY = 'y' };

typedef struct {
	int type;                  /* OIL_CREATE ... */
	int etype;                 /* OIL_DIR, OIL_FILE, OIL_LINK */
	char *src, *dst;           /* absolute paths; create and delete use src */
	char *link;                /* a new link's target */
} OilAction;

/* An entry of the cache (cache.lua): its directory ("/abs/dir/"), name,
   type; id 0 is "..". */
int  oil_entry(const char *dir, const char *name, int type, const char *link, int linkdir);  /* stores it: its id, or -1 */
/* Reads dir ("/abs/dir/") into the cache (adapters/files.lua's list): a
   name already there keeps its id.  0 ok, -1 not a directory. */
int  oil_list(const char *dir);
/* The buffer's lines for dir (view.lua's render_buffer), '\n'-ended, in
   *out (malloc'd).  0 ok, -1 out of memory. */
int  oil_render(const char *dir, char **out, size_t *n);

/* A parse error (parser.lua): message and 0-based line. */
typedef struct { const char *msg; int buf; long line; } OilError;
/* The actions for the edited buffers (dirs[i] shown as text[i]), in
   oil's order (enforce_action_order).  Returns how many, or -1 on errors
   (then *err holds nerr of them) or no memory.  Free with oil_free. */
int  oil_actions(int nbuf, const char *const *dirs, const char *const *text, const size_t *len,
                 OilAction **out, OilError **err, int *nerr);
void oil_free(OilAction *a, int n);
/* Forgets every entry (cache.clear_everything). */
void oil_reset(void);
#endif
