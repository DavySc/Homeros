/* Variables of many kinds in many kinds of place. Each is shown to `probe` with its name: a debugger stopped
   at probe's first instruction has the name, the address and the size in the argument registers, and must
   find the same from the debug information for the frame that called (tests/zoo_check.c). A variable shown
   by value (`size` 0 in the address) has no address taken, so optimised code may keep it in a register. */
typedef struct { int a; char b; double c; } Pair;
typedef struct { long long x[3]; } Big;
typedef int Alias;
enum Colour { RED, GREEN = 5 };
union Either { int i; float f; };

int global_int = 7;
static long long file_static = -3;
Pair global_pair = {1, 'x', 2.5};
const char *global_text = "text";
volatile unsigned long long sink;
volatile long long unknown = 2;   /* so that the compiler cannot work the loop out */

__declspec(noinline) void probe(const char *name, const void *addr, unsigned long long size, unsigned long long value)
{
  sink += (unsigned long long)name[0] + (unsigned long long)addr + size + value;
}
#define AT(v) probe(#v, &v, sizeof v, 0)
#define VALUE(v) probe(#v, 0, sizeof v, (unsigned long long)v)

__declspec(noinline) static int params(int first, char second, Pair small, Big large, Big *pointer, double real)
{
  int local = first + 1;
  AT(first); AT(second); AT(small); AT(large); AT(pointer); AT(real); AT(local);
  return local + second + small.a + (int)large.x[0] + (int)pointer->x[1] + (int)real;
}

__declspec(noinline) static Big returns(int seed)
{
  Big made = {{seed, seed + 1, seed + 2}};
  AT(made); AT(seed);
  return made;
}

__declspec(noinline) static int blocks(int n)
{
  int x = n;
  AT(x);
  {
    int x = n + 1, inner_most = 4, inner = 2;   /* a name that begins with another's */
    AT(x); AT(inner); AT(inner_most);
    {
      long long x = n + 2;
      AT(x); AT(inner);
    }
    AT(x);
  }
  AT(x);
  return x;
}

__declspec(noinline) static long long kept(long long n)
{
  long long twice = n * 2, sum = n;   /* none begins as a constant: clang writes such a variable down as that constant for the whole function */
  int count = (int)n + 3;
  for (int i = (int)n - 2; i < count; i += 1) {
    sum += twice + i;
    VALUE(i); VALUE(sum); VALUE(twice); VALUE(count); VALUE(n);
  }
  return sum + twice;
}

__declspec(noinline) static int statics(void)
{
  static int function_static = 11;
  function_static += 1;
  AT(function_static); AT(file_static); AT(global_int); AT(global_pair); AT(global_text);
  return function_static;
}

/* a frame so large that its variables are further from the stack pointer than a 16-bit offset reaches */
__declspec(noinline) static int far_frame(int n, Big large)
{
  char block[40000];
  int beyond = n;
  block[0] = (char)n, block[39999] = 1;
  AT(beyond); AT(block); AT(n); AT(large);
  return beyond + block[39999] + (int)large.x[1];
}

int main(void)
{
  char c = 'c';
  short s = -2;
  unsigned u = 3;
  long long wide = -4;
  float f = 1.5f;
  double d = 2.5;
  int array[5] = {1, 2, 3, 4, 5};
  Pair pair = {1, 'b', 3.0};
  Big big = {{1, 2, 3}};
  Pair *pointer = &pair;
  enum Colour colour = GREEN;
  union Either either = {.i = 1};
  Alias alias = 9;
  void (*function)(const char *, const void *, unsigned long long, unsigned long long) = probe;
  AT(c); AT(s); AT(u); AT(wide); AT(f); AT(d); AT(array); AT(pair); AT(big); AT(pointer); AT(colour); AT(either); AT(alias); AT(function);
  int total = params(1, 'p', pair, big, &big, d);
  big = returns(total);
  total += blocks(3) + (int)kept(unknown) + statics() + (int)big.x[2];
  total += far_frame(total, big);
  VALUE(total); VALUE(GREEN);
  return (total + (int)sink) & 0;
}
