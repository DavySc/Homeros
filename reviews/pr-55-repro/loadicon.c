#include <windows.h>
#include <stdio.h>
int main(void){ HICON a=LoadIconW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(1));
 HICON s=LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(1),IMAGE_ICON,16,16,0);
 HICON b=LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(1),IMAGE_ICON,256,256,0);
 ICONINFO ii; BITMAP bm={0}; if(s&&GetIconInfo(s,&ii)) GetObjectW(ii.hbmColor,sizeof bm,&bm);
 printf("large %p small %p (%ldx%ld) 256 %p\n",(void*)a,(void*)s,bm.bmWidth,bm.bmHeight,(void*)b); return !(a&&s&&b);}
