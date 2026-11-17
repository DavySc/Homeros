#include <stddef.h>
/* Review spike, planted bug (must NOT prove): proto/u8/u8.c with its shifts and masks rewritten as comparisons, with a contract that says
   what the result IS (the gate phase 0 asks for), not only that it is in bounds. */
/*@ predicate cont(integer c) = 0x80 <= c < 0xC0;
    logic integer want(integer c) = c < 0x80 ? 1 : (0xC0 <= c < 0xE0) ? 2 : (0xE0 <= c < 0xF0) ? 3 : (0xF0 <= c < 0xF8) ? 4 : 0;
    predicate wellformed{L}(unsigned char *s, integer n, integer i, integer k) =
      k == want(s[i]) && k >= 1 && i + k <= n && \forall integer j; 1 <= j < k ==> cont(s[i + j]);
*/
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures 1 <= \result <= 4 && i + \result <= n;
    ensures wellformed(s, n, i, want(s[i])) ==> \result == want(s[i]);
    ensures !wellformed(s, n, i, want(s[i])) ==> \result == 1;
*/
size_t u8len(const unsigned char *s, size_t n, size_t i) { return 1; }
