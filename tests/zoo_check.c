/* The names and expressions of subjects/zoo.c, worked out at the frame that shows them (dbg/eval.c). The program
   calls `probe` with a text, an address, a size and a value. Stopped at probe's first instruction these are in
   rcx, rdx, r8 and r9; the text evaluated in the caller's frame must be of that size and, when an address above 1
   is shown, in memory there; else its value as an unsigned long long (address 0) or as a double (address 1) must be
   that value (a bit field is shown with size 0, which is not compared). Optimised code may have no place for a variable at some instruction (clang's has
   none for many whose address is taken): the count of texts with such a variable is printed; a place that is given
   must hold the value.
   Address 5: the text has no meaning, the value says why. Address 2: the program says what a person is shown for the text; 3: how its type is written; 4: the text is the list of the
   variables in scope (dbg/view.c, eval_locals).
   At every stop the variables in scope are the roots of a table of rows (view_rows) of which a few more are opened or closed
   at random. All its rows must be those that a plain walk of the open rows gives; any window of it must be that part of the
   whole; a row below another must be inside its bytes (a member), at its place by number (an element) or where it points;
   and while the program stays in one function with the same variables, the rows stay the same.
   usage: zoo_check <zoo.exe> <number of texts the program shows> <least that must be right> <and of them variables in a register> <least rows looked at> <least frames of a stack that show their own x> [seed of the opening and closing] [names the compiler dropped] [texts it does otherwise, with ; between] */
#include "../dbg/view.c"
#include "../dbg/ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) return printf("zoo_check: " __VA_ARGS__), printf("\n"), 1

enum { ROWS = 1 << 16, ROOTS = 64 };
typedef struct { const Engine *e; const EvalFrame *r; const View *view; const ViewRow *rows; uint64_t k, total; bool gone; } Table;

static bool is_open(const View *view, uint64_t parent, uint64_t child)
{
  for (uint32_t i = 0; i < view->count; i += 1) if (view->open[i].parent == parent && view->open[i].child == child) return true;
  return false;
}

/* The rows below `text` (a row with this key, so deep) are the next of the table, each where it must be. 0: they are. */
static int below(Table *t, const char *text, uint64_t key, uint32_t depth)
{
  const char *why;
  EvalValue v = eval(t->e, t->r, text, &why), pointed = v.kind ? eval_load(t->e, t->r, v, &why) : v;
  EvalShape s = eval_shape(v.type);
  RdiSpan members = v.kind && s.form == EVAL_RECORD ? eval_members(s) : (RdiSpan){0};
  t->gone |= !v.kind;   /* an open row that has no value at this instruction (optimised code) has no rows below it now */
  uint64_t count = !v.kind ? 0 : s.form == EVAL_ROW ? s.size / eval_shape(s.to).size : s.form == EVAL_ADDRESS && eval_shape(s.to).form != EVAL_NOTHING, i = 0;
  for (uint64_t m = 0; m < (s.form == EVAL_RECORD ? members.n : count); m += 1) {
    const RdiMember *b = (const RdiMember *)members.p + m;
    char want[VIEW_TEXT];
    RdiSpan name = s.form == EVAL_RECORD ? rdi_str(&s.to.module->rdi, b->name) : (RdiSpan){0};
    if (s.form == EVAL_RECORD && b->kind != 1) continue;
    int n = s.form == EVAL_RECORD ? snprintf(want, sizeof want, "%s.%.*s", text, (int)name.n, (const char *)name.p) : snprintf(want, sizeof want, "%s[%llu]", text, (unsigned long long)i);
    if (n >= VIEW_TEXT) want[0] = 0;
    if (t->k == t->total) FAIL("the table ends before %s", want);
    const ViewRow *row = &t->rows[t->k++];
    if (strcmp(row->text, want) || row->depth != depth || row->parent != key || row->child != i || row->label != (want[0] ? strlen(text) : 0)) FAIL("row %llu is %s (depth %u, child %llu), not %s (depth %u, child %llu)", (unsigned long long)t->k - 1, row->text, row->depth, (unsigned long long)row->child, want, depth, (unsigned long long)i);
    EvalValue c = eval(t->e, t->r, want, &why);
    EvalShape cs = eval_shape(c.type);
    if (v.kind == EVAL_MEMORY && s.form != EVAL_ADDRESS && (c.kind != EVAL_MEMORY || (s.form == EVAL_ROW ? c.at != v.at + i * cs.size : c.at < v.at || c.at + cs.size > v.at + s.size))) FAIL("%s is at %llx with %u bytes, outside %s at %llx with %u bytes", want, (unsigned long long)c.at, cs.size, text, (unsigned long long)v.at, s.size);
    if (s.form == EVAL_ADDRESS && pointed.kind && (c.kind != EVAL_MEMORY || c.at != pointed.at)) FAIL("%s is at %llx, and %s points to %llx", want, (unsigned long long)c.at, text, (unsigned long long)pointed.at);
    if (is_open(t->view, key, i) && below(t, want, mix(key, i), depth + 1)) return 1;
    i += 1;
  }
  return 0;
}

