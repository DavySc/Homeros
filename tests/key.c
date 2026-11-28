/* The program that presses the halt key itself: Ctrl+C on the console it shares with the debugger (check.sh, front). */
#include <windows.h>
int main(void)
{
  GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
  Sleep(1000);   /* the key comes on a thread of its own, later */
  return 3;
}
