#include <windows.h>
#include <stdio.h>
int main(void){HDC dc=CreateCompatibleDC(NULL);const wchar_t*n[]={L"JetBrainsMono Nerd Font Mono",L"JetBrains Mono"};
for(int i=0;i<2;i++){HFONT f=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,n[i]);
SelectObject(dc,f);wchar_t g[32];GetTextFaceW(dc,32,g);TEXTMETRICW t;GetTextMetricsW(dc,&t);printf("%ls -> %ls h=%ld w=%ld\n",n[i],g,t.tmHeight,t.tmAveCharWidth);}return 0;}
