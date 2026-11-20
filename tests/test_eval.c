/* The evaluator (dbg/eval.c) without a program. Places from locations written out by hand: every kind of
   location, the cases of a set, and the bytecode, operation by operation and broken in every way. Expressions
   of numbers: the compiler of this test is the oracle, each text is also compiled here and must give the same
   type and value (C(...)); what C leaves open, addresses, and what must be refused, by hand. Names, scopes and
   members need real debug information: they are checked on a running program (tests/zoo_check.c). */
#include "../dbg/eval.c"
#include <stdio.h>
#include <stdlib.h>

static uint8_t memory[64];   /* the target's, at MEMORY */
enum { MEMORY = 0x7000 };
bool target_read(Target *t, uint64_t addr, void *bytes, uint32_t n)
{
  (void)t;
  if (addr < MEMORY || addr - MEMORY > sizeof memory - n) return false;
  return memcpy(bytes, memory + (addr - MEMORY), n), true;
}
const EngineModule *engine_module(const Engine *e, uint64_t addr) { return (void)e, (void)addr, NULL; }

#define REG(code) 5, code, 8, 0, 0
#define U8(v) 11, v
#define U16(v) 12, (uint8_t)(v), (uint8_t)((v) >> 8)
#define U32(v) 13, (uint8_t)(v), (uint8_t)((v) >> 8), (uint8_t)((v) >> 16), (uint8_t)((v) >> 24)
#define U64(v) 14, (uint8_t)(v), (uint8_t)((v) >> 8), (uint8_t)((v) >> 16), (uint8_t)((v) >> 24), 0, 0, 0, 0
#define ADD 21, 0, 0
#define SIGNED(bits) 42, bits
#define READ(n) 4, n
#define CODE(...) (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__})

static const TargetRegs regs = {.gp = {0x100, 0x200, 0x300, 0x400, MEMORY, 0x600, 0x700, 0x800, 0x900, 0xa00, 0xb00, 0xc00, 0xd00, 0xe00, 0xf00, 0x1000}};
static int failed;

/* The bytecode gives `want`; -1: it must be refused. */
static void code(const char *what, const uint8_t *bytes, size_t n, int64_t want)
{
  Engine e = {0};
  Rdi r = {0};
  uint64_t got = 0;
  uint8_t *copy = malloc(n);   /* exactly as long as it is, so that a read past its end is seen */
  memcpy(copy, bytes, n);
  r.s[RDI_LOCATION_BYTECODE] = (RdiSpan){copy, n};
  bool ok = bytecode(&e, &r, 0, &regs, &got);
  if (ok != (want != -1) || (ok && got != (uint64_t)want)) printf("test_eval: %s: %s %llx, not %llx\n", what, ok ? "gives" : "refused, with", (unsigned long long)got, (unsigned long long)want), failed = 1;
  free(copy);
}

static void at(const char *what, const Rdi *r, uint64_t location, uint64_t voff, int kind, uint64_t where)
{
  Engine e = {0};
  EngineModule m = {.image = {.base = 0x140000000}, .rdi = *r};
  EvalValue p = place(&e, &m, 7, location, voff, &regs);
  if (p.kind != kind || (kind && (p.at != where || p.type.idx != 7 || p.type.stars || p.type.module != &m))) printf("test_eval: %s: kind %d at %llx, not %d at %llx\n", what, p.kind, (unsigned long long)p.at, kind, (unsigned long long)where), failed = 1;
}

/* The text gives a value of this built-in kind under so many stars, with these bits; why: it must be refused for this reason. */
static void expression(const char *text, uint32_t kind, uint32_t stars, uint64_t bits, const char *why)
{
  Engine e = {0};
  const char *error = "unset";
  EvalValue v = eval(&e, &regs, false, text, &error);
  uint64_t there = 0;
  if (v.kind == EVAL_MEMORY) v.kind = eval_read(&e, &regs, v, &there, eval_size(v.type)) ? EVAL_VALUE : -1, v.at = there;   /* a place: what is there (no test here has a sign to carry) */
  if (why ? v.kind || !error || strcmp(error, why) : v.kind != EVAL_VALUE || error || v.type.module || v.type.idx != kind || v.type.stars != stars || v.at != bits)
    printf("test_eval: %s: kind %d, type %x under %u stars, %llx (%s), not type %x under %u stars, %llx (%s)\n", text, v.kind, v.type.idx, v.type.stars, (unsigned long long)v.at, error ? error : "no error", kind, stars, (unsigned long long)bits, why ? why : "no error"), failed = 1;
}

static uint64_t bits_whole(uint64_t v) { return v; }
static uint64_t bits_float(float f) { uint64_t v = 0; return memcpy(&v, &f, 4), v; }
static uint64_t bits_double(double d) { uint64_t v; return memcpy(&v, &d, 8), v; }
/* As this compiler has it. Its long has 8 bytes where the debugged program's has 4: no text here says `long` alone. */
#define C(x) expression(#x, _Generic((x), char: CHAR, signed char: S8, unsigned char: UCHAR, short: S16, unsigned short: 0xb, int: S32, unsigned: U32, long: S64, unsigned long: U64, \
                                     long long: S64, unsigned long long: U64, float: F32, double: F64, _Bool: BOOL), 0, _Generic((x), float: bits_float, double: bits_double, default: bits_whole)(x), NULL)
