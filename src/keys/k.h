/* Shared by the files of src/keys: a port of Neovim 0.12's normal mode
   (normal.c, ops.c, register.c, textobject.c, search.c, edit.c) onto the
   core's API.  Names follow Neovim's so a rule can be traced to its source.
   Lines are counted from 0; a position may sit on the end of line (col ==
   length), Neovim's NUL. */
#ifndef K_H
#define K_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"

/* coladd: screen cells past col, with 'virtualedit' block in a Visual
   block (Neovim's pos_T) */
typedef struct { long lnum, col, coladd; } Pos;

/* vim_strchr() for a key or code point: strchr() would take the low byte,
   so U+2200 would match the terminating NUL */
static inline int has(const char *s, int c) { return c > 0 && c < 0x80 && strchr(s, c); }
enum { FORWARD = 1, BACKWARD = -1 };
enum { MCHAR, MLINE, MBLOCK };
#define MAXCOL 0x7fffffffL   /* Neovim's (INT_MAX), so it fits a Windows long */

/* the editor keys_feed is working on (one at a time: Neovim's curwin) */
extern Editor *E;
#define W (E->win)
#define B (E->buf)

/* buf.c: lines, positions, edits */
long nlines(void);
const char *ml_get(long l);           /* NUL-terminated copy, valid for a few calls */
long ml_len(long l);
int  gchar(Pos p);                    /* code point at p, NUL at the end of line */
int  gchar_cursor(void);
int  clen(const char *s, long n, long i);   /* bytes of the character at s[i] */
long head(const char *s, long i);           /* start of the character holding s[i] */
int  whitechar(const char *s, long n, long i);
int  inc(Pos *p);
int  dec(Pos *p);
int  incl(Pos *p);
int  decl(Pos *p);
int  inc_cursor(void);
int  dec_cursor(void);
int  lt(Pos a, Pos b);
int  eq(Pos a, Pos b);
Pos  cur(void);
void setcur(Pos p);
int virtual_active(void);
void getvvcol(Pos p, long *start, long *end);
long getviscol(void);
void coladvance_force(long want);
long vcol_of(long l, long col);       /* screen column where col starts */
long vcol_end(long l, long col);      /* last screen column of the char at col */
long col_of_vcol(long l, long v, int past_end);   /* coladvance's column */
void coladvance(long want);           /* MAXCOL for the end of line */
void beginline(int flags);
enum { BL_WHITE = 1, BL_SOL = 2, BL_FIX = 4 };
long get_indent(long l);
int  linewhite(long l);
int  lineempty(long l);
int  oneright(void);
int  oneleft(void);
int  cursor_down(long n);
int  cursor_up(long n);
void check_cursor(void);
void check_cursor_col(void);
void adjust_cursor_eol(void);
void update_curswant(void);
void validate_virtcol(void);
extern int set_curswant;
int  replace_bytes(long l, long col, long ndel, const char *ins, long nins);
int  append_lines(long after, const char *s, long n);   /* lines s (joined by '\n') below line after (-1: at the top) */
int  del_lines(long l, long n);
int  set_indent(long l, long cols);
void u_save_cursor(void);             /* an undo step even if nothing changes */
void u_save(size_t a, size_t z);      /* one that saves lines [a, z) as they are */
int  is_empty_buf(void);
void beep(void);                      /* beep_flush(): drops stuffed keys */
extern int beeped;

/* motion.c */
int  cls_of(int c);
int  fwd_word(long count, int bigword, int eol);
int  bck_word(long count, int bigword, int stop);
int  end_word(long count, int bigword, int stop, int empty);
int  bckend_word(long count, int bigword, int eol);
int  findsent(int dir, long count);
int  findpar(int *pincl, int dir, long count, int what, int both);
int  startPS(long l, int para, int both);
int  findmatch(Pos *out, int initc);
int  findmatchlimit(Pos *out, int initc, int flags);
enum { FM_BACKWARD = 1, FM_FORWARD = 2, FM_BLOCKSTOP = 4, FM_SKIPCOMM = 8 };
int  searchc(int c, int dir, int t_cmd, long count, int repeat);
extern const char *p_mps;
extern int virtual_op, ins_at_eol;

