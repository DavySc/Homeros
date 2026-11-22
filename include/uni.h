/* Unicode kernels (package 3, src/uni/): steps by extended grapheme cluster
   as Neovim 0.12 does and takes widths as Neovim does, with tables generated from
   Unicode 18 data (the version of the utf8proc that the tools' Neovim links).
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
    logic integer ulen{L}(unsigned char *s, integer n, integer i) =
      wellformed(s, n, i, want(s[i])) ? want(s[i]) : 1;
    // the code point Neovim's utf_ptr2char() reads at i: an invalid byte reads as its own value
    logic integer cpx{L}(unsigned char *s, integer n, integer i) =
      !wellformed(s, n, i, want(s[i])) || want(s[i]) == 1 ? s[i] :
      want(s[i]) == 2 ? (s[i] - 0xC0) * 64 + pay(s[i + 1]) :
      want(s[i]) == 3 ? ((s[i] - 0xE0) * 64 + pay(s[i + 1])) * 64 + pay(s[i + 2]) :
      (((s[i] - 0xF0) * 64 + pay(s[i + 1])) * 64 + pay(s[i + 2])) * 64 + pay(s[i + 3]);

    // Unicode data per code point c.  gcb: Grapheme_Cluster_Break with
    // Extended_Pictographic, as 0 Other, 1 Control (CR, LF), 2 Extend, 3 L, 4 V,
    // 5 T, 6 LV, 7 LVT, 8 Regional_Indicator, 9 SpacingMark, 10 Prepend, 11 ZWJ,
    // 12 Extended_Pictographic.  incb: Indic_Conjunct_Break, as 0 None, 1 Linker,
    // 2 Consonant, 3 Extend.  cellc: Neovim's cells, as 0 for 1, 1 for 2, 2 for
    // 4 (<xx>), 3 for 6 (<xxxx>).  Data, not logic: the implementation's tables
    // equal Neovim's own values for every c below 2^21 (test_uni_nvim.c).
    axiomatic UniData {
      logic integer gcb(integer c);
      logic integer incb(integer c);
      logic integer cellc(integer c);
    }
    // no break between a cluster in state l and a code point of class t:
    // utf8proc's grapheme_break_simple() is false.  State 13 is
    // Extended_Pictographic Extend* ZWJ.
    predicate glue(integer l, integer t) =
      l != 1 && t != 1 &&                                         // GB4, GB5
      ((l == 3 && (t == 3 || t == 4 || t == 6 || t == 7)) ||      // GB6
       ((l == 6 || l == 4) && (t == 4 || t == 5)) ||             // GB7
       ((l == 7 || l == 5) && t == 5) ||                         // GB8
       t == 2 || t == 11 || t == 9 || l == 10 ||                 // GB9, GB9a, GB9b
       (l == 13 && t == 12) || (l == 8 && t == 8));              // GB11, GB12/13
    // the state after t, as utf8proc's grapheme_break_extended()
    logic integer nbc(integer l, integer t) =
      l == 8 && t == 8 ? 0 : l == 12 && t == 2 ? 12 : l == 12 && t == 11 ? 13 : t;
    logic integer nic(integer i, integer t) = i == 1 && t == 3 ? 1 : t;
    // c joins the cluster: utf8proc (GB9c: Linker Extend* x Consonant; GB3:
    // CR x LF, which only an overlong LF reaches, as ASCII never joins), or
    // Neovim's arabic_combine() (lam then alef, 'arabicshape' on)
    predicate joins(integer l, integer i, integer p, integer c) =
      glue(l, gcb(c)) || (i == 1 && incb(c) == 2) || (p == 0x0D && c == 0x0A) ||
      (p == 0x644 && (c == 0x622 || c == 0x623 || c == 0x625 || c == 0x627));
    // bytes from j on that join a cluster in state (l, i) whose last code
    // point is p; ASCII never joins (Neovim's utf_composinglike())
    logic integer more{L}(unsigned char *s, integer n, integer j, integer l, integer i, integer p) =
      j >= n || s[j] < 0x80 || !joins(l, i, p, cpx(s, n, j)) ? 0 :
      ulen(s, n, j) + more(s, n, j + ulen(s, n, j), nbc(l, gcb(cpx(s, n, j))),
                           nic(i, incb(cpx(s, n, j))), cpx(s, n, j));
    // the cluster at i, as Neovim's utfc_ptr2len(): an invalid byte is one cluster
    logic integer glen{L}(unsigned char *s, integer n, integer i) =
      s[i] >= 0x80 && !wellformed(s, n, i, want(s[i])) ? 1 :
      ulen(s, n, i) + more(s, n, i + ulen(s, n, i), gcb(cpx(s, n, i)), incb(cpx(s, n, i)), cpx(s, n, i));
    // cluster starts: the steps by glen from 0.  A cluster holds no ASCII
    // byte after its first, so every ASCII byte is a start; listing them
    // lets a step back begin at one instead of at 0.
    inductive bound{L}(unsigned char *s, integer n, integer j) {
      case zero{L}: \forall unsigned char *s, integer n; bound(s, n, 0);
      case ascii{L}: \forall unsigned char *s, integer n, integer j; 0 <= j < n && s[j] < 0x80 ==> bound(s, n, j);
      case step{L}: \forall unsigned char *s, integer n, integer j;
        0 <= j < n && bound(s, n, j) ==> bound(s, n, j + glen(s, n, j));
    }
    logic integer cells(integer k) = k == 0 ? 1 : k == 1 ? 2 : k == 2 ? 4 : 6;
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

/* Bytes in the extended grapheme cluster at s[i], as Neovim 0.12 steps. */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures \result == glen(s, n, i);
    ensures 1 <= \result <= n - i;
