#include <stdio.h>
#include <stddef.h>
size_t u8len(const unsigned char *s, size_t n, size_t i);
int main(){ unsigned char a[]={0xC0,0x80}, b[]={0xED,0xA0,0x80}, c[]={0xF7,0xBF,0xBF,0xBF}, d[]={0xE0,0x80,0x80};
 printf("overlong C0 80 -> %zu\nsurrogate ED A0 80 -> %zu\n>U+10FFFF F7 BF BF BF -> %zu\noverlong E0 80 80 -> %zu\n",u8len(a,2,0),u8len(b,3,0),u8len(c,4,0),u8len(d,3,0)); }
