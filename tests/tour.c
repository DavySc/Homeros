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
  while (spin) spin -= 1;
  return total;
}
