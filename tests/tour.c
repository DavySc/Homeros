/* The program the console debugger is taken through (tests/hdbg.txt; what it must write: tests/hdbg.expect). */
typedef struct { int a; char b; double c; } Pair;
volatile unsigned spin = 1000000000;   /* for longer than the script lets it run before it stops it: 50 ms, and a fast machine takes 300 ms */
__declspec(noinline) static int twice(int n)
{
  int doubled = n * 2;
  return doubled;
}
int main(void)
{
  Pair pair = {1, 'b', 3.0};
  int total = twice(pair.a);
  total += twice(total);
  if (total > 0) total += twice(total);   /* clang writes this line as two records, the call in the second */
  for (int i = 0; i < 2; i += 1) {   /* clang puts the test and the step apart, and the step jumps to the test */
    int odd = i & 1;                 /* a block with a variable is a scope to step out of, and `break` jumps out of it */
    if (odd) break;
    total += i;
  }
  while (spin) spin -= 1;
  { int six(int, char, Pair, double, int, Pair *); return six(total, 'x', pair, 2.5, total + 1, &pair); }   /* parameters in registers, as an address, in xmm, on the stack */
}
__declspec(noinline) int six(int a, char b, Pair c, double d, int e, Pair *f)
{
  int rest = b + c.a + (int)d + e + f->a;
  return a + rest - rest;
}
