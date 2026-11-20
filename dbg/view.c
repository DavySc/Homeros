#include "view.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Writes on from text[at], never past n (which is not 0); where the text then ends. */
static uint32_t put(char *text, uint32_t n, uint32_t at, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  int more = vsnprintf(text + at, n - at, format, args);
  va_end(args);
  return more < 0 || (uint32_t)more > n - 1 - at ? n - 1 : at + (uint32_t)more;
}

/* ---- a type as text ---- */

static uint32_t type_text(EvalType t, char *text, uint32_t n, uint32_t at)
{
  static const char *const basic[] = {[1] = "void", [4] = "char", [7] = "unsigned char", [0xb] = "unsigned short", [0xc] = "unsigned int", [0xd] = "unsigned long long",
                                      [0x11] = "signed char", "short", "int", "long long", [0x18] = "bool", [0x1b] = "float", [0x1e] = "double"};
  const Rdi *r = t.module ? &t.module->rdi : NULL;
  const RdiType *y = r ? rdi_at(r, RDI_TYPE_NODES, RdiType, t.idx) : NULL;
  uint32_t kind = y ? y->kind : t.idx, element = t.idx;
  if (kind == 0x1004) {   /* an array: what its innermost elements are, then every count from the outermost in */
    for (uint32_t hops = 0; rdi_at(r, RDI_TYPE_NODES, RdiType, element)->kind == 0x1004 && hops < 64; hops += 1) element = rdi_at(r, RDI_TYPE_NODES, RdiType, element)->a;
    at = put(text, n, type_text((EvalType){t.module, element, 0}, text, n, at), " ");
    for (uint32_t hops = 0; y->kind == 0x1004 && hops < 64; hops += 1, y = rdi_at(r, RDI_TYPE_NODES, RdiType, y->a)) at = put(text, n, at, "[%u]", y->b);
  } else if (kind >= 0x2000 && kind < 0xf000) {
    RdiSpan name = rdi_str(r, y->a);
    at = put(text, n, at, "%.*s", (int)name.n, name.p);
  } else if (kind == 0x1000 || kind == 0x1001 || kind == 0xf000) {   /* const and the like are not shown */
    at = type_text((EvalType){t.module, y->a, 0}, text, n, at);
    t.stars += kind == 0x1001;
    if (kind == 0xf000) at = put(text, n, at, " : %u", y->c);
  } else at = put(text, n, at, "%s", kind < sizeof basic / sizeof *basic && basic[kind] ? basic[kind] : kind == 0x1005 ? "function" : "?");
  for (uint32_t i = 0; i < t.stars; i += 1) at = put(text, n, at, at && text[at - 1] == '*' ? "*" : " *");
  return at;
}

void view_type(EvalType type, char *text, uint32_t n) { type_text(type, text, n, 0); }

/* ---- rows ---- */

/* The key of the child `child` of the row with the key `parent` (splitmix64's last step: every bit of both counts). */
static uint64_t mix(uint64_t parent, uint64_t child)
{
  uint64_t x = (parent ^ child) + 0x9e3779b97f4a7c15 * (child + 1);
  x = (x ^ x >> 30) * 0xbf58476d1ce4e5b9;
  x = (x ^ x >> 27) * 0x94d049bb133111eb;
  return x ^ x >> 31;
}

static uint32_t opened(const View *view, uint64_t parent, uint64_t child)
{
  uint32_t i = 0;
  while (i < view->count && (view->open[i].parent != parent || view->open[i].child != child)) i += 1;
  return i;
}

bool view_toggle(View *view, const ViewRow *row)
{
  uint32_t i = opened(view, row->parent, row->child);
  if (i < view->count) return view->open[i] = view->open[--view->count], true;
  if (view->count == VIEW_OPEN) return false;
  view->open[view->count].parent = row->parent, view->open[view->count++].child = row->child;
  return true;
}

typedef struct { const View *view; const Engine *e; const EvalFrame *frame;
                 uint64_t at, first; ViewRow *rows; uint32_t most, n; } Walk;

/* The row that the i-th member, element or pointed-at of `text` (a row of this shape) is. */
static ViewRow child(const char *text, EvalShape s, uint64_t key, uint64_t i, uint32_t depth)
{
  ViewRow row = {key, i, depth, (uint32_t)strlen(text), {0}};
  RdiSpan members = s.form == EVAL_RECORD ? eval_members(s) : (RdiSpan){0}, name = {0};
  for (uint64_t m = 0, data = 0; m < members.n && !name.p; m += 1) {
    const RdiMember *b = (const RdiMember *)members.p + m;
    if (b->kind == 1 && data++ == i) name = rdi_str(&s.to.module->rdi, b->name);
  }
  uint32_t end = s.form == EVAL_RECORD ? put(row.text, VIEW_TEXT, 0, "%s.%.*s", text, (int)name.n, name.p)
                                       : put(row.text, VIEW_TEXT, 0, "%s[%llu]", text, (unsigned long long)i);
  if (end == VIEW_TEXT - 1) row.text[0] = 0, row.label = 0;   /* cut off it would be another expression */
  return row;
}

