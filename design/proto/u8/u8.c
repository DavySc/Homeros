#include <stddef.h>
/* Length of the well-formed UTF-8 sequence at s[i], or 1 for an invalid byte
   (invalid bytes are shown as single replacement cells, as Neovim does). */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures 1 <= \result <= 4 && i + \result <= n; */
size_t u8len(const unsigned char *s, size_t n, size_t i) {
  unsigned char c = s[i];
  size_t k = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
  if (k == 0 || k > n - i) return 1;
  /*@ loop invariant 1 <= j <= k;
      loop assigns j;
      loop variant k - j; */
  for (size_t j = 1; j < k; j++)
    if ((s[i + j] & 0xC0) != 0x80) return 1;
  return k;
}
