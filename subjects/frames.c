/* Local variables in the places few functions have: a struct too big for a register, passed and
   returned by value (the callee gets addresses); one of 3 bytes (not a register's size either);
   a frame so large that offsets from the stack pointer no longer fit 16 bits. */
typedef struct { long long a, b, c; } Triple;
typedef struct { char a, b, c; } Three;
static volatile int sink;

__declspec(noinline) static Triple shift(Triple t, int by)
{
  Triple r = {t.a + by, t.b + by, t.c + by};
  return r;
}

__declspec(noinline) static int sum(Three t, int more)
{
  int s = t.a + t.b + t.c + more;
  return s;
}

__declspec(noinline) static int wide_frame(int n)
{
  volatile char wide[40000];
  int last = n;
  wide[n] = 1;
  wide[39999] = (char)n;
  return wide[n] + wide[39999] + last;
}

#ifdef __clang__   /* built without the C runtime, which has the stack probe */
void __chkstk(void) {}
#endif

int main(void)
{
  Triple t = {1, 2, 3};
  Three small = {1, 2, 3};
  t = shift(t, 2);
  sink = (int)t.c + sum(small, 4);
  sink = wide_frame(3);
  return 0;
}
