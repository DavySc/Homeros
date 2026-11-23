#include <stdio.h>
#include <time.h>
#include "str.h"
int main(void){ struct timespec a,b; Str s={0}; clock_gettime(CLOCK_MONOTONIC,&a);
 for(int r=0;r<100000;r++){ for(int c=0;c<12;c++) str_addf(&s,"%d\t%s",r*c,"OrderLine-value"); str_addf(&s,"\n"); if(s.n>1<<20) s.n=0; }
 clock_gettime(CLOCK_MONOTONIC,&b); printf("%.1f\n",(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6); return 0; }
