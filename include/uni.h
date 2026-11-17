/* Unicode kernels.  ref/uni.c steps by code point and calls every character
   one cell wide; package 3 (src/uni/) steps by extended grapheme cluster and
   takes widths from East Asian Width, with tables generated from Unicode 16
   and Frama-C result contracts (no bit tricks in proved code). */
#ifndef UNI_H
#define UNI_H
#include <stddef.h>

/* Length of the well-formed sequence at s[i], or 1 for an invalid byte. */
size_t uni_len(const unsigned char *s, size_t n, size_t i);
/* Start of the character before i (i > 0). */
size_t uni_prev(const unsigned char *s, size_t n, size_t i);
/* Code point at s[i], or 0xFFFD for an invalid byte. */
unsigned uni_cp(const unsigned char *s, size_t n, size_t i);
/* Cells the character takes on screen: 1 or 2. */
int uni_width(unsigned cp);
#endif
