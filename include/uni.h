/* Unicode kernels.  ref/uni.c steps by code point and calls every character
   one cell wide; package 3 (src/uni/) steps by extended grapheme cluster and
   takes widths from East Asian Width, with tables generated from Unicode 16.
   The contracts below state each result and are what Frama-C WP proves
   (make frama) for whichever implementation is built; no bit tricks in
   proved code. */
#ifndef UNI_H
#define UNI_H
#include <stddef.h>

/*@ predicate cont(integer c) = 0x80 <= c < 0xC0;
    logic integer want(integer c) = c < 0x80 ? 1 : (0xC0 <= c < 0xE0) ? 2 :
                                    (0xE0 <= c < 0xF0) ? 3 : (0xF0 <= c < 0xF8) ? 4 : 0;
    // the continuation bytes are listed, not quantified, so the provers finish
    predicate wellformed{L}(unsigned char *s, integer n, integer i, integer k) =
      k == want(s[i]) && k >= 1 && i + k <= n &&
      (k >= 2 ==> cont(s[i + 1])) && (k >= 3 ==> cont(s[i + 2])) && (k >= 4 ==> cont(s[i + 3]));
    logic integer pay(integer c) = c - 0x80;
*/

/* Length of the well-formed sequence at s[i], or 1 for an invalid byte. */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures 1 <= \result <= 4 && i + \result <= n;
    ensures wellformed(s, n, i, want(s[i])) ==> \result == want(s[i]);
    ensures !wellformed(s, n, i, want(s[i])) ==> \result == 1;
*/
size_t uni_len(const unsigned char *s, size_t n, size_t i);

/* Start of the character before i (i > 0): the start of the well-formed
   sequence that ends at i if there is one, else i - 1.  (At most one can
   end there: inside a sequence every byte but the first is a continuation
   byte, which starts none.) */
/*@ requires n > 0 && 0 < i <= n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures i - 4 <= \result < i;
    ensures \result == i - 1 || wellformed(s, n, \result, i - \result);
    ensures i >= 2 && wellformed(s, n, i - 2, 2) ==> \result == i - 2;
    ensures i >= 3 && wellformed(s, n, i - 3, 3) ==> \result == i - 3;
    ensures i >= 4 && wellformed(s, n, i - 4, 4) ==> \result == i - 4;
*/
size_t uni_prev(const unsigned char *s, size_t n, size_t i);

/* Code point at s[i], or 0xFFFD for an invalid byte. */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures s[i] < 0x80 ==> \result == s[i];
    ensures want(s[i]) == 2 && wellformed(s, n, i, 2) ==> \result == (s[i] - 0xC0) * 64 + pay(s[i + 1]);
    ensures want(s[i]) == 3 && wellformed(s, n, i, 3) ==>
      \result == ((s[i] - 0xE0) * 64 + pay(s[i + 1])) * 64 + pay(s[i + 2]);
    ensures want(s[i]) == 4 && wellformed(s, n, i, 4) ==>
      \result == (((s[i] - 0xF0) * 64 + pay(s[i + 1])) * 64 + pay(s[i + 2])) * 64 + pay(s[i + 3]);
    ensures s[i] >= 0x80 && !wellformed(s, n, i, want(s[i])) ==> \result == 0xFFFD;
*/
unsigned uni_cp(const unsigned char *s, size_t n, size_t i);

/* Cells the character takes on screen: 1 or 2.  Not proved: the reference
   says 1 for everything, package 3 takes it from East Asian Width tables. */
int uni_width(unsigned cp);
#endif
