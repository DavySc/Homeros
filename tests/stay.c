/* The program that runs already when the debugger comes (check.sh, front): it says who it is, and ends with 5 if it is
   still running when the debugger has gone again. */
#include <stdio.h>
#include <windows.h>
int main(void)
{
  printf("%lu\n", (unsigned long)GetCurrentProcessId()), fflush(stdout);
  Sleep(5000);
  return 5;
}