#define IS(text, kind, stars, bits) expression(text, kind, stars, (uint64_t)(bits), NULL)
#define NO(text, why) expression(text, 0, 0, 0, why)

#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wbool-operation"
#pragma GCC diagnostic ignored "-Wint-in-bool-context"
#pragma GCC diagnostic ignored "-Wconstant-logical-operand"
#pragma GCC diagnostic ignored "-Wtautological-compare"
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
#pragma GCC diagnostic ignored "-Wdivision-by-zero"
#pragma GCC diagnostic ignored "-Wconstant-conversion"
#pragma GCC diagnostic ignored "-Woverflow"
#pragma GCC diagnostic ignored "-Wliteral-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-const-int-float-conversion"
#pragma GCC diagnostic ignored "-Wsizeof-array-div"
#pragma GCC diagnostic ignored "-Wlogical-not-parentheses"
#pragma GCC diagnostic ignored "-Wshift-op-parentheses"
#pragma GCC diagnostic ignored "-Wbitwise-op-parentheses"
#pragma GCC diagnostic ignored "-Wlogical-op-parentheses"
#pragma GCC diagnostic ignored "-Wpointer-bool-conversion"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wbitwise-instead-of-logical"
#pragma GCC diagnostic ignored "-Wtautological-constant-out-of-range-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wliteral-range"
#pragma GCC diagnostic ignored "-Wbool-compare"
static void expressions(void)
{
  /* numbers as written, and their types */
  C(0); C(1); C(2147483647); C(2147483648); C(4294967295); C(4294967296); C(9223372036854775807); C(18446744073709551615u);
  C(0x7fffffff); C(0x80000000); C(0xffffffff); C(0x100000000); C(0x7fffffffffffffff); C(0x8000000000000000); C(0xffffffffffffffff);
  C(017); C(0777777777777); C(037777777777); C(1u); C(1U); C(1ll); C(1LL); C(1ull); C(1llu); C(4294967295u); C(4294967296u); C(0x10ll); C(0xffffffffffffffffll);
  C(1.5); C(1.5f); C(.5); C(5.); C(1e3); C(1E-3); C(1.5e2f); C(0x1p4); C(0.1); C(0.1f); C(123456789.125); C(1e300); C(16777217.0f);
  C('a'); C('0'); C(' '); C('\n'); C('\t'); C('\r'); C('\0'); C('\\'); C('\''); C('"'); C('?');
  /* each operator on each kind of number */
  C(7 + 2); C(7 - 9); C(7 * -3); C(7 / 2); C(-7 / 2); C(7 % 3); C(-7 % 3); C(7 % -3); C(7u / 2); C(-7 / 2u); C(-7 % 3u); C(7 << 3); C(-7 >> 1); C(0xf0000000 >> 4); C(0xf0000000ll >> 4);
  C(1u << 31); C(1ll << 40); C(1ull << 63); C(1 << 1ll); C(6 & 3); C(6 | 3); C(6 ^ 3); C(~6); C(~6u); C(~6ll); C(~0ull); C(-6); C(-6u); C(- -6); C(+6); C(+-6); C(!6); C(!0); C(!!6);
  C(7.5 + 2); C(7.5 - 9); C(7.5 * -3); C(7.5 / 2); C(1 / 3.0); C(1.0f / 3); C(1.0f / 3.0); C(0.1f + 0.2f); C(0.1f * 3); C(0.1 + 0.2); C(-1.5); C(-0.0); C(+1.5f); C(!1.5); C(!0.0); C(-1.5f);
  C(1.0 / 0.0 > 1e308); C(-1.0 / 0.0 < -1e308); C(0.0 / 0.0 == 0.0 / 0.0); C(0.0 / 0.0 != 0.0 / 0.0); C(0.0 / 0.0 < 1.0); C(0.0 / 0.0 >= 1.0);
  C(1 < 2); C(2 < 1); C(1 <= 1); C(1 >= 2); C(1 > 0); C(1 == 1); C(1 != 1); C(1.5 < 2); C(2 > 1.5f); C(1.5 == 1.5f); C(0.1 == 0.1f); C(1.5 != 1); C(1.5 <= 1.5); C(1.5 >= 2.5);
  /* signed with unsigned, narrow with wide: C's usual conversions */
  C(-1 < 0u); C(-1 < 0); C(-1 < 0ll); C(-1 < 0ull); C(-1 > 1u); C(-1ll < 1u); C(-1 + 1u); C(-1 + 1ull); C(-1ll + 1u); C(1u - 2); C(1u - 2ll); C(0u - 1); C(0ull - 1); C(4294967295u + 1); C(4294967295u + 1ll);
  C(2147483647 + 1u); C(-2147483647 - 1); C((-2147483647 - 1) / -1ll); C(65535 * 65535u); C(100000 * 100000ll); C(-5 / 2u); C(-5ll / 2u); C(-5 % 2ull); C(-1 >> 1u); C(-1u >> 1); C(-1ll >> 60);
  C((char)1 + (char)2); C((char)100 + (char)100); C((unsigned char)200 + (unsigned char)100); C((short)-1 < (unsigned short)1); C((unsigned short)65535 + 1); C((unsigned char)1 - 2); C(-(unsigned char)1); C(~(unsigned char)1);
  C(~(short)0); C(!(char)0); C((char)1 << 10); C((short)-1 + 0u); C((char)-1 + 0ull); C((_Bool)1 + (_Bool)1); C(-(_Bool)1); C(~(_Bool)1);
  C(1 + 1.5f); C(1u + 1.5f); C(1ll + 1.5f); C(1.5f + 1.5); C((char)1 + 1.5f); C(16777217 + 0.0f); C(16777217 + 0.0); C(9007199254740993 + 0.0); C(18446744073709551615u + 0.0); C(-1 + 0.0f); C(4294967295u * 1.0f);
  /* conversions */
  C((char)65); C((char)200); C((char)-200); C((signed char)200); C((unsigned char)200); C((unsigned char)-1); C((short)70000); C((unsigned short)70000); C((short)-70000); C((int)4000000000); C((unsigned)-1);
  C((unsigned int)4294967296); C((long long)-1); C((unsigned long long)-1); C((long long)4294967295u); C((long long int)1); C((unsigned long long int)1); C((long long unsigned)1); C((signed)1u); C((unsigned)1); C((short int)1); C((unsigned short int)65537);
  C((signed short)65535); C((int signed)1); C((const int)1); C((volatile unsigned char)257); C((int const)1); C((_Bool)2); C((_Bool)0); C((_Bool)0.5); C((_Bool)-1); C((_Bool)0.0); C((char)(_Bool)2);
  C((int)1.9); C((int)-1.9); C((int)1.9f); C((unsigned)3.9); C((long long)1e18); C((unsigned long long)1e19); C((unsigned long long)1.8e19f); C((char)65.9); C((unsigned char)255.9); C((short)-32768.9); C((long long)-9.2e18);
  C((float)1); C((float)-1); C((float)16777217); C((float)0.1); C((double)0.1f); C((double)1); C((double)-1); C((double)4294967295u); C((double)18446744073709551615u); C((double)-9223372036854775807); C((float)18446744073709551615u);
  C((float)1e-50); C((double)(float)1e-40); C((float)(double)1.5f); C((long double)1.5 > 1); C((int)(char)300); C((unsigned)(char)-1); C((unsigned long long)(char)-1); C((unsigned long long)(unsigned char)-1); C((long long)(unsigned)-1); C((double)(unsigned char)-1);
  /* which operator takes what */
  C(1 + 2 * 3); C((1 + 2) * 3); C(1 - 2 - 3); C(100 / 10 / 5); C(100 / 10 * 5); C(100 % 7 * 2); C(2 * 3 % 4); C(1 << 2 + 3); C(1 + 2 << 3); C(256 >> 2 << 1); C(256 >> 2 >> 1); C(1 < 2 == 1); C(3 > 2 > 1); C(1 == 1 < 0);
  C(1 << 2 < 3); C(1 & 2 == 2); C(3 & 2 == 2); C(1 == 3 & 1); C(6 & 3 ^ 5); C(6 ^ 3 & 5); C(6 ^ 3 | 5); C(6 | 3 ^ 5); C(6 | 3 & 5); C(6 & 3 | 5); C(1 | 2 && 0); C(0 && 1 | 2); C(1 || 0 && 0); C(0 && 0 || 1); C((1 || 0) && 0);
  C(1 ? 2 : 3); C(0 ? 2 : 3); C(1 ? 2 : 0 ? 4 : 5); C(0 ? 2 : 0 ? 4 : 5); C(0 ? 2 : 1 ? 4 : 5); C(1 ? 0 ? 6 : 7 : 8); C(0 || 0 ? 1 : 2); C(1 + 1 ? 3 : 4); C(1 ? 2 : 3 + 10); C(0 ? 2 : 3 + 10); C((0 ? 2 : 3) + 10);
  C(-2 * 3); C(- 2 - - 3); C(!1 + 1); C(~1 + 1); C(-(1 + 1)); C(!(1 - 1)); C(- - - 1); C(!!!1); C(~~~1); C(-~!0); C((int)1.9 + 1.9); C((int)(1.9 + 1.9)); C(-(char)-128); C((unsigned char)-1 * 2); C((unsigned char)(-1 * 2)); C((double)1 / 2); C((double)(1 / 2));
  C(1+2*3-4/2); C( 1 +  2 ); C(((((1))))); C((1)+(2)); C(1<2); C(1<<2); C(1<=2); C(1>=2>=1); C(2>1); C(8>>1); C(1==1); C(1!=1); C(!1!=1); C(1&1); C(1&&1); C(1|0); C(1||0); C(1^1); C(5%3); C(1?2:3); C(-1); C(~1); C(1- -1); C(1-+1); C(2*-1); C(1&-1); C(1&&-1); C(1&&!1);
  /* a part C does not work out is not worked out: its division by zero is not met, its type still counts */
  C(0 && 1 / 0); C(1 || 1 / 0); C(1 ? 2 : 1 / 0); C(0 ? 1 / 0 : 3); C(0 && 1 % 0); C(1 || (0 && 1 / 0)); C(0 && (1 || 1 / 0)); C(1 && 0 && 1 / 0); C(0 || 1 || 1 / 0); C(0 ? 1 / 0 ? 1 : 2 : 3); C(1 && 2); C(1 && 0); C(0 || 0); C(0 || 2); C(1.5 && 1); C(0.0 || 0); C(0.5 || 0); C(1 && 0.0);
  C(1 ? 2 : 3.5); C(0 ? 2 : 3.5); C(1 ? 2u : -1); C(0 ? 2u : -1); C(1 ? -1 : 2ull); C(1 ? (char)1 : (char)2); C(1 ? 1.5f : 2); C(0 ? 1.5f : 2); C(1 ? 1.5f : 2.5); C(1 ? -1 : 0u); C(0 ? 1ll : -1); C((1 ? -1 : 0u) < 0); C(sizeof(1 ? 1 : 1ll)); C(sizeof(0 ? 1.5f : 1));
  /* sizes */
  C(sizeof(char)); C(sizeof(signed char)); C(sizeof(unsigned char)); C(sizeof(short)); C(sizeof(unsigned short)); C(sizeof(int)); C(sizeof(unsigned)); C(sizeof(long long)); C(sizeof(unsigned long long)); C(sizeof(float)); C(sizeof(double)); C(sizeof(_Bool));
  C(sizeof(char *)); C(sizeof(int **)); C(sizeof(void *)); C(sizeof(const char * const *)); C(sizeof 1); C(sizeof 1u); C(sizeof 1ll); C(sizeof 2147483648); C(sizeof 0x80000000); C(sizeof 0x100000000); C(sizeof 1.5); C(sizeof 1.5f); C(sizeof 'a'); C(sizeof(1)); C(sizeof (1) + 1);
  C(sizeof 1 + 1); C(sizeof(char) + 1); C(sizeof -(char)1); C(sizeof((char)1 + (char)1)); C(sizeof(1 + 1ll)); C(sizeof(1 + 1.5f)); C(sizeof(1.5f + 1.5)); C(sizeof(1 / 0)); C(sizeof sizeof 1); C(sizeof(int) * 2); C(sizeof(int) - 5 < 0); C(-sizeof(int)); C(sizeof !1.5); C(sizeof(1 < 2ll));
  C(sizeof(1 << 1ll)); C(sizeof(1ll << 1)); C(sizeof((short)1 << 1)); C(sizeof ~(char)1); C(sizeof(1 && 1.5)); C(sizeof(int)); C(sizeof(int ) ); C(sizeof ( int )); C(sizeof(int*)); C(sizeof(char *) + sizeof(char) * 3);

  /* what C leaves open, as the processor has it and as this file says it */
  IS("2147483647 + 1", S32, 0, -2147483648ll); IS("-2147483647 - 2", S32, 0, 2147483647); IS("65536 * 65536", S32, 0, 0); IS("9223372036854775807 + 1", S64, 0, 1ull << 63); IS("(-2147483647 - 1) / -1", S32, 0, -2147483648ll);
  IS("(-2147483647 - 1) % -1", S32, 0, 0); IS("(-9223372036854775807 - 1) / -1", S64, 0, 1ull << 63); IS("(-9223372036854775807 - 1) % -1", S64, 0, 0); IS("-(-2147483647 - 1)", S32, 0, -2147483648ll);
  IS("(unsigned char)255 << 24 >> 24", S32, 0, -1ll); IS("1 << 32", S32, 0, 1); IS("1 << 33", S32, 0, 2); IS("1 << -1", S32, 0, -2147483648ll); IS("1ll << 64", S64, 0, 1); IS("-8 >> 33", S32, 0, -4); IS("-1 << 3", S32, 0, -8); IS("1 << 31", S32, 0, -2147483648ll); IS("3 << 31", S32, 0, -2147483648ll);
  IS("(int)1e10", S32, 0, 1410065408); IS("(int)-1e10", S32, 0, -1410065408); IS("(int)1e30", S32, 0, 0); IS("(long long)1e30", S64, 0, 1ull << 63); IS("(long long)-1e30", S64, 0, 1ull << 63); IS("(unsigned long long)1e30", U64, 0, 1ull << 63);
  IS("(unsigned long long)-1.5", U64, 0, -1ll); IS("(unsigned)-1.5", U32, 0, 0xffffffff); IS("(unsigned char)-1.5", UCHAR, 0, 0xff); IS("(char)1000.5", CHAR, 0, -24); IS("(long long)(0.0 / 0.0)", S64, 0, 1ull << 63); IS("(int)(0.0 / 0.0)", S32, 0, 0);
  IS("(unsigned long long)9223372036854775808.0", U64, 0, 1ull << 63); IS("(unsigned long long)18446744073709549568.0", U64, 0, 0xfffffffffffff800); IS("(unsigned long long)18446744073709551616.0", U64, 0, 1ull << 63);
  IS("(long long)9223372036854774784.0", S64, 0, 0x7ffffffffffffc00); IS("(long long)-9223372036854775808.0", S64, 0, 1ull << 63); IS("(float)1e40 > 1e38", S32, 0, 1); IS("(float)-1e40 < -1e38", S32, 0, 1);
  /* long has 4 bytes there; types written in other ways */
  IS("sizeof(long)", U64, 0, 4); IS("sizeof(unsigned long)", U64, 0, 4); IS("sizeof(long int)", U64, 0, 4); IS("sizeof(long long)", U64, 0, 8); IS("sizeof(long double)", U64, 0, 8); IS("sizeof(__int64)", U64, 0, 8); IS("sizeof(unsigned __int64)", U64, 0, 8);
  IS("sizeof(bool)", U64, 0, 1); IS("sizeof(void)", U64, 0, 0); IS("sizeof(1l)", U64, 0, 4); IS("sizeof(1ul)", U64, 0, 4); IS("sizeof(2147483648l)", U64, 0, 8); IS("(long)4294967297", S32, 0, 1); IS("(unsigned long)-1", U32, 0, 0xffffffff);
  IS("(int long)4294967297", S32, 0, 1); IS("(long int long)4294967297", S64, 0, 4294967297); IS("(long unsigned)-1", U32, 0, 0xffffffff); IS("(bool)5", BOOL, 0, 1); IS("(__int64)-1", S64, 0, -1ll); IS("1L", S32, 0, 1); IS("1uL", U32, 0, 1); IS("0xffffffffl", U32, 0, 0xffffffff);
  IS("(struct int)1", S32, 0, 1);   /* the words that say nothing are passed over wherever they stand */
  /* addresses */
  IS("(int *)8", S32, 1, 8); IS("(int **)8", S32, 2, 8); IS("(int * const *)8", S32, 2, 8); IS("(const char *)8", CHAR, 1, 8); IS("(void *)-1", VOID, 1, -1ll); IS("(char *)(int *)8", CHAR, 1, 8); IS("(int *)8 + 2", S32, 1, 16); IS("2 + (int *)8", S32, 1, 16);
  IS("(int *)8 - 2", S32, 1, 0); IS("(int *)8 - 3", S32, 1, -4ll); IS("(short *)8 + -1", S16, 1, 6); IS("(char *)8 + 4294967295u", CHAR, 1, 0x100000007); IS("(double *)8 + (char)1", F64, 1, 16); IS("(int **)8 + 1", S32, 2, 16); IS("(long long *)0 + 1ull", S64, 1, 8);
  IS("(int *)16 - (int *)8", S64, 0, 2); IS("(int *)8 - (int *)16", S64, 0, -2ll); IS("(char *)16 - (char *)8", S64, 0, 8); IS("(int **)16 - (int **)0", S64, 0, 2); IS("(int *)8 == (int *)8", S32, 0, 1); IS("(int *)8 != (int *)8", S32, 0, 0);
  IS("(int *)8 < (int *)16", S32, 0, 1); IS("(int *)-8 > (int *)16", S32, 0, 1); IS("(int *)0 == 0", S32, 0, 1); IS("0 != (int *)8", S32, 0, 1); IS("!(int *)8", S32, 0, 0); IS("!(int *)0", S32, 0, 1); IS("(int *)8 && 1", S32, 0, 1); IS("(int *)0 || 0", S32, 0, 0);
  IS("(int *)8 ? 1 : 2", S32, 0, 1); IS("1 ? (int *)8 : (int *)16", S32, 1, 8); IS("0 ? (int *)8 : (char *)16", CHAR, 1, 16); IS("(long long)(int *)-8", S64, 0, -8ll); IS("(int)(char *)0x123456789", S32, 0, 0x23456789); IS("(_Bool)(int *)8", BOOL, 0, 1);
  IS("sizeof *(int *)8", U64, 0, 4); IS("sizeof *(short **)8", U64, 0, 8); IS("sizeof **(short **)8", U64, 0, 2); IS("sizeof ((char *)8)[5]", U64, 0, 1); IS("sizeof &*(char *)8", U64, 0, 8); IS("sizeof(*(double *)0)", U64, 0, 8);
  /* the target's memory: bytes 1, 2, 3, ... from 0x7000 */
  IS("*(unsigned char *)0x7000", UCHAR, 0, 1); IS("*(char *)0x7004", CHAR, 0, 5); IS("*(unsigned short *)0x7002", 0xb, 0, 0x0403); IS("*(int *)0x7000", S32, 0, 0x04030201); IS("*(long long *)0x7008", S64, 0, 0x100f0e0d0c0b0a09); IS("*(unsigned *)0x703c", U32, 0, 0x403f3e3d);
  IS("((unsigned char *)0x7000)[5]", UCHAR, 0, 6); IS("((unsigned short *)0x7000)[2]", 0xb, 0, 0x0605); IS("5[(unsigned char *)0x7000]", UCHAR, 0, 6); IS("((int *)0x7010)[-4]", S32, 0, 0x04030201); IS("*((char *)0x7000 + 3)", CHAR, 0, 4); IS("(*(char *)0x7000 + 3)", S32, 0, 4);
  IS("&*(int *)0x7000", S32, 1, 0x7000); IS("*&*(char *)0x7001", CHAR, 0, 2); IS("&((int *)0x7000)[3]", S32, 1, 0x700c); IS("&((short *)0x7000)[3] - (short *)0x7000", S64, 0, 3); IS("&**(char **)0x7000", CHAR, 1, 0x0807060504030201);
  IS("*(char *)0x7000 + *(char *)0x7001 * 2", S32, 0, 5); IS("-*(char *)0x7000", S32, 0, -1ll); IS("*(float *)0x7000 < 1e-30", S32, 0, 1); IS("*(double *)0x7000 > 0", S32, 0, 1); IS("*(char *)0x7000 == 1 && *(char *)0x703f == 64", S32, 0, 1);
  IS("0 && *(char *)0", S32, 0, 0); IS("1 || *(char *)0", S32, 0, 1); IS("1 ? 7 : *(char *)0", S32, 0, 7); IS("0 ? *(char *)0 : 7", S32, 0, 7); IS("sizeof *(int *)0", U64, 0, 4); IS("0 && **(char **)0", S32, 0, 0); IS("0 && ((char *)0)[9]", S32, 0, 0);
  IS("(int *)0x7000 ? *(char *)0x7000 : *(char *)0", S32, 0, 1);

  /* refused */
  NO("", "a value is missing"); NO(" ", "a value is missing"); NO("1 +", "a value is missing"); NO("* ", "a value is missing"); NO("()", "a value is missing"); NO("(1", "a ) is missing"); NO("((1)", "a ) is missing"); NO("1)", "what follows is not understood");
  NO("1 2", "what follows is not understood"); NO("1 = 2", "what follows is not understood"); NO("1 ? 2", "a : is missing"); NO("1 ? 2 ; 3", "a : is missing"); NO("1 : 2", "what follows is not understood"); NO("1 ++ ", "a value is missing");
  NO("nothing", "no such name"); NO("(nothing)1", "no such name"); NO("sizeof(nothing)", "no such name"); NO("1 + nothing", "no such name"); NO("0 && nothing", "no such name"); NO("int", "no such name"); NO("(int)", "a value is missing"); NO("sizeof", "a value is missing");
  NO("1 / 0", "division by zero"); NO("1 % 0", "division by zero"); NO("1u / 0", "division by zero"); NO("1ll % 0", "division by zero"); NO("1 / (1 - 1)", "division by zero"); NO("1 || 0 ? 1 / 0 : 2", "division by zero"); NO("1 && 1 / 0", "division by zero");
  NO("*5", "not an address"); NO("*1.5", "not an address"); NO("5[5]", "not an address"); NO("**(char *)0x7000", "not an address"); NO("&5", "it has no address"); NO("&(int *)8", "it has no address"); NO("&&*(int *)0x7000", "it has no address");
  NO("*(int *)1 + 0", "cannot be read"); NO("!*(char *)0x7040", "cannot be read"); NO("(char)*(int *)0x703d", "cannot be read"); NO("**(char **)0x7000 ? 1 : 2", "cannot be read"); NO("((char *)0)[0] == 0", "cannot be read"); NO("1 ? *(char *)0 : 7", "cannot be read"); NO("*(char *)0 && 0", "cannot be read");
  NO("1.5 % 2", "not for a real number"); NO("2 % 1.5", "not for a real number"); NO("1.5 & 1", "not for a real number"); NO("1 | 1.5f", "not for a real number"); NO("1 ^ 1.5", "not for a real number"); NO("~1.5", "not for a real number");
  NO("1 << 1.5", "not for a real number"); NO("1.5 >> 1", "not for a real number"); NO("(int *)8 * 2", "not for an address"); NO("(int *)8 + (int *)8", "not for an address"); NO("2 - (int *)8", "not for an address"); NO("(int *)8 / 2", "not for an address");
  NO("(int *)8 + 1.5", "not for an address"); NO("(int *)8 < 1.5", "not for an address"); NO("-(int *)8", "not for an address"); NO("~(int *)8", "not for an address"); NO("(int *)8 & 1", "not for an address"); NO("(void *)8 + 1", "not for an address");
  NO("(void *)8 - (void *)8", "not for an address"); NO("(float)(int *)8", "no such conversion") ; NO("(int *)1.5", "no such conversion"); NO("(void)1", "no such conversion"); NO("*(void *)0x7000 + 1", "not a number"); NO("(int)*(void *)0x7000", "not a number");
  NO("1 . a", "no such member"); NO("(1).a", "no such member"); NO("((int *)0x7000)->a", "no such member"); NO("1 .", "no such member"); NO("1[", "a value is missing"); NO("((char *)0x7000)[1", "a ] is missing");
  NO("'ab'", "a ' is missing"); NO("'a", "a ' is missing"); NO("'", "not a character"); NO("''", "not a character"); NO("'\\q'", "not a character"); NO("'\\", "not a character"); NO("'\\n", "a ' is missing");
  NO("99999999999999999999", "a number too large"); NO("0x1ffffffffffffffff", "a number too large"); NO("1x", "what follows is not understood"); NO("1.5.5", "no such member"); NO("1ff", "what follows is not understood"); NO("#", "a value is missing"); NO("1 @ 2", "what follows is not understood");
  NO("sizeof (char)1", "what follows is not understood"); NO("sizeof(int *)8", "what follows is not understood");   /* as in C: sizeof takes the type */
  /* nesting: 63 deep is taken, 64 is not, by brackets and by signs */
  char deep[200];
  for (int n = 63; n <= 64; n += 1) {
    memset(deep, '(', (size_t)n), deep[n] = '1', memset(deep + n + 1, ')', (size_t)n), deep[2 * n + 1] = 0;
    expression(deep, S32, 0, 1, n == 64 ? "too deep" : NULL);
    memset(deep, '!', (size_t)n), deep[n] = '1', deep[n + 1] = 0;
    expression(deep, S32, 0, 0, n == 64 ? "too deep" : NULL);
  }
}