/* operator and command state (normal.c) */
typedef struct {
	int op_type, regname, motion_type, motion_force;
	int use_reg_one, inclusive, end_adjusted, empty, is_visual, block_mode;
	Pos start, end, cursor_start;   /* cursor_start: where gq was typed */
	long line_count, start_vcol, end_vcol;
} Oparg;
enum { OP_NOP, OP_DELETE, OP_YANK, OP_CHANGE, OP_LSHIFT, OP_RSHIFT, OP_TILDE,
	OP_UPPER, OP_LOWER, OP_JOIN, OP_JOIN_NS, OP_REPLACE, OP_INSERT, OP_APPEND,
	OP_NR_ADD, OP_NR_SUB, OP_FORMAT };

/* text objects (textobject.c); the Visual state they extend */
extern int VIsual_active, VIsual_mode;
extern Pos VIsual;
int  current_word(Oparg *oap, long count, int include, int bigword);
int  current_sent(Oparg *oap, long count, int include);
int  current_par(Oparg *oap, long count, int include);
int  current_block(Oparg *oap, long count, int include, int what, int other);
int  current_quote(Oparg *oap, long count, int include, int quotechar);

/* registers and operators (ops.c) */
typedef struct { char *s; long n; int type; long width; } Reg;
int  valid_yank_reg(int r, int writing);
Reg *get_reg(int r, int yank);
int  op_delete(Oparg *oap);
int  op_yank(Oparg *oap);
int  op_change(Oparg *oap);
void op_shift(Oparg *oap, long amount);
void op_tilde(Oparg *oap);
void op_format(Oparg *oap);
int  is_sql(void);
int  sql_format(size_t a, size_t z);   /* the edit for bytes [a, z), -1 out of memory */
int  op_replace(Oparg *oap, int c);
void op_insert(Oparg *oap, long count1);
void op_addsub(Oparg *oap, long count1, int g_cmd);
int  do_join(long count, int insert_space);
void do_put(int regname, int dir, long count, int flags);
enum { PUT_FIXINDENT = 1, PUT_CURSEND = 2, PUT_LINE = 4, PUT_LINE_SPLIT = 8, PUT_LINE_FORWARD = 16, PUT_BLOCK_INNER = 32 };
void block_prep(Oparg *oap, long l, int is_del, long *textcol, long *textlen,
                long *startspaces, long *endspaces, int *is_short, long *start_vcol, long *end_vcol);
void set_reg(int r, const char *s, long n, int type);

/* typeahead (keys.c) */
void stuff(const int *k, long n);        /* stuffReadbuff(): readbuf1 */
void stuff_redo(const int *k, long n);   /* readbuf2: "." and an insert's count */
void stuff_str(const char *s, long n);
void stuff_key(int k);
void stuff_num(long n);
void typebuf_ins(const int *k, long n);  /* an executed register */
int  stuff_empty(void);
extern int key_stuffed;                  /* KeyStuffed */

/* insert mode (edit.c) */
void edit_start(int cmdchar, long count);
void invoke_edit(int cmdchar, long count, int repl);
void insert_check(void);
void insert_key(int k, int pending);
int  open_cmd(int dir);
void ins_char_bytes(const char *s, long n);
int  stuff_inserted(int c, long count, int no_esc);
long last_insert_keys(const int **k);
void set_last_insert(int c);
extern int restart_edit, arrow_used, did_ai_flag;
extern long old_indent;              /* open_line()'s second_line_indent */

/* normal.c */
void normal_key(int k);
void prep_redo(int regname, long num, int c1, int c2, int c3, int c4, int c5);
void redo_reset(void);
void redo_cancel(void);
int  redo_append(int k);
void redo_add_keys(const int *k, long n);
void redo_add_str(const char *s);
void redo_num_append(long n);
long redo_get(const int **k);
void start_redo_ins(void);
void end_visual_mode(void);
long visual_last_line(void);
Pos visual_end(void);
void restart_insert(void);
int normal_pending(void);
/* a fresh session's registers, redo, last insert and last f/t (tests) */
void normal_reset(void), ops_reset(void), edit_reset(void), motion_reset(void);
extern int finish_op;
extern int redo_busy;
extern int searchc_inclusive;

/* ops.c: the '[ and '] marks, registers, recording */
extern Pos b_op_start, b_op_end, b_op_start_orig;
extern Reg *put_override;
extern int reg_recording, reg_executing, reg_recorded;
int  swapchar(int op_type, Pos *pos);
int  do_record(int c);
int  do_execreg(int regname);
int  insert_reg(int regname, int literally);
void record_key(int k);
long key_to_bytes(int k, char *b);
void set_last_cmdline(const char *s);
void change_done(void);
void insert_block_done(Pos insstart);
void buf_changed(void);
#endif
