/* Step-trace subject: recursion deep enough that a step over or a step out must tell this frame
   from the hundreds of other frames of the same function at the same addresses. */
static volatile int sink;

static int down(int n)
{
  if (n == 0)
  {
    return 0;
  }
  int below = down(n - 1);
  return below + 1;
}

static int twice(int n)
{
  int a = down(n);
  int b = down(n);
  return a + b;
}

int main(void)
{
  sink = down(3);
  sink = twice(300);
  sink = down(2);
  return 0;
}
