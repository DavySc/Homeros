/* The program the process-control stress test (stress_target.c) runs: threads that never stop
   calling three small functions, and one that keeps starting short-lived threads. It tells the
   debugger where things are in a debug string: the three functions, the counters. */
#include <windows.h>
#include <stdio.h>

volatile long long counts[4];

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

int main(void)
{
  char text[160];
  snprintf(text, sizeof text, "spin %p %p %p %p", (void *)first, (void *)second, (void *)third, (void *)counts);
  for (INT_PTR i = 0; i < 3; i += 1) CreateThread(NULL, 0, worker, (void *)i, 0, NULL);
  for (;;) {
    OutputDebugStringA(text);
    HANDLE h = CreateThread(NULL, 0, brief, (void *)(INT_PTR)3, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
  }
}
