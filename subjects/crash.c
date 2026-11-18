/* Step-trace subject: a stop that is not a step ending. The write through a null pointer in
   poke raises an access violation three calls deep; the trace must name that line. */
static volatile int sink;
static int *volatile nowhere;

static void poke(int v)
{
  *nowhere = v;
}

static void middle(int v)
{
  sink = v;
  poke(v + 1);
  sink = v + 2;
}

int main(void)
{
  sink = 1;
  middle(5);
  sink = 2;
  return 0;
}
