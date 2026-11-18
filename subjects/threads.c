/* Step-trace subject: stepping one thread while another runs. The worker only spins on its own
   counter, so the main thread's trace is the same on every run whatever the worker does. */
#include <windows.h>

static volatile LONG stop;
static volatile LONG spins;
static volatile int sink;

static DWORD WINAPI worker(void *arg)
{
  (void)arg;
  while (!stop)
  {
    spins += 1;
  }
  return 0;
}

static int work(int n)
{
  int sum = 0;
  for (int i = 0; i < n; i += 1)
  {
    sum += i;
  }
  return sum;
}

int main(void)
{
  HANDLE t = CreateThread(0, 0, worker, 0, 0, 0);
  int a = work(3);
  int b = work(4);
  sink = a + b;
  stop = 1;
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
  return 0;
}