static uint32_t random_below(uint64_t *seed, uint64_t n)   /* xorshift64 */
{
  *seed ^= *seed << 13, *seed ^= *seed >> 7, *seed ^= *seed << 17;
  return (uint32_t)(*seed % n);
}

/* h with a text added (FNV-1a; not the key function of dbg/view.c, which is what is being checked). */
static uint64_t digest(uint64_t h, const char *text)
{
  for (; ; text += 1) {
    h = (h ^ (uint8_t)*text) * 0x100000001b3;
    if (!*text) return h;
  }
}

/* All `total` rows but those below row `but` (which come straight after it, deeper). */
static uint64_t others(const ViewRow *rows, uint64_t total, uint64_t but)
{
  uint64_t h = 0;
  for (uint64_t i = 0; i < total; i += 1) {
    h = digest(h, rows[i].text);
    while (i >= but && i + 1 < total && rows[i + 1].depth > rows[but].depth) i += 1;
    if (i >= but) but = total;
  }
  return h;
}

/* The table of the variables in scope at the frame; then a few rows more opened or closed. 0: it is as it must be.
   before: the function and variables of the stop before, and its rows as they were left. */
static int table(const Engine *e, const EvalFrame *r, View *view, ViewRow *rows, uint64_t *seed, uint64_t looked[2], uint64_t before[2])
{
  RdiSpan names[ROOTS];
  char texts[ROOTS][64];
  const char *roots[ROOTS];
  uint32_t count = eval_locals(e, r, names, ROOTS), shown = 0;
  const EngineModule *m = engine_module(e, r->regs.rip - 1);
  uint32_t scope = m ? rdi_vmap(&m->rdi, RDI_SCOPE_VMAP, r->regs.rip - 1 - m->image.base) : 0;
  for (uint32_t hops = 0; m && hops < 64 && rdi_at(&m->rdi, RDI_SCOPES, RdiScope, scope)->parent; hops += 1) scope = rdi_at(&m->rdi, RDI_SCOPES, RdiScope, scope)->parent;   /* the function's own scope */
  uint64_t where = scope, all = 0;
  for (uint32_t i = 0; i < count; i += 1) snprintf(texts[i], sizeof texts[i], "%.*s", (int)names[i].n, (const char *)names[i].p), roots[i] = texts[i], where = digest(where, texts[i]);
  uint64_t total = view_rows(view, e, r, roots, count, 0, rows, ROWS, &shown);
  if (total > ROWS) return view->count = 0, before[0] = 0, 0;   /* too many are open to look at: begin again */
  if (shown != total) FAIL("%u rows of %llu are given", shown, (unsigned long long)total);
  Table t = {e, r, view, rows, 0, total, false};
  for (uint32_t i = 0; i < count; i += 1) {
    uint64_t id = 0;
    for (const char *c = roots[i]; *c; c += 1) id = mix(id, (uint8_t)*c);
    char inside[VIEW_TEXT];
    snprintf(inside, sizeof inside, "(%s)", roots[i]);
    if (t.k == total) FAIL("the table ends before %s", roots[i]);
    const ViewRow *row = &rows[t.k++];
    if (strcmp(row->text, roots[i]) || row->depth || row->parent || row->child != id || row->label) FAIL("row %llu is %s, not the variable %s", (unsigned long long)t.k - 1, row->text, roots[i]);
    if (is_open(view, 0, id) && below(&t, inside, mix(0, id), 1)) return 1;
  }
  if (t.k != total) FAIL("the table has %llu rows, the open rows make %llu", (unsigned long long)total, (unsigned long long)t.k);
  for (uint64_t i = 0; i < total; i += 1) all = digest(all, rows[i].text);
  if (!t.gone && where == before[0] && all != before[1]) FAIL("the rows changed while the program stayed in one function with the same variables");
  /* any window is that part of the whole; every row has a value and a type to show */
  for (uint32_t round = 0; round < 8; round += 1) {
    static ViewRow window[64];
    uint64_t first = random_below(seed, total + 2);
    uint32_t most = random_below(seed, 65), got = 0, want = first >= total ? 0 : total - first < most ? (uint32_t)(total - first) : most;
    if (view_rows(view, e, r, roots, count, first, window, most, &got) != total || got != want || (got && memcmp(window, rows + first, got * sizeof *window))) FAIL("the window of %u rows from %llu is not that part of the %llu rows", most, (unsigned long long)first, (unsigned long long)total);
    for (uint32_t i = 0; i < got; i += 1) {
      char value[64], type[64];
      const char *why = NULL;
      EvalValue v = eval(e, r, window[i].text, &why);
      view_value(e, r, window[i].text, value, sizeof value), view_type(v.type, type, sizeof type);
      if (!value[0] || (v.kind && !type[0])) FAIL("%s has no value or no type to show", window[i].text);
    }
  }
  looked[0] += total;
  for (uint64_t i = 0; i < total; i += 1) if (rows[i].depth > looked[1]) looked[1] = rows[i].depth;
  for (uint32_t i = 0; total && i < 3; i += 1) {   /* a row opened or closed changes the rows below it and no other */
    uint64_t which = random_below(seed, total), rest = others(rows, total, which);
    ViewRow row = rows[which];
    bool was = is_open(view, row.parent, row.child);
    if (!view_toggle(view, &row)) { view->count = random_below(seed, VIEW_OPEN), which = total; }   /* no room: close some, the last ones */
    else if (is_open(view, row.parent, row.child) == was) FAIL("row %llu (%s) is as %s as before", (unsigned long long)which, row.text, was ? "open" : "closed");
    total = view_rows(view, e, r, roots, count, 0, rows, ROWS, &shown);
    if (total > ROWS) return view->count = 0, before[0] = 0, 0;
    if (which < total && others(rows, total, which) != rest) FAIL("opening or closing row %llu (%s) changed other rows than those below it", (unsigned long long)which, rows[which].text);
  }
  before[0] = t.gone || total > ROWS ? 0 : where, before[1] = 0;
  for (uint64_t i = 0; i < shown; i += 1) {
    const char *why;
    before[1] = digest(before[1], rows[i].text);
    /* a row opened just now where it has no value (a pointer not yet set): its rows come when it has one */
    if (is_open(view, rows[i].parent, rows[i].child) && !eval(e, r, rows[i].text, &why).kind) before[0] = 0;
  }
  return 0;
}

