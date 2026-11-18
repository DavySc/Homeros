/* Nested blocks with their own variables, so the debug info has scopes inside scopes. */
volatile int sink;

static int inner(int n)
{
  int total = n;
  {
    int a = total * 2;
    {
      int b = a + 1;
      total += b;
    }
    {
      int c = a - 1;
      total += c;
    }
  }
  for (int i = 0; i < 3; i += 1) {
    int d = i * total;
    sink += d;
  }
  return total;
}

int main(void)
{
  sink = inner(3);
  return 0;
}
