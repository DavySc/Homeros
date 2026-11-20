/* ws LANG GAP: a word, GAP bytes of blank lines, then "(x)." ; time of a
   50-line frame (256 bytes a call) at the top, median of 21 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "tsql.h"
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static int cmp(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
int main(int c,char**v){ int lang=atoi(v[1]); size_t gap=strtoul(v[2],0,10);
 const char *w = lang==2 ? "edge" : lang==1 ? "{\"a\"" : "select";
 size_t wl=strlen(w), n=wl+gap+5; char*b=malloc(n); memcpy(b,w,wl); for(size_t i=0;i<gap;i++) b[wl+i]=(i%2)?'\n':' '; memcpy(b+wl+gap,"(x).\n",5);
 Text*t=text_new(b,n); Tsql*q=tsql_new(t,lang); tsql_update(q,NULL);
 unsigned char f[256]; double r[21];
 for(int k=0;k<21;k++){ double t0=now(); for(size_t l=0;l<50;l++){size_t a=text_line_start(t,l),z=text_line_start(t,l+1); for(;a<z;a+=256){size_t e=a+256<z?a+256:z; tsql_faces(q,a,e,f);}} r[k]=now()-t0;}
 qsort(r,21,sizeof*r,cmp); printf("lang %d gap %zu: frame median %.3f ms (min %.3f max %.3f) face0=%d\n",lang,gap,r[10],r[0],r[20],f[0]); return 0;}
