/* A function that calls itself, stepped over and out of at every depth: the traps of a step are reached by the inner calls first,
   at the same places with another stack pointer. A line that calls it twice; loops on one line and over several; a block. Built by clang as well,
   with columns: then a line is several pieces of code that follow one another without a gap. */
__declspec(noinline) static int down(int n)
{
  int r = n;
  if (n > 0) r += down(n - 1);
  if (n > 1) r += down(n - 2) + down(n - 2);
  for (int i = 0; i < 2; i += 1) r += i;
  for (int i = 0; i < 2; i += 1) {   /* its pieces lie apart, around the body */
    r += i;
  }
  {
    int twice = r * 2;   /* a block with a variable of its own is a scope to step out of */
    r += twice;
    r -= twice;
  }
  return r;
}

int main(void)
{
  volatile int t = 0;
  t += down(3);
  t += down(2); t += down(1);
  for (int i = 0; i < 3; i += 1) t += down(i);
  return t & 0;
}