/* What the editor would be answered here (dbg/ask.c) is what the views give: a window of the stack, a window of the table of the variables of the
   frame above probe with the rows that are open, two of them as a table of their own, the registers; and where probe and this line are. */
static int asks(const Engine *e, uint32_t thread, const View *view, uint64_t probe, uint32_t line, uint64_t *seed)
{
  static uint64_t out[8192];
  static struct { Ask a; View v; char texts[2 * 64]; } m;
  static struct { Ask a; char text[64]; } where;
  static ViewRow rows[ASK_MOST];
  TargetRegs r;
  ViewFrame frames[16];
  RdiSpan names[ROOTS];
  char texts[ROOTS][64], value[128], type[64];
  const char *roots[ROOTS], *why;
  if (!target_regs(e->target, thread, &r)) FAIL("no registers");
  uint32_t count = view_stack(e, r, 0, frames, 16), first = (uint32_t)random_below(seed, count + 1), most = (uint32_t)random_below(seed, 5), want = count - first < most ? count - first : most;
  m.a = (Ask){ASK_STACK, thread, 0, 0, most, 0, first};
  if (ask_answer(e, &m, sizeof m.a, out, sizeof out) != want * sizeof(AskFrame)) FAIL("asked for %u frames from %u of %u, the answer is not %u", most, first, count, want);
  for (uint32_t i = 0; i < want; i += 1) {
    const AskFrame *a = (const AskFrame *)out + i;
    const ViewFrame *f = &frames[first + i];
    const char *path = f->module ? f->module->path : "";
    char file[1024];   /* the whole path, of which the answer has the end that fits */
    uint64_t len = f->module ? rdi_path(&f->module->rdi, rdi_at(&f->module->rdi, RDI_SOURCE_FILES, RdiSourceFile, f->line.file)->path_node, file, sizeof file) : 0;
    if (a->addr != f->at.regs.rip || a->line != f->line.line || strlen(a->name) != f->name.n || memcmp(a->name, f->name.p, f->name.n) || !*a->module != !f->module || strcmp(path + strlen(path) - strlen(a->module), a->module)
        || strlen(a->file) != (len < sizeof a->file ? len : sizeof a->file - 1) || memcmp(a->file, file + len - strlen(a->file), strlen(a->file))) FAIL("frame %u is answered as %s in %s at %s:%u", first + i, a->name, a->module, a->file, a->line);
  }
  count = eval_locals(e, &frames[1].at, names, ROOTS);
  for (uint32_t i = 0; i < count; i += 1) snprintf(texts[i], sizeof texts[i], "%.*s", (int)names[i].n, (const char *)names[i].p), roots[i] = texts[i];
  for (uint32_t what = ASK_TEXTS; what <= ASK_REGISTERS; what += 1) {
    uint32_t n = what == ASK_LOCALS ? count : what == ASK_REGISTERS ? 18 : count < 2 ? count : 2, shown = 0, more = 0;
    for (uint32_t i = 0; what == ASK_TEXTS && i < n; i += 1) more += (uint32_t)snprintf(m.texts + more, sizeof m.texts - more, "%s", texts[i]) + 1;
    uint64_t total = view_rows(view, e, &frames[1].at, what == ASK_REGISTERS ? eval_registers : roots, n, 0, rows, 0, &shown);
    m.a = (Ask){ASK_ROWS, thread, 1, what, (uint32_t)random_below(seed, 9), 0, random_below(seed, total + 1)}, m.v = *view;
    view_rows(view, e, &frames[1].at, what == ASK_REGISTERS ? eval_registers : roots, n, m.a.first, rows, m.a.most, &shown);
    if (ask_answer(e, &m, sizeof m.a + sizeof m.v + more, out, sizeof out) != 8 + shown * sizeof(AskRow) || out[0] != total) FAIL("asked for %u rows from %llu of the %llu of table %u, the answer is not %u", m.a.most, (unsigned long long)m.a.first, (unsigned long long)total, what, shown);
    for (uint32_t i = 0; i < shown; i += 1) {
      const AskRow *a = (const AskRow *)(out + 1) + i;
      view_value(e, &frames[1].at, rows[i].text, value, sizeof value), view_type(eval(e, &frames[1].at, rows[i].text, &why).type, type, sizeof type);
      if (memcmp(&a->row, &rows[i], sizeof rows[i]) || strcmp(a->value, value) || strcmp(a->type, type)) FAIL("row %u of table %u is answered as %s = %s (%s), not %s = %s (%s)", i, what, a->row.text, a->value, a->type, rows[i].text, value, type);
    }
  }
  /* where code is: probe by its name; this line by the end of its file's path in any case (code of it begins shortly before the call), but not by half a name or under another folder, of whatever name */
  const struct { const char *text; uint64_t most, has; } places[] = {{"probe", 1, probe}, {"zoo.c:%u", 16, 0}, {"SUBJECTS\\ZOO.C:%u", 16, 0}, {"oo.c:%u", 0, 0}, {"other/zoo.c:%u", 0, 0}, {"subjectz/zoo.c:%u", 0, 0}, {"no_such_function", 0, 0}, {"zoo.c:", 0, 0}};
  for (uint32_t i = 0; i < sizeof places / sizeof *places; i += 1) {
    where.a = (Ask){.kind = ASK_PLACES};
    uint32_t n = ask_answer(e, &where, (uint32_t)sizeof where.a + (uint32_t)snprintf(where.text, sizeof where.text, places[i].text, line) + 1, out, sizeof out) / 8, near = 0;
    for (uint32_t k = 0; k < n; k += 1) near += places[i].has ? out[k] == places[i].has : out[k] < frames[1].at.regs.rip && frames[1].at.regs.rip - out[k] <= 64;
    if (n > places[i].most || (line || i == 0 ? (places[i].most != 0) != (near != 0) : false)) FAIL("%s is answered with %u places, %u of them the right one", where.text, n, near);
  }
  return 0;
}

