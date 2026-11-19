/* The program the process-control stress test (stress_target.c) runs: threads that never stop
   calling three small functions, and one that does so between starting short-lived threads, raising an
   exception it handles itself now and then, until the debugger sets `quit`. It tells the debugger
   where things are in a debug string: the three functions, the counters, `quit`, `handled`. */
#include <windows.h>
#include <stdio.h>

volatile long long counts[4], handled;
volatile int quit;

__declspec(noinline) void first(int i) { counts[i] += 1; }
__declspec(noinline) void second(int i) { counts[i] += 2; }
__declspec(noinline) void third(int i) { counts[i] += 3; }

static DWORD WINAPI worker(void *arg)
{
  for (;;) first((int)(INT_PTR)arg), second((int)(INT_PTR)arg), third((int)(INT_PTR)arg);
  return 0;   /* never; mingw's gcc asks for it */
}

static DWORD WINAPI brief(void *arg)
{
  third((int)(INT_PTR)arg);
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
    HANDLE h = CreateThread(NULL, 0, brief, (void *)(INT_PTR)3, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }
  return 7;
}
