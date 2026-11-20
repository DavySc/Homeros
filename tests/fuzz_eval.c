/* libFuzzer: the input's last bytes (up to 96, after the last zero byte) as an expression, evaluated at a frame of a module whose debug information
   is the whole input (when it is a usable RDI file: else there is no module). The properties: no read outside
   the file or the text, an end for every text (the fuzzer's timeout), and the header's promise: no value exactly
   when there is an error. */
#include "../dbg/eval.c"
#include <stdlib.h>

enum { BASE = 0x140000000, MEMORY = 0x7000 };
bool target_read(Target *t, uint64_t addr, void *bytes, uint32_t n)
{
  (void)t;
  if (addr < MEMORY || addr - MEMORY > 4096 - n) return false;
  /* every 8 bytes hold an address in this memory, so that what an expression reads there can be followed */
  for (uint32_t i = 0; i < n; i += 1) ((uint8_t *)bytes)[i] = (uint8_t)((MEMORY + ((addr + i) / 8 * 56 & 0xff8)) >> 8 * ((addr + i) % 8));
  return true;
}
const EngineModule *engine_module(const Engine *e, uint64_t addr) { return e->module_count && addr >= BASE ? e->modules : NULL; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  uint64_t *buf = malloc(size ? size : 1);   /* an exact-size heap copy, so ASan sees one byte past the file */
  size_t n = 0;
  while (n < size && n < 96 && data[size - n - 1]) n += 1;
  char *text = malloc(n + 1);
  memcpy(buf, data, size);
  memcpy(text, data + size - n, n), text[n] = 0;
  EngineModule m = {.image = {.base = BASE}};
  Engine e = {.modules = &m, .module_count = rdi_open(&m.rdi, buf, size)};
  TargetRegs regs = {.rip = BASE + (size > 8 ? data[size / 2] | (uint64_t)data[size / 2 + 1] << 8 : 0)};
  for (int i = 0; i < 16; i += 1) regs.gp[i] = MEMORY + 16 * (uint64_t)i;
  for (int called = 0; called < 2; called += 1) {
    const char *error = "unset";
    uint64_t bytes = 0;
    EvalValue v = eval(&e, &regs, called, text, &error);
    if ((v.kind == EVAL_NONE) != (error != NULL)) abort();
    if (v.kind) eval_read(&e, &regs, v, &bytes, eval_size(v.type));
  }
  free(text), free(buf);
  return 0;
}