#define NAME "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
/* Is the text one of the list's, which `between` separates? */
static bool word(const char *list, char between, const char *text, size_t n)
{
  for (const char *w = list; *w; w += strcspn(w, (char[]){between, 0}), w += *w == between)
    if (strcspn(w, (char[]){between, 0}) == n && !memcmp(w, text, n)) return true;
  return false;
}

int main(int argc, char **argv)
{
  const char *dropped = argc > 8 ? argv[8] : "", *other = argc > 9 ? argv[9] : "";
  Engine e;
  uint32_t thread, xs = 0, shown = 0, in_memory = 0, in_register = 0, by_value = 0, missing = 0, written = 0, locked = 0, bytes = 0, cut = 0;
  static View view;
  ViewRow *rows = malloc(ROWS * sizeof *rows);
  uint64_t seed = argc > 7 ? strtoull(argv[7], NULL, 10) : 1, looked[2] = {0}, before[2] = {0};
  if (argc < 2 || !engine_open(&e, argv[1], 0, &thread)) FAIL("cannot start");
  const EngineModule *m = e.modules;
  RdiSpan found = rdi_matches(&m->rdi, RDI_NAMES_PROCEDURES, "probe", 5);
  if (!found.n) FAIL("no probe in the debug information");
  uint64_t probe = m->image.base + *rdi_at(&m->rdi, RDI_SCOPE_VOFFS, uint64_t, rdi_at(&m->rdi, RDI_SCOPES, RdiScope, rdi_at(&m->rdi, RDI_PROCEDURES, RdiSymbol, *(const uint32_t *)found.p)->root_scope)->voff_first);
  for (;;) {
    TargetEvent ev = engine_run(&e, ENGINE_RUN, 0, &probe, 1);
    if (ev.kind == TARGET_BREAK && !shown) continue;   /* the loader's own */
    if (ev.kind == TARGET_EXIT) break;
    TargetRegs r;
    char text[128] = {0}, as[160];
    const char *why;
    if (ev.kind != TARGET_TRAP || !target_regs(e.target, ev.thread, &r)) FAIL("event %d (code %llx), not a stop at probe", ev.kind, (unsigned long long)ev.code);
    uint64_t addr = r.gp[2], size = r.gp[8], value = r.gp[9];
    for (uint32_t i = 0; i < sizeof text - 1 && target_read(e.target, r.gp[1] + i, &text[i], 1) && text[i]; i += 1) {}
    if (asks(&e, ev.thread, &view, probe, addr == 7 ? (uint32_t)value : 0, &seed)) FAIL("(asked where %s is shown)", text);
    if (addr == 7) {   /* the stack: probe, then the functions the program names, the last of them main */
      ViewFrame frames[16];
      uint32_t count = view_stack(&e, r, 0, frames, 16), f = 1;
      for (uint32_t first = 0; first <= count; first += 1) {   /* any window is that part of the whole */
        ViewFrame part[3];
        uint32_t got = view_stack(&e, r, first, part, 3), want = count - first < 3 ? count - first : 3;
        for (uint32_t i = 0; i < got && i < want; i += 1) {
          const ViewFrame *a = &part[i], *b = &frames[first + i];
          if (memcmp(&a->at.regs, &b->at.regs, sizeof a->at.regs) || a->at.called != b->at.called || a->at.inlined != b->at.inlined || a->module != b->module || a->name.p != b->name.p || a->line.file != b->line.file || a->line.line != b->line.line) got = 99;
        }
        if (got != want) FAIL("3 frames from %u of the stack of %s are not that part of its %u", first, text, count);
      }
      char got[128] = {0};
      for (uint32_t at = 0; f < count && !(f > 1 && frames[f - 1].name.n == 4 && !memcmp(frames[f - 1].name.p, "main", 4)); f += 1)
        at += (uint32_t)snprintf(got + at, sizeof got - at, "%s%.*s", f > 1 ? " " : "", (int)frames[f].name.n, (const char *)frames[f].name.p);
      if (count == 16 || !frames[count - 1].at.regs.rip || frames[0].name.n != 5 || memcmp(frames[0].name.p, "probe", 5) || frames[0].at.called || !frames[1].at.called || strcmp(got, text)) FAIL("the stack of %u frames is %s, the program says %s", count, got, text);
      for (uint32_t i = 1; i < f; i += 1) {   /* each stands at the line the program says: the innermost at this report's, the others where they call */
        char line_in[64];
        uint64_t line = value;
        snprintf(line_in, sizeof line_in, "line_in_%.*s", (int)frames[i].name.n, (const char *)frames[i].name.p);
        if (i > 1) line = eval_load(&e, &frames[i].at, eval(&e, &frames[i].at, line_in, &why), &why).at;
        /* and its own x is the one it had when it called, not the x of an inlined call inside it (optimised, it may have no place) */
        char *x_in = line_in + 3;   /* x_in_<function> */
        *x_in = 'x';
        EvalValue had = eval_load(&e, &frames[i].at, eval(&e, &frames[i].at, x_in, &why), &why), x = eval_load(&e, &frames[i].at, eval(&e, &frames[i].at, "x", &why), &why);
        if (i > 1 && had.kind && (x.kind ? x.at != had.at : strcmp(why, "it has no place here") != 0)) FAIL("frame %u of %s has x = %lld (%s), the program says %lld", i, text, (long long)x.at, x.kind ? "" : why, (long long)had.at);
        xs += i > 1 && had.kind && x.kind;
        if (frames[i].module != m || frames[i].line.line != line || frames[i].line.file != frames[0].line.file) FAIL("frame %u of %s stands at line %u, the program says %llu", i, text, frames[i].line.line, (unsigned long long)line);
      }
      shown += 1;
      continue;
    }
    if (!unwind_step(target_read, e.target, &engine_module(&e, r.rip)->image, &r)) FAIL("%s: no frame above probe", text);
    unsigned long long voff = r.rip - m->image.base;
    EvalFrame at = {r, true, 0};
    if (table(&e, &at, &view, rows, &seed, looked, before)) FAIL("(the table at +0x%llx, where %s is shown)", voff, text);
    if (addr == 6 && ++shown) continue;
    if (word(other, ';', text, strlen(text)) && ++missing) continue;   /* a text this compiler is known to work out otherwise than C says, or to have too little about (check.sh has the reasons) */
    if (addr == 4) {   /* the variables in scope: each is one the program names; all of them, unless optimised code has dropped some; a name with $ is the compiler's own (RAD lists them too) */
      RdiSpan names[ROOTS];
      uint32_t count = eval_locals(&e, &at, names, ROOTS), said = 0;
      for (uint32_t i = 0; i < count; i += 1) {
        const char *w = text;
        while (*w && !(names[i].n == strcspn(w, " ") && !memcmp(names[i].p, w, names[i].n))) w += strcspn(w, " "), w += *w == ' ';
        if (!*w && *(const char *)names[i].p != '$') FAIL("at +0x%llx the variable %.*s is in scope, the program says only %s", voff, (int)names[i].n, (const char *)names[i].p, text);
      }
      for (const char *w = text; *w; w += strcspn(w, " "), w += *w == ' ') said += 1;
      if (said == count) shown += 1; else missing += 1;
      continue;
    }
    EvalValue v = eval(&e, &at, text, &why);
    if (!v.kind && !strcmp(why, "it has no place here") && ++missing) continue;
    /* a variable this compiler is known to have dropped (check.sh has the reasons) */
    for (const char *w = text; !v.kind && !strcmp(why, "no such name") && *w; w += strspn(w, NAME), w += strcspn(w, NAME))
      if (word(dropped, ' ', w, strspn(w, NAME))) { why = NULL; break; }
    if (!v.kind && !why && ++missing) continue;
    if (addr == 5) {   /* no meaning, and why */
      char want[64] = {0};
      for (uint32_t i = 0; i < sizeof want - 1 && target_read(e.target, value + i, &want[i], 1) && want[i]; i += 1) {}
      if (v.kind || strcmp(why, want)) FAIL("%s (at +0x%llx): %s, the program says: %s", text, voff, v.kind ? "it has a value" : why, want);
      shown += 1;
      continue;
    }
    if (!v.kind) FAIL("%s (shown at +0x%llx): %s", text, voff, why);
    if (addr == 2 || addr == 3) {   /* as a person is shown it, or its type */
      char want[96] = {0}, got[128];
      for (uint32_t i = 0; i < sizeof want - 1 && target_read(e.target, value + i, &want[i], 1) && want[i]; i += 1) {}
      if (addr == 2) view_value(&e, &at, text, got, sizeof got); else view_type(v.type, got, sizeof got);
      const char *part = want[0] == '"' && strchr(got, ' ') ? strchr(got, ' ') + 1 : got;
      if (strcmp(part, want)) FAIL("%s (at +0x%llx) %s %s, the program says %s", text, voff, addr == 2 ? "is shown as" : "has the type", got, want);
      shown += 1;
      continue;
    }
    if (size && eval_shape(v.type).size != size) FAIL("%s (at +0x%llx): its type has %u bytes, the program says %llu", text, voff, eval_shape(v.type).size, (unsigned long long)size);
    if (addr > 4 && (v.kind != EVAL_MEMORY || v.at != addr)) FAIL("%s (at +0x%llx): kind %d at %llx, the program says memory at %llx", text, voff, v.kind, (unsigned long long)v.at, (unsigned long long)addr);
    snprintf(as, sizeof as, "(%s)(%s)", addr ? "double" : "unsigned long long", text);
    EvalValue is = addr > 4 ? v : eval(&e, &at, as, &why);
    if (addr < 2 && (is.kind != EVAL_VALUE || is.at != value)) FAIL("%s (at +0x%llx): kind %d, %llx (%s), the program says %llx", as, voff, is.kind, (unsigned long long)is.at, why ? why : "no error", (unsigned long long)value);
    /* the program's bytes around what is in memory, asked for (up to 9000 from up to 9000 before it): each is the byte there, and they end only where no byte can be read */
    if (v.kind == EVAL_MEMORY && !eval_shape(v.type).count) {
      struct { Ask a; char text[600]; } mem = {.a = {.kind = ASK_MEMORY, .thread = ev.thread, .frame = 1, .most = random_below(&seed, 9000), .first = (uint64_t)0 - random_below(&seed, 9000)}};
      static uint64_t answer[2 + 9000 / 8];
      const unsigned char *got = (const unsigned char *)(answer + 1);
      unsigned char is;
      uint32_t n = ask_answer(&e, &mem, (uint32_t)sizeof mem.a + (uint32_t)snprintf(mem.text, sizeof mem.text, "&(%s)", text) + 1, answer, sizeof answer);
      if (n < 8 || n - 8 > mem.a.most || answer[0] != v.at + mem.a.first) FAIL("%s (at +0x%llx): asked for %u bytes around it, the answer has %u and begins at %llx", text, voff, mem.a.most, n, (unsigned long long)answer[0]);
      for (uint32_t i = 0; i < n - 8; i += 1) if (!target_read(e.target, answer[0] + i, &is, 1) || is != got[i]) FAIL("%s (at +0x%llx): byte %u of the memory around it is another", text, voff, i);
      if (n - 8 < mem.a.most && target_read(e.target, answer[0] + n - 8, &is, 1)) FAIL("%s (at +0x%llx): the memory around it ends after %u bytes where there is more", text, voff, n - 8);
      bytes += n - 8, cut += n - 8 < mem.a.most;
    }
    /* a number or an address in memory is written through an ask as C assigns (0 where it is not 0, else 1): that is there after it, and the 8 bytes on either side are as before */
    EvalShape shape = eval_shape(v.type);
    if (v.kind == EVAL_MEMORY && (shape.form == EVAL_INT || shape.form == EVAL_REAL || shape.form == EVAL_ADDRESS)) {
      struct { Ask a; char text[600]; } set = {.a = {.kind = ASK_SET, .thread = ev.thread, .frame = 1}};
      unsigned char before[24], after[24], answer[8];
      uint32_t len = (uint32_t)snprintf(set.text, sizeof set.text, "%s%c(%s) == 0", text, 0, text) + 1;
      EvalValue more = eval_as(&e, &at, set.text + strlen(text) + 1, v.type, &why);
      if (len > sizeof set.text || !more.kind || !target_read(e.target, v.at - 8, before, shape.size + 16)) FAIL("%s (at +0x%llx): nothing to write there", text, voff);
      bool done = ask_answer(&e, &set, (uint32_t)sizeof set.a + len, answer, sizeof answer) == 1;
      if (shape.count) {   /* a bit field is not written (its neighbours share its bytes) */
        if (done) FAIL("%s (at +0x%llx): a bit field was written", text, voff);
        goto counted;
      }
      /* not written only where the program's memory takes no write (a string of the read-only data) */
      if (!done && !target_write(e.target, v.at, before + 8, shape.size) && ++locked) goto counted;
      if (!done || !target_read(e.target, v.at - 8, after, shape.size + 16)) FAIL("%s (at +0x%llx) is not written", text, voff);
      if (memcmp(after, before, 8) || memcmp(after + 8, &more.at, shape.size) || memcmp(after + 8 + shape.size, before + 8 + shape.size, 8)) FAIL("%s (at +0x%llx): written otherwise than as 0 or 1 in its %u bytes", text, voff, shape.size);
      if (!target_write(e.target, v.at, before + 8, shape.size)) FAIL("%s is not put back", text);
      written += 1;
    }
  counted:
    shown += 1, by_value += addr < 2, in_memory += v.kind == EVAL_MEMORY, in_register += v.kind == EVAL_REGISTER;
  }
  engine_close(&e);
  if (argc < 7 || shown + missing != (uint32_t)atoi(argv[2]) || shown < (uint32_t)atoi(argv[3]) || in_register < (uint32_t)atoi(argv[4]) || looked[0] < strtoull(argv[5], NULL, 10) || xs < (uint32_t)atoi(argv[6])) FAIL("%u frames with their own x; %u texts shown, only %u right, %u variables in a register, %llu rows looked at", xs, shown + missing, shown, in_register, (unsigned long long)looked[0]);
  if (!written || !bytes) FAIL("nothing was written, or no memory read");
  printf("zoo_check: %u bytes of memory read (%u reads ended where the memory does); %u written and put back, %u in memory that takes no write; ", bytes, cut, written, locked);
  printf("%u names and expressions are what the program says (%u by value; %u in memory, %u variables in a register; %u frames of a stack with their own x); %u have a variable without a place there; %llu rows of the tables looked at, the deepest %llu below its variable\n", shown, by_value, in_memory, in_register, xs, missing, (unsigned long long)looked[0], (unsigned long long)looked[1]);
  return 0;
}
