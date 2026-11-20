/* The program tests/stress_engine.c runs: three threads in the same two functions, and, once the test writes `go`,
   an exception this program handles, one nobody handles, and a fault. */
#include <windows.h>

volatile LONG handled, go;

__attribute__((noinline)) void shared(volatile LONG *n) { *n += 1; }
__attribute__((noinline)) void caller(volatile LONG *n) { shared(n), shared(n); }

static DWORD WINAPI worker(void *arg)
{
  volatile LONG n = 0;
  (void)arg;
  while (handled < 2) caller(&n);   /* for ever */
  return 0;
}

static LONG CALLBACK handler(EXCEPTION_POINTERS *x)
{
  if (x->ExceptionRecord->ExceptionCode != 0xe0000001) return EXCEPTION_CONTINUE_SEARCH;
  handled += 1;
  return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
  AddVectoredExceptionHandler(1, handler);
  for (int i = 0; i < 3; i += 1) CreateThread(0, 0, worker, 0, 0, 0);
  while (!go) Sleep(1);
  RaiseException(0xe0000001, 0, 0, 0);
  RaiseException(0xe0000002, 0, 0, 0);
  *(volatile int *)0 = 1;
  return 7;
}