*/
size_t uni_glen(const unsigned char *s, size_t n, size_t i);

/* Start of the cluster that holds byte i - 1.  Neovim's utf_head_off() can
   stop inside a cluster its forward step made (a Devanagari conjunct), so
   this takes Helix's rule: the step back undoes the step forward.  It walks
   back to the nearest ASCII byte, so a step costs time in proportion to that
   distance (5.7 ms on a 1 MB line with none, measured). */
/*@ requires n > 0 && 0 < i <= n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures bound(s, n, \result) && \result < i <= \result + glen(s, n, \result);
*/
size_t uni_gprev(const unsigned char *s, size_t n, size_t i);

/* Cells the cluster at s[i] takes, as Neovim's utf_ptr2cells(): 1 for ASCII
   (the caller draws ^X and tabs), 4 for an invalid byte or U+0080..U+009F
   (<xx>), 6 for what Neovim cannot print (<xxxx>), 2 for wide characters and
   emoji, also when U+FE0F follows, else 1. */
/*@ requires n > 0 && i < n && \valid_read(s + (0 .. n - 1));
    assigns \nothing;
    ensures s[i] < 0x80 ==> \result == 1;
    ensures s[i] >= 0x80 && !wellformed(s, n, i, want(s[i])) ==> \result == 4;
    ensures s[i] >= 0x80 && wellformed(s, n, i, want(s[i])) ==> \result ==
      (cellc(cpx(s, n, i)) == 0 && gcb(cpx(s, n, i)) == 12 &&
       i + ulen(s, n, i) < n && cpx(s, n, i + ulen(s, n, i)) == 0xFE0F ? 2 : cells(cellc(cpx(s, n, i))));
*/
int uni_width(const unsigned char *s, size_t n, size_t i);

/* Writes code point c as UTF-8 into b and returns its length: the one
   encoder (Neovim's utf_char2bytes()), for the keys, the pickers, the LSP's
   JSON and the terminal. */
/*@ requires c < 0x110000 && \valid(b + (0 .. 3));
    assigns b[0 .. 3];
    ensures \result == (c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4);
    ensures wellformed(b, \result, 0, \result) && cpx(b, \result, 0) == c;
*/
int uni_put(unsigned char *b, unsigned c);
#endif
