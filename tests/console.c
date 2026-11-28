/* console <command line>: runs it on a console of its own, with this one's input and output, and ends as it ends. For
   the halt key (check.sh, front): Ctrl+C goes to all that share a console, and must not go to the check itself. */
#include <string.h>
#include <windows.h>
int main(void)
{
  char *line = GetCommandLineA();
  line += strcspn(line, " "), line += strspn(line, " ");
  STARTUPINFOA start = {.cb = sizeof start, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = GetStdHandle(STD_INPUT_HANDLE), .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE), .hStdError = GetStdHandle(STD_ERROR_HANDLE)};
  PROCESS_INFORMATION made;
  DWORD code = 1;
  if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &start, &made)) return 2;
  WaitForSingleObject(made.hProcess, INFINITE), GetExitCodeProcess(made.hProcess, &code);
  return (int)code;
}
