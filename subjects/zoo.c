/* Variables of many kinds in many kinds of place, and expressions of them. Each is shown to `probe` with its
   text: a debugger stopped at probe's first instruction has the text, the address and the size in the argument
   registers, and must work out the same from the debug information for the frame that called
   (tests/zoo_check.c). What is shown by value (address 0; 1 when the value is the bits of a double) has no
   address taken, so optimised code may keep a variable in a register. */
typedef struct { int a; char b; double c; } Pair;
typedef struct { long long x[3]; } Big;
typedef int Alias;
enum Colour { RED, GREEN = 5 };
union Either { int i; float f; };
typedef struct { unsigned low : 3; int mid : 5; unsigned long long high : 40; unsigned full : 32; signed char tiny : 2; } Bits;
typedef struct { Pair pair; Pair *next; int row[2][3]; Bits bits; union Either either; enum Colour colour; } Nest;

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
/* called through a pointer the compiler cannot see through: clang makes its own copies of a function for calls whose arguments it knows */
void (*volatile show)(const char *, const void *, unsigned long long, unsigned long long) = probe;
#define AT(v) show(#v, &(v), sizeof(v), 0)
#define VALUE(v) show(#v, 0, sizeof(v), (unsigned long long)(v))
#define FIELD(v) show(#v, 0, 0, (unsigned long long)(v))   /* a bit field has no sizeof: size 0, not compared */
#define REAL(v) show(#v, (const void *)1, sizeof(v), (union { double d; unsigned long long u; }){.d = (v)}.u)

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
  Nest nest = {pair, &pair, {{1, 2, 3}, {4, 5, 6}}, {5, -3, 0x123456789a, 0xfffffff0, -2}, {.f = 0.5f}, GREEN};
  Nest *np = &nest;
  Pair **pp = &pointer;
  /* places */
  AT(pair.c); AT(pair.b); AT(pointer->b); AT(*pointer); AT((*pointer).c); AT(pointer[0]); AT(array[3]); AT(array[u]); AT(array[s + 3]); AT(*array); AT(*(array + 2)); AT(1[array]); AT(big.x[u - 1]);
  AT(nest.row); AT(nest.row[1]); AT(nest.row[1][2]); AT(*nest.row[1]); AT(**nest.row); AT(nest.pair.c); AT(nest.next->c); AT(*nest.next); AT(np->pair.a); AT(np->next->b); AT(*np); AT((*np).next); AT(np[0].row[1]);
  AT((&nest)->row); AT(*&c); AT(**pp); AT((*pp)->c); AT(pp[0]->a); AT(*pp); AT(nest.either.f); AT(np->either); AT(either.i); AT(global_text[1]); AT(*global_text); AT(global_pair.c); AT(np->colour);
  AT(np -> pair . b); AT(( pair ).a); AT(*(char *)&u); AT(((char *)&wide)[7]); AT(*(Pair *)&nest); AT(((Pair *)np)->c); AT(((Nest *)&nest.pair)->row[0][1]); AT(*(short *)array); AT(((Big *)&big)->x[2]);
  /* values: whole numbers and addresses */
  VALUE(c + u); VALUE(s * wide); VALUE(-s); VALUE(~u); VALUE(!wide); VALUE(u - 5); VALUE(s < u); VALUE(s < (int)u); VALUE(wide >> 1); VALUE(s >> 1); VALUE(u << 30); VALUE(wide % 3); VALUE(wide / 3); VALUE(c / s);
  VALUE(array[1] + array[4]); VALUE(pointer->a + pair.b); VALUE(&array[4] - &array[1]); VALUE(&array[1] - &array[4]); VALUE(*(array + 2)); VALUE(array + 2 == &array[2]); VALUE(&nest.row[1][0] - &nest.row[0][0]); VALUE(&nest.row[1] - &nest.row[0]);
  VALUE((char)(c + 300)); VALUE((unsigned char)s); VALUE((short)wide); VALUE((unsigned)wide); VALUE((long long)f); VALUE((int)d); VALUE((int)-d); VALUE((Alias)wide); VALUE((enum Colour)5 == colour); VALUE(colour); VALUE(GREEN + 1); VALUE(colour == GREEN); VALUE(np->colour - RED);
  VALUE(sizeof array); VALUE(sizeof(Pair)); VALUE(sizeof pair.c); VALUE(sizeof(Pair *)); VALUE(sizeof nest.row[0]); VALUE(sizeof nest.row); VALUE(sizeof nest); VALUE(sizeof(Nest)); VALUE(sizeof(Bits)); VALUE(sizeof(union Either)); VALUE(sizeof(enum Colour)); VALUE(sizeof *np);
  VALUE(sizeof np->pair); VALUE(sizeof(Alias)); VALUE(sizeof(struct Nest *)); VALUE(sizeof array / sizeof array[0]); VALUE(sizeof *pp); VALUE(sizeof **pp); VALUE(sizeof global_text); VALUE(sizeof *global_text); VALUE(sizeof(Big)); VALUE(sizeof big.x); VALUE(sizeof function);
  FIELD(nest.bits.low); FIELD(nest.bits.mid); FIELD(nest.bits.high); FIELD(nest.bits.full); FIELD(nest.bits.tiny); FIELD(np->bits.mid); VALUE(nest.bits.low - 6); VALUE(nest.bits.full + 16); VALUE(nest.bits.high << 30); VALUE(np->bits.mid * np->bits.tiny); VALUE(-nest.bits.low < 0);
  VALUE(sizeof(nest.bits.low + 0)); VALUE(sizeof(nest.bits.full + 0)); VALUE(sizeof(nest.bits.high + 0)); VALUE(nest.bits.full > -1); VALUE(nest.bits.low > -1);
  VALUE(c == 'c'); VALUE(u && wide); VALUE(!u || !wide); VALUE(wide ? u : s); VALUE(u ? s : wide); VALUE(pointer == &pair); VALUE(pointer != 0); VALUE(!pointer); VALUE(pointer && np); VALUE((long long)pointer == (long long)&pair); VALUE(np->next == pointer);
  VALUE(((Pair *)&nest)->b); VALUE(*(char *)&u); VALUE(*(unsigned char *)&s); VALUE(global_text[0]); VALUE(*global_text); VALUE(global_text[4]); VALUE(global_pair.a + file_static); VALUE(global_int * file_static); VALUE(f > 1); VALUE(d * 2 > 4.9); VALUE((u > 2) + (s < 0));
  VALUE(u & 6 | 8 ^ 1); VALUE(0x7fffffff + u); VALUE(c - 'a'); VALUE(array[array[0]]); VALUE(array[nest.bits.low - 2]); VALUE(nest.row[1][array[1]]); VALUE((*np).row[u - 3][s + 4]); VALUE(*nest.row[1] + **nest.row); VALUE(pp[0]->a); VALUE((**pp).b);
  VALUE((unsigned long long)pointer); VALUE((long long)(pointer + 1) - (long long)pointer); VALUE((char *)(np + 1) - (char *)np); VALUE(pointer + 1 > pointer); VALUE((void *)pointer == (void *)np->next); VALUE(either.i); VALUE(np->either.i); VALUE(big.x[2] - big.x[0]);
  VALUE((unsigned short)wide * (unsigned short)u); VALUE((unsigned char)c + (unsigned char)250); VALUE(-(long long)u); VALUE(-u); VALUE(u / s); VALUE(wide * u); VALUE((unsigned char)wide >> 1); VALUE((signed char)wide >> 1); VALUE(unknown); VALUE(unknown * global_int);
  /* values: real numbers */
  REAL(f); REAL(d); REAL(f + 1); REAL(f * d); REAL(d / 4); REAL(-d); REAL((float)d); REAL((double)u); REAL(s * 0.5); REAL(f / 0.3f); REAL(f / 0.3); REAL((float)wide); REAL(u / 2.0); REAL(u / 2); REAL((float)0.1); REAL(wide ? d : s); REAL(u ? s : d);
  REAL(nest.pair.c); REAL(pointer->c); REAL(*&d); REAL(np->either.f); REAL(nest.either.f + f); REAL(global_pair.c * 2); REAL(pair.c / array[2]); REAL((int)pair.c / array[1]); REAL(d - (int)d); REAL(f * f * f); REAL(wide * 1.5f); REAL((float)u / 7); REAL(*(float *)&np->either);
  int total = params(1, 'p', pair, big, &big, d);
  big = returns(total);
  total += blocks(3) + (int)kept(unknown) + statics() + (int)big.x[2];
  total += far_frame(total, big);
  VALUE(total); VALUE(GREEN);
  return (total + (int)sink) & 0;
}