/* How many rows a row of this shape opens into: a record's members, an array's elements, what an address leads to. */
static uint64_t children(EvalShape s)
{
  uint64_t count = 0;
  RdiSpan members = s.form == EVAL_RECORD ? eval_members(s) : (RdiSpan){0};
  for (uint64_t m = 0; m < members.n; m += 1) count += ((const RdiMember *)members.p + m)->kind == 1;
  if (s.form == EVAL_ROW) count = eval_shape(s.to).size ? s.size / eval_shape(s.to).size : 0;
  if (s.form == EVAL_ADDRESS) count = eval_shape(s.to).form != EVAL_NOTHING;
  return count;
}

/* ---- a value as text ---- */

typedef struct { const Engine *e; const EvalFrame *frame; char *text; uint32_t n; } Show;

static bool characters(EvalType t)
{
  EvalShape s = eval_shape(t);
  return s.form == EVAL_INT && s.size == 1 && !s.truth;
}

/* Writes the expression's value from text[at] on; where the text then ends. */
static uint32_t show(const Show *w, const char *expression, uint32_t at)
{
  const char *error = NULL;
  char *text = w->text;
  uint32_t n = w->n, start = at;
  EvalValue v = eval(w->e, w->frame, expression, &error);
  EvalShape s = eval_shape(v.type);
  if (v.kind && (s.form == EVAL_RECORD || (s.form == EVAL_ROW && !characters(s.to)))) {   /* each part is the expression of its row */
    at = put(text, n, at, s.form == EVAL_ROW ? "[" : "{");
    for (uint64_t i = 0, count = children(s); i < count && at < n - 1; i += 1) at = show(w, child(expression, s, 0, i, 0).text, put(text, n, at, "%s", i ? ", " : ""));
    return put(text, n, at, s.form == EVAL_ROW ? "]" : "}");
  }
  if (v.kind) v = eval_load(w->e, w->frame, v, &error), s = eval_shape(v.type);
  double d = 0;
  float f;
  uint8_t c = 1;
  if (!v.kind) return put(text, n, at, "%s", error);
  if (s.form == EVAL_REAL) {   /* the fewest digits that read back as the same number */
    if (s.size == 4) memcpy(&f, &v.at, 4), d = (double)f; else memcpy(&d, &v.at, 8);
    for (int digits = s.size == 4 ? 6 : 15, most = digits + (s.size == 4 ? 3 : 2); digits <= most; digits += 1) {   /* 9 and 17 are always enough */
      at = put(text, n, start, "%.*g", digits, d);
      if ((s.size == 4 ? (double)strtof(text + start, NULL) : strtod(text + start, NULL)) == d) break;
    }
    return strspn(text + start, "-0123456789") == at - start ? put(text, n, at, ".0") : at;   /* a whole number is still seen to be a real one */
  }
  if (s.form == EVAL_ADDRESS) at = put(text, n, at, "0x%llx", (unsigned long long)v.at);
  else if (s.truth) return put(text, n, at, v.at ? "true" : "false");
  else {   /* a number; after its name in an enum, or after the character it is */
    RdiSpan names = {0}, name = {0};
    const Rdi *r = s.names ? &v.type.module->rdi : NULL;
    const RdiUdt *u = r ? rdi_at(r, RDI_UDTS, RdiUdt, s.names) : NULL;
    if (u) names = rdi_run(r, RDI_ENUM_MEMBERS, u->member_first, u->member_count);
    for (uint64_t i = 0; i < names.n && !name.p; i += 1)
      if (((const RdiEnumMember *)names.p + i)->val == v.at) name = rdi_str(r, ((const RdiEnumMember *)names.p + i)->name);
    bool letter = !name.p && s.size == 1 && v.at >= ' ' && v.at < 127;
    if (name.p) at = put(text, n, at, "%.*s (", (int)name.n, name.p);
    if (letter) at = put(text, n, at, "'%c' (", (int)v.at);
    at = put(text, n, at, s.sign ? "%lld" : "%llu", (unsigned long long)v.at);
    return name.p || letter ? put(text, n, at, ")") : at;
  }
  /* at an address of characters (an array of them is one) the text there */
  if (!characters(s.to) || !target_read(w->e->target, v.at, &c, 1)) return at;
  at = put(text, n, at, " \"");
  for (uint64_t i = 1; c && i <= 64; i += 1) {
    at = put(text, n, at, "%c", c < ' ' ? '.' : c);
    if (!target_read(w->e->target, v.at + i, &c, 1)) c = 0;
  }
  return put(text, n, at, c ? "\"..." : "\"");
}

