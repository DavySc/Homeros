#include <stddef.h>
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures 1 <= \result <= 4 && i + \result <= n; */
size_t u8len(const unsigned char *s, size_t n, size_t i) { return 1; }
