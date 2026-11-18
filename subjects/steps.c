/* Step-trace subject: every construct a line stepper must handle, with no library calls
   and no input, so a trace of it is the same on every run. One statement per line on purpose:
   a trace line then names exactly one thing. */
typedef struct Pair { int a, b; } Pair;
typedef int (*Op)(int, int);

static volatile int sink;

static int add(int x, int y)
{
  return x + y;
}

static int mul(int x, int y)
{
  int r = 0;
  for (int i = 0; i < y; i += 1)
  {
    r = add(r, x);
  }
  return r;
}

static int fact(int n)
{
  if (n <= 1)
  {
    return 1;
  }
  return n * fact(n - 1);
}

static int classify(int v)
{
  switch (v % 4)
  {
    case 0: return 10;
    case 1: return 11;
    case 2: return 12;
    default: return 13;
  }
}

static Pair swap(Pair p)
{
  Pair q;
  q.a = p.b;
  q.b = p.a;
  return q;
}

static int apply(Op op, int x, int y)
{
  return op(x, y);
}

static int nested(int x)
{
  return add(mul(x, 2), fact(3));
}

int main(void)
{
  int total = 0;
  total += add(1, 2);
  total += mul(3, 4);
  total += fact(5);
  for (int i = 0; i < 6; i += 1)
  {
    total += classify(i);
  }
  Pair p = {1, 2};
  p = swap(p);
  total += p.a;
  total += apply(add, 7, 8);
  total += apply(mul, 2, 3);
  total += nested(4);
  int k = 0;
  while (k < 3)
  {
    k += 1;
    if (k == 2) continue;
    total += k;
  }
  total += (total > 100) ? add(1, 1) : mul(1, 1);
  sink = total;
  return 0;
}
