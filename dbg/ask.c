#include "ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The end of a path that fits in n bytes, with its 0. */
static void tail(char *to, uint32_t n, const char *path, uint64_t len)
{
  uint64_t skip = len < n ? 0 : len - (n - 1);
  memcpy(to, path + skip, len - skip), to[len - skip] = 0;
}

/* Is `end` the end of the path, from the start of one of its parts on? Letters of either case are the same, and `end` may have either slash (rdi_path writes /). */
static bool ends(const char *path, uint64_t len, const char *end, uint64_t n)
{
  if (n == 0 || n > len || (n < len && path[len - n - 1] != '/')) return false;
  for (uint64_t i = 0; i < n; i += 1) {
    char a = path[len - n + i], b = end[i];
    a = a >= 'A' && a <= 'Z' ? (char)(a + 32) : a, b = b == '\\' ? '/' : b >= 'A' && b <= 'Z' ? (char)(b + 32) : b;
    if (a != b) return false;
  }
  return true;
}

uint32_t ask_answer(const Engine *e, const void *bytes, uint32_t n, void *out, uint32_t cap)
{
  Ask ask;
  TargetRegs regs;
  ViewFrame frames[ASK_MOST];
  char path[1024];
  const char *more = (const char *)bytes + sizeof ask;
  if (n < sizeof ask) return 0;
  memcpy(&ask, bytes, sizeof ask);
  uint32_t most = ask.most < ASK_MOST ? ask.most : ASK_MOST, used = 0;

  if (ask.kind == ASK_PLACES && n > sizeof ask && more[n - sizeof ask - 1] == 0) {
    const char *colon = strrchr(more, ':');
    unsigned long line = colon ? strtoul(colon + 1, NULL, 10) : 0;
    for (uint32_t i = 0; i < e->module_count; i += 1) {
      const EngineModule *m = &e->modules[i];
      uint64_t function = line ? 0 : engine_function(m, more, strlen(more));
      if (function && cap - used >= 8) memcpy((char *)out + used, &function, 8), used += 8;
      for (uint64_t f = 1; line && f < m->rdi.s[RDI_SOURCE_FILES].n; f += 1) {
        uint64_t len = rdi_path(&m->rdi, rdi_at(&m->rdi, RDI_SOURCE_FILES, RdiSourceFile, f)->path_node, path, sizeof path);
        RdiSpan voffs = len <= sizeof path && ends(path, len, more, (uint64_t)(colon - more)) ? rdi_voffs_from_line(&m->rdi, (uint32_t)f, (uint32_t)line) : (RdiSpan){0};
        for (uint64_t k = 0; k < voffs.n && cap - used >= 8; k += 1, used += 8) memcpy((char *)out + used, &(uint64_t){m->image.base + ((const uint64_t *)voffs.p)[k]}, 8);
      }
    }
    return used;
  }
  if (ask.kind == ASK_THREADS) return 4 * target_threads(e->target, out, cap / 4);
  if (ask.kind > ASK_ROWS || !target_regs(e->target, ask.thread, &regs)) return 0;

  if (ask.kind == ASK_STACK) {
    AskFrame *to = out;
    if (most > cap / sizeof *to) most = cap / sizeof *to;
    uint32_t count = view_stack(e, regs, (uint32_t)ask.first, frames, most);
    for (uint32_t i = 0; i < count; i += 1) {
      const ViewFrame *f = &frames[i];
      const Rdi *r = f->module ? &f->module->rdi : NULL;
      uint64_t len = r ? rdi_path(r, rdi_at(r, RDI_SOURCE_FILES, RdiSourceFile, f->line.file)->path_node, path, sizeof path) : 0;
      to[i] = (AskFrame){.addr = f->at.regs.rip, .line = f->line.line};
      snprintf(to[i].name, sizeof to[i].name, "%.*s", (int)(f->name.n < sizeof to[i].name ? f->name.n : sizeof to[i].name - 1), (const char *)f->name.p);
      if (f->module && f->module->path) tail(to[i].module, sizeof to[i].module, f->module->path, strlen(f->module->path));
      tail(to[i].file, sizeof to[i].file, path, len < sizeof path ? len : sizeof path);
    }
    return count * (uint32_t)sizeof *to;
  }

  /* rows: of the frame's own table of expressions, of its variables, or of its registers */
  View view;
  ViewRow rows[ASK_MOST];
  RdiSpan names[ASK_MOST * 4];
  char texts[ASK_MOST * 4 * 64], *at = texts;
  const char *roots[ASK_MOST * 4];
  uint32_t count = 0, shown = 0;
  if (n < sizeof ask + sizeof view || cap < 8 || view_stack(e, regs, ask.frame, frames, 1) != 1) return 0;
  memcpy(&view, more, sizeof view), more += sizeof view;
  if (view.count > VIEW_OPEN) return 0;
  if (ask.roots == ASK_TEXTS)
    for (const char *end = (const char *)bytes + n; count < ASK_MOST * 4 && more < end && memchr(more, 0, (size_t)(end - more)); more += strlen(more) + 1) roots[count++] = more;
  if (ask.roots == ASK_LOCALS)
    for (uint32_t i = 0, found = eval_locals(e, &frames->at, names, ASK_MOST * 4); i < found && names[i].n < (size_t)(texts + sizeof texts - at); i += 1)
      roots[count++] = at, memcpy(at, names[i].p, names[i].n), at[names[i].n] = 0, at += names[i].n + 1;
  if (ask.roots == ASK_REGISTERS) memcpy(roots, eval_registers, sizeof eval_registers), count = 18;
  AskRow *to = (AskRow *)((char *)out + 8);
  if (most > (cap - 8) / sizeof *to) most = (cap - 8) / sizeof *to;
  uint64_t total = view_rows(&view, e, &frames->at, roots, count, ask.first, rows, most, &shown);
  memcpy(out, &total, 8);
  for (uint32_t i = 0; i < shown; i += 1) {
    const char *error;
    to[i].row = rows[i];
    view_value(e, &frames->at, rows[i].text, to[i].value, sizeof to[i].value);
    view_type(eval(e, &frames->at, rows[i].text, &error).type, to[i].type, sizeof to[i].type);
  }
  return 8 + shown * (uint32_t)sizeof *to;
}
