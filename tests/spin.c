/* The program the process-control stress test (stress_target.c) runs: threads that never stop
   calling three small functions, and one that does so between starting short-lived threads, raising an
   exception it handles itself now and then, until the debugger sets `quit`. It tells the debugger
   where things are in a debug string: the three functions, the counters, `quit`, `handled`. */
#include <windows.h>
#include <stdio.h>

volatile long long counts[5], handled;   /* one for each of the four threads that stay, one for the short-lived */
volatile int quit;

/* counts[i] += n, written out so that the first instruction torn by a trap byte does harm: without its first byte it
   leaves half of the address in rax, and a thread that went on behind a trap byte faults (stress_target.c, Whole). */
#define ADDER(name, n) void name(long long i); __asm__(".text\n" #name ": lea counts(%rip), %rax\n addq $" #n ", (%rax,%rcx,8)\n ret\n");
ADDER(first, 1) ADDER(second, 2) ADDER(third, 3)

static DWORD WINAPI worker(void *arg)
{
  for (;;) first((INT_PTR)arg), second((INT_PTR)arg), third((INT_PTR)arg);
  return 0;   /* never; mingw's gcc asks for it */
}

static DWORD WINAPI brief(void *arg)
{
  third((INT_PTR)arg);
  return 0;
}

static LONG WINAPI handler(EXCEPTION_POINTERS *x)
{
  if (x->ExceptionRecord->ExceptionCode != 0xe0000001) return EXCEPTION_CONTINUE_SEARCH;
  handled += 1;
  return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
  char text[200];
  AddVectoredExceptionHandler(1, handler);
  snprintf(text, sizeof text, "spin %p %p %p %p %p %p", (void *)first, (void *)second, (void *)third, (void *)counts, (void *)&quit, (void *)&handled);
  for (INT_PTR i = 0; i < 3; i += 1) CreateThread(NULL, 0, worker, (void *)i, 0, NULL);
  for (int n = 0; !quit; n += 1) {
    OutputDebugStringA(text);
    if (n % 8 == 7) RaiseException(0xe0000001, 0, 0, NULL);
    for (int i = 0; i < 100000; i += 1) first(3), second(3);   /* this thread is busy too */
    HANDLE h = CreateThread(NULL, 0, brief, (void *)(INT_PTR)4, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }
  return 7;
}