void view_value(const Engine *e, const EvalFrame *frame, const char *expression, char *text, uint32_t n)
{
  /* what the parts add comes after the expression in brackets, as below a root row (of one too long for that no part can be shown) */
  char root[VIEW_TEXT];
  show(&(Show){e, frame, text, n}, put(root, VIEW_TEXT, 0, "(%s)", expression) < VIEW_TEXT - 1 ? root : expression, 0);
}

/* The rows below the open row `text` with the key `key`: runs of closed ones are counted, or written where the window is;
   the open ones among them are gone into. */
static void walk(Walk *w, const char *text, uint64_t key, uint32_t depth)
{
  const char *error;
  EvalValue v = eval(w->e, w->frame, text, &error);
  EvalShape s = eval_shape(v.type);
  uint64_t count = v.kind ? children(s) : 0;
  for (uint64_t i = 0; i < count;) {
    uint64_t next = count;   /* the first open one from i on */
    for (uint32_t k = 0; k < w->view->count; k += 1)
      if (w->view->open[k].parent == key && w->view->open[k].child >= i && w->view->open[k].child < next) next = w->view->open[k].child;
    uint64_t end = next < count ? next + 1 : count, skip = w->at < w->first ? w->first - w->at : 0;   /* skip: so many rows from here on come before the window */
    for (uint64_t j = i + skip; skip < end - i && j < end && w->n < w->most; j += 1) w->rows[w->n++] = child(text, s, key, j, depth);
    w->at += end - i, i = end;
    if (next < count) walk(w, child(text, s, key, next, depth).text, mix(key, next), depth + 1);
  }
}

uint64_t view_rows(const View *view, const Engine *e, const EvalFrame *frame, const char *const *roots, uint32_t root_count,
                   uint64_t first, ViewRow *rows, uint32_t most, uint32_t *shown)
{
  Walk w = {view, e, frame, 0, first, rows, most, 0};
  for (uint32_t r = 0; r < root_count; r += 1) {
    ViewRow row = {0, 0, 0, 0, {0}};
    for (const char *c = roots[r]; *c; c += 1) row.child = mix(row.child, (uint8_t)*c);
    put(row.text, VIEW_TEXT, 0, "%s", roots[r]);
    if (w.at >= first && w.n < most) rows[w.n++] = row;
    w.at += 1;
    if (opened(view, 0, row.child) == view->count) continue;
    /* what the rows below add comes after the root in brackets: (root).member, (root)[1] */
    char text[VIEW_TEXT];
    put(text, VIEW_TEXT, 0, "(%s)", roots[r]);
    walk(&w, text, mix(0, row.child), 1);
  }
  *shown = w.n;
  return w.at;
}

/* ---- the stack ---- */

uint32_t view_stack(const Engine *e, TargetRegs regs, ViewFrame *frames, uint32_t most)
{
  uint32_t n = 0;
  for (bool called = false; n < most; called = true) {
    const EngineModule *m = engine_module(e, regs.rip - called);
    const Rdi *r = m ? &m->rdi : NULL;
    uint64_t voff = m ? regs.rip - called - m->image.base : 0;
    uint32_t innermost = m ? rdi_vmap(r, RDI_SCOPE_VMAP, voff) : 0;
    /* each inlined call around the instruction, from the innermost (RAD stops at a block between two of them: the calls outside it are not frames there) */
    uint32_t inlined = 0;
    for (uint32_t s = innermost, hops = 0; s && n < most && hops < 256; hops += 1, s = rdi_at(r, RDI_SCOPES, RdiScope, s)->parent) {
      uint32_t i = rdi_at(r, RDI_SCOPES, RdiScope, s)->inline_site;
      const RdiInlineSite *site = rdi_at(r, RDI_INLINE_SITES, RdiInlineSite, i);
      if (i) frames[n++] = (ViewFrame){{regs, called, inlined++}, m, rdi_str(r, site->name), rdi_row_in(r, site->line_table, voff).line};
    }
    if (n < most) frames[n++] = (ViewFrame){{regs, called, inlined}, m, m ? rdi_str(r, rdi_at(r, RDI_PROCEDURES, RdiSymbol, rdi_at(r, RDI_SCOPES, RdiScope, innermost)->proc)->name) : (RdiSpan){0}, m ? rdi_row(r, voff).line : (RdiLine){0}};
    /* on until a step fails or comes to no instruction (RAD's d_unwind_from_thread; its third end, a step that changes nothing, this unwinder cannot make: it pops at least the return address) */
    if (!unwind_step(target_read, e->target, m ? &m->image : NULL, &regs) || !regs.rip) break;
  }
  return n;
}
