#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"
static double ms(struct timespec a, struct timespec b){return (b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;}
int main(int argc,char**argv){ (void)argc;
	size_t L=atoi(argv[1]); const char*u=argv[2]; size_t ul=strlen(u);
	char*b=malloc(L*ul+1); for(size_t i=0;i<L;i++) memcpy(b+i*ul,u,ul);
	Text*t=text_new(b,L*ul); OrgFx*x=orgfx_new(t); orgfx_update(x,NULL);
	static unsigned char f[1024]; Conceal c[256]; struct timespec t0,t1,t2;
	size_t l=text_lines(t)-60, a=text_line_start(t,l), z=text_line_start(t,text_lines(t)-1);
	clock_gettime(CLOCK_MONOTONIC,&t0);
	for(size_t p=a;p<z;p+=1024) orgfx_faces(x,p,p+1024<z?p+1024:z,f);
	clock_gettime(CLOCK_MONOTONIC,&t1);
	for(size_t k=l;k<l+50;k++){ size_t s=text_line_start(t,k),e=text_line_start(t,k+1); orgfx_faces(x,s,e,f); orgfx_conceal(x,k,c,256);}
	clock_gettime(CLOCK_MONOTONIC,&t2);
	printf("%zu lines of %s: faces of last ~60 lines %.2f ms, conceal of 50 lines %.2f ms\n",L,argv[3],ms(t0,t1),ms(t1,t2));
	return 0;}
