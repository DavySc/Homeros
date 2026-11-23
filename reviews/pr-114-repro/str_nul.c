#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "str.h"
int main(void){ Str s={0}; str_addz(&s,"keep"); int r=str_addf(&s,"abcdef%ls",L"\xD800"); printf("r=%d n=%zu s=[%s] len=%zu\n",r,s.n,s.s,strlen(s.s)); str_free(&s); return 0; }
