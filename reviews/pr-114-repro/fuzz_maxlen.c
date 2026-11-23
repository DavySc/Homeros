#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) { if (n >= 300 && d[299] == 'Z') abort(); return 0; }
