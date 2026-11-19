/* A function inlined three times, once inside a loop: clang at -O2 gives one call pieces with
   other code between them (a length annotation followed by more offsets). */
volatile int sink;
static __forceinline int twice(int x)
{
  if (x > 10) {
    sink = x;
    sink = x + 1;
    sink = x + 2;
  }
  return x * 2 + sink;
}
int work(int a, int b)
{
  int r = 0;
  for (int i = 0; i < a; i += 1) {
    r += twice(b + i);
    sink = r;
  }
  r += twice(b);
  sink = r;
  r += twice(a);
  return r;
}
int main(void)
{
  return work(sink, sink);
}