int main(void)
{
  for (uint32_t i = 0; i < sizeof memory; i += 1) memory[i] = (uint8_t)(i + 1);
  /* as the converter writes a far place: register, offset as 32 bits with its sign, add */
  code("register + offset", CODE(REG(5), U32(40000), SIGNED(32), ADD, 0), MEMORY + 40000);
  code("register - offset", CODE(REG(6), U32(-40000), SIGNED(32), ADD, 0), 0x600 - 40000);
  code("the address stored there", CODE(REG(5), U32(8), SIGNED(32), ADD, READ(8), 0), 0x100f0e0d0c0b0a09);
  code("every register", CODE(REG(1), REG(16), ADD, 0), 0x1100);
  code("constants of each width", CODE(U8(0xff), U16(0xffff), ADD, U64(0x80000000), ADD, 0), 0x800100fe);
  code("sign from bit 8", CODE(U16(0x17f), SIGNED(8), 0), 0x7f);
  code("sign from bit 8, set", CODE(U16(0x180), SIGNED(8), 0), -128);
  code("no sign from bit 9", CODE(U16(0x0ff), SIGNED(9), 0), 0xff);
  code("all 64 bits kept", CODE(U64(0x12345678), SIGNED(64), 0), 0x12345678);
  code("a read of 2 bytes", CODE(U16(MEMORY + 2), READ(2), 0), 0x0403);
  code("a stack of 8", CODE(U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), ADD, ADD, ADD, ADD, ADD, ADD, ADD, 0), 8);
  /* refused */
  code("a stack of 9", CODE(U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), 0), -1);
  code("nothing computed", CODE(0), -1);
  code("two values left", CODE(U8(1), U8(2), 0), -1);
  code("add with one value", CODE(U8(1), ADD, 0), -1);
  code("read with no address", CODE(READ(8), 0), -1);
  code("sign with no value", CODE(SIGNED(8), 0), -1);
  code("sign from bit 0", CODE(U8(1), SIGNED(0), 0), -1);
  code("sign from bit 65", CODE(U8(1), SIGNED(65), 0), -1);
  code("a read of 9 bytes", CODE(U16(MEMORY), READ(9), 0), -1);
  code("a read outside memory", CODE(U8(1), READ(8), 0), -1);
  code("register 0", CODE(REG(0), 0), -1);
  code("register 17", CODE(REG(17), 0), -1);
  code("half a register", CODE(5, 1, 4, 0, 0, 0), -1);
  code("a register from its second byte", CODE(5, 1, 8, 1, 0, 0), -1);
  code("an operation places do not use", CODE(1, 0), -1);   /* "no operation", which has no operand */
  code("an operation beyond the table", CODE(U8(1), 200, 0), -1);
  code("no end", CODE(U8(1)), -1);
  code("an operand cut short", CODE(13, 1, 0), -1);
  code("empty", (const uint8_t *)"", 0, -1);
  uint8_t longer[2 + 5 * 33 + 1] = {U8(0)};   /* 1 + 2 * 33 operations and the end: more than a place has */
  for (uint32_t i = 2; i < sizeof longer - 1; i += 5) memcpy(longer + i, (const uint8_t[]){U8(1), ADD}, 5);
  code("too many operations", longer, sizeof longer, -1);

  static const uint8_t far[] = {REG(5), U32(48), SIGNED(32), ADD, 0, REG(5), U32(8), SIGNED(32), ADD, READ(8), 0};
  static const RdiLocationCase cases[] = {{0x10, 0x20, 5ull << 56 | 3ull << 48}, {0x20, 0x30, 3ull << 56 | 5ull << 48 | 16ull << 32}, {0x30, 0x40, 1ull << 56 | 0}, {0x10, 0x50, 8ull << 56}};
  Rdi r = {0};
  r.s[RDI_LOCATION_BYTECODE] = (RdiSpan){far, sizeof far};
  r.s[RDI_LOCATION_SETS] = (RdiSpan){cases, 4};
  at("no location", &r, 0, 0, EVAL_NONE, 0);
  at("bytecode", &r, 1ull << 56, 0, EVAL_MEMORY, MEMORY + 48);
  at("bytecode that reads", &r, 1ull << 56 | 16, 0, EVAL_MEMORY, 0x100f0e0d0c0b0a09);
  at("register + offset", &r, 3ull << 56 | 5ull << 48 | 24ull << 32, 0, EVAL_MEMORY, MEMORY + 24);
  at("register - offset", &r, 3ull << 56 | 6ull << 48 | 0xfff8ull << 32, 0, EVAL_MEMORY, 0x600 - 8);
  at("register 17 + offset", &r, 3ull << 56 | 17ull << 48, 0, EVAL_NONE, 0);
  at("at the address stored there", &r, 4ull << 56 | 5ull << 48 | 16ull << 32, 0, EVAL_MEMORY, 0x1817161514131211);
  at("at an address stored outside memory", &r, 4ull << 56 | 6ull << 48, 0, EVAL_NONE, 0);
  at("in a register", &r, 5ull << 56 | 16ull << 48, 0, EVAL_REGISTER, 15);
  at("in register 0", &r, 5ull << 56, 0, EVAL_NONE, 0);
  at("in register 17", &r, 5ull << 56 | 17ull << 48, 0, EVAL_NONE, 0);
  at("in the image", &r, 6ull << 56 | 0x3000, 0, EVAL_MEMORY, 0x140003000);
  at("in the thread's block", &r, 7ull << 56 | 8, 0, EVAL_NONE, 0);
  at("a constant", &r, 8ull << 56 | 16, 0, EVAL_CONSTANT, 16);
  at("a set, before its cases", &r, 9ull << 56 | 3ull << 32, 0xf, EVAL_NONE, 0);
  at("a set, first case", &r, 9ull << 56 | 3ull << 32, 0x10, EVAL_REGISTER, 2);
  at("a set, end of the first case", &r, 9ull << 56 | 3ull << 32, 0x1f, EVAL_REGISTER, 2);
  at("a set, second case", &r, 9ull << 56 | 3ull << 32, 0x20, EVAL_MEMORY, MEMORY + 16);
  at("a set, third case", &r, 9ull << 56 | 3ull << 32, 0x3f, EVAL_MEMORY, MEMORY + 48);
  at("a set, after its cases", &r, 9ull << 56 | 3ull << 32, 0x40, EVAL_NONE, 0);
  at("a set of two, in the third's range", &r, 9ull << 56 | 2ull << 32, 0x30, EVAL_NONE, 0);
  at("a set from its second case", &r, 9ull << 56 | 2ull << 32 | 1, 0x30, EVAL_MEMORY, MEMORY + 48);
  at("a set beyond the table", &r, 9ull << 56 | 2ull << 32 | 4, 0, EVAL_NONE, 0);
  at("a set whose cases overlap: the first", &r, 9ull << 56 | 4ull << 32, 0x20, EVAL_MEMORY, MEMORY + 16);
  at("a set whose cases overlap: the only one left", &r, 9ull << 56 | 4ull << 32, 0x40, EVAL_CONSTANT, 0);

  /* reading: memory, a register's low bytes, a constant's, a value's */
  static const uint64_t constants[] = {0x1111111111111111, 0x8877665544332211};
  EngineModule m = {.rdi.s[RDI_LOCATION_CONSTANTS] = {constants, 2}};
  Engine e = {0};
  uint64_t got = 0;
  if (!eval_read(&e, &regs, (EvalValue){EVAL_MEMORY, {&m, 0, 0}, MEMORY + 4}, &got, 4) || got != 0x08070605) printf("test_eval: memory read %llx\n", (unsigned long long)got), failed = 1;
  if (!eval_read(&e, &regs, (EvalValue){EVAL_REGISTER, {&m, 0, 0}, 15}, &got, 2) || (uint16_t)got != 0x1000) printf("test_eval: register read %llx\n", (unsigned long long)got), failed = 1;
  if (!eval_read(&e, &regs, (EvalValue){EVAL_CONSTANT, {&m, 0, 0}, 8}, &got, 8) || got != 0x8877665544332211) printf("test_eval: constant read %llx\n", (unsigned long long)got), failed = 1;
  if (!eval_read(&e, &regs, (EvalValue){EVAL_VALUE, {&m, 0, 0}, 0x1234}, &got, 8) || got != 0x1234) printf("test_eval: value read %llx\n", (unsigned long long)got), failed = 1;
  if (eval_read(&e, &regs, (EvalValue){EVAL_CONSTANT, {&m, 0, 0}, 16}, &got, 8)) printf("test_eval: a constant beyond the table was read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalValue){EVAL_REGISTER, {&m, 0, 0}, 1}, &got, 9)) printf("test_eval: 9 bytes of a register were read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalValue){EVAL_NONE, {&m, 0, 0}, 0}, &got, 1)) printf("test_eval: no place was read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalValue){EVAL_MEMORY, {&m, 0, 0}, 1}, &got, 1)) printf("test_eval: memory outside was read\n"), failed = 1;

  expressions();
  if (!failed) printf("test_eval: ok\n");
  return failed;
}
