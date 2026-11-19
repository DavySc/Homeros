/* The whole file at path, in a block the caller frees; null when it cannot be read. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void *load(const char *path, uint64_t *size)
{
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  void *buf = malloc((size_t)n + 8);
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = 0; }
  fclose(f);
  *size = (uint64_t)n;
  return buf;
}
